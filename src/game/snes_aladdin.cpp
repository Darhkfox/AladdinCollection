// Aladdin (Super Nintendo, Capcom 1993): widescreen, HUD, cheats, level select, sound, achievements.
//
// Game logic, RAM map and addresses: docs/re_snes_game.md; widescreen: docs/re_snes_wide.md; reference scripts
// tools/re/snes_*.py. Addresses are FastROM CPU addresses ($80:xxxx) and WRAM ($7E, low RAM mirrored).
//
// Widescreen. Capcom's engine streams two BG layers (BG1 the level, BG2 the background; structs at $1738 / $188E)
// from the level's data in RAM: a page table at $7FF000 (256x256-px pages), each page a 16x16 map of 16-px blocks in
// bank $7F, each block four tile entries in ROM. The margins are drawn straight from that data (fetchWideTile), so
// nothing of the game's own streaming changes, any width works and a width change mid-level needs no redraw. Layers
// the game scrolls on its own (a background a raster band scrolls, a fully loaded repeating picture) keep their
// VRAM map, which holds the whole repeating picture. Objects: the draw-list and keep-alive range tests and the
// level-object spawner follow the wider view; sprites are placed by their true 16-bit x. The status bar (BG3, the top
// 40 lines) is split to the picture's edges; the game's centred texts (PAUSE) stay at the picture's middle.
#include "game.h"
#include "snes_common.h"
#include "../core/snes.h"
#include <algorithm>
#include <cstring>

using namespace snes_game;

