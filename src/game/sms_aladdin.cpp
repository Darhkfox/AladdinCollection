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
// sound driver
constexpr uint16_t SOUND_ON = 0xC203, SOUND_MUSIC = 0xDE06, SOUND_SFX = 0xDE07, SOUND_PRIORITY = 0xDE11;
constexpr uint16_t JUKEBOX_IDLE = 0xC0A0;   // music player: the CPU waits here (below the stack at $C100)

bool stage_mode(int m) { return m >= 2 && m <= 0x0B; }
bool in_stage(const Sms& s) { return stage_mode(s.ram8(MODE)) && s.ram8(STAGE) <= 9; }

// ---- levels: the ten stages, entered through the password screen like a player would -------------------
struct Stage { const char* group; const char* name; const char* password; };
const Stage kStages[] = {   // (numbered as the developers' TEST MODE numbers them)
    {"Agrabah", "Stage 1-1 - The Market", "AJGJ"},
    {"Agrabah", "Stage 1-2 - The Rooftops", "LAEA"},
    {"The Cave of Wonders", "Stage 2-1 - The Caverns", "ASNF"},
    {"The Cave of Wonders", "Stage 2-2 - The Treasure Room", "DMIA"},
    {"The Cave of Wonders", "Stage 2-3 - The Lamp", "DIER"},
    {"The Cave of Wonders", "Stage 3 - Escape from the Cave", "INSI"},
    {"Prince Ali", "Stage 4 - The Sultan's Palace", "NEUA"},
    {"Prince Ali", "Stage 5 - A Whole New World", "AALG"},
    {"Jafar", "Stage 6 - Jafar's Palace", "BLTO"},
    {"Jafar", "Stage 7 - The Final Battle", "UIAN"},
};

// ---- sounds: the driver's commands (docs/re_sms_game.md section 5). Music names follow where the game plays them;
// the effects are named where their use is clear (tools/re/sms_sfx_context.py), numbered otherwise.
const SoundEntry kSounds[] = {
    {0x81, "Opening Story", true, 77},
    {0x82, "Title", true, 77},
    {0x83, "Agrabah", true, 0},
    {0x84, "The Cave of Wonders", true, 0},
    {0x85, "Escape from the Cave", true, 43},
    {0x86, "The Sultan's Palace", true, 51},
    {0x87, "A Whole New World", true, 0},
    {0x88, "Jafar's Palace", true, 205},
    {0x89, "Jafar", true, 29},
    {0x8A, "The Treasure Room", true, 0},
    {0x8B, "Jasmine", true, 51},
    {0x8C, "Agrabah at Night", true, 102},
    {0x8D, "Danger", true, 0},
    {0x8E, "The Old Prisoner", true, 0},
    {0x8F, "Jafar's Plot", true, 0},
    {0x90, "The Genie", true, 130},
    {0x91, "Prince Ali", true, 154},
    {0x92, "Jafar the Genie", true, 77},
    {0x93, "Unused Theme", true, 77},
    {0x94, "Defeated", true, 3},
    {0x95, "Effect 95", false}, {0x96, "Effect 96", false}, {0x97, "Effect 97", false}, {0x98, "Effect 98", false},
    {0x99, "Effect 99", false}, {0x9A, "Effect 9A", false}, {0x9B, "Effect 9B", false}, {0x9C, "Effect 9C", false},
    {0x9D, "Effect 9D", false}, {0x9E, "Effect 9E", false}, {0x9F, "Effect 9F", false}, {0xA0, "Effect A0", false},
    {0xA1, "Effect A1", false}, {0xA2, "Story Effect", false}, {0xA3, "Effect A3", false}, {0xA4, "Effect A4", false},
    {0xA5, "Effect A5", false}, {0xA6, "Effect A6", false}, {0xA7, "Effect A7", false}, {0xA8, "Effect A8", false},
    {0xA9, "Effect A9", false}, {0xAA, "Effect AA", false}, {0xAB, "Carpet Crash", false}, {0xAC, "Effect AC", false},
    {0xAD, "Effect AD", false}, {0xAE, "Effect AE", false}, {0xAF, "Effect AF", false}, {0xB0, "Effect B0", false},
    {0xB1, "Password Letter", false}, {0xB2, "Effect B2", false}, {0xB3, "Effect B3", false}, {0xB4, "Effect B4", false},
    {0xB5, "Effect B5", false}, {0xB6, "Effect B6", false}, {0xB7, "Story Effect 2", false}, {0xB8, "Effect B8", false},
    {0xB9, "Effect B9", false}, {0xBA, "Menu Cursor", false}, {0xBB, "Menu Select", false}, {0xBC, "Pick Up", false},
    {0xBD, "Effect BD", false}, {0xBE, "Throw a Stone", false}, {0xBF, "Effect BF", false}, {0xC0, "Effect C0", false},
    {0xC1, "Aladdin Hurt", false}, {0xC2, "Aladdin Hurt 2", false}, {0xC3, "Effect C3", false}, {0xC4, "Jafar's Magic", false},
    {0xC5, "Big Hit", false}, {0xC6, "Effect C6", false}, {0xC7, "Effect C7", false}, {0xC8, "Effect C8", false},
    {0xC9, "Jump", false}, {0xCA, "Effect CA", false}, {0xCB, "Effect CB", false}, {0xCC, "Effect CC", false},
    {0xCD, "Effect CD", false}, {0xCE, "Effect CE", false}, {0xCF, "Effect CF", false}, {0xD0, "Effect D0", false},
};

