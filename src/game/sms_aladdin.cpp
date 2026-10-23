// Aladdin (Master System, SIMS 1994, European release) - enhancement module.
// Addresses and techniques: docs/re_sms_game.md, docs/re_sms_wide.md.
#include "game.h"
#include "sms_common.h"
#include "../core/sms.h"
#include <algorithm>
#include <cstring>
#include <iterator>

using namespace sms_game;

namespace {

// ---- RAM (docs/re_sms_game.md) ---------------------------------------------------------------------
constexpr uint16_t MODE = 0xC200;        // main mode: handler table $092E (2..$B = the ten stages)
constexpr uint16_t STAGE = 0xC105;       // stage 0..9 (mode = stage + 2)
constexpr uint16_t DEMO = 0xC104;        // attract-mode demo: the demo's mode, else 0 ($FF: the lamp chamber's exit)
constexpr uint16_t SCENE = 0xC11F;       // story scene shown by mode $13
constexpr uint16_t HEALTH = 0xC116;      // half gems: 6 = three gems
constexpr uint16_t ROCKS = 0xC456;       // stones to throw (0..9)
constexpr uint16_t INVULN = 0xC467;      // invulnerability timer after a hit ($FF = for good: password AIQY)
constexpr uint16_t FRAMES = 0xC20E;      // frame counter (bit 0 picks the sprite table's upload order)
constexpr uint16_t TITLE_STATE = 0xCF01; // mode 0's script: $12 = title menu
constexpr uint16_t TITLE_CURSOR = 0xCF24;// title menu: bit 0 = PASSWORD
constexpr uint16_t PASSWORD = 0xC924;    // the four letters on the password screen (0 = A)
constexpr uint16_t STAGE_INIT = 0xC912;  // set while a stage loads
// camera and level map (all stages)
constexpr uint16_t CAM_X = 0xC482, CAM_MAX = 0xC480, CAM_MIN = 0xC4B9;
constexpr uint16_t MAP = 0xC48F, MAP_STRIDE = 0xC489, METATILES = 0xC49D;
constexpr uint16_t SAT_XT = 0xC340;      // sprite table buffer: Y at $C300 (64), X / tile pairs at $C340
constexpr int M_SCENE = 0x13, M_PASSWORD = 0x0F, M_GAME_OVER = 0x11;

bool stage_mode(int m) { return m >= 2 && m <= 0x0B; }
bool in_stage(const Sms& s) { return stage_mode(s.ram8(MODE)) && s.ram8(STAGE) <= 9; }

// ---- levels: the ten stages, entered through the password screen like a player would -------------------
struct Stage { const char* group; const char* name; const char* password; };
const Stage kStages[] = {
    {"Agrabah", "Agrabah Market", "AJGJ"},
    {"Agrabah", "Rooftop Chase", "LAEA"},
    {"The Cave of Wonders", "The Cave of Wonders", "ASNF"},
    {"The Cave of Wonders", "The Treasure Room", "DMIA"},
    {"The Cave of Wonders", "The Lamp", "DIER"},
    {"The Cave of Wonders", "Escape from the Cave", "INSI"},
    {"The Palace", "Prince Ali", "NEUA"},
    {"The Palace", "A Whole New World", "AALG"},
    {"The Palace", "Jafar's Palace", "BLTO"},
    {"The Palace", "Jafar", "UIAN"},
};

// ---- cheats (docs/re_sms_game.md section 4) --------------------------------------------------------------
// Invincibility makes the game's own after-hit invulnerability test ($C467 != 0) always pass: hits by enemies
// ($3F5F; the type 5 hazards that ignore it still hurt), the carpet's crashes ($38A9) and the boss's touch
// ($4B64C) are ignored. Falling into a pit or the lava still ends the stage.
const std::vector<RomPatch> kInvincible = {
    {0x3F5F, {0x3A, 0x67, 0xC4, 0xB7, 0xC0}, {0x3E, 0xFF, 0xB7, 0xC0, 0x00}},
    {0x38A9, {0x3A, 0x67, 0xC4, 0xB7, 0x20}, {0x3E, 0xFF, 0x00, 0xB7, 0x20}},
    {0x4B64C, {0x3A, 0x67, 0xC4, 0xB7, 0xC0}, {0x3E, 0xFF, 0xB7, 0xC0, 0x00}},
};
// The three ways a hit takes health: one half gem ($3F88), one but never the last ($3F76), two ($4A6F4).
const std::vector<RomPatch> kInfHealth = {
    {0x3F88, {0x3D, 0x32, 0x16, 0xC1}, {0x00, 0x32, 0x16, 0xC1}},
    {0x3F76, {0x3D, 0x20, 0x02}, {0x00, 0x20, 0x02}},
    {0x4A6F4, {0xD6, 0x02}, {0xD6, 0x00}},
};
const std::vector<RomPatch> kInfRocks = {{0x2156, {0x35, 0x3E, 0x11}, {0x00, 0x3E, 0x11}}};
// The developers' menu (mode 1: stage select, sound test, sprite viewers, story scene select) is behind the title's
// PASSWORD choice instead of the password screen.
const std::vector<RomPatch> kDevMenu = {{0x4BC4, {0x0E, 0x0F}, {0x0E, 0x01}}};

// ---- widescreen (docs/re_sms_wide.md) ----------------------------------------------------------------
// The 32-column name table holds exactly the 256-px view (the column at the left edge is the one the streamer
// overwrites, hidden by the blanked left column). Every other background cell of the camera's band comes from the
// level map (map_entry); parallax bands repeat every 256 px and wrap. Objects live in screen coordinates: the
// windows that wake / keep them and the per-piece sprite clipping are widened by E; the true X of a sprite beyond
// the 8-bit table X is recorded by PC hooks on the three sprite writers (sprite_x).
// Code caves in bank 1's unused zero fill ($7943-$7DFF).
constexpr uint16_t CAVE_WAKE = 0x7A00, CAVE_LOAD = 0x7A10, CAVE_FULL_A = 0x7A30, CAVE_FULL_B = 0x7A38;

std::vector<RomPatch> wide_patches(int E) {
    const uint8_t e = (uint8_t)E;
    auto zeros = [](size_t n) { return std::vector<uint8_t>(n, 0x00); };
    auto lo = [](uint16_t a) { return (uint8_t)a; };
    auto hi = [](uint16_t a) { return (uint8_t)(a >> 8); };
    // Stages 0/1/5/7/8 keep 7 objects from the stage's list ($410D loads the next one into a free slot): a waiting
    // object wakes when the camera reaches its X ($3E50, `ld hl,($C482)`) at its preset screen X (mostly 256, the
    // right edge). Objects that move with the camera (+$20) are loaded E px further right and wake E px earlier:
    // the same place in the level, in view of the wider screen.
    const std::vector<uint8_t> wake = {0xDD, 0x7E, 0x20, 0xB7, 0x2A, 0x82, 0xC4, 0xC8, 0x11, e, 0x00, 0x19, 0xC9};
    const std::vector<uint8_t> load = {0xE5, 0xDD, 0x7E, 0x20, 0xB7, 0x28, 0x10, 0xDD, 0x6E, 0x02, 0xDD, 0x66, 0x03,
                                       0x11, e, 0x00, 0x19, 0xDD, 0x75, 0x02, 0xDD, 0x74, 0x03, 0xE1, 0xC3, 0xB3, 0x41};
    // The sprite slot allocators ($3D74 / $3D5E) put an object that no longer fits in the 64 entries to sleep, but
    // the draw call after them still writes its pieces past the table (over the palette buffer at $C3C0). Rare in the
    // 256-px view, common with more objects awake: the draw is skipped.
    const std::vector<uint8_t> full_a = {0xDD, 0x36, 0x1D, 0x00, 0xE1, 0xC3, 0x39, 0x3D};
    const std::vector<uint8_t> full_b = {0xDD, 0x36, 0x1D, 0x00, 0xE1, 0xC3, 0xE4, 0x3C};
    const uint16_t keep = (uint16_t)(0x40 + E), win = (uint16_t)(0x170 + 2 * E), gone = (uint16_t)(0x20 + E);
    return {
        {0x3E50, {0x2A, 0x82, 0xC4}, {0xCD, lo(CAVE_WAKE), hi(CAVE_WAKE)}},
        {CAVE_WAKE, zeros(wake.size()), wake},
        {0x4151, {0xCD, 0xB3, 0x41}, {0xCD, lo(CAVE_LOAD), hi(CAVE_LOAD)}},
        {CAVE_LOAD, zeros(load.size()), load},
        // ... and leave (the next one of the list takes the slot) E px further left ($3EC5: x < -32)
        {0x3EC5, {0x11, 0x20, 0x00}, {0x11, lo(gone), hi(gone)}},
        // The other stages keep all their objects: one is awake while its x is in [-64, 304) ($3E1F)
        {0x3E25, {0x11, 0x40, 0x00}, {0x11, lo(keep), hi(keep)}},
        {0x3E29, {0x11, 0x70, 0x01}, {0x11, lo(win), hi(win)}},
        {0x3D86, {0xDD, 0x36, 0x1D, 0x00, 0xC9}, {0xC3, lo(CAVE_FULL_A), hi(CAVE_FULL_A), 0x00, 0x00}},
        {CAVE_FULL_A, zeros(full_a.size()), full_a},
        {0x3D6B, {0xDD, 0x36, 0x1D, 0x00, 0xC9}, {0xC3, lo(CAVE_FULL_B), hi(CAVE_FULL_B), 0x00, 0x00}},
        {CAVE_FULL_B, zeros(full_b.size()), full_b},
    };
}

// The stages whose map is a table of 256-px pages ($2E19); the others have one flat map from x = ($C4B9) on ($2DEC).
bool paged_map(int stage) { return stage == 0 || stage == 1 || stage == 5 || stage == 7 || stage == 8; }

uint8_t bank_rd8(const Sms& s, int bank, uint16_t a) { return s.rom_orig8((uint32_t)bank * 0x4000 + (a & 0x3FFF)); }
uint16_t bank_rd16(const Sms& s, int bank, uint16_t a) {
    return (uint16_t)(bank_rd8(s, bank, a) | (bank_rd8(s, bank, (uint16_t)(a + 1)) << 8));
}

// The name-table entry the column streamer ($2E3A) builds for world x `wx`, playfield cell row `row` (0..19 =
// table rows 2..21), or -1 outside the map. Metatiles are 16x16: 8 bytes, the TL TR BL BR entries.
int map_entry(const Sms& s, int wx, int row) {
    const int stage = s.ram8(STAGE);
    if (stage > 9 || row < 0 || row >= 20 || wx < 0) return -1;
    const int bank = s.rom_orig8(0x3306 + stage);
    const int stride = s.ram16(MAP_STRIDE);
    uint16_t at;
    if (paged_map(stage)) {
        at = (uint16_t)(bank_rd16(s, bank, (uint16_t)(s.ram16(MAP) + 2 * (wx >> 8))) + ((wx & 255) >> 4) + (row >> 1) * stride);
    } else {
        const int mx = (wx - (int)s.ram16(CAM_MIN)) >> 4;
        if (mx < 0 || mx >= stride) return -1;
        at = (uint16_t)(s.ram16(MAP) + mx + (row >> 1) * stride);
    }
    const uint8_t mt = bank_rd8(s, bank, at);
    return bank_rd16(s, bank, (uint16_t)(s.ram16(METATILES) + mt * 8 + (row & 1) * 4 + ((wx >> 3) & 1) * 2));
}

// The camera's range: the original view shows world x [lo, hi + 256). A flat map can end before the camera's
// limit allows (Jafar's room): the map's end counts.
void level_bounds(const Sms& s, int& lo, int& hi) {
    lo = s.ram16(CAM_MIN);
    hi = s.ram16(CAM_MAX);
    const int stage = s.ram8(STAGE);
    if (stage <= 9 && !paged_map(stage)) hi = std::min(hi, lo + s.ram16(MAP_STRIDE) * 16 - 256);
}

// module_state: [0..3] each sprite table entry's 256-px page as displayed (2 bits each, see sprite_x), [4] the
// camera X at the last VBlank (the view of the next frame), [5] the camera X of the frame being drawn.
constexpr int MS_CAM_NEXT = 4, MS_CAM = 5;
enum : uint8_t { PAGE_0 = 0, PAGE_RIGHT = 1, PAGE_LEFT = 2, PAGE_HIDDEN = 3 };

class SmsAladdin : public SmsModule {
public:
    const char* id() const override { return "sms"; }
    const char* title() const override { return "Aladdin"; }
    const char* subtitle() const override { return "SIMS"; }
    const char* year() const override { return "1994"; }
    uint32_t rom_crc32() const override { return 0xC8718D40; }
    uint32_t rom_size() const override { return 512u << 10; }
    const char* rom_hint() const override { return "Disney's Aladdin (Europe).sms"; }
    const char* rom_names() const override { return "9027"; }
    uint32_t accent_color() const override { return 0xFFF0705E; }
    int max_ext() const override { return 102; }