namespace {

// ---- RAM ($7E)
constexpr uint32_t kDebug = 0x0009;        // the game's debug mode (controller-2 code on OPTIONS): title stage select
constexpr uint32_t kDemo = 0x0331;         // 0 play, 1 recording (developer), 2 attract demo
constexpr uint32_t kStage = 0x035C, kAct = 0x035D, kLevel = 0x035E;   // stage 0-7, act, level index ($80:B73E)
constexpr uint32_t kLivesShown = 0x0363, kLives = 0x0364;             // lives (the bar shows lives - 1)
constexpr uint32_t kHeartsMax = 0x0365, kHeartsShown = 0x0366, kHearts = 0x0367;
constexpr uint32_t kApples = 0x0369, kGems = 0x036B;                  // BCD; $0368 / $036A the bar's copies
constexpr uint32_t kCamX = 0x1773, kCamY = 0x1777;                   // the camera (BG1 struct + $3B / $3F)
constexpr uint32_t kCamCentre = 0x1A44;                               // camera + $80 (objects' range tests)
constexpr uint32_t kCamMaxX = 0x1A04;                                 // the level's last camera x (width - 256)
constexpr uint32_t kCamLimLo = 0x1A40, kCamLimHi = 0x1A42;            // a zone's camera limits (boss arenas)
constexpr uint32_t kBg1Shown = 0x19ED, kBg1ShownY = 0x19F1;           // the scroll the NMI writes (+ shake $1A18)
constexpr uint32_t kBgMode = 0x083F;                                  // the background's mode ($81:C781): its speed
constexpr uint32_t kObjBase = 0x092B;                                 // 35 object slots of $50 bytes
constexpr int kObjSize = 0x50, kObjCount = 35;
constexpr uint32_t kSpawnCam = 0x1BEF;                                // the spawner's camera (moves by 32 px)
constexpr uint32_t kSpawnRight = 0x1BEB, kSpawnLeft = 0x1BED;         // its next object columns (32 px) each side

// The BG layer structs (+$2E VRAM map, +$3B x, +$3F y, +$42 page table offset, +$4C metatile bank, +$4F flags).
constexpr uint32_t kLayer[2] = {0x1738, 0x188E};
constexpr uint32_t kPageTable = 0x7FF000;

// ---- code
// The main task's frame wait in a level ($81:82E6 JSL wait, returns to $82E9) and in the pause loop.
constexpr uint32_t kLevelWaitRet = 0x8182E9, kPauseWaitRet = 0x8183FA;
// Sprites: the OAM builder stores each piece's 16-bit screen x (A; X = OAM index * 4); the NMI's OAM DMA.
constexpr uint32_t kOamPiece = 0x81938B, kOamPieceFlip = 0x8193F9, kOamBuild = 0x819258, kOamDma = 0x8081B2;
// Objects drawn while within a range of the camera's centre ($81:91D4, table $80:BA02 by the object's class $12) and
// deleted beyond another ($83:962E, table $80:CE73).
constexpr uint32_t kDrawTest = 0x8191D4, kDrawIn = 0x8191E3, kDrawOut = 0x81921D;
constexpr uint32_t kKeepTest = 0x83962E, kKeepIn = 0x83963D, kKeepOut = 0x83964C;
constexpr uint32_t kDrawRanges = 0x80BA02, kKeepRanges = 0x80CE73;
// Level objects: spawned by 32-px column when the spawner's camera crosses one ($82:A79C, the edge columns camera +
// $120 / - $40), all at once at a level start ($82:A72C, 12 columns), rows within [camera - $40, camera + $140).
constexpr uint32_t kSpawnInit = 0x82A72C, kSpawnInitCam = 0x82A75F, kSpawnInitRight = 0x82A767;
constexpr uint32_t kSpawnInitLeft = 0x82A772, kSpawnInitCount = 0x82A77E;
constexpr uint32_t kSpawnFrameCam = 0x82A7AA, kSpawnRowX = 0x82A876, kSpawnRowIn = 0x82A882, kSpawnRowOut = 0x82A892;
// The level's main loop (top) and the scene reset every screen's set-up calls.
constexpr uint32_t kLevelLoop = 0x8182CF;

inline uint8_t rd8(const Snes& s, uint32_t a) { return s.wram[a & 0x1FFFF]; }
inline uint16_t rd16(const Snes& s, uint32_t a) { return (uint16_t)(s.wram[a & 0x1FFFF] | (s.wram[(a + 1) & 0x1FFFF] << 8)); }
inline void wr8(Snes& s, uint32_t a, uint8_t v) { s.wram[a & 0x1FFFF] = v; }
inline void wr16(Snes& s, uint32_t a, uint16_t v) { s.wram[a & 0x1FFFF] = (uint8_t)v; s.wram[(a + 1) & 0x1FFFF] = (uint8_t)(v >> 8); }
inline uint16_t rom16(const Snes& s, uint32_t a) { return s.peek16(a); }
// A 10-bit scroll register completed to the world position nearest `near`.
inline int unwrap10(int reg, int near) { return near + ((((reg - near) & 0x3FF) + 0x200) & 0x3FF) - 0x200; }

// =====================================================================================================================
// Widescreen
// =====================================================================================================================
class AladdinWide : public PpuHooks {
public:
    static constexpr int kHudLines = 40;          // the status bar: BG3 rows 1-4
    static constexpr int kHudLeftEnd = 152;       // lives and hearts (up to 10) left of this, apples and gems right
    static constexpr int kSlide = 4;              // px a frame the view moves to a new place

    void configure(Snes& s, const GameConfig& cfg) {
        snes_ = &s;
        cfg_ = cfg;
        if (ext_ != cfg.ext && level_ && last_ext_ >= 0) respawn_ = true;   // objects for the new width's margins
        ext_ = cfg.ext;
        last_ext_ = ext_;
        s.ppu.hooks = this;
        for (uint32_t pc : {kOamPiece, kOamPieceFlip, kOamBuild, kOamDma, kLevelLoop}) s.markExec(pc, true);
        if (ext_ > 0)
            for (uint32_t pc : {kDrawTest, kKeepTest, kSpawnInitCam, kSpawnInitRight, kSpawnInitLeft, kSpawnInitCount,
                                kSpawnFrameCam, kSpawnRowX})
                s.markExec(pc, true);
        write_stub(s);
    }