// ---- cheats (docs/re_sms_game.md section 4) --------------------------------------------------------------
// Invincibility makes the game's own after-hit invulnerability test ($C467 != 0) always pass: hits by enemies
// ($3F5F, also by the type 5 ones that ignore it in the original: $3F5D), the carpet's crashes ($38A9) and the
// boss's touch ($4B64C) are ignored. Falling into a pit or the lava still ends the stage.
const std::vector<RomPatch> kInvincible = {
    {0x3F5D, {0x28, 0x05}, {0x00, 0x00}},
    {0x3F5F, {0x3A, 0x67, 0xC4, 0xB7, 0xC0}, {0x3E, 0xFF, 0xB7, 0xC0, 0x00}},
    {0x38A9, {0x3A, 0x67, 0xC4, 0xB7, 0x20}, {0x3E, 0xFF, 0x00, 0xB7, 0x20}},
    {0x4B64C, {0x3A, 0x67, 0xC4, 0xB7, 0xC0}, {0x3E, 0xFF, 0xB7, 0xC0, 0x00}},
};
// The three ways a hit takes health: one half gem ($3F88), one but never the last ($3F76), two ($4A6F4). The
// `dec a`s become `or a`: the test after them ("none left?") must see the health, not stale flags.
const std::vector<RomPatch> kInfHealth = {
    {0x3F88, {0x3D, 0x32, 0x16, 0xC1}, {0xB7, 0x32, 0x16, 0xC1}},
    {0x3F76, {0x3D, 0x20, 0x02}, {0xB7, 0x20, 0x02}},
    {0x4A6F4, {0xD6, 0x02}, {0xD6, 0x00}},
};
const std::vector<RomPatch> kInfRocks = {{0x2156, {0x35, 0x3E, 0x11}, {0x00, 0x3E, 0x11}}};
// The developers' TEST MODE (mode 1: character viewer, stage select, sound test, input test, the story scenes) is
// behind the title's PASSWORD choice instead of the password screen ($4BC4: the mode the title hands over to).
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
    // the draw call after them still writes its pieces past the table (over the palette buffer at $C3C0), and a
    // sleeping object of stages 0/1/5/7/8 stops following the camera ($3DC8) for that frame. Rare in the 256-px view,
    // common with more objects awake: the object stays awake, and the module has the VDP draw it after the table's
    // 64 sprites (extra_sprites).
    const std::vector<uint8_t> full_a = {0xE1, 0xC3, 0x39, 0x3D};
    const std::vector<uint8_t> full_b = {0xE1, 0xC3, 0xE4, 0x3C};
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
// (the same five stages keep 7 objects in a queue fed from the stage's list, $3D09; the others keep them all, $3CAE)
bool queue_objects(int stage) { return paged_map(stage); }

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

// "A Whole New World" (stage 7) reloads tiles 259-374 with each of its five sections' graphics as the camera passes
// $400 / $B00 / $1400 / $1B00 ($31EE: the lists in bank $1A, 8 bytes an entry: source, VRAM address, tile count).
// The cells of a section use only its own list's tiles, and none of them lie within 320 px after a threshold (the
// view while the next list loads). The view's sides can show a section whose graphics are not (or no longer) in
// VRAM: those cells take their patterns from a copy of the section's tiles made from ROM.
constexpr int S7_FIRST = 259, S7_TILES = 116, S7_SECTIONS = 5;

