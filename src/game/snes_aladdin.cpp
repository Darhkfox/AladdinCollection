// Aladdin (Super Nintendo, Capcom 1993): widescreen, HUD, cheats, level select, sound, achievements.
//
// Game logic, RAM map and addresses: docs/re_snes_game.md; widescreen: docs/re_snes_wide.md; reference scripts
// tools/re/snes_*.py. Addresses are FastROM CPU addresses ($80:xxxx) and WRAM ($7E, low RAM mirrored).
//
// Widescreen. Capcom's engine streams two BG layers (BG1 the level, BG2 the background; structs at $1738 / $188E)
// from the level's data in RAM: a page table at $7FF000 (256x256-px pages), each page a 16x16 map of 16-px blocks in
// bank $7F, each block four tile entries in ROM. The margins are drawn straight from that data (fetchWideTile), so
// nothing of the game's own streaming changes, any width works and a width change mid-level needs no redraw. Agrabah's
// raster bands are drawn from their picture's 512-px repeat; other layers the game scrolls on its own (the lava's
// bands, a fully loaded repeating picture) keep their VRAM map, which holds the whole repeating picture. The view
// stops at every level's ends; the background then moves line by line as the game's own code would move it with its
// camera at the picture's middle (its parallax, raster bands, phases, an object: band_adj). Objects: the draw-list
// and keep-alive range tests and the level-object spawner follow the wider view; sprites are placed by their true
// 16-bit x. The status bar (BG3, the top 40 lines) is split to the picture's edges; the game's centred texts (PAUSE)
// stay at the picture's middle.
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
constexpr uint32_t kLives = 0x0364;                                   // lives (the bar shows lives - 1; $0363 its copy)
constexpr uint32_t kHeartsMax = 0x0365, kHearts = 0x0367;             // ($0366 the bar's count)
constexpr uint32_t kGems = 0x036B;                                    // BCD ($0369 apples; $0368 / $036A the bar's copies)
constexpr uint32_t kCamX = 0x1773, kCamY = 0x1777;                   // the camera (BG1 struct + $3B / $3F)
constexpr uint32_t kCamCentre = 0x1A44;                               // camera + $80 (objects' range tests)
constexpr uint32_t kCamMaxX = 0x1A04, kCamMaxY = 0x1A06;              // the level's last camera x (width - 256), y
constexpr uint32_t kCamLimLo = 0x1A40, kCamLimHi = 0x1A42;            // a zone's camera limits (boss arenas)
constexpr uint32_t kCamHoldX = 0x1A10, kCamHoldY = 0x1A12;            // nonzero: the camera doesn't follow in x / y
constexpr uint32_t kBg1Shown = 0x19ED, kBg2Shown = 0x19F5;            // the scroll the NMI writes (+ shake $1A18)
constexpr uint32_t kBgMode = 0x083F;                                  // the background's mode ($81:C781): its speed
constexpr uint32_t kBgSub = 0x0841;                                   // mode $10's phase (Jafar's Palace 3)
constexpr uint32_t kBgDrift = 0x1A30;                                 // the band routines' drift ($1A2F, 24 bits)
constexpr uint32_t kCamPrev = 0x19E5;                                 // the camera before this frame's move
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
// $120 / - $40), all at once at a level start ($82:A72C, 12 columns), rows within [camera - $40, camera + $140). The
// level start first sets the spawner up ($82:A93A: its lists and spawned bits at $7E:C100-$C7FF cleared and rebuilt,
// $82:A73C); kSpawnColumns is the part after that (the edge columns from the camera, then the columns spawned).
constexpr uint32_t kSpawnInit = 0x82A72C, kSpawnColumns = 0x82A742;
constexpr uint32_t kSpawnInitCam = 0x82A75F, kSpawnInitRight = 0x82A767;
constexpr uint32_t kSpawnInitLeft = 0x82A772, kSpawnInitCount = 0x82A77E;
constexpr uint32_t kSpawnFrameCam = 0x82A7AA, kSpawnRowX = 0x82A876, kSpawnRowIn = 0x82A882, kSpawnRowOut = 0x82A892;
// The frame spawner's new edge column (JSR $A89D: $82:A7E3 left, $82:A7EB right; both go on at $82:A7EE), and the level
// start's column loop entered for one column ($82:A780 with Y = 1: PHX PHY JSR $A89D PLY PLX INX DEY BNE, PLB PLD RTL).
constexpr uint32_t kSpawnEdgeLeft = 0x82A7E3, kSpawnEdgeRight = 0x82A7EB, kSpawnEdgeDone = 0x82A7EE;
constexpr uint32_t kSpawnOneColumn = 0x82A780;
// The level objects' spawned bits ($7E:C600 + index, object +$34; bit 0 out, bit 7 once only: kept on a delete), their
// list ($82:8A7A + $006D + (index - 1) * 6: type, flags, x, y), and the game's sweep of every object ($81:9D82, from
// an event such as an arena's start: JSL $83:96D0 per slot, Y the slot).
constexpr uint32_t kSpawnedBits = 0x7EC600, kObjList = 0x828A7A, kObjListOff = 0x006D;
constexpr uint32_t kSweepDelete = 0x819D8E;
// The spawner's columns: objects per 32-px column ($7E:C100), the first one's list index ($7E:C200); its camera y.
constexpr uint32_t kColCount = 0x7EC100, kColFirst = 0x7EC200, kSpawnCamY = 0x1BF1;
// The level's main loop (its top: the stack is the task's own, nothing in flight) and its frame wait (the frame's
// logic done). The scheduler's way back in ($81:BD28) from any task's wait ($81:BD31), restart ($81:BCF6) or end.
constexpr uint32_t kLevelLoop = 0x8182CF, kLevelWait = 0x8182E6, kTaskYield = 0x81BD28;
// The band routines' tables ($81:C84B): by routine (0: Agrabah Rooftops, 2: Agrabah Market) the band count * 2 and the
// offset of its bands' world-y bounds from $80:C606.
constexpr uint32_t kBandCount = 0x80C602, kBandBounds = 0x80C606;
// The IRQ ($80:8332) switches HDMA on (STA $420C) at the line its NMI armed: 8, the level's own ($1C7D: 12 in Agrabah
// Rooftops) or a few lines after an NMI that ran past the frame's top.
constexpr uint32_t kHdmaOn = 0x808345;
constexpr uint32_t kLavaTable = 0x7EF400;                             // the Lava Cavern's HDMA table (count, word)