    static std::vector<RomPatch> patch_list(const GameConfig& cfg) {
        std::vector<RomPatch> p;
        auto add = [&](const std::vector<RomPatch>& v) { p.insert(p.end(), v.begin(), v.end()); };
        if (cfg.cheat("invincible")) add(kInvincible);
        if (cfg.cheat("inf_health")) add(kInfHealth);
        if (cfg.cheat("inf_rocks")) add(kInfRocks);
        if (cfg.cheat("dev_menu")) add(kDevMenu);
        if (cfg.ext > 0) add(wide_patches(cfg.ext));
        return p;
    }

    bool can_configure(const Sms& s, const GameConfig& cfg) const override { return patches_safe(s, patch_list(cfg)); }

    void configure(Sms& s, const GameConfig& cfg) override {
        apply_patches(s, patch_list(cfg));
        sms_ = &s;
        hud_anchor_ = cfg.hud_anchor;
        s.clear_pc_hooks();
        SmsVdp& v = s.vdp;
        v.hook_host = this;
        const bool wide = cfg.ext > 0;
        v.hw_x0 = wide ? 8 : 0;
        v.hw_x1 = 256;
        v.bg_entry = wide ? bg_entry : nullptr;
        v.sprite_x = wide ? sprite_x : nullptr;
        v.line_hook = wide ? line_hook : nullptr;
        s.on_vblank = nullptr;
        s.on_frame_start = nullptr;
        std::memset(page_, 0, sizeof(page_));
        if (!wide) return;
        // the camera the VBlank shows next (the scroll registers are written from it right after line $C1) and the
        // one the frame being drawn uses
        s.on_vblank = [](Sms& m) { m.module_state[MS_CAM_NEXT] = m.ram16(CAM_X); };
        s.on_frame_start = [](Sms& m) { m.module_state[MS_CAM] = m.module_state[MS_CAM_NEXT]; };
        // sprite pieces: the player ($23C2), the large second figure ($3C63) and the objects ($0C99), each with
        // hl = the piece's 16-bit screen X just before the "off screen" test (`ld a,h`)
        s.add_pc_hook(0x23C2, [this](Sms& m) { piece_x(m, (m.z80.de - SAT_XT) >> 1); });
        s.add_pc_hook(0x3C63, [this](Sms& m) { piece_x(m, (m.z80.de - SAT_XT) >> 1); });
        s.add_pc_hook(0x0C99, [this](Sms& m) {
            const uint16_t at = (uint16_t)(m.peek(m.z80.sp) | (m.peek((uint16_t)(m.z80.sp + 1)) << 8));
            piece_x(m, (at - SAT_XT) >> 1);
        });
        // the VBlank's sprite table upload ($02D9): in reverse order on odd frames (the game's flicker)
        s.add_pc_hook(0x02D9, [this](Sms& m) {
            const bool rev = m.ram8(FRAMES) & 1;
            for (int w = 0; w < 4; w++) m.module_state[w] = 0;
            for (int i = 0; i < 64; i++)
                m.module_state[i >> 4] |= (uint32_t)page_[rev ? 63 - i : i] << ((i & 15) * 2);
        });
    }