struct SectionTiles {
    uint8_t tiles[S7_SECTIONS][S7_TILES * 32];
    int thresholds[S7_SECTIONS];
    void build(const Sms& s) {
        std::memset(tiles, 0, sizeof(tiles));
        for (int sec = 0; sec < S7_SECTIONS; sec++) {
            thresholds[sec] = s.rom_orig8(0x32A0 + 2 * sec) | (s.rom_orig8(0x32A1 + 2 * sec) << 8);
            uint16_t e = bank_rd16(s, 0x1A, (uint16_t)(0x8000 + 2 * sec));
            for (int k = 0; k < 16 && bank_rd8(s, 0x1A, e) != 0xFF; k++, e = (uint16_t)(e + 8)) {
                const uint16_t src = bank_rd16(s, 0x1A, e);
                const int tile = (bank_rd16(s, 0x1A, (uint16_t)(e + 2)) & 0x3FFF) / 32, n = bank_rd8(s, 0x1A, (uint16_t)(e + 4));
                for (int t = 0; t < n; t++)
                    for (int b = 0; b < 32; b++) {
                        const int at = (tile + t - S7_FIRST) * 32 + b;
                        if (at >= 0 && at < S7_TILES * 32) tiles[sec][at] = bank_rd8(s, 0x1A, (uint16_t)(src + t * 32 + b));
                    }
            }
        }
    }
    // the section whose graphics world x's cells use (the 256 px after a threshold use none of the reloaded tiles)
    int section(int wx) const {
        int sec = 0;
        for (int i = 1; i < S7_SECTIONS; i++) if (wx >= thresholds[i] + 256) sec = i;
        return sec;
    }
};

// Jafar's room (stage 9) is the boss arena: a 384-px map the camera crosses 0..136, all of it within Aladdin's
// reach. Past its ends there is no level: the widened view carries the room's pillars on there (its middle part,
// x 128..255, repeats every 128 px; the hourglass at the right end is not repeated), dimmed (FrameInfo dim), the way
// the Castle of Illusion port shows its arenas.
constexpr int ARENA_STAGE = 9, ARENA_FROM = 128, ARENA_PERIOD = 128;

// The camera's range: the original view shows world x [lo, hi + 256). A flat map can end before the camera's
// limit allows (Jafar's room): the map's end counts.
void level_bounds(const Sms& s, int& lo, int& hi) {
    lo = s.ram16(CAM_MIN);
    hi = s.ram16(CAM_MAX);
    const int stage = s.ram8(STAGE);
    if (stage <= 9 && !paged_map(stage)) hi = std::min(hi, lo + s.ram16(MAP_STRIDE) * 16 - 256);
}

// module_state: [0..3] each sprite table entry's 256-px page as displayed (2 bits each, see sprite_x), [4] the
// camera X at the last VBlank (the view of the next frame), [5] the camera X of the frame being drawn, [6] the
// objects ($CA00 table) the slot allocator had no room for this frame (bit per object), [7] the extension the waiting
// objects of the queue stages were loaded for (wide_patches: they are placed E px further right).
constexpr int MS_CAM_NEXT = 4, MS_CAM = 5, MS_UNDRAWN = 6, MS_LOADED_E = 7;
constexpr uint16_t OBJECTS = 0xCA00;     // 7 (stages 0/1/5/7/8) or up to 16 objects, $40 bytes each
constexpr int OBJ_SIZE = 0x40, MAX_OBJECTS = 16;