    // True if the hook moved the PC.
    bool exec_hook(Snes& s, uint32_t pc) {
        CpuRegs& r = s.cpu.r;
        switch (pc) {
        case kOamBuild:
            build_count_ = 0;
            for (bool& b : build_set_) b = false;
            return false;
        case kOamPiece:
        case kOamPieceFlip: {
            const int idx = (r.x >> 2) & 127;
            build_x_[idx] = (int16_t)r.a;
            build_set_[idx] = true;
            build_count_ = std::max(build_count_, idx + 1);
            return false;
        }
        case kOamDma:
            std::memcpy(oam_x_, build_x_, sizeof(oam_x_));
            std::memcpy(oam_set_, build_set_, sizeof(oam_set_));
            return false;
        case kLevelLoop:
            loop_ran_ = true;
            if (respawn_) {
                // A width change mid-level: the spawner's edge columns move to the new margins, and the game's own
                // level-start spawn runs again (objects already out are skipped), so the new margins get theirs.
                respawn_ = false;
                if (ext_ > 0) {
                    r.pb = 0x60;
                    r.pc = (uint16_t)kStubRespawn;
                    return true;
                }
            }
            return false;
        default:
            break;
        }
        if (ext_ == 0) return false;
        int el, er;
        object_split(s, el, er);
        switch (pc) {
        case kDrawTest:
        case kKeepTest: {
            // centre - x within [-off - er, off + el) (the original's [-off, off), off the table's half range)
            const uint16_t d = (uint16_t)(rd16(s, kCamCentre) - rd16(s, r.dp + 0x0C));
            const uint32_t tab = pc == kDrawTest ? kDrawRanges : kKeepRanges;
            const int off = rom16(s, tab + (r.x & 0xFFFF)), range = rom16(s, tab + 2 + (r.x & 0xFFFF));
            const bool in = (uint16_t)(d + off + er) < range + el + er;
            if (pc == kDrawTest) r.pc = (uint16_t)(in ? kDrawIn : kDrawOut);
            else {
                r.pc = (uint16_t)(in ? kKeepIn : kKeepOut);
                r.c = !in;
            }
            return true;
        }
        case kSpawnInitCam:
        case kSpawnFrameCam:
            // The spawner's camera: the picture's middle (the camera where the view is centred on it).
            r.a = (uint16_t)(rd16(s, kCamX) - el + ext_);
            r.pc = (uint16_t)(pc + 3);
            return true;
        case kSpawnInitRight:   // ADC #$0120: the right column past the view's right margin
            r.a = (uint16_t)(r.a + 0x120 + ext_ + (r.c ? 1 : 0));
            r.pc = (uint16_t)(pc + 3);
            return true;
        case kSpawnInitLeft:    // SBC #$0040
            r.a = (uint16_t)(r.a - 0x40 - ext_ - (r.c ? 0 : 1));
            r.pc = (uint16_t)(pc + 3);
            return true;
        case kSpawnInitCount:   // LDY #$0C: every column between the two
            r.y = (uint16_t)std::max(12, ((rd8(s, kSpawnRight) - rd8(s, kSpawnLeft)) & 0xFF) + 1);
            r.pc = (uint16_t)(pc + 2);
            return true;
        case kSpawnRowX: {      // a row's objects within [camera - $40, camera + $140), widened by the margins
            const uint16_t v = (uint16_t)(r.a + 0x40 + ext_ - rd16(s, kSpawnCam));
            r.pc = (uint16_t)(v < 0x180 + 2 * ext_ ? kSpawnRowIn : kSpawnRowOut);
            return true;
        }
        default:
            return false;
        }
    }

    // After every frame.
    void frame_end(Snes& s, FrameInfo& info) {
        snes_ = &s;
        level_ = in_level(s);
        // The view's limits for the next frame; when they change the view keeps its place and slides.
        if (level_ && ext_ > 0) {
            const int cam = (int16_t)rd16(s, kCamX), camy = (int16_t)rd16(s, kCamY);
            int lo, hi;
            span(s, cam, camy, lo, hi);
            const bool jump = !lim_valid_ || std::abs(cam - last_cam_) >= 64;
            if (!jump && (lo != lim_lo_ || hi != lim_hi_) && loop_ran_) {
                const int before = target_v(cam, lim_lo_, lim_hi_);
                lim_lo_ = lo;
                lim_hi_ = hi;
                d_off_ += before - target_v(cam, lo, hi);
            } else if (jump) {
                lim_lo_ = lo;
                lim_hi_ = hi;
                d_off_ = 0;
            }
            lim_valid_ = true;
            last_cam_ = cam;
            d_off_ = std::clamp(d_off_, -2 * ext_, 2 * ext_);
            if (loop_ran_) d_off_ = d_off_ > 0 ? std::max(0, d_off_ - kSlide) : std::min(0, d_off_ + kSlide);
        } else {
            lim_valid_ = false;
            d_off_ = 0;
        }
        loop_ran_ = false;
        if (!level_) respawn_ = false;
        info.gameplay = level_;
        info.view_left = level_ ? el_ : -1;
    }