inline uint8_t rd8(const Snes& s, uint32_t a) { return s.wram[a & 0x1FFFF]; }
// An attract demo playing (the game's own flag: input from its recording at $84:F700). Demos replay recorded input:
// any change to the code they run (the wider spawn / keep ranges, the cheats, Remove Slowdown's timing) sends them off
// their path, so while one plays the hooks change nothing, the turbo stays off and the demo is shown at 4:3.
inline bool demo_playing(const Snes& s) { return s.wram[0x0331] == 2; }
// A level index ($035E below $14; the screens - story scenes, which run the level-start object spawn too, STAGE CLEAR,
// the title - are above) and no demo. (The title zeroes $035E while it waits for GAME START: AladdinWide::armed_.)
inline bool playing_level(const Snes& s) { return s.wram[0x035E] < 0x14 && !demo_playing(s); }
inline uint16_t rd16(const Snes& s, uint32_t a) { return (uint16_t)(s.wram[a & 0x1FFFF] | (s.wram[(a + 1) & 0x1FFFF] << 8)); }
inline void wr8(Snes& s, uint32_t a, uint8_t v) { s.wram[a & 0x1FFFF] = v; }
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
    static constexpr int kHdmaRows = 3;           // an HDMA table's second entry: rows after the line HDMA goes on

    void configure(Snes& s, const GameConfig& cfg) {
        snes_ = &s;
        cfg_ = cfg;
        // Objects for the new width's margins: at the level loop's next top (pending through screens that aren't the
        // loop yet, such as the Underground River's set-up; a level's own start spawn sees the new width anyway).
        if (ext_ != cfg.ext && last_ext_ >= 0) respawn_ = true;
        if (ext_ != cfg.ext) band_next_.valid = false;   // (the virtual camera jumps with the width: no ratchet move)
        ext_ = cfg.ext;
        last_ext_ = ext_;
        s.ppu.hooks = this;
        for (uint32_t pc :
             {kOamPiece, kOamPieceFlip, kOamBuild, kOamDma, kLevelLoop, kLevelWait, kTaskYield, kSpawnInit})
            s.markExec(pc, true);
        if (ext_ > 0)
            for (uint32_t pc : {kDrawTest, kKeepTest, kSpawnInitCam, kSpawnInitRight, kSpawnInitLeft, kSpawnInitCount,
                                kSpawnFrameCam, kSpawnRowX, kSpawnEdgeLeft, kSpawnEdgeRight, kStubRetryR + 4,
                                kStubRetryL + 4, kSweepDelete, kHdmaOn})
                s.markExec(pc, true);
        write_stub(s);
    }

    // True if the hook moved the PC.
    bool exec_hook(Snes& s, uint32_t pc) {
        CpuRegs& r = s.cpu.r;
        switch (pc) {
        case kOamBuild:
            for (bool& b : build_set_) b = false;
            return false;
        case kOamPiece:
        case kOamPieceFlip: {
            const int idx = (r.x >> 2) & 127;
            build_x_[idx] = (int16_t)r.a;
            build_set_[idx] = true;
            return false;
        }
        case kOamDma:
            std::memcpy(oam_x_, build_x_, sizeof(oam_x_));
            std::memcpy(oam_set_, build_set_, sizeof(oam_set_));
            return false;
        case kLevelWait:
            logic_done_ = true;
            return false;
        case kHdmaOn:   // (only watched: where this frame's band tables start on the screen, band_adj)
            hdma_line_ = s.vPos();
            return false;
        case kSpawnInit:
            respawn_ = false;   // (a level's start spawn: for the width set now)
            armed_ = playing_level(s);
            return false;
        case kTaskYield:
            // Remove Slowdown ends wherever the level's main task gives the CPU back: its frame wait, the pause loop's,
            // and the restart that leaves the level (an exit, a life lost: the next screen keeps the console's timing).
            s.turboActive = false;
            return false;
        case kLevelLoop:
            // Remove Slowdown: the frame's work runs faster up to the task's yield (not in a demo: its input is a
            // recording of frames, and its lag frames are part of it)
            s.turboActive = s.cpuTurbo > 1 && active(s);
            loop_frame_ = s.frameCount();
            logic_done_ = band_taken_ = false;
            if (respawn_ && active(s)) {
                // A width change mid-level: the spawner's edge columns move to the new margins, and the game's own
                // level-start spawn runs again past its set-up, which would clear the spawned bits: objects already
                // out are skipped by them, so the new margins get only theirs.
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
        if (ext_ == 0 || !active(s)) return false;   // (a demo, a story scene, the title: the original ranges)
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
        // A level object the game refuses to create (no free sprite VRAM bank for its graphics, $82:AA61, or no free
        // slot) keeps its spawned bit clear but is only tried again when the spawner's edge crosses its column again.
        // In the margins the spawner meets objects earlier, and more are alive (the wider keep range): the Market at
        // Dusk's Jasmine (type $56, three banks), who ends the level after the boss, met the margin's edge while the
        // market's enemies held the banks and was never created. So at each edge column the spawner first takes the
        // columns at the original's edge (camera + $120 / - $40, and the one before it) where an object the 4:3
        // game would create there now isn't out (spawned bits clear): it gets the original's chance. (None pending,
        // the usual case: the game's own JSR runs, no cycle changes.)
        case kSpawnEdgeLeft:
        case kSpawnEdgeRight: {
            const bool right = pc == kSpawnEdgeRight;
            uint8_t cols[3];
            const int n = retry_columns(s, right, cols);
            const int i = next_column(s, cols, n, -1);
            if (i == n - 1) return false;
            r.x = cols[i];
            r.pb = (uint8_t)(kStubRetryR >> 16);
            r.pc = (uint16_t)(right ? kStubRetryR : kStubRetryL);
            return true;
        }
        case kStubRetryR + 4:   // a column done (X = it + 1): the next one, if any (stateless: from the camera's RAM)
        case kStubRetryL + 4: {
            const bool right = pc == kStubRetryR + 4;
            uint8_t cols[3];
            const int n = retry_columns(s, right, cols);
            const uint8_t done = (uint8_t)(r.x - 1);
            for (int i = 0; i + 1 < n; i++)
                if (cols[i] == done) {
                    r.x = cols[next_column(s, cols, n, i)];
                    r.pc = (uint16_t)(right ? kStubRetryR : kStubRetryL);
                    return true;
                }
            return false;
        }
        case kSweepDelete: {
            // The sweep deletes what the margins spawned ahead of the original too; a once-only object (bit 7: the
            // Market at Dusk's boss and Jasmine, out in the margin when its arena starts) would never come back. One
            // whose place in the list is beyond the original's spawn edges (its edge columns included) is left
            // unspawned instead, for the spawner to take when the original's edge gets there (above).
            const uint16_t obj = r.y;
            const uint8_t idx = rd8(s, obj + 0x34);
            if (rd8(s, obj) != 0 && idx != 0) {
                const uint32_t e = kObjList + (uint32_t)((idx - 1) * 6 + rd16(s, kObjListOff));
                const int col = (rom16(s, e + 2) & 0x3FFF) >> 5, cam = (int16_t)rd16(s, kCamX);
                if (col >= (cam + 0x120) >> 5 || col <= floor_shr(cam - 0x40, 5)) wr8(s, kSpawnedBits + idx, 0);
            }
            return false;
        }
        default:
            return false;
        }
    }

    // After every frame.
    void frame_end(Snes& s, FrameInfo& info) {
        snes_ = &s;
        // (a frame the game's logic didn't finish in, slowdown: the main task is still running, its saved stack
        // is stale; the loop's top ran a moment ago)
        if (!playing_level(s)) armed_ = false;
        level_ = (in_level(s) || s.frameCount() - loop_frame_ <= 4) && active(s);   // (a demo: shown 4:3)
        // The view for the next frame. It stops at the level's ends where every layer can follow a view that doesn't
        // move with the game's camera (can_stop): the level (BG1), and the background at its parallax speed (modes
        // 0-8) or line by line as the game scrolls it itself (modes $0A-$14, band_adj). The span changes only at a
        // level's start, when its width changes or an arena holds the camera; then the view's offset closes only as
        // fast as the camera moves (it never pans by itself while the game's picture holds still).
        if (level_ && ext_ > 0) {
            const int cam = (int16_t)rd16(s, kCamX), camy = (int16_t)rd16(s, kCamY);
            int lo, hi;
            span(s, cam, lo, hi);
            reach(s, cam, camy, reach_lo_, reach_hi_);
            const bool stop = can_stop(s);
            const bool jump = !lim_valid_ || std::abs(cam - last_cam_) >= 64;
            if (!jump && (lo != lim_lo_ || hi != lim_hi_ || stop != stop_)) {
                const int before = target_v(cam, lim_lo_, lim_hi_, stop_);
                lim_lo_ = lo;
                lim_hi_ = hi;
                stop_ = stop;
                d_off_ += before - target_v(cam, lo, hi, stop);
            } else if (jump) {
                lim_lo_ = lo;
                lim_hi_ = hi;
                stop_ = stop;
                d_off_ = 0;
            }
            const int moved = jump ? 0 : std::abs(cam - last_cam_);
            lim_valid_ = true;
            last_cam_ = cam;
            d_off_ = std::clamp(d_off_, -2 * ext_, 2 * ext_);
            const int step = std::min(kSlide, moved);
            d_off_ = d_off_ > 0 ? std::max(0, d_off_ - step) : std::min(0, d_off_ + step);
            // A background the game scrolls itself: the inputs of its scroll for the frame its logic just finished.
            if (logic_done_ && !band_taken_) {
                band_taken_ = true;
                take_band(s, cam, jump);
            }
        } else {
            lim_valid_ = false;
            d_off_ = 0;
            band_next_.valid = band_shown_.valid = false;
        }
        info.gameplay = level_;
        info.view_left = level_ ? el_ : -1;
    }

    bool level() const { return level_; }
    // A level being played: from its own start spawn ($82:A72C with a level index and no demo) while the level index
    // stays one (a story scene, STAGE CLEAR, the title, a demo end it). Only then do the hooks change what the game does
    // (wider object ranges, the margins' spawn, Remove Slowdown); everything else runs the original code, the attract
    // demos above all (they replay recorded input).
    bool active(const Snes& s) const { return armed_ && playing_level(s); }

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
            // Agrabah's bands (world_bands) scroll a picture that repeats every 512 px: every band's line is drawn
            // from that repeat (x mod 512). Their VRAM map holds it only where the game has streamed it: the column
            // before x = 0 keeps the border column's page from the level's start until the camera passes x 480.
            const bool rep = l == 1 && world_bands(rd16(s, kBgMode));
            if ((!rep && std::abs(wx - sx) > 64) || std::abs(wy - sy) > 64) continue;   // scrolled on its own: VRAM
            ll.decode = true;
            ll.wx = wx;
            ll.wy = wy + row + 1;
            ll.off = rd16(s, b + 0x42);
            ll.bank = rd8(s, b + 0x4C);
            ll.lo = -0x8000;
            ll.hi = 0x7FFF;
            ll.rep = rep;
            if (l == 1 && bg2_ratio_ != 0x100 && bg2_ratio_ != 0) {
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
        // A background the game scrolls itself (raster bands, an object, a phase of its own): on each line where the
        // game's own code would scroll it with its camera at the picture's middle (band_adj), so where the view stops
        // at a level's end it holds still with the picture and moves exactly as the game moves it.
        if (band_on_) {
            const int adj = band_adj(p, row, line_[1]);
            if (adj != 0) {
                p.bgHofsAdj[1] = (int16_t)adj;
                if (line_[1].decode) {
                    p.wideFetchAll[1] = true;
                    line_[1].wx += adj;
                }
            }
        }
        // A picture an object moves on BG2 (the Carpet Escape's wave: the map's first 256x256 screen, the rest empty,
        // placed by the scroll at [-x, 256 - x); parked at x = 256 while it isn't there): in the margins only its
        // own copy, not the map's 512-px repeat.
        obj_bg2_ = !line_[1].decode && rd16(s, kBgMode) == 0x12;
        if (obj_bg2_) {
            const int h = unwrap10(p.bgHofs(1), (int16_t)rd16(s, kBg2Shown));
            obj_lo_ = h == 256 ? 0 : -h;
            obj_hi_ = h == 256 ? 0 : 256 - h;
        }
        // The level's extent on this line (BG1's page row): past it the level is mirrored and dimmed (or black).
        int lo = -kMaxExt, hi = 256 + kMaxExt;
        if (line_[0].decode) {
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
            p.wideMirror = 0x03;
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
        if (layer == 1 && obj_bg2_ && level_) return screenX >= obj_lo_ && screenX < obj_hi_ ? kWideVram : kWideBlank;
        if (layer > 1 || !level_ || !snes_ || !line_[layer].decode) return kWideVram;
        const LineLayer& ll = line_[layer];
        int x = ll.wx + screenX;
        if (ll.rep) x &= 0x1FF;
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
            // (a level's first frame: the view the level's extent gives)
            int lo, hi;
            span(s, cam, lo, hi);
            el = std::clamp(cam - target_v(cam, lo, hi, can_stop(s)), 0, 2 * ext_);
        }
        er = 2 * ext_ - el;
    }

    // Save states (and the app's rewind, a state every two frames) carry the view, the sprites' recorded x (the build
    // of the frame in progress too: its OAM DMA comes after the load) and how long ago the level loop's top ran.
    std::vector<uint8_t> save_ext() const {
        std::vector<uint8_t> v;
        v.push_back(4);   // version
        v.push_back((uint8_t)((level_ ? 1 : 0) | (lim_valid_ ? 2 : 0) | (stop_ ? 4 : 0) | (respawn_ ? 16 : 0) |
                              (armed_ ? 32 : 0)));
        const uint32_t since = snes_ ? snes_->frameCount() - loop_frame_ : 255;
        v.push_back((uint8_t)std::min<uint32_t>(since, 255));
        for (int x : {ext_, lim_lo_, lim_hi_, d_off_, last_cam_, reach_lo_, reach_hi_}) {
            v.push_back((uint8_t)x);
            v.push_back((uint8_t)(x >> 8));
        }
        auto put = [&](const int16_t* x, const bool* set) {
            for (int i = 0; i < 128; i++) {
                v.push_back((uint8_t)x[i]);
                v.push_back((uint8_t)(x[i] >> 8));
                v.push_back(set[i] ? 1 : 0);
            }
        };
        put(oam_x_, oam_set_);
        put(build_x_, build_set_);
        // (version 3) the band routines' inputs: for the frame on screen and the next, and mode $0A's ratchet
        v.push_back((uint8_t)((logic_done_ ? 1 : 0) | (band_taken_ ? 2 : 0) | (band_next_.valid ? 4 : 0) |
                              (band_shown_.valid ? 8 : 0)));
        for (int x : {band_d_, band_next_.cam, band_next_.vcam, (int)band_next_.t, band_shown_.cam, band_shown_.vcam,
                      (int)band_shown_.t}) {
            v.push_back((uint8_t)x);
            v.push_back((uint8_t)(x >> 8));
        }
        return v;
    }
    void load_ext(const uint8_t* d, size_t n) {
        lim_valid_ = false;
        loop_frame_ = 0x80000000u;
        d_off_ = 0;
        for (bool& b : oam_set_) b = false;
        for (bool& b : build_set_) b = false;
        respawn_ = true;   // (a state without the module's data: the margins' objects)
        logic_done_ = band_taken_ = false;
        band_next_.valid = band_shown_.valid = false;
        // (a state without it: a level being played if one is on screen)
        armed_ = snes_ && playing_level(*snes_) && in_level(*snes_);
        const size_t need1 = 2 + 5 * 2 + 128 * 3, need2 = 3 + 7 * 2 + 2 * 128 * 3, need3 = need2 + 1 + 7 * 2;
        if (n < 2 || d[0] < 1 || d[0] > 4 || (d[0] == 1 && n < need1) || (d[0] == 2 && n < need2) ||
            (d[0] >= 3 && n < need3))
            return;
        level_ = (d[1] & 1) != 0;
        if (d[0] >= 4) armed_ = (d[1] & 32) != 0;
        lim_valid_ = (d[1] & 2) != 0;
        size_t o = 2;
        stop_ = true;
        if (d[0] >= 2) {
            stop_ = (d[1] & 4) != 0;
            if (snes_ && d[2] < 255) loop_frame_ = snes_->frameCount() - d[2];
            o = 3;
        }
        const int count = d[0] >= 2 ? 7 : 5;
        int v[7] = {};
        for (int i = 0; i < count; i++, o += 2) v[i] = (int16_t)(d[o] | (d[o + 1] << 8));
        lim_lo_ = v[1]; lim_hi_ = v[2]; d_off_ = v[3]; last_cam_ = v[4];
        reach_lo_ = d[0] >= 2 ? v[5] : lim_lo_;
        reach_hi_ = d[0] >= 2 ? v[6] : lim_hi_;
        // the margins' objects: spawned again for a state made at another width (or saved with that pending)
        respawn_ = v[0] != ext_ || (d[0] >= 2 && (d[1] & 16));
        auto get = [&](int16_t* x, bool* set) {
            for (int i = 0; i < 128; i++, o += 3) {
                x[i] = (int16_t)(d[o] | (d[o + 1] << 8));
                set[i] = d[o + 2] != 0;
            }
        };
        get(oam_x_, oam_set_);
        if (d[0] >= 2) get(build_x_, build_set_);
        if (d[0] >= 3) {
            const uint8_t f = d[o++];
            int b[7];
            for (int i = 0; i < 7; i++, o += 2) b[i] = (int16_t)(d[o] | (d[o + 1] << 8));
            logic_done_ = (f & 1) != 0;
            band_taken_ = (f & 2) != 0;
            band_next_.valid = (f & 4) != 0;
            band_shown_.valid = (f & 8) != 0;
            band_d_ = b[0] & 0xFFF;
            band_next_.cam = b[1]; band_next_.vcam = b[2]; band_next_.t = (uint16_t)b[3];
            band_shown_.cam = b[4]; band_shown_.vcam = b[5]; band_shown_.t = (uint16_t)b[6];
        }
    }

private:
    struct LineLayer {
        bool decode = false;
        int wx = 0, wy = 0;   // world x of the game's screen's left edge and world y on this line
        int lo = 0, hi = 0;   // the layer's extent (mirrored beyond)
        bool rep = false;     // drawn from the picture's first 512 px, its repeat (world_bands)
        uint16_t off = 0;
        uint8_t bank = 0;
    };
    // Patch-ROM stub: the level start's object spawn past its set-up (kSpawnColumns, entered as $82:A72C has it: DP 0,
    // DB $82, D and B on the stack under the return address), then back to the main loop's top.
    static constexpr uint32_t kStubRespawn = 0x608000;
    // Patch-ROM stubs for the spawner's edge columns (right / left): JSL kStubColumn (one column, X), then
    // JML $82:A7EE; an exec hook after the JSL (+4) sends X round again for the next column.
    static constexpr uint32_t kStubRetryR = 0x608020, kStubRetryL = 0x608028, kStubColumn = 0x608030;

    const Snes* snes_ = nullptr;
    GameConfig cfg_;
    int ext_ = 0, last_ext_ = -1;
    bool level_ = false, respawn_ = false;
    bool armed_ = false;   // a level being played since its start spawn (active)
    uint32_t loop_frame_ = 0x80000000u;   // the frame the level loop's top last ran in
    bool lim_valid_ = false;
    int lim_lo_ = 0, lim_hi_ = 0, d_off_ = 0, last_cam_ = 0;   // where the view stops; its offset still to close
    bool stop_ = true;                    // the view may stop at the level's ends (can_stop)
    int reach_lo_ = 0, reach_hi_ = 0;     // where the game's camera can show now (beyond: dimmed)
    int el_ = 0, er_ = 0, mid_ = 0;
    int bg2_ratio_ = 0;      // this frame: BG2's x speed against the camera (8.8), 0 = scrolled by the game itself
    bool obj_bg2_ = false;   // this line: BG2 holds an object's picture (obj_lo_ / obj_hi_: its own copy on screen)
    int obj_lo_ = 0, obj_hi_ = 0;
    int dim_lo_ = -kMaxExt, dim_hi_ = 256 + kMaxExt;
    LineLayer line_[2];
    // A background the game scrolls itself ($083F modes $0A-$14): the inputs of its scroll the module can't read off
    // the screen, taken when a frame's logic is done (band_next_) and used while that frame is shown (band_shown_).
    struct BandSnap {
        bool valid = false;
        int cam = 0, vcam = 0;   // the game's camera and the picture's middle's (the virtual camera)
        uint16_t t = 0;          // $1A30 + camera: the drifting (and in mode $0A ratcheting) bands' sum
    };
    BandSnap band_next_, band_shown_;
    int band_d_ = 0;               // mode $0A: the ratchet's lead at the virtual camera over the game's (mod 4096)
    bool logic_done_ = false;      // the level loop reached its frame wait since its top
    bool band_taken_ = false;      // band_next_ is this logic frame's
    bool band_on_ = false;         // this frame: BG2 corrected per line (band_adj)
    int band_cam_ = 0, band_vcam_ = 0;
    int hdma_line_ = -1;           // this frame: the line the IRQ switched HDMA on at (-1: not yet)
    int16_t build_x_[128] = {}, oam_x_[128] = {};
    bool build_set_[128] = {}, oam_set_[128] = {};

    static void write_stub(Snes& s) {
        PatchAsm a(s, kStubRespawn);
        const uint32_t entry = kStubRespawn + 8;
        for (uint8_t b : jsl(entry)) a.b8(b);                           // JSL entry
        for (uint8_t b : jml(kLevelLoop)) a.b8(b);                      // JML $81:82CF
        a.b({0xE2, 0x30, 0x0B, 0xA9, 0x00, 0x48, 0x48, 0x2B});           // entry: SEP #$30, PHD, LDA #0, PHA x2, PLD
        a.b({0x8B, 0xA9, 0x82, 0x48, 0xAB});                            // PHB, LDA #$82, PHA, PLB
        for (uint8_t b : jml(kSpawnColumns)) a.b8(b);                   // JML $82:A742 (its RTL comes back here)
        // The edge columns (from the exec hook at the JSR $A89D of the spawner's new edge column: DP 0, DB $82, 8-bit
        // registers).
        for (uint32_t at : {kStubRetryR, kStubRetryL}) {
            PatchAsm c(s, at);
            for (uint8_t b : jsl(kStubColumn)) c.b8(b);                 // JSL column (X)
            for (uint8_t b : jml(kSpawnEdgeDone)) c.b8(b);              // JML $82:A7EE
        }
        PatchAsm c(s, kStubColumn);
        c.b({0x0B, 0x8B, 0xE2, 0x30, 0xA0, 0x01});                      // PHD, PHB, SEP #$30, LDY #1
        for (uint8_t b : jml(kSpawnOneColumn)) c.b8(b);                 // JML $82:A780 (its PLB PLD RTL comes back)
    }
    // The columns for the spawner's new edge column, in order: the original's edge column (camera + $120 right, - $40
    // left, as $82:A742 works it out) and the one before it on that side (the original's crossings are up to 31 px
    // off the 32-px grid), then the spawner's own (the margin's), each once.
    static int retry_columns(const Snes& s, bool right, uint8_t* out) {
        const int cam = rd16(s, kCamX);
        const int c = (((cam + (right ? 0x120 : -0x40)) & 0xFFFF) >> 5) & 0xFF;
        int n = 0;
        auto add = [&](int col) {
            for (int i = 0; i < n; i++)
                if (out[i] == (uint8_t)col) return;
            out[n++] = (uint8_t)col;
        };
        const int own = rd8(s, right ? kSpawnRight : kSpawnLeft);
        if ((uint8_t)(right ? c - 1 : c + 1) != own) add(right ? c - 1 : c + 1);
        if ((uint8_t)c != own) add(c);
        add(own);
        return n;
    }
    // The next of those to spawn after index `i`: one of the original's with an object not out that the spawner would
    // take now ($82:A8C0: spawned bits & $81 clear, y within [$1BF1 - $40, + $140)), else the spawner's own (the last).
    static int next_column(const Snes& s, const uint8_t* cols, int n, int i) {
        for (i++; i < n - 1; i++) {
            const int count = rd8(s, kColCount + cols[i]), first = rd8(s, kColFirst + cols[i]);
            for (int k = 0; k < count; k++) {
                const int idx = (first + 1 + k) & 0xFF;
                const uint32_t e = kObjList + (uint32_t)((first + k) * 6 + rd16(s, kObjListOff));
                const uint16_t dy = (uint16_t)(rom16(s, e + 4) + 0x40 - rd16(s, kSpawnCamY));
                if (!(rd8(s, kSpawnedBits + idx) & 0x81) && dy < 0x180) return i;
            }
        }
        return n - 1;
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

    // The level's extent: [0, last camera x + 256) without the columns of pages no row of the camera's reach may enter
    // (bit 15 from top to bottom: the Underground River's first column; the camera never shows them).
    static void level_extent(const Snes& s, int& lo, int& hi) {
        lo = 0;
        hi = std::max(256, (int16_t)rd16(s, kCamMaxX) + 256);
        const int rows = (std::max(0, (int)(int16_t)rd16(s, kCamMaxY)) + 223) >> 8;
        auto wall = [&](int px) {
            for (int py = 0; py <= rows; py++)
                if (!barrier(s, px, py)) return false;
            return true;
        };
        int c0 = 0, c1 = (hi - 1) >> 8;
        while (c0 < c1 && wall(c0)) c0++;
        while (c1 > c0 && wall(c1)) c1--;
        lo = c0 * 256;
        hi = std::min(hi, (c1 + 1) * 256);
    }
    // Where the view stops: the level's ends (level_extent); an arena holding the camera, the picture around its
    // screen - centred on it, kept inside the level where the level is as wide as the picture (an arena at a level's
    // end: the Cave of Wonders' last event), else centred. (Not the zones' limits nor the pages the camera can't enter
    // at its height: they change as the camera goes, and the view would have to pan by itself; past them the margins
    // are dimmed / mirrored instead.)
    void span(const Snes& s, int cam, int& lo, int& hi) const {
        level_extent(s, lo, hi);
        if (arena(s)) {
            const int vw = 256 + 2 * ext_;
            if (hi - lo >= vw) {
                lo = std::clamp(cam - ext_, lo, hi - vw);
                hi = lo + vw;
            } else {
                lo = cam;
                hi = cam + 256;
            }
        }
    }
    // The camera held in x while it follows in y: a fixed arena (Jafar's). (Both held: a death's fall.)
    static bool arena(const Snes& s) { return rd16(s, kCamHoldX) && !rd16(s, kCamHoldY); }
    // Whether every layer can follow a view stopped at a level's end (section 3 of docs/re_snes_wide.md): BG2 at a
    // known speed (modes 0-8: the parallax shift), or scrolled by the game in a way the module follows line by line
    // (modes $0A-$14: band_adj).
    static bool can_stop(const Snes& s) {
        const int mode = rd16(s, kBgMode);
        return (mode & 1) == 0 && mode <= 0x14;
    }
    // The modes ($81:C781) in which the game scrolls BG2 itself: raster bands from HDMA tables ($0A Agrabah Market and
    // Market at Dusk, $0C the Lava Cavern, $0E Agrabah Rooftops), by phases ($10 Jafar's Palace 3), an object ($12 the
    // Carpet Escape's wave), with the camera but moving in y by itself ($14 Sultan's Dungeon 3).
    static bool band_mode(int mode) { return (mode & 1) == 0 && mode >= 0x0A && mode <= 0x14; }
    // Agrabah's: bands by world y over a picture that repeats every 512 px (the VRAM map's width: a band at another
    // speed than BG2's struct shows the map's repeat), the routines $81:C952 ($0E) and $81:C98C ($0A).
    static bool world_bands(int mode) { return mode == 0x0A || mode == 0x0E; }
    // Their band k of n ($7E:F600 + 2k): a shift of the drifting sum t = $1A30 + camera (mode $0A's first four, which
    // also ratchet: band_d_; mode $0E's first two, t / 2 and t / 4), else a function of the camera x: mode $0A's
    // camera / 2 and camera * 5/4; mode $0E fills its bands from the bottom up, each half the one below it, camera /
    // 2^(n-k) (the Rooftops: / 32, / 16, / 8, / 4, / 2 below the sky).
    static int band_sum_shift(int mode, int k) { return mode == 0x0A ? (k <= 3 ? k : -1) : (k <= 1 ? k + 1 : -1); }
    static int band_cam(int mode, int n, int k, int x) {
        if (mode == 0x0A) return k == 5 ? x + (x >> 2) : x >> 1;
        return x >> (n - k);
    }
    // The first row of this frame's HDMA tables' second entry: the IRQ switches the channel on (its NMI set the table's
    // start and a line count of 1), the first entry's line follows, then the second reaches the screen (kHdmaRows).
    // Before the IRQ (or in a frame without one) every line has the NMI's scroll, the first entry's.
    int hdma_row0() const { return hdma_line_ >= 0 ? hdma_line_ + kHdmaRows : 0x10000; }

    // Mode $10's x: the camera, at half speed past $1000 once Jafar's Palace 3 reaches its last part ($81:CA55).
    static int phase_x(int sub, int x) { return sub == 0 || x < 0x1000 ? x : ((x - 0x1000) >> 1) + 0x1000; }
    static int floor_shr(int v, int k) { return v >= 0 ? v >> k : -((-v + (1 << k) - 1) >> k); }

    // At the end of a frame's logic: what the band routines take from it ($81:C781, run with the camera's move).
    // Mode $0A's routine ($81:C98C) holds its first four bands still while the camera moves right: $1A30 drops by the
    // move, so band 0 = $1A30 + camera keeps its place (drifting 3/4 px a frame); with the camera at the picture's
    // middle the game would drop it by that camera's moves instead (band_d_ the difference).
    void take_band(const Snes& s, int cam, bool jump) {
        BandSnap b;
        b.valid = band_mode(rd16(s, kBgMode));
        b.cam = cam;
        b.vcam = cam - split_left(cam) + ext_;
        b.t = (uint16_t)(rd16(s, kBgDrift) + cam);
        if (b.valid && rd16(s, kBgMode) == 0x0A && band_next_.valid && !jump) {
            const int dc = (int16_t)(cam - rd16(s, kCamPrev)), dv = b.vcam - band_next_.vcam;
            band_d_ = (band_d_ + std::max(0, dc) - std::max(0, dv)) & 0xFFF;
        }
        band_next_ = b;
    }

    // One line of a background the game scrolls itself: the scroll its code would give with the camera at the
    // picture's middle (v) against the one it gave (c, on screen: the PPU's register), as a shift of the line on top of
    // the view's (v - c). The bands' values and their lines, as the band routines ($81:C84B, $81:C7E7) build the HDMA
    // tables: Agrabah's by world y from camera y + 8 (bounds $80:C606), the lava's fixed; the second entry's first row
    // from the line the IRQ switched the channel on at this frame (hdma_row0: row 11 from line 8, the Rooftops' row
    // 15 from line 12). A line whose register isn't the band's value as worked out (a frame the logic didn't finish
    // in) takes it from the register (a sum's low bits unknown: within 1 px). Agrabah's band lines are drawn from
    // their picture's 512-px repeat (world_bands), also before BG2's first column where the view stops at a level's
    // start (the page table's border column isn't that repeat in every row: the Rooftops' palace and bottom rows, the
    // Market's sky); Jafar's Palace 3's background at half speed is mirrored past what the game shows of it (the line's
    // extent, `ll`).
    int band_adj(const Ppu& p, int row, LineLayer& ll) const {
        const Snes& s = *snes_;
        const int mode = rd16(s, kBgMode);
        const int c = band_cam_, v = band_vcam_, h = p.bgHofs(1) & 0x3FF;
        int dh = v - c;   // (a layer moving with the camera: no shift)
        if (world_bands(mode)) {
            const uint32_t idx = mode == 0x0A ? 2 : 0;
            const int n = rom16(s, kBandCount + idx) >> 1;
            const uint32_t bounds = kBandBounds + rom16(s, kBandBounds + idx);
            const int camy = unwrap10(p.bgVofs(1), (int16_t)rd16(s, kCamY));
            const int y = camy + 8 + std::max(0, row - hdma_row0());
            int k = 0;
            while (k + 1 < n && y >= rom16(s, bounds + 2 * (k + 1))) k++;
            // band k's value for the camera c (-1: a sum not known for this frame)
            const bool known = band_shown_.valid && band_shown_.cam == c;
            auto value = [&](int band) -> int {
                const int sh = band_sum_shift(mode, band);
                if (sh >= 0) return known ? (band_shown_.t >> sh) & 0x3FF : -1;
                return band_cam(mode, n, band, c) & 0x3FF;
            };
            const int vk = value(k);
            if (vk >= 0 && vk != h) {   // (a frame the game's logic didn't finish in)
                if (k > 0 && value(k - 1) == h) k--;
                else if (k + 1 < n && value(k + 1) == h) k++;
            }
            const int sh = band_sum_shift(mode, k);
            if (sh >= 0) {
                const int low = value(k) == h ? band_shown_.t & ((1 << sh) - 1) : 0;
                dh = floor_shr(low + (v - c) + (mode == 0x0A ? band_d_ : 0), sh);
            } else {
                dh = band_cam(mode, n, k, v) - band_cam(mode, n, k, c);
            }
        } else if (mode == 0x0C) {
            // the Lava Cavern: every band drifts at its own speed but one, which moves with the camera (band 9)
            int band = 0;
            const int row0 = hdma_row0();
            if (row >= row0) {
                int start = row0;
                for (int e = 1; e < 16; e++) {
                    const int cnt = rd8(s, kLavaTable + 3 * e) & 0x7F;
                    if (cnt == 0) break;
                    band = e;
                    if (row < start + cnt) break;
                    start += cnt;
                }
            }
            dh = band == 9 ? v - c : 0;
        } else if (mode == 0x10) {
            const int sub = rd16(s, kBgSub);
            dh = phase_x(sub, v) - phase_x(sub, c);
            // (past what the game shows of it at the level's end its last pages hold other data: mirrored there)
            if (sub != 0) ll.hi = phase_x(sub, (int16_t)rd16(s, kCamMaxX)) + 256;
        }
        return dh - (v - c);
    }

    // Where the game's camera can show now (beyond: dimmed): the level, the zone's camera limits, an arena, and the
    // pages the camera may enter in the rows its screen covers (bit 15: a page it may not).
    static void reach(const Snes& s, int cam, int camy, int& lo, int& hi) {
        level_extent(s, lo, hi);
        if (arena(s)) {
            lo = cam;
            hi = cam + 256;
        }
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

    // The view's left edge (world x) for the camera within [lo, hi): centred on the camera, stopped at the edges
    // (where it may stop); a range narrower than the picture centred.
    int target_v(int cam, int lo, int hi, bool stop) const {
        const int vw = 256 + 2 * ext_;
        if (!stop) return cam - ext_;
        if (hi - lo <= vw) return lo + (hi - lo - vw) / 2;
        return std::clamp(cam - ext_, lo, hi - vw);
    }
    int split_left(int cam) const {
        return std::clamp(cam - (target_v(cam, lim_lo_, lim_hi_, stop_) + d_off_), 0, 2 * ext_);
    }

    void frame_start(Ppu& p) {
        el_ = er_ = ext_;
        mid_ = 0;
        dim_lo_ = -kMaxExt;
        dim_hi_ = 256 + kMaxExt;
        band_on_ = false;
        hdma_line_ = -1;   // (the IRQ comes after the first lines)
        if (level_ && ext_ > 0 && snes_) {
            const Snes& s = *snes_;
            // the camera on screen: BG1's scroll register, completed from the NMI's copy
            const int cam = unwrap10(p.bgHofs(0), (int16_t)rd16(s, kBg1Shown));
            int rlo = reach_lo_, rhi = reach_hi_;
            if (lim_valid_) {
                el_ = split_left(cam);
            } else {
                // (a state made at another width just loaded, the width set from 4:3: where the view stops already)
                int lo, hi;
                span(s, cam, lo, hi);
                el_ = std::clamp(cam - target_v(cam, lo, hi, can_stop(s)), 0, 2 * ext_);
                reach(s, cam, (int16_t)rd16(s, kCamY), rlo, rhi);
            }
            er_ = 2 * ext_ - el_;
            mid_ = (er_ - el_) / 2;
            dim_lo_ = rlo - cam;
            dim_hi_ = rhi - cam;
            // the band routine's inputs for the frame on screen (the one its logic finished for this camera)
            if (band_next_.valid && band_next_.cam == cam) band_shown_ = band_next_;
            band_on_ = band_mode(rd16(s, kBgMode));
            band_cam_ = cam;
            band_vcam_ = cam - el_ + ext_;
        }
        p.setExt(el_, er_);
        // BG2's speed against the camera ($083F: the level's background mode, $81:C781; modes $0A-$14 scroll it
        // themselves: band_adj)
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
// The levels in the order the game plays them: stage, act (the level index the game derives in the comments).
// Stage 6 (A Whole New World) comes between the dungeon and the palace ($80:B988, the stage after each stage); the
// last, stage 7, is Jafar (level $11, which is also the palace's act 3, unused by the game's progression).
struct Level { uint8_t stage, act; const char* group; const char* name; };
const Level kLevels[] = {
    {0, 0, "Agrabah", "Agrabah Market"},                // $00
    {0, 1, "Agrabah", "Desert Ruins"},                  // $01
    {0, 2, "Agrabah", "Agrabah Rooftops"},              // $02
    {0, 3, "Agrabah", "Market at Dusk"},                // $03
    {1, 0, "Cave of Wonders", "Cave of Wonders"},       // $04
    {1, 1, "Cave of Wonders", "Underground River"},     // $05
    {2, 0, "Escape", "Lava Cavern"},                    // $06
    {2, 1, "Escape", "Carpet Escape"},                  // $07
    {3, 0, "Inside the Lamp", "Inside the Lamp 1"},     // $08
    {3, 1, "Inside the Lamp", "Inside the Lamp 2"},     // $09
    {3, 2, "Inside the Lamp", "Inside the Lamp 3"},     // $0A
    {4, 0, "Sultan's Dungeon", "Sultan's Dungeon 1"},   // $0B
    {4, 1, "Sultan's Dungeon", "Sultan's Dungeon 2"},   // $0C
    {4, 2, "Sultan's Dungeon", "Sultan's Dungeon 3"},   // $0D
    {6, 0, "A Whole New World", "A Whole New World"},   // $12
    {5, 0, "Jafar's Palace", "Jafar's Palace 1"},       // $0E
    {5, 1, "Jafar's Palace", "Jafar's Palace 2"},       // $0F
    {5, 2, "Jafar's Palace", "Jafar's Palace 3"},       // $10
    {7, 0, "Jafar's Palace", "Jafar"},                  // $11
};
constexpr int kLevelCount = (int)(sizeof(kLevels) / sizeof(kLevels[0]));

// Code for the cheats and the level select.
constexpr uint32_t kLevelCompute = 0x818157;   // the level index from stage / act (at GAME START: the first time)
constexpr uint32_t kHit = 0x838024;            // a hit (ignored while $08E3 bit 7, the blink after a hit: RTS $8051)
constexpr uint32_t kHitSkip = 0x838051;
constexpr uint32_t kHitAlways = 0x838004;      // a hit regardless of the blink: JSR $8008 / RTL
constexpr uint32_t kHitAlwaysRtl = 0x838007;
constexpr uint32_t kHeartLost = 0x838010, kHeartLost2 = 0x838031;   // DEC $0367 in the two hit paths
constexpr uint32_t kLifeLost = 0x81DC67, kLifeLost2 = 0x81E837;     // DEC $0364 (a life lost; game over at 0)
constexpr uint32_t kAppleThrown = 0x81CE5C;    // STA $0369: one apple fewer
// A game's start reads the debug flag for its 10 hearts / 99 apples (LDA $0009): the attract demos' start too.
constexpr uint32_t kDebugStart = 0x8180ED, kDebugStart2 = 0x84AEFD;

// ---- Sound (Capcom's driver; docs/re_snes_game.md, Sound). The CPU sends the APU one queued byte a frame from the
// NMI ($80:B463; ring $03BF, 32 bytes). A sound effect: JSL $81:FA00 with A = its id (queued). A song: JSL $81:FA23
// with A = its upload index (command $FA and the song's data block, table $85:8000; the song starts when the upload
// ends); OPTIONS' B.G.M. test plays upload $10 + n. $F2 (JSL $81:F9F7) stops the music.
constexpr uint32_t kSfx = 0x81FA00, kSong = 0x81FA23;
// The driver's stop commands: $F2 the music only ($81:F9F7), $F1 the effects only, $F0 both (and later songs and
// effects play as before; $F9 silences the driver for good).
constexpr uint8_t kStopAll = 0xF0;
// The music player parks the CPU right after the boot uploads the driver ($81:8021 JSL $80:B4AA, before the logos
// and their jingle) in a stub in patch ROM that waits for requests in a RAM mailbox (cmd: 1 song, 2 effect, 3 stop).
constexpr uint32_t kJukeboxPark = 0x818025;
constexpr uint32_t kJukeboxStub = 0x608100;
constexpr uint32_t kMailCmd = 0x1FF8, kMailId = 0x1FF9, kMailBefore = 0x1FFA;

// ---- Achievements: screens by level index $035E (levels are $00-$13, the attract demos' $14-$1C).
constexpr uint32_t kNmiCount = 0x032E;          // counted by the NMI
constexpr uint32_t kContinues = 0x035B;         // continues left (3 at GAME START)
constexpr int kScreenFirst = 0x14;
constexpr int kScreenStageClear = 0x24, kScreenContinue = 0x25, kScreenPassword = 0x2C, kScreenBonus = 0x2F;
constexpr int kScreenNextAct = 0x30, kScreenEnding = 0x39;   // (between two parts of a stage; the ending's story)
constexpr uint32_t kTitleWaitRet = 0x809FBE, kOptionsWaitRet = 0x838A6F;   // the main task waiting in those screens
const uint8_t kLevelStage[kScreenFirst] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 7, 6, 8};

// The main task's wait: the return address its frame wait left on its stack (task 0, saved stack pointer $0071).
inline uint32_t main_wait(const RamViewLE& m) {
    const uint16_t sp = m.w(0x0071);
    if (sp < 0x0100 || sp > 0x0170) return 0;
    return m.w(sp + 7) | ((uint32_t)m.b(sp + 9) << 16);
}

// The jukebox's tracks: song upload index, name, seconds (one pass); then sound effects (id + $100).
struct Tune { int id; const char* name; int seconds; int before; };
// Songs: their upload index (the B.G.M. test's number + $10), and seconds for the jingles (0: a loop). Songs written to
// follow another one inherit its volume settings and are silent after the boot alone: `before` is the song the game
// plays ahead of them, played first (its upload is replaced at once).
const Tune kSongs[] = {
    {0x10, "Capcom Logo", 3, 0},
    {0x12, "Title Fanfare", 8, 0},
    {0x13, "Title", 0, 0},
    {0x1C, "Story: Welcome to Agrabah", 0, 0x10},
    {0x1F, "Story: Jafar", 0, 0x10},
    {0x15, "Agrabah", 0, 0},
    {0x16, "Cave of Wonders", 0, 0},
    {0x17, "Escape", 0, 0},
    {0x18, "Inside the Lamp", 0, 0},
    {0x19, "Sultan's Dungeon", 0, 0},
    {0x1B, "A Whole New World", 0, 0},
    {0x1A, "Jafar's Palace", 0, 0},
    {0x2E, "Jafar's Throne Room", 0, 0},
    {0x22, "Jafar", 0, 0},
    {0x21, "Boss", 0, 0},
    {0x25, "Danger", 0, 0},
    {0x30, "Chase (Intro)", 0, 0x16},
    {0x2F, "Chase", 0, 0},
    {0x31, "Magic Carpet", 7, 0},
    {0x28, "Area Clear", 1, 0},
    {0x27, "Stage Clear", 8, 0},
    {0x29, "Break Time! (Bonus Wheel)", 0, 0},
    {0x2A, "Lost a Life", 2, 0},
    {0x2B, "Continue", 0, 0},
    {0x2D, "Password", 0, 0},
    {0x1E, "Story: Aladdin and Jasmine", 0, 0x27},
    {0x11, "Story: The Old Man", 0, 0x27},
    {0x35, "Story: Trapped in the Cave", 0, 0x27},
    {0x1D, "Story: The Genie", 0, 0x27},
    {0x36, "Jafar Defeated", 10, 0},
    {0x32, "Ending: Jafar's Wish", 0, 0},
    {0x34, "Ending: Absolute Power", 14, 0},
    {0x23, "Ending: Into the Lamp", 8, 0},
    {0x24, "Ending: Free at Last", 0, 0},
    {0x26, "Staff Roll", 0, 0},
    {0x14, "Title Logo", 3, 0},
    {0x20, "Short Fanfare", 2, 0},
    {0x2C, "Jingle", 3, 0},
    {0x33, "Short Jingle", 3, 0},
};
// Sound effects (id + $100): the ones whose use was traced are named, the others numbered. Not listed: $0D and $17,
// which key off looping effects ($3F; $2A and $33), and $19 and $29, which sound nothing on their own.
const Tune kEffects[] = {
    {0x101, "Jump", 0, 0}, {0x102, "Fall", 0, 0}, {0x103, "Throw an Apple", 0, 0}, {0x104, "Effect 04", 0, 0},
    {0x105, "Effect 05", 0, 0}, {0x106, "Land", 0, 0}, {0x107, "Effect 07", 0, 0}, {0x108, "Effect 08", 0, 0},
    {0x109, "Heart / Extra Life", 0, 0}, {0x10A, "Effect 0A", 0, 0}, {0x10B, "Pick Up", 0, 0},
    {0x10C, "Cursor", 0, 0}, {0x10E, "Pause", 0, 0}, {0x10F, "Enemy Hit", 0, 0},
    {0x110, "Effect 10", 0, 0}, {0x111, "Effect 11", 0, 0}, {0x112, "Enemy Defeated", 0, 0},
    {0x113, "Effect 13", 0, 0}, {0x114, "Effect 14", 0, 0}, {0x115, "Effect 15", 0, 0}, {0x116, "Effect 16", 0, 0},
    {0x118, "Effect 18", 0, 0}, {0x11A, "Explosion", 0, 0},
    {0x11B, "Effect 1B", 0, 0}, {0x11C, "Effect 1C", 0, 0}, {0x11D, "Effect 1D", 0, 0}, {0x11E, "Effect 1E", 0, 0},
    {0x11F, "Effect 1F", 0, 0}, {0x120, "Effect 20", 0, 0}, {0x121, "Effect 21", 0, 0}, {0x122, "Effect 22", 0, 0},
    {0x123, "Effect 23", 0, 0}, {0x124, "Effect 24", 0, 0}, {0x125, "Effect 25", 0, 0}, {0x126, "Effect 26", 0, 0},
    {0x127, "Effect 27", 0, 0}, {0x128, "Secret", 0, 0}, {0x12A, "Effect 2A", 0, 0},
    {0x12B, "Effect 2B", 0, 0}, {0x12C, "Effect 2C", 0, 0}, {0x12D, "Effect 2D", 0, 0}, {0x12E, "Effect 2E", 0, 0},
    {0x12F, "Effect 2F", 0, 0}, {0x130, "Effect 30", 0, 0}, {0x131, "Gem", 0, 0}, {0x132, "Effect 32", 0, 0},
    {0x133, "Effect 33", 0, 0}, {0x134, "Effect 34", 0, 0}, {0x135, "Effect 35", 0, 0}, {0x136, "Effect 36", 0, 0},
    {0x137, "Effect 37", 0, 0}, {0x138, "Effect 38", 0, 0}, {0x139, "Effect 39", 0, 0},
    {0x13A, "Bonus Wheel Spin", 0, 0}, {0x13B, "Bonus Wheel Stop", 0, 0}, {0x13C, "Effect 3C", 0, 0},
    {0x13D, "Effect 3D", 0, 0}, {0x13E, "Effect 3E", 0, 0}, {0x13F, "Effect 3F", 0, 0}, {0x140, "Effect 40", 0, 0},
    {0x141, "Effect 41", 0, 0}, {0x142, "Effect 42", 0, 0}, {0x143, "Effect 43", 0, 0}, {0x144, "Effect 44", 0, 0},
    {0x145, "Effect 45", 0, 0}, {0x146, "Effect 46", 0, 0}, {0x147, "Effect 47", 0, 0}, {0x148, "Effect 48", 0, 0},
    {0x149, "Jafar", 0, 0}, {0x14A, "Effect 4A", 0, 0}, {0x14B, "Effect 4B", 0, 0}, {0x14C, "Effect 4C", 0, 0},
    {0x14D, "Effect 4D", 0, 0}, {0x14E, "Effect 4E", 0, 0}, {0x14F, "Game Start", 0, 0}, {0x150, "Effect 50", 0, 0},
};

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
    // The opening story as its music starts (after the Capcom logo), then the game's own attract loop (the title, the
    // demo). Not the title: it is silent after its jingle until the demo, and the card would be quiet when chosen.
    int preview_start() const override { return 760; }
    // Its music is mixed about 8 dB quieter than Super Star Wars' on the same sound chip (RMS ~1000 against ~3000).
    float audio_gain() const override { return 1.8f; }

    void configure(Snes& s, const GameConfig& cfg) override {
        apply_patches(s, {});
        s.clearExecMarks();
        // Remove Slowdown: the CPU's speed for the game's frame work (turboActive is set from the top of the level's
        // main loop to the task's yield, in a level being played - not a demo -, in the wide view's hooks).
        s.cpuTurbo = std::max(1, cfg.cpu_turbo);
        if (s.cpuTurbo == 1) s.turboActive = false;
        wide_.configure(s, cfg);
        // Cheats that change code paths run as exec hooks (no ROM bytes change, so they switch at any time).
        invincible_ = cfg.cheat("invincible");
        inf_health_ = cfg.cheat("inf_health");
        inf_lives_ = cfg.cheat("inf_lives");
        inf_apples_ = cfg.cheat("inf_apples");
        const bool debug = cfg.cheat("debug_mode");
        if (debug_ && !debug) clear_debug_ = true;   // (switched off: the game's own flag goes back off)
        debug_ = debug;
        if (invincible_) for (uint32_t pc : {kHit, kHitAlways}) s.markExec(pc, true);
        if (inf_health_) for (uint32_t pc : {kHeartLost, kHeartLost2}) s.markExec(pc, true);
        if (inf_lives_) for (uint32_t pc : {kLifeLost, kLifeLost2}) s.markExec(pc, true);
        if (inf_apples_) s.markExec(kAppleThrown, true);
        if (debug) for (uint32_t pc : {kDebugStart, kDebugStart2}) s.markExec(pc, true);
        if (ls_.active) s.markExec(kLevelCompute, true);
        write_jukebox_stub(s);
        if (jb_park_) s.markExec(kJukeboxPark, true);
    }

    // ---- Music player
    std::vector<SoundEntry> sounds() const override {
        std::vector<SoundEntry> v;
        for (const Tune& t : kSongs) v.push_back({t.id, t.name, true, t.seconds});
        for (const Tune& t : kEffects) v.push_back({t.id, t.name, false, t.seconds});
        return v;
    }
    void jukebox_boot(Snes& s) override {
        s.reset(true);
        write_jukebox_stub(s);
        jb_park_ = true;
        s.markExec(kJukeboxPark, true);
        s.skipRender = true;
        run_until(s, [&]() { return s.cpu.r.pb == (kJukeboxStub >> 16); }, 600);
        run_frames(s, 8);
        s.skipRender = false;
        s.markExec(kJukeboxPark, false);
        jb_park_ = false;
    }
    void jukebox_frame(Snes& s) override { s.input[0] = s.input[1] = 0; }
    void play_sound(Snes& s, int id) override {
        int before = 0;
        for (const Tune& t : kSongs) if (t.id == id) before = t.before;
        wr8(s, kMailId, (uint8_t)id);
        wr8(s, kMailBefore, (uint8_t)before);
        wr8(s, kMailCmd, id >= 0x100 ? 2 : 1);
    }
    void stop_sound(Snes& s) override { wr8(s, kMailCmd, 3); }

    bool exec_hook(Snes& s, uint32_t pc) override {
        CpuRegs& r = s.cpu.r;
        const bool cheats = !demo_playing(s);   // (a demo plays as recorded: no cheat changes its course)
        switch (pc) {
        case kJukeboxPark:
            if (!jb_park_) break;
            r.pb = (uint8_t)(kJukeboxStub >> 16);
            r.pc = (uint16_t)kJukeboxStub;
            return true;
        case kHit:          // as if blinking: the hit doesn't land (A negative, as the game's own skip returns it)
            if (!invincible_ || !cheats) break;
            r.a = (uint16_t)((r.a & 0xFF00) | 0x80);
            r.n = true;
            r.z = false;
            r.pc = (uint16_t)kHitSkip;
            return true;
        case kHitAlways:
            if (!invincible_ || !cheats) break;
            r.pc = (uint16_t)kHitAlwaysRtl;
            return true;
        case kHeartLost:    // the hit lands (knocked back, blinking) but no heart is lost
        case kHeartLost2:
            if (!inf_health_ || !cheats) break;
            r.pc = (uint16_t)(pc + 3);
            return true;
        case kLifeLost:     // the DEC still runs (its flags decide the game over) on a count raised by one first
        case kLifeLost2:
            if (inf_lives_ && cheats) wr8(s, kLives, (uint8_t)(rd8(s, kLives) + 1));
            break;
        case kAppleThrown:
            if (!inf_apples_ || !cheats) break;
            r.pc = (uint16_t)(pc + 3);
            return true;
        case kDebugStart:   // (a demo starts as recorded: without the developer mode's hearts and apples)
        case kDebugStart2:
            if (cheats) break;
            r.a &= 0xFF00;   // LDA $0009 (8-bit) read as 0
            r.z = true;
            r.n = false;
            r.pc = (uint16_t)(pc + 3);
            return true;
        case kLevelCompute:
            // The level select: GAME START's first level computation gets the chosen stage and act, as the game's own
            // debug stage select on the title screen gives them; the game then starts that level as usual.
            if (ls_.active && !ls_.done && rd8(s, kDemo) == 0) {
                wr8(s, kStage, ls_.stage);
                wr8(s, kAct, ls_.act);
                ls_.done = true;
            }
            break;
        default:
            break;
        }
        return wide_.exec_hook(s, pc);
    }

    void pre_frame(Snes& s, const GameConfig& cfg) override {
        if (clear_debug_) {
            wr8(s, kDebug, 0);
            clear_debug_ = false;
        }
        if (cfg.cheat("debug_mode")) wr8(s, kDebug, 1);
        if (!wide_.level()) return;
        if (cfg.cheat("inf_gems") && rd8(s, kGems) != 0x99) wr8(s, kGems, 0x99);
    }

    void post_frame(Snes& s, const GameConfig& cfg, FrameInfo& info) override {
        (void)cfg;
        wide_.frame_end(s, info);
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies, their shots and hazards don't hurt Aladdin. Falling into a pit or the lava, or being crushed, still costs a life."},
            {"inf_health", "Infinite Health", "Hits still knock Aladdin back, but he never loses a heart."},
            {"inf_lives", "Infinite Lives", "Losing a life never lowers the lives counter."},
            {"inf_apples", "Infinite Apples", "Throwing an apple never uses one up."},
            {"inf_gems", "Infinite Gems", "The gem counter stays at 99: every gem picked up completes a hundred (an extra heart, then an extra life)."},
            {"debug_mode", "Debug Mode", "The game's own developer mode: 10 hearts and 99 apples when a game starts, and a stage select on the title screen (hold L or R: up / down for the stage, left / right for the act)."},
        };
    }

    std::vector<LevelEntry> levels() const override {
        std::vector<LevelEntry> v;
        for (const Level& l : kLevels) v.push_back({l.group, l.name});
        return v;
    }

    // From power-on to the level: START through the logos and the story to the title menu, GAME START, whose first
    // level computation gets the chosen stage and act (exec_hook). Run headless.
    bool start_level(Snes& s, const GameConfig& cfg, int index, int variant) override {
        (void)variant;
        if (index < 0 || index >= kLevelCount) return false;
        ls_ = LevelSelect();
        ls_.active = true;
        ls_.stage = kLevels[index].stage;
        ls_.act = kLevels[index].act;
        s.reset(true);
        configure(s, cfg);
        s.skipRender = true;
        bool ok = false;
        int in_level = 0;
        for (int f = 0; f < 6000 && !ok; f++) {
            const uint16_t pad = !ls_.done && (f / 4) % 2 == 0 ? kPadStart : 0;
            pre_frame(s, cfg);   // (cheats act from power-on, as in the app)
            run_frames(s, 1, pad);
            FrameInfo info;
            post_frame(s, cfg, info);
            in_level = ls_.done && info.gameplay ? in_level + 1 : 0;
            ok = in_level >= 2;
        }
        s.skipRender = false;
        s.markExec(kLevelCompute, false);
        ls_.active = false;
        return ok;
    }

    std::vector<AchievementDef> achievements() const override {
        return {
            {"snes_agrabah", "Street Rat", "Run through Agrabah's market and over its rooftops: clear Stage 1."},
            {"snes_cave", "Diamond in the Rough", "Make it through the Cave of Wonders."},
            {"snes_escape", "Hold On Tight", "Fly out of the collapsing cave on the Magic Carpet."},
            {"snes_lamp", "Phenomenal Cosmic Power", "Find a way through the inside of the Genie's lamp."},
            {"snes_dungeon", "Jailbreak", "Escape from the Sultan's dungeon."},
            {"snes_carpet", "Magic Carpet Ride", "Finish the moonlit carpet ride of A Whole New World."},
            {"snes_palace", "Storming the Palace", "Fight your way through Jafar's palace to Jafar himself."},
            {"snes_jafar", "Snake Charmer", "Defeat Jafar and his giant cobra form to finish the game."},
            {"snes_no_continue", "One Jump Ahead", "Finish a new game without using a continue."},
            {"snes_stage_clean", "Riffraff, Street Rat", "Clear a whole stage, from its first part to STAGE CLEAR, without losing a life."},
            {"snes_untouched", "Not a Scratch", "Clear a part of a stage from its start without losing a single heart."},
            {"snes_wheel", "Break Time!", "Collect a Genie token in a stage and spin the Genie's bonus wheel."},
            {"snes_hearts", "Heart of Gold", "Raise Aladdin's hearts to the full ten.", false, 10},
            {"snes_gems", "Treasure of the Cave", "Collect a hundred gems.", false, 100},
            {"snes_lives", "Nine Lives", "Have nine spare lives at once.", true},
            {"snes_jafar_flawless", "Never Had a Friend Like Me", "Beat Jafar's final stage without losing a single heart.", true},
            {"snes_debug", "Developer's Secret", "Discover Capcom's developer code on the OPTIONS screen.", true},
        };
    }

    // ---- Achievements (RAM only; reference: tools/re/snes_achievements.py). Indices follow achievements().
    enum { A_AGRABAH, A_CAVE, A_ESCAPE, A_LAMP, A_DUNGEON, A_CARPET, A_PALACE, A_JAFAR, A_NO_CONTINUE, A_STAGE_CLEAN,
           A_UNTOUCHED, A_WHEEL, A_HEARTS, A_GEMS, A_LIVES, A_JAFAR_FLAWLESS, A_DEBUG };
    // scratch: fired bits; the title was seen (a GAME START follows); the last level played + 1; the stage begun from
    // its first part with no life lost since + 1; the level begun from its start with no heart lost since + 1
    enum { S_FIRED, S_TITLE, S_LAST_LEVEL, S_STAGE_CLEAN, S_ACT_CLEAN };
    // run bits: a new game from the title's GAME START; a continue was used
    enum : uint32_t { R_NEWGAME = 1, R_CONTINUED = 2 };

    void achievement_frame(const Snes& snes, AchievementCtx& c) const override {
        if (c.prev.size() != sizeof(snes.wram)) return;
        const RamViewLE s{snes.wram, 0x1FFFF}, p{c.prev.data(), 0x1FFFF};
        int32_t* sc = c.scratch;
        auto fire = [&](int i) {
            if (sc[S_FIRED] & (1 << i)) return;
            sc[S_FIRED] |= 1 << i;
            c.unlocked.push_back(i);
        };
        // A jump of the NMI's frame counter (a state loaded, a reset) breaks every condition in progress.
        if (((s.b(kNmiCount) - p.b(kNmiCount)) & 0xFF) > 4) {
            for (int i = S_TITLE; i <= S_ACT_CLEAN; i++) sc[i] = 0;
            return;
        }
        if (s.b(kDemo) || p.b(kDemo)) return;             // attract demo
        // The developer code (controller 2 on OPTIONS): then its 10 hearts and 99 apples spoil the rest.
        if (!p.b(kDebug) && s.b(kDebug) == 1 && main_wait(s) == kOptionsWaitRet) fire(A_DEBUG);
        if (s.b(kDebug) || p.b(kDebug)) return;
        const int pp = p.b(kLevel), cp = s.b(kLevel);
        const bool level_now = main_wait(s) == kLevelWaitRet || main_wait(s) == kPauseWaitRet;

        // run bits: GAME START from the title (not a password: its screen clears the title mark), a continue taken
        if (main_wait(s) == kTitleWaitRet) sc[S_TITLE] = 1;
        if (cp == kScreenPassword) sc[S_TITLE] = 0;
        if (sc[S_TITLE] && level_now && cp == 0) {
            c.run_bits = R_NEWGAME;
            sc[S_TITLE] = 0;
        }
        if (s.b(kContinues) < p.b(kContinues)) c.run_bits |= R_CONTINUED;

        // A level begun from its start: its act (and, at a stage's first part, the stage) counts as clean so far.
        if (cp < kScreenFirst && (cp != pp || (sc[S_LAST_LEVEL] == 0 && level_now))) {
            const int before = sc[S_LAST_LEVEL] - 1;
            sc[S_LAST_LEVEL] = cp + 1;
            sc[S_ACT_CLEAN] = cp + 1;
            if (s.b(kAct) == 0 && (before < 0 || kLevelStage[before] != kLevelStage[cp])) sc[S_STAGE_CLEAN] = kLevelStage[cp] + 1;
            if (pp == kScreenContinue) sc[S_STAGE_CLEAN] = 0;   // (restarted after a game over)
            c.progress.push_back({A_HEARTS, std::min<int>(10, s.b(kHeartsMax))});
            c.progress.push_back({A_GEMS, (s.b(kGems) >> 4) * 10 + (s.b(kGems) & 15)});
        }
        if (cp < kScreenFirst && cp == pp && level_now) {
            if (s.b(kLives) < p.b(kLives)) sc[S_STAGE_CLEAN] = 0;
            if (s.b(kHearts) < p.b(kHearts) && s.b(kHeartsMax) == p.b(kHeartsMax)) sc[S_ACT_CLEAN] = 0;
            if (s.b(kHeartsMax) >= 10 && p.b(kHeartsMax) < 10) fire(A_HEARTS);
            if (s.b(kLives) >= 10 && p.b(kLives) < 10) fire(A_LIVES);
            if (p.b(kGems) >= 0x90 && s.b(kGems) < 0x10) fire(A_GEMS);   // (a hundred: the counter starts over)
            // progress: the hearts' maximum (3-10) and the gem counter (BCD 0-99), whenever either goes up
            if (s.b(kHeartsMax) > p.b(kHeartsMax)) c.progress.push_back({A_HEARTS, std::min<int>(10, s.b(kHeartsMax))});
            const int gems = (s.b(kGems) >> 4) * 10 + (s.b(kGems) & 15), pgems = (p.b(kGems) >> 4) * 10 + (p.b(kGems) & 15);
            if (gems > pgems) c.progress.push_back({A_GEMS, gems});
        }
        // A level left forward: the next part, the STAGE CLEAR or bonus wheel, the ending (not a game over).
        if (pp < kScreenFirst && cp != pp &&
            (cp < kScreenFirst || cp == kScreenStageClear || cp == kScreenBonus || cp == kScreenNextAct || cp == kScreenEnding)) {
            if (sc[S_ACT_CLEAN] == pp + 1) {
                fire(A_UNTOUCHED);
                if (pp == 0x11 && cp == kScreenEnding) fire(A_JAFAR_FLAWLESS);
            }
            sc[S_ACT_CLEAN] = 0;
        }
        if (cp == kScreenBonus && pp != kScreenBonus) fire(A_WHEEL);
        // A stage cleared: its last part leads to the STAGE CLEAR screen (after the bonus wheel, if any).
        if (cp == kScreenStageClear && pp != kScreenStageClear) {
            const int last = sc[S_LAST_LEVEL] - 1;
            switch (last) {
            case 0x03: fire(A_AGRABAH); break;
            case 0x05: fire(A_CAVE); break;
            case 0x07: fire(A_ESCAPE); break;
            case 0x0A: fire(A_LAMP); break;
            case 0x0D: fire(A_DUNGEON); break;
            case 0x12: fire(A_CARPET); break;
            case 0x10: fire(A_PALACE); break;
            default: break;
            }
            if (last >= 0 && sc[S_STAGE_CLEAN] == kLevelStage[last] + 1) fire(A_STAGE_CLEAN);
            sc[S_STAGE_CLEAN] = 0;
        }
        // Jafar beaten: his stage hands over to the ending's story.
        if (cp == kScreenEnding && pp != kScreenEnding && sc[S_LAST_LEVEL] == 0x11 + 1) {
            fire(A_JAFAR);
            if ((c.run_bits & R_NEWGAME) && !(c.run_bits & R_CONTINUED)) fire(A_NO_CONTINUE);
        }
    }

    std::vector<uint8_t> save_ext() const override { return wide_.save_ext(); }
    void load_ext(const uint8_t* d, size_t n) override { wide_.load_ext(d, n); }

private:
    // The music player's loop (8-bit registers, DB $80, D 0): wait for a frame, take a request from the mailbox, run the
    // game's own routine for it. (The NMI sends the queued bytes and keeps running as in the game.)
    static void write_jukebox_stub(Snes& s) {
        PatchAsm a(s, kJukeboxStub);
        a.b({0x78, 0xE2, 0x30, 0xA9, 0x80, 0x48, 0xAB, 0xF4, 0x00, 0x00, 0x2B, 0x58});   // SEI SEP PHB.. PLD CLI
        const uint32_t loop = a.pc;
        a.b({0xCB});                                        // WAI
        a.b({0xAD}); a.w16((uint16_t)kMailCmd);             // LDA cmd
        a.b({0xF0, (uint8_t)(loop - (a.pc + 2))});          // BEQ loop
        a.b({0xC9, 0x01, 0xD0, 0x16});                      // CMP #1 / BNE not_song (+22)
        a.b({0xAD}); a.w16((uint16_t)kMailBefore);          // LDA before
        a.b({0xF0, 0x08});                                  // BEQ play_id (+8)
        for (uint8_t b : jsl(kSong)) a.b8(b);               // JSL song (the one played ahead)
        a.b({0xCB, 0xCB, 0xCB, 0xCB});                      // WAI x4: it starts and sets the driver up first
        a.b({0xAD}); a.w16((uint16_t)kMailId);              // play_id: LDA id
        for (uint8_t b : jsl(kSong)) a.b8(b);               // JSL song
        a.b({0x80, 0x13});                                  // BRA done (+19)
        a.b({0xC9, 0x02, 0xD0, 0x09});                      // not_song: CMP #2 / BNE stop (+9)
        a.b({0xAD}); a.w16((uint16_t)kMailId);              // LDA id
        for (uint8_t b : jsl(kSfx)) a.b8(b);                // JSL effect
        a.b({0x80, 0x06});                                  // BRA done (+6)
        a.b({0xA9, kStopAll});                              // stop: LDA #$F0
        for (uint8_t b : jsl(kSfx)) a.b8(b);                // JSL queue (stop: music and effects)
        a.b({0xE2, 0x30});                                  // done: SEP #$30
        a.b({0x9C}); a.w16((uint16_t)kMailCmd);             // STZ cmd
        a.b({0x80, (uint8_t)(loop - (a.pc + 2))});          // BRA loop
    }

    struct LevelSelect {
        bool active = false, done = false;
        uint8_t stage = 0, act = 0;
    };
    bool jb_park_ = false;
    AladdinWide wide_;
    LevelSelect ls_;
    bool invincible_ = false, inf_health_ = false, inf_lives_ = false, inf_apples_ = false;
    bool debug_ = false, clear_debug_ = false;
};

}  // namespace

GameModule* create_snes_aladdin() { return new SnesAladdin; }