    void post_frame(Sms& s, const GameConfig& cfg, FrameInfo& info) override {
        const int E = cfg.ext;
        info.gameplay = in_stage(s);
        if (!info.gameplay || E == 0) return;
        // world x outside what the original can show is masked (the camera itself is the original's: Aladdin's
        // position is an 8-bit screen X, so the view can't move inside the level)
        const int cam = (int)s.module_state[MS_CAM];
        int lo, hi;
        level_bounds(s, lo, hi);
        info.mask_left = std::clamp(lo + E - cam, 0, E);
        info.mask_right = std::clamp(cam + E - hi, 0, E);
        info.mask_from = 16;     // the status bar (lines 0..15) stays whole
        info.mask_lines = 176;   // below the playfield: the text box of the stages' story moments, 256 px wide
        info.pillar_below = true;
        info.view_left = E;
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies, their shots and the carpet's crashes can't hurt Aladdin (the game's own "
                                            "invulnerability, as the developers' password AIQY gives it). Pits and lava still end the stage."},
            {"inf_health", "Infinite Health", "Hits never take a gem away."},
            {"inf_rocks", "Infinite Stones", "Throwing a stone never uses one up."},
            {"dev_menu", "Developer Menu", "The title's PASSWORD opens the developers' menu instead: stage select, sound test, "
                                           "sprite viewers and the story scene select (Left / Right change, button 1 starts)."},
        };
    }