    bool level() const { return level_; }

    // ---- PPU hooks
    void beginLine(Ppu& p, int row) override {
        if (row == 0) frame_start(p);
        if (!level_ || ext_ == 0 || !snes_) return;
        const Snes& s = *snes_;
        // The layers drawn from the level's data: world position of this line.
        for (int l = 0; l < 2; l++) {
            LineLayer& ll = line_[l];
            ll.decode = false;
            const uint32_t b = kLayer[l];
            if (!rd8(s, b + 0x4F) || p.bgMapBase(l) != rd16(s, b + 0x2E) || p.bgMapSize(l) != 1) continue;
            const int sx = (int16_t)rd16(s, b + 0x3B), sy = (int16_t)rd16(s, b + 0x3F);
            const int wx = unwrap10(p.bgHofs(l), sx), wy = unwrap10(p.bgVofs(l), sy);
            if (std::abs(wx - sx) > 64 || std::abs(wy - sy) > 64) continue;   // scrolled on its own: VRAM
            ll.decode = true;
            ll.wx = wx;
            ll.wy = wy + row + 1;
            ll.base = b;
            ll.off = rd16(s, b + 0x42);
            ll.bank = rd8(s, b + 0x4C);
            ll.lo = -0x8000;
            ll.hi = 0x7FFF;
            if (l == 1 && row >= kHudLines && bg2_ratio_ != 0x100 && bg2_ratio_ != 0) {
                // A background at another speed than the level (parallax): where the view stops at a level edge
                // while the game's camera goes on, it is drawn where the game would have it with its camera at the
                // picture's middle, so it stands still with the picture instead of sliding against it. That is
                // (ratio - 1) * (ext - extL) from the game's own position (0 wherever the view is centred), the whole
                // line drawn from the level's data; before its first column and past what the game's camera shows
                // of it at the level's end, it is mirrored.
                const int n = (bg2_ratio_ - 0x100) * (ext_ - el_);
                const int adj = n >= 0 ? (n + 0x80) >> 8 : -((-n + 0x7F) >> 8);
                if (adj != 0) {
                    p.bgHofsAdj[1] = (int16_t)adj;
                    p.wideFetchAll[1] = true;
                    ll.wx += adj;
                }
                ll.lo = 0;
                ll.hi = ((((int16_t)rd16(s, kCamMaxX)) * bg2_ratio_) >> 8) + 256;
            }
        }
        // The level's extent on this line (BG1's page row): past it the level is mirrored and dimmed (or black).
        int lo = -kMaxExt, hi = 256 + kMaxExt;
        if (line_[0].decode && row >= kHudLines) {
            const int wy = line_[0].wy;
            int slo, shi;
            row_run(s, line_[0].wx, wy, slo, shi);
            lo = slo - line_[0].wx;
            hi = shi - line_[0].wx;
        }
        if (row < kHudLines) {
            // the status bar's lines: its two parts at the picture's edges
            p.wideShowLo = lo;
            p.wideShowHi = hi;
            p.wideShowKeep = 0x14;
            if (cfg_.hud_anchor) {
                PpuLayerRemap& rm = p.layerRemap[2];
                rm.count = 2;
                rm.seg[0] = {(int16_t)(-el_), (int16_t)(kHudLeftEnd - el_), 0, 0};
                rm.seg[1] = {(int16_t)(kHudLeftEnd + er_), (int16_t)(256 + er_), (int16_t)kHudLeftEnd, 0};
            } else {
                p.hideWideBg[2] = true;
            }
        } else {
            p.wideShowLo = lo;
            p.wideShowHi = hi;
            p.wideMirror = 0x03;
            // BG3 below the bar holds the game's centred texts (ACTION, PAUSE): at the picture's middle
            p.hideWideBg[2] = true;
            if (cfg_.hud_anchor && mid_ != 0) {
                PpuLayerRemap& rm = p.layerRemap[2];
                rm.count = 1;
                rm.seg[0] = {(int16_t)mid_, (int16_t)(256 + mid_), 0, 0};
            }
        }
        p.wideDimLo = std::max(dim_lo_, std::min(0, p.wideShowLo));
        p.wideDimHi = std::min(dim_hi_, std::max(256, p.wideShowHi));
        p.wideDimSpare = row < kHudLines ? 0x04 : 0;
    }