// The pieces of an object the sprite table had no room for, drawn by the VDP after the table's 64 (as $0C45 would
// have: frame data in bank $12 at +$0F, count then dy / dx / tile per piece; the 16-bit Y / X at +$00 / +$02).
void extra_sprites(Sms& s) {
    SmsVdp& v = s.vdp;
    v.extra_count = 0;
    const uint32_t undrawn = s.module_state[MS_UNDRAWN];
    s.module_state[MS_UNDRAWN] = 0;
    const int E = v.ext;
    for (int i = 0; i < MAX_OBJECTS && undrawn; i++) {
        if (!(undrawn & (1u << i))) continue;
        const uint16_t o = (uint16_t)(OBJECTS + i * OBJ_SIZE);
        const int oy = (int16_t)s.ram16(o), ox = (int16_t)s.ram16((uint16_t)(o + 2));
        const uint16_t fp = s.ram16((uint16_t)(o + 0x0F));
        const int n = bank_rd8(s, 0x12, fp);
        for (int k = 0; k < n && v.extra_count < SmsVdp::kMaxExtra; k++) {
            const uint16_t p = (uint16_t)(fp + 1 + 3 * k);
            const int y = oy + (int8_t)bank_rd8(s, 0x12, p), x = ox + (int8_t)bank_rd8(s, 0x12, (uint16_t)(p + 1));
            if (y < 0 || y >= 0xD0 || x <= -8 - E || x >= 256 + E) continue;
            v.extra[v.extra_count++] = {(int16_t)x, (uint8_t)(y - 1), bank_rd8(s, 0x12, (uint16_t)(p + 2))};
        }
    }
}