    std::vector<LevelEntry> levels() const override {
        std::vector<LevelEntry> v;
        for (const Stage& st : kStages) v.push_back({st.group, st.name});
        return v;
    }

    bool start_level(Sms& s, const GameConfig& cfg, int entry, int) override {
        if (entry < 0 || entry >= (int)std::size(kStages)) return false;
        s.power_on();
        configure(s, cfg);
        auto mode = [&]() { return s.ram8(MODE); };
        // power-on -> intro (button 1 skips it) -> title menu ($CF01 = $12)
        if (!press_until(s, SMS_1, [&]() { return mode() == 0 && s.ram8(TITLE_STATE) == 0x12; }, 2, 10, 120)) return false;
        if (!press_until(s, SMS_DOWN, [&]() { return (s.ram8(TITLE_CURSOR) & 1) != 0; }, 2, 4, 10)) return false;
        if (!press_until(s, SMS_1, [&]() { return mode() == M_PASSWORD; }, 2, 4, 20)) return false;
        run_frames(s, 10);
        // the password screen checks the four letters when button 1 is pressed ($744D)
        const char* pw = kStages[entry].password;
        for (int i = 0; i < 4; i++) s.set_ram8((uint16_t)(PASSWORD + i), (uint8_t)(pw[i] - 'A'));
        const int want = entry + 2;
        if (!press_until(s, SMS_1, [&]() { return mode() == want; }, 2, 4, 10)) return false;
        return run_until(s, [&]() { return mode() == want && s.ram8(STAGE_INIT) == 0; }, 600);
    }

private:
    Sms* sms_ = nullptr;
    bool hud_anchor_ = true;
    uint8_t page_[64];   // each sprite table buffer entry's page as the writers left it (uploaded at the VBlank)