    int fetchWideTile(Ppu& p, int layer, int mapX, int mapY, int screenX, uint16_t* entry) override {
        (void)p; (void)mapX; (void)mapY;
        if (layer > 1 || !level_ || !snes_ || !line_[layer].decode) return kWideVram;
        const LineLayer& ll = line_[layer];
        int x = ll.wx + screenX;
        uint16_t flip = 0;
        if (x < ll.lo) { x = 2 * ll.lo - 1 - x; flip = 0x4000; }          // (mirrored about the layer's ends)
        else if (x >= ll.hi) { x = 2 * ll.hi - 1 - x; flip = 0x4000; }
        *entry = (uint16_t)(tile_at(*snes_, ll, x, ll.wy) ^ flip);
        return kWideEntry;
    }

    // The split for objects this logic frame: the picture's extensions for the camera now.
    void object_split(const Snes& s, int& el, int& er) const {
        el = er = ext_;
        if (ext_ == 0) return;
        const int cam = (int16_t)rd16(s, kCamX);
        if (lim_valid_) {
            el = split_left(cam);
        } else {
            // (a state just loaded, a level's first frame: the view the level's extent gives, without a slide)
            int lo, hi;
            span(s, cam, (int16_t)rd16(s, kCamY), lo, hi);
            el = std::clamp(cam - target_v(cam, lo, hi), 0, 2 * ext_);
        }
        er = 2 * ext_ - el;
    }

    std::vector<uint8_t> save_ext() const {
        std::vector<uint8_t> v;
        v.push_back(1);   // version
        v.push_back((uint8_t)((level_ ? 1 : 0) | (lim_valid_ ? 2 : 0)));
        for (int x : {ext_, lim_lo_, lim_hi_, d_off_, last_cam_}) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
        for (int i = 0; i < 128; i++) {
            v.push_back((uint8_t)oam_x_[i]);
            v.push_back((uint8_t)(oam_x_[i] >> 8));
            v.push_back(oam_set_[i] ? 1 : 0);
        }
        return v;
    }
    void load_ext(const uint8_t* d, size_t n) {
        lim_valid_ = false;
        d_off_ = 0;
        for (bool& b : oam_set_) b = false;
        const size_t need = 2 + 5 * 2 + 128 * 3;
        respawn_ = true;   // (a state from before the module's data, or made at another width: the margins' objects)
        if (n < need || d[0] != 1) return;
        level_ = (d[1] & 1) != 0;
        lim_valid_ = (d[1] & 2) != 0;
        int v[5];
        for (int i = 0; i < 5; i++) v[i] = (int16_t)(d[2 + i * 2] | (d[3 + i * 2] << 8));
        lim_lo_ = v[1]; lim_hi_ = v[2]; d_off_ = v[3]; last_cam_ = v[4];
        respawn_ = v[0] != ext_;
        for (int i = 0; i < 128; i++) {
            const uint8_t* q = d + 12 + i * 3;
            oam_x_[i] = (int16_t)(q[0] | (q[1] << 8));
            oam_set_[i] = q[2] != 0;
        }
    }

private:
    struct LineLayer {
        bool decode = false;
        int wx = 0, wy = 0;   // world x of the game's screen's left edge and world y on this line
        int lo = 0, hi = 0;   // the layer's extent (mirrored beyond)
        uint32_t base = 0;
        uint16_t off = 0;
        uint8_t bank = 0;
    };
    // Patch-ROM stub: the level start's object spawn, then back to the main loop's top.
    static constexpr uint32_t kStubRespawn = 0x608000;