// A sprite table entry's 256-px page (module_state[0..3], see sprite_x)
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
    // The title: button 1 skips the intro story (~40 s without input); mode 0 with the title script at its menu.
    int preview_start() const override { return 3000; }
    uint16_t preview_pad(int frame, const uint8_t* ram) const override { (void)ram; return (frame / 4) % 2 == 0 ? SMS_1 : 0; }
    bool preview_ready(const uint8_t* ram) const override { return ram[0x0200] == 0 && ram[0x0F01] == 0x12; }

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
        // A width change in a queue stage: the objects waiting for the camera were placed for the old width
        if (in_stage(s) && queue_objects(s.ram8(STAGE))) {
            const int delta = cfg.ext - (int)s.module_state[MS_LOADED_E];
            for (int i = 0; i < 7 && delta; i++) {
                const uint16_t o = (uint16_t)(OBJECTS + i * OBJ_SIZE);
                if (s.ram8((uint16_t)(o + 0x14)) || s.ram8((uint16_t)(o + 0x1D)) || !s.ram8((uint16_t)(o + 0x20))) continue;
                s.set_ram16((uint16_t)(o + 2), (uint16_t)(s.ram16((uint16_t)(o + 2)) + delta));
            }
        }
        s.module_state[MS_LOADED_E] = (uint32_t)cfg.ext;
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
        v.extra_count = 0;
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
            // stack: the piece's table pointer, two counters, the object ($0C45 pushed ix)
            auto word = [&](int o) { return (uint16_t)(m.peek((uint16_t)(m.z80.sp + o)) | (m.peek((uint16_t)(m.z80.sp + o + 1)) << 8)); };
            const uint16_t obj = word(6);
            // An object parked at the window's left end (x = -64, not moving with the camera: Jafar's room keeps two
            // there) is never seen in the original: its pieces stay hidden
            const bool parked = m.ram16(obj + 2) == 0xFFC0 && !m.ram8((uint16_t)(obj + 0x20));
            piece_x(m, (word(0) - SAT_XT) >> 1, parked);
        });
        // an object the slot allocator has no room for (the caves of wide_patches): drawn as extra sprites
        auto undrawn = [](Sms& m) {
            const int i = (m.z80.ix - OBJECTS) / OBJ_SIZE;
            if (i >= 0 && i < MAX_OBJECTS) m.module_state[MS_UNDRAWN] |= 1u << i;
        };
        s.add_pc_hook(CAVE_FULL_A, undrawn);
        s.add_pc_hook(CAVE_FULL_B, undrawn);
        // the VBlank's sprite table upload ($02D9): in reverse order on odd frames (the game's flicker)
        s.add_pc_hook(0x02D9, [this](Sms& m) {
            const bool rev = m.ram8(FRAMES) & 1;
            for (int w = 0; w < 4; w++) m.module_state[w] = 0;
            for (int i = 0; i < 64; i++)
                m.module_state[i >> 4] |= (uint32_t)page_[rev ? 63 - i : i] << ((i & 15) * 2);
            if (in_stage(m)) extra_sprites(m);
            else m.vdp.extra_count = 0;
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
        const int left = std::clamp(lo + E - cam, 0, E), right = std::clamp(cam + E - hi, 0, E);
        if (s.ram8(STAGE) == ARENA_STAGE) {
            info.dim_left = left;
            info.dim_right = right;
        } else {
            info.mask_left = left;
            info.mask_right = right;
        }
        info.mask_from = 16;     // the status bar (lines 0..15) stays whole
        info.mask_lines = 176;   // (below the playfield: line_hook)
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies, their shots and the carpet's crashes can't hurt Aladdin (the game's own "
                                            "invulnerability, as the developers' password AIQY gives it). Pits and lava still end the stage."},
            {"inf_health", "Infinite Health", "Hits never take a gem away."},
            {"inf_rocks", "Infinite Stones", "Throwing a stone never uses one up."},
            {"dev_menu", "Developer Menu", "The title's PASSWORD opens the developers' TEST MODE instead: character viewer, stage "
                                           "select, sound test, input test and VISUAL TEST (the story scenes). Up / Down choose, "
                                           "Left / Right change, button 1 starts."},
        };
    }

    std::vector<SoundEntry> sounds() const override { return std::vector<SoundEntry>(std::begin(kSounds), std::end(kSounds)); }

    std::vector<AchievementDef> achievements() const override {
        return {
            {"sms_market", "Street Rat", "Escape the guards through the Agrabah market."},
            {"sms_rooftops", "One Jump Ahead", "Clear the rooftop chase."},
            {"sms_cave", "Into the Cave", "Make it through the first halls of the Cave of Wonders."},
            {"sms_treasure", "Touch Nothing", "Get through the Treasure Room."},
            {"sms_lamp", "The Lamp", "Reach the lamp."},
            {"sms_escape", "Out of the Fire", "Escape the collapsing Cave of Wonders on the magic carpet."},
            {"sms_prince", "Prince Ali", "Make it through the Sultan's palace."},
            {"sms_carpet", "Magic Carpet Ride", "Finish the night flight over Agrabah."},
            {"sms_palace", "Jafar's Palace", "Make it through Jafar's palace."},
            {"sms_jafar", "Jafar Defeated", "Defeat Jafar and finish the game."},
            {"sms_newgame", "Diamond in the Rough", "Finish the game from GAME START, without a password."},
            {"sms_nocontinue", "Never Gave Up the Lamp", "Finish the game from GAME START without a game over.", true},
            {"sms_noscratch", "Not a Scratch", "Clear a stage without losing a gem."},
            {"sms_flawless", "Flawless Tour", "Clear every stage, and beat Jafar, without losing a gem.", false, 10, true},
            {"sms_jafar_flawless", "Sultan's Champion", "Defeat Jafar without losing a gem.", true},
            {"sms_stones", "Rock Collector", "Carry nine stones at once.", false, 9},
            {"sms_skip", "Seen It Before", "Skip a story scene (both buttons at once).", true},
        };
    }

    // ---- Achievements (indices follow achievements())
    enum { A_MARKET, A_ROOFTOPS, A_CAVE, A_TREASURE, A_LAMP, A_ESCAPE, A_PRINCE, A_CARPET, A_PALACE, A_JAFAR, A_NEWGAME,
           A_NOCONTINUE, A_NOSCRATCH, A_FLAWLESS, A_JAFAR_FLAWLESS, A_STONES, A_SKIP };
    enum { S_FIRED, S_CLEAN, S_ATTEMPT };
    // run bits: a run is going (a stage was entered), it began with GAME START, a game over happened
    enum : uint32_t { R_RUN = 1, R_NEWGAME = 2, R_GAME_OVER = 4 };

    void achievement_frame(const Sms& m, AchievementCtx& c) const override {
        if (c.prev.size() != sizeof(m.ram)) return;
        const RamViewLE s{m.ram, 0x1FFF}, p{c.prev.data(), 0x1FFF};
        auto fire = [&](int i) {
            if (c.scratch[S_FIRED] & (1 << i)) return;
            c.scratch[S_FIRED] |= 1 << i;
            c.unlocked.push_back(i);
        };
        const int sm = s.b(MODE), pm = p.b(MODE), stage = s.b(STAGE), pstage = p.b(STAGE);
        auto demo = [](const RamViewLE& r) { const int d = r.b(DEMO); return d >= 2 && d <= 0x0B; };
        if (demo(s) || demo(p)) { c.scratch[S_ATTEMPT] = 0; return; }   // the attract mode's recorded play
        // runs: GAME START (the opening scene from the title), a password, a game over, back to the title
        if (pm == 0 && sm == M_SCENE && s.b(SCENE) == 0) c.run_bits = R_RUN | R_NEWGAME;
        if (pm == M_PASSWORD && stage_mode(sm)) c.run_bits = R_RUN;
        if (sm == M_GAME_OVER && pm != M_GAME_OVER) c.run_bits |= R_GAME_OVER;
        if (sm == 0 && pm != 0) c.run_bits = 0;
        // stage clears: the stage number goes up as the stage hands over to its story scene / the next stage
        if (stage_mode(pm) && pstage <= 8 && stage == pstage + 1 && c.scratch[S_ATTEMPT]) {
            fire(A_MARKET + pstage);
            if (c.scratch[S_CLEAN]) {
                fire(A_NOSCRATCH);
                c.marked.push_back({A_FLAWLESS, pstage});
            }
            c.scratch[S_ATTEMPT] = 0;
        }
        // a stage attempt: from its first frame (entered - also straight from the stage before, as the lamp's chamber
        // is -, restarted after a game over, or by the level select)
        const bool selected = c.level_select && stage_mode(sm);
        c.level_select = false;
        if ((stage_mode(sm) && sm != pm) || selected) {
            c.scratch[S_ATTEMPT] = 1;
            c.scratch[S_CLEAN] = 1;
        }
        if (stage_mode(sm) && sm == pm && s.b(HEALTH) < p.b(HEALTH)) c.scratch[S_CLEAN] = 0;
        // Jafar: eight hits ($C6AD) and the stage hands over to the ending's first scene ($20)
        if (pm == 0x0B && sm == M_SCENE && s.b(SCENE) == 0x20) {
            fire(A_JAFAR);
            if (c.scratch[S_ATTEMPT] && c.scratch[S_CLEAN]) {
                fire(A_NOSCRATCH);
                fire(A_JAFAR_FLAWLESS);
                c.marked.push_back({A_FLAWLESS, 9});
            }
            c.scratch[S_ATTEMPT] = 0;
            if (c.run_counts && (c.run_bits & R_RUN) && (c.run_bits & R_NEWGAME)) {
                fire(A_NEWGAME);
                if (!(c.run_bits & R_GAME_OVER)) fire(A_NOCONTINUE);
            }
        }
        if (stage_mode(sm)) {
            const int rocks = s.b(ROCKS);
            if (rocks > p.b(ROCKS) && rocks <= 9) c.progress.push_back({A_STONES, rocks});
            if (rocks == 9) fire(A_STONES);
        }
        // a story scene skipped: both buttons set $CF92 ($66B7, scenes 0-$1F)
        if (sm == M_SCENE && pm == M_SCENE && s.b(SCENE) < 0x20 && s.b(0xCF92) && !p.b(0xCF92)) fire(A_SKIP);
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
        // the title fades out and the password screen sets up (~170 frames) before its loop reads the pad: the four
        // letters are written and button 1 pressed until the game takes them ($744D checks them, sets the stage and
        // its mode)
        const char* pw = kStages[entry].password;
        const int want = entry + 2;
        bool taken = false;
        for (int t = 0; t < 80 && !taken; t++) {
            for (int i = 0; i < 4; i++) s.set_ram8((uint16_t)(PASSWORD + i), (uint8_t)(pw[i] - 'A'));
            run_frames(s, 2, SMS_1);
            for (int f = 0; f < 4 && !taken; f++) { s.run_frame(); taken = mode() == want; }
        }
        if (!taken) return false;
        // the stage's handler starts after the fade-out (rst $28) and loads the stage ($C912 set meanwhile)
        if (!run_until(s, [&]() { return s.ram8(STAGE_INIT) != 0; }, 300)) return false;
        return run_until(s, [&]() { return s.ram8(STAGE_INIT) == 0; }, 300);
    }

    // ---- music player ------------------------------------------------------------------------------------
    // The PSG driver (bank 7, $8000) runs from the VBlank interrupt while $C203 is set; it takes requests in $DE06 /
    // $DE07 / $DE08 (the one with the highest priority in its table at $8C40 wins against the playing sound's,
    // $DE11): $81-$94 music, $95-$D0 sound effects, $D1 fade out, $D2 stop.
    void jukebox_boot(Sms& s) override {
        s.power_on();
        s.clear_pc_hooks();
        run_frames(s, 120);   // (the intro has started the driver)
        // park the CPU in RAM (ei / halt / jr) with nothing for the VBlank to do but call the driver
        static const uint8_t idle[] = {0xFB, 0x76, 0x18, 0xFD};
        for (int i = 0; i < 4; i++) s.set_ram8((uint16_t)(JUKEBOX_IDLE + i), idle[i]);
        s.z80.sp = 0xC100;
        s.z80.pc = JUKEBOX_IDLE;
        s.z80.halted = false;
        s.set_ram8(0xC201, 0);
        stop_sound(s);
        run_frames(s, 4);
    }
    void jukebox_frame(Sms& s) override {
        s.set_ram8(0xC201, 0);
        s.set_ram8(SOUND_ON, 1);
    }
    void play_sound(Sms& s, int id) override {
        s.set_ram8(SOUND_ON, 1);
        s.set_ram8(SOUND_PRIORITY, 0);
        s.set_ram8(id < 0x95 ? SOUND_MUSIC : SOUND_SFX, (uint8_t)id);
    }
    void stop_sound(Sms& s) override {
        s.set_ram8(SOUND_ON, 1);
        s.set_ram8(SOUND_MUSIC, 0xD2);
    }