    void piece_x(Sms& m, int slot) {
        if (slot < 0 || slot >= 64) return;
        if (!in_stage(m)) { page_[slot] = PAGE_0; return; }
        const int x = (int16_t)m.z80.hl, E = m.vdp.ext;
        if (x >= 0 && x < 256) {
            page_[slot] = PAGE_0;
        } else if (x > -8 - E && x < 256 + E) {
            page_[slot] = x < 0 ? PAGE_LEFT : PAGE_RIGHT;
            m.z80.hl &= 0x00FF;   // the test passes: the table gets the low byte
        } else {
            page_[slot] = PAGE_HIDDEN;   // (the player / second figure writers park it at X = 0)
        }
    }

    static int bg_entry(void* host, int line, int vcol, int vrow) {
        (void)line;
        const Sms& s = *((SmsAladdin*)host)->sms_;
        const SmsVdp& v = s.vdp;
        if (!in_stage(s)) return -1;
        // the status bar (rows 0-1): blank beyond the 256 px; below the playfield (pillarboxed): the table
        if (vrow < 2) return v.name_entry(0, vrow);
        if (vrow >= 22) return -1;
        const int hs = v.reg[8];
        const int cam = (int)s.module_state[MS_CAM];
        if (((hs + cam) & 0xFF) != 0) return -1;   // a parallax band: it repeats every 256 px
        return map_entry(s, cam + hs + vcol * 8, vrow - 2);
    }

    static int sprite_x(void* host, int index, int x, int y, int pattern) {
        (void)y; (void)pattern;
        const Sms& s = *((SmsAladdin*)host)->sms_;
        if (!in_stage(s)) return x;
        switch ((s.module_state[index >> 4] >> ((index & 15) * 2)) & 3) {
        case PAGE_RIGHT: return x + 256;
        case PAGE_LEFT: return x - 256;
        case PAGE_HIDDEN: return -4096;
        default: return x;
        }
    }

    static void line_hook(void* host, int line, uint8_t* bg, uint8_t* spr) {
        const SmsAladdin& mod = *(SmsAladdin*)host;
        const Sms& s = *mod.sms_;
        const SmsVdp& v = s.vdp;
        const int E = v.ext;
        if (!in_stage(s)) {
            // pillarboxed screens: the blanked left column as the hardware shows it
            if (v.reg[0] & 0x20) {
                for (int i = E; i < E + 8; i++) { bg[i] = (uint8_t)(0x10 | (v.reg[7] & 0x0F)); spr[i] = 0; }
            }
            return;
        }
        // status bar: the stone counter (x 192..255) goes to the right edge; the gems stay in the middle
        if (line < 16 && mod.hud_anchor_) {
            for (int x = 255; x >= 192; x--) bg[x + 2 * E] = bg[x + E];
            for (int x = 192; x < 192 + E; x++) bg[x + E] = bg[E];
        }
    }
};

}  // namespace

GameModule* create_sms_aladdin() { return new SmsAladdin; }