    const Snes* snes_ = nullptr;
    GameConfig cfg_;
    int ext_ = 0, last_ext_ = -1;
    bool level_ = false, loop_ran_ = false, respawn_ = false;
    bool lim_valid_ = false;
    int lim_lo_ = 0, lim_hi_ = 0, d_off_ = 0, last_cam_ = 0;
    int el_ = 0, er_ = 0, mid_ = 0;
    int bg2_ratio_ = 0;   // this frame: BG2's x speed against the camera (8.8), 0 = scrolled by the game itself
    int dim_lo_ = -kMaxExt, dim_hi_ = 256 + kMaxExt;
    LineLayer line_[2];
    int16_t build_x_[128] = {}, oam_x_[128] = {};
    bool build_set_[128] = {}, oam_set_[128] = {};
    int build_count_ = 0;

    static void write_stub(Snes& s) {
        PatchAsm a(s, kStubRespawn);
        for (uint8_t b : jsl(kSpawnInit)) a.b8(b);
        for (uint8_t b : jml(kLevelLoop)) a.b8(b);
    }

    // A level screen: the main task waits in the level's main loop or its pause loop (task 0's saved return address).
    static bool in_level(const Snes& s) {
        const uint16_t sp = rd16(s, 0x0071);
        if (sp < 0x0100 || sp > 0x0170) return false;
        const uint32_t ret = rd16(s, sp + 7) | (rd8(s, sp + 9) << 16);
        return ret == kLevelWaitRet || ret == kPauseWaitRet;
    }

    // BG1's page entry (page x, page y in pages; -1 .. border).
    static uint16_t page(const Snes& s, uint32_t base, int px, int py) {
        const int idx = rd16(s, base + 0x42) + (py + 1) * 64 + (((px + 1) * 2) & 0xFE);
        return rd16(s, kPageTable + (idx & 0xFFF));
    }
    static bool barrier(const Snes& s, int px, int py) { return (page(s, kLayer[0], px, py) & 0x8000) != 0; }

    // A tile entry of a streaming layer at world (x, y), as the column builder ($81:C2E0) finds it.
    static uint16_t tile_at(const Snes& s, const LineLayer& ll, int x, int y) {
        const int idx = ll.off + (((y + 256) & 0xFF00) >> 2) + ((((x >> 8) + 1) * 2) & 0xFE);
        uint16_t pg = rd16(s, kPageTable + (idx & 0xFFF));
        if (pg & 0x8000) pg = 0;
        const int blk_off = ((x >> 3) & 0x1E) | ((y << 1) & 0x1E0);
        const uint16_t blk = rd16(s, 0x7F0000 + ((pg + blk_off) & 0xFFFF)) | 0x8000;
        const int sub = ((x >> 2) & 2) | ((y >> 1) & 4);
        return rom16(s, ((uint32_t)ll.bank << 16) | (uint16_t)(blk + sub));
    }

    // The level's horizontal extent: [0, last camera x + 256), the zone's camera limits, and the pages the camera
    // may enter in the rows its screen covers (bit 15: a page it may not).
    static void span(const Snes& s, int cam, int camy, int& lo, int& hi) {
        lo = 0;
        hi = (int16_t)rd16(s, kCamMaxX) + 256;
        const int lmin = (int16_t)rd16(s, kCamLimLo), lmax = (int16_t)rd16(s, kCamLimHi);
        lo = std::max(lo, std::min(cam, lmin));
        hi = std::min(hi, std::max(cam, lmax) + 256);
        const int r0 = camy >> 8, r1 = (camy + 255) >> 8;
        int p = std::max(0, cam >> 8);
        while (p > 0 && !barrier(s, p - 1, r0) && !barrier(s, p - 1, r1)) p--;
        lo = std::max(lo, p * 256);
        p = std::max(0, (cam + 255) >> 8);
        while (p < 126 && !barrier(s, p + 1, r0) && !barrier(s, p + 1, r1)) p++;
        hi = std::min(hi, (p + 1) * 256);
        if (hi < lo + 256) hi = lo + 256;
    }