private:
    Sms* sms_ = nullptr;
    bool hud_anchor_ = true;
    uint8_t page_[64];   // each sprite table buffer entry's page as the writers left it (uploaded at the VBlank)
    SectionTiles s7_;    // stage 7's sections' reloaded tiles (made from ROM when first needed)
    bool s7_built_ = false;

    void piece_x(Sms& m, int slot, bool parked = false) {
        if (slot < 0 || slot >= 64) return;
        if (!in_stage(m)) { page_[slot] = PAGE_0; return; }
        const int x = (int16_t)m.z80.hl, E = m.vdp.ext;
        if (x >= 0 && x < 256) {
            page_[slot] = PAGE_0;
        } else if (x > -8 - E && x < 256 + E && !parked) {
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
        // the status bar (rows 0-1): blank beyond the 256 px; below the playfield: see line_hook
        if (vrow < 2) return v.name_entry(0, vrow);
        if (vrow >= 22) return -1;
        const int hs = v.reg[8];
        const int cam = (int)s.module_state[MS_CAM];
        if (((hs + cam) & 0xFF) != 0) return -1;   // a parallax band: it repeats every 256 px
        int wx = cam + hs + vcol * 8;
        if (s.ram8(STAGE) == ARENA_STAGE) {   // past the arena's ends: its pillars carry on (dimmed by post_frame)
            const int lo = s.ram16(CAM_MIN), end = lo + s.ram16(MAP_STRIDE) * 16;
            if (wx < lo || wx >= end) wx = lo + ARENA_FROM + (((wx - lo - ARENA_FROM) % ARENA_PERIOD) + ARENA_PERIOD) % ARENA_PERIOD;
        }
        const int e = map_entry(s, wx, vrow - 2);
        const int tile = e & 0x1FF;
        if (e >= 0 && s.ram8(STAGE) == 7 && tile >= S7_FIRST && tile < S7_FIRST + S7_TILES) {
            SmsAladdin& mod = *(SmsAladdin*)host;
            if (!mod.s7_built_) { mod.s7_.build(s); mod.s7_built_ = true; }
            SmsVdp& vv = mod.sms_->vdp;
            vv.alt_patterns = mod.s7_.tiles[mod.s7_.section(wx)];
            vv.alt_first = S7_FIRST;
            vv.alt_count = S7_TILES;
            return e | 0x8000;
        }
        return e;
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
        // Below the playfield (lines 176-191) the stages show blank rows (black, Jafar's room red: in either palette's
        // colour 0, depending on the stage), or the text box of a story moment (the lamp's chamber): the sides take the
        // colour the original picture shows at its own edges, so the box stays 256 px wide.
        // (Sprites stay: they can cross the border as in the original.)
        if (line >= 176) {
            std::memset(bg, bg[E] & 0x1F, E);
            std::memset(bg + 256 + E, bg[E + 255] & 0x1F, E);
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