    // One line's extent: the pages of its row the camera may enter, around the game's screen.
    static void row_run(const Snes& s, int camx, int wy, int& lo, int& hi) {
        lo = 0;
        hi = (int16_t)rd16(s, kCamMaxX) + 256;
        const int r = wy >> 8;
        int p = std::max(0, camx >> 8);
        while (p > 0 && !barrier(s, p - 1, r)) p--;
        lo = std::max(lo, p * 256);
        p = std::max(0, (camx + 255) >> 8);
        while (p < 126 && !barrier(s, p + 1, r)) p++;
        hi = std::min(hi, (p + 1) * 256);
    }

    // The view's left edge (world x) for the camera within [lo, hi): centred on the camera, stopped at the edges; a
    // range narrower than the picture centred.
    int target_v(int cam, int lo, int hi) const {
        const int vw = 256 + 2 * ext_;
        if (hi - lo <= vw) return lo + (hi - lo - vw) / 2;
        return std::clamp(cam - ext_, lo, hi - vw);
    }
    int split_left(int cam) const { return std::clamp(cam - (target_v(cam, lim_lo_, lim_hi_) + d_off_), 0, 2 * ext_); }

    void frame_start(Ppu& p) {
        el_ = er_ = ext_;
        mid_ = 0;
        dim_lo_ = -kMaxExt;
        dim_hi_ = 256 + kMaxExt;
        if (level_ && ext_ > 0 && snes_ && lim_valid_) {
            const Snes& s = *snes_;
            // the camera on screen: BG1's scroll register, completed from the NMI's copy
            const int cam = unwrap10(p.bgHofs(0), (int16_t)rd16(s, kBg1Shown));
            el_ = split_left(cam);
            er_ = 2 * ext_ - el_;
            mid_ = (er_ - el_) / 2;
            dim_lo_ = lim_lo_ - cam;
            dim_hi_ = lim_hi_ - cam;
        }
        p.setExt(el_, er_);
        // BG2's speed against the camera ($083F: the level's background mode, $81:C781; HDMA-band modes scroll it
        // themselves and keep their VRAM picture)
        bg2_ratio_ = 0;
        if (snes_) {
            switch (rd16(*snes_, kBgMode)) {
            case 0x00: case 0x06: case 0x08: bg2_ratio_ = 0x80; break;
            case 0x02: case 0x04: bg2_ratio_ = 0x100; break;
            default: break;
            }
        }
        for (int i = 0; i < 128; i++) {
            p.objTrueXSet[i] = level_ && ext_ > 0 && oam_set_[i];
            p.objTrueX[i] = oam_x_[i];
        }
    }
};

// =====================================================================================================================
// The module
// =====================================================================================================================
class SnesAladdin : public SnesModule {
public:
    const char* id() const override { return "snes"; }
    const char* title() const override { return "Aladdin"; }
    const char* subtitle() const override { return "Capcom"; }
    const char* year() const override { return "1993"; }
    uint32_t rom_crc32() const override { return 0x124D8E4D; }
    uint32_t rom_size() const override { return 1310720; }
    const char* rom_hint() const override { return "Aladdin (USA).sfc"; }
    const char* rom_names() const override { return "ALADDIN"; }
    uint32_t accent_color() const override { return 0xFFB394F7; }
    int max_ext() const override { return 140; }

    void configure(Snes& s, const GameConfig& cfg) override {
        apply_patches(s, {});
        s.clearExecMarks();
        wide_.configure(s, cfg);
    }

    bool exec_hook(Snes& s, uint32_t pc) override { return wide_.exec_hook(s, pc); }

    void post_frame(Snes& s, const GameConfig& cfg, FrameInfo& info) override {
        (void)cfg;
        wide_.frame_end(s, info);
    }

    std::vector<uint8_t> save_ext() const override { return wide_.save_ext(); }
    void load_ext(const uint8_t* d, size_t n) override { wide_.load_ext(d, n); }

private:
    AladdinWide wide_;
};

}  // namespace

GameModule* create_snes_aladdin() { return new SnesAladdin; }
