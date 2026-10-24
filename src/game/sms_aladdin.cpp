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
// A demo of the attract mode plays as recorded and is shown at 4:3 (see the caves).
bool demo_playing(const Sms& s) { const int d = s.ram8(DEMO); return d >= 2 && d <= 0x0B; }
// A stage being played (the widescreen view): stage modes, not a demo.
bool in_stage(const Sms& s) { return stage_mode(s.ram8(MODE)) && s.ram8(STAGE) <= 9 && !demo_playing(s); }

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
    // the Sultan's palace guards' grab (stage 6's second figure, bank 12): `call c,$B90F` after the touch test
    // $B8B3 (which changes nothing) takes Aladdin straight to his death, past the tests above
    {0x32C76, {0xDC, 0x0F, 0xB9}, {0x00, 0x00, 0x00}},
    {0x329DA, {0xDC, 0x0F, 0xB9}, {0x00, 0x00, 0x00}},
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
// level map (map_entry); parallax bands repeat every 256 px and wrap. Near a level's ends the picture slides
// (view_slide: the game's camera stays the original's) so the view shows the level from its first / to its last
// pixel: it then reaches up to 2E px past the original 256-px screen on one side. Objects live in screen coordinates:
// they wake, stay awake and leave with the original's margins around the view as it is shown (the slide included,
// per side, per frame: PC hooks in the caves below give the values); the true X of a sprite beyond the 8-bit table
// X is recorded by PC hooks on the three sprite writers (sprite_x).
constexpr uint16_t GAME_OVER_ENTRY = 0x7270, DEMO_END = 0x352C;
// The attract mode's demos replay recorded input: they run on the original ROM (demo_patch_list) and are shown at
// 4:3 (in_stage); as a second line of defence every cave first tests $C104 and does what the original does.
// Code caves in bank 1's unused zero fill ($7943-$7DFF).
constexpr uint16_t CAVE_WAKE = 0x7A00, CAVE_GONE = 0x7A90, CAVE_FULL_A = 0x7AD0, CAVE_FULL_B = 0x7AF0,
                   CAVE_KEEP_A = 0x7B10, CAVE_KEEP_B = 0x7B30, CAVE_BOULDER = 0x7B50;
// the caves' last instructions, where the module's hooks put the values for the view of the frame (see configure)
constexpr uint16_t CAVE_WAKE_DECIDE = CAVE_WAKE + 11, CAVE_GONE_DE = CAVE_GONE + 19, CAVE_KEEP_A_DE = CAVE_KEEP_A + 19,
                   CAVE_BOULDER_DE = CAVE_BOULDER + 4;
constexpr uint16_t CAVE_FULL_A_WIDE = CAVE_FULL_A + 16, CAVE_FULL_B_WIDE = CAVE_FULL_B + 16;   // (their wide paths)
constexpr uint8_t QUEUE_SLOTS = 7, QUEUE_WIDE = 12;   // the queue stages' object slots: original, wide (see below)
constexpr uint16_t QUEUE_LOOP_COUNT = 0x3D14, RECORD_INDEX = 0xC680;

std::vector<RomPatch> wide_patches(int E) {
    auto zeros = [](size_t n) { return std::vector<uint8_t>(n, 0x00); };
    auto lo = [](int a) { return (uint8_t)a; };
    auto hi = [](int a) { return (uint8_t)(a >> 8); };
    // (a demo playing: carry set; a and the flags change)
    const std::vector<uint8_t> demo_test = {
        0x3A, 0x04, 0xC1,        // ld a,($C104)        the demo's mode (2..$B), else 0 or $FF
        0xD6, 0x02,              // sub 2
        0xFE, 0x0A,              // cp 10
    };
    auto with_demo_test = [&](std::vector<uint8_t> head, const std::vector<uint8_t>& tail) {
        head.insert(head.end(), demo_test.begin(), demo_test.end());
        head.insert(head.end(), tail.begin(), tail.end());
        return head;
    };
    const int E2 = 2 * E;
    // Stages 0/1/5/7/8 keep 7 objects from the stage's list ($410D loads the next one into a free slot): a waiting
    // object wakes when the camera reaches its X (+$28, $3E46: $3E50 `ld hl,($C482)`, then hl - X >= 0). CAVE_WAKE
    // gives hl = $FFFF (wake) or 0 (not yet) from the module's hook (wake_decide), which also places the object.
    const std::vector<uint8_t> wake = with_demo_test({
        0x2A, 0x82, 0xC4,        // ld hl,($C482)       (the original)
    }, {                         // (demo test)
        0xD8,                    // ret c               (a demo: the original test)
        0xC9,                    // ret                 CAVE_WAKE_DECIDE: hl from wake_decide
    });
    // An object leaves (the next one of the list takes the slot) once 32 px past the view's left edge: CAVE_GONE gives
    // the distance for `add hl,de` at $3EC8 (x + de < 0: gone), the hook at CAVE_GONE_DE 32 + E - the slide (32, as
    // the original, for one above or below the screen).
    const std::vector<uint8_t> gone = with_demo_test({
        0xF5,                    // push af
    }, {                         // (demo test)
        0x30, 0x05,              // jr nc,wide
        0xF1,                    // pop af
        0x11, 0x20, 0x00,        // ld de,$0020         (a demo: the original distance)
        0xC9,                    // ret
        0xF1,                    // wide: pop af
        0x11, lo(32 + E), hi(32 + E),   // ld de,32+E
        0xC9,                    // ret                 CAVE_GONE_DE: 32 + E - the slide
    });
    // The sprite slot allocators ($3D74 / $3D5E) put an object that no longer fits in the 64 entries to sleep, but
    // the draw call after them still writes its pieces past the table (over the palette buffer at $C3C0), and a
    // sleeping object of stages 0/1/5/7/8 stops following the camera ($3DC8) for that frame. Rare in the 256-px view,
    // common with more objects awake: the object stays awake, and the module has the VDP draw it after the table's
    // 64 sprites (extra_sprites).
    // (a demo: the original's sleep; the module's hook marks the object at CAVE_FULL_x_WIDE)
    auto full = [&](uint16_t to) {
        return with_demo_test({
            0xF5,                    // push af
        }, {                         // (demo test)
            0x30, 0x06,              // jr nc,wide
            0xF1,                    // pop af
            0xDD, 0x36, 0x1D, 0x00,  // ld (ix+$1D),0       (a demo: as the original)
            0xC9,                    // ret
            0xF1,                    // wide: pop af
            0xE1,                    // pop hl
            0xC3, lo(to), hi(to),    // jp to               (keep it awake, skip the draw)
        });
    };
    const std::vector<uint8_t> full_a = full(0x3D39), full_b = full(0x3CE4);
    // The other stages keep all their objects: one is awake while its x is in [-64, 304) ($3E1F: x + $40 < $170);
    // in a wide view [-64 - E + s, 304 + E + s) for the slide s: x + 64 + E - s < 368 + 2E. CAVE_KEEP_A / _B
    // instead of `ld de,$0040` / `ld de,$0170`; the hook at CAVE_KEEP_A_DE gives 64 + E - s.
    const std::vector<uint8_t> keep_a = with_demo_test({
        0xF5,                    // push af
    }, {                         // (demo test)
        0x30, 0x05,              // jr nc,wide
        0xF1,                    // pop af
        0x11, 0x40, 0x00,        // ld de,$0040         (a demo)
        0xC9,                    // ret
        0xF1,                    // wide: pop af
        0x11, lo(64 + E), hi(64 + E),   // ld de,64+E
        0xC9,                    // ret                 CAVE_KEEP_A_DE: 64 + E - the slide
    });
    const std::vector<uint8_t> keep_b = with_demo_test({
        0xF5,                    // push af
    }, {                         // (demo test)
        0x11, lo(368 + E2), hi(368 + E2),   // ld de,368+2E
        0x30, 0x03,              // jr nc,done
        0x11, 0x70, 0x01,        // ld de,$0170         (a demo)
        0xF1,                    // done: pop af
        0xC9,                    // ret
    });
    // The Caverns' and the Treasure Room's rolling boulders (type $0A, bank $12 $AADE) come back to their home once
    // it is off the screen (`ld a,h; or a; ret z` with hl = home - camera: not within x 0..255) - just past the
    // original's left edge, under its blanked column, they popped up inside a wide view's left side. Off the view
    // now, and on the left past the keep window's edge: not within [-64 - E + s, 256 + E + s). (A home between the
    // two came back only to roll out of the window at once and be put back home again, every few frames while the
    // camera stood still: one past the window's edge stays asleep there.)
    const std::vector<uint8_t> boulder = {
        0xE5,                    // push hl
        0x11, lo(64 + E), hi(64 + E),               // ld de,64+E
        0x19,                    // add hl,de           CAVE_BOULDER_DE: de = 64 + E - the slide
        0x11, lo(320 + E2), hi(320 + E2),           // ld de,320+2E
        0xB7,                    // or a
        0xED, 0x52,              // sbc hl,de           in the view or the keep window left of it
        0xE1,                    // pop hl
        0xD0,                    // ret nc              (back: come home)
        0xD1,                    // pop de              (the call's return)
        0xAF,                    // xor a
        0xC9,                    // ret                 (as the original `ret z`: not yet)
    };
    // Stages 0/1/5/7/8 keep 7 objects of their list at a time ($3D13 the objects' loop, $425B the stage's first
    // loads; bank $12 $B25A a thrower's search for its stone). A wide view shows more at once: with 7 the list
    // could not keep up, and objects woke late, inside the view (A Whole New World at 21:9). QUEUE_WIDE slots
    // (their RAM up to $CCFF is the free-scrolling stages' objects', unused here).
    return {
        {0x3D13, {0x06, QUEUE_SLOTS}, {0x06, QUEUE_WIDE}},
        {0x425B, {0x06, QUEUE_SLOTS}, {0x06, QUEUE_WIDE}},
        {0x4B25A, {0x06, QUEUE_SLOTS}, {0x06, QUEUE_WIDE}},
        {0x4AAEB, {0x7C, 0xB7, 0xC8}, {0xCD, lo(CAVE_BOULDER), hi(CAVE_BOULDER)}},
        {CAVE_BOULDER, zeros(boulder.size()), boulder},
        {0x3E50, {0x2A, 0x82, 0xC4}, {0xCD, lo(CAVE_WAKE), hi(CAVE_WAKE)}},
        {CAVE_WAKE, zeros(wake.size()), wake},
        {0x3EC5, {0x11, 0x20, 0x00}, {0xCD, lo(CAVE_GONE), hi(CAVE_GONE)}},
        // A Whole New World's companion that flies in from the left (type $3B, bank $12 $B3BD) leaves by itself once
        // 32 px past the screen's left edge (x + $20 < 0: off the bottom): past the view's, as everything else
        {0x4B3D6, {0x11, 0x20, 0x00}, {0xCD, lo(CAVE_GONE), hi(CAVE_GONE)}},
        {CAVE_GONE, zeros(gone.size()), gone},
        {0x3E25, {0x11, 0x40, 0x00}, {0xCD, lo(CAVE_KEEP_A), hi(CAVE_KEEP_A)}},
        {CAVE_KEEP_A, zeros(keep_a.size()), keep_a},
        {0x3E29, {0x11, 0x70, 0x01}, {0xCD, lo(CAVE_KEEP_B), hi(CAVE_KEEP_B)}},
        {CAVE_KEEP_B, zeros(keep_b.size()), keep_b},
        {0x3D86, {0xDD, 0x36, 0x1D, 0x00, 0xC9}, {0xC3, lo(CAVE_FULL_A), hi(CAVE_FULL_A), 0x00, 0x00}},
        {CAVE_FULL_A, zeros(full_a.size()), full_a},
        {0x3D6B, {0xDD, 0x36, 0x1D, 0x00, 0xC9}, {0xC3, lo(CAVE_FULL_B), hi(CAVE_FULL_B), 0x00, 0x00}},
        {CAVE_FULL_B, zeros(full_b.size()), full_b},
    };
}

// Before this version a waiting object of the queue stages was placed when loaded, by the width of the time
// (module_state[7] = that width): a state from then has them placed so.
int old_wake_shift(int x, int lo, int E) {
    const int d = std::max(0, x - lo);
    return d <= 2 * E ? d : E;
}

// The stages whose map is a table of 256-px pages ($2E19); the others have one flat map from x = ($C4B9) on ($2DEC).
bool paged_map(int stage) { return stage == 0 || stage == 1 || stage == 5 || stage == 7 || stage == 8; }
// (the same five stages keep 7 objects in a queue fed from the stage's list, $3D09; the others keep them all, $3CAE)
bool queue_objects(int stage) { return paged_map(stage); }

uint8_t bank_rd8(const Sms& s, int bank, uint16_t a) { return s.rom_orig8((uint32_t)bank * 0x4000 + (a & 0x3FFF)); }
uint16_t bank_rd16(const Sms& s, int bank, uint16_t a) {
    return (uint16_t)(bank_rd8(s, bank, a) | (bank_rd8(s, bank, (uint16_t)(a + 1)) << 8));
}

// The pages of a paged map: its table ($C48F, in the stage's bank) is followed by the pages' data, so it ends where
// the first page begins. Past it lie other data (stage 0's table has 15 pages, 3840 px, while the camera's limit
// $C480 is $1300: its camera stops at the map's end, 3584).
int map_pages(const Sms& s, int stage) {
    const int bank = s.rom_orig8(0x3306 + stage);
    const uint16_t table = s.ram16(MAP);
    static int key = -1, pages = 0;   // (from the ROM alone)
    if (key == (bank << 16 | table)) return pages;
    int first = 0x10000, n = 0;
    while (n < 64 && table + 2 * n < first) first = std::min(first, (int)bank_rd16(s, bank, (uint16_t)(table + 2 * n++)));
    key = bank << 16 | table;
    return pages = n;
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
        if ((wx >> 8) >= map_pages(s, stage)) return -1;
        at = (uint16_t)(bank_rd16(s, bank, (uint16_t)(s.ram16(MAP) + 2 * (wx >> 8))) + ((wx & 255) >> 4) + (row >> 1) * stride);
    } else {
        const int mx = (wx - (int)s.ram16(CAM_MIN)) >> 4;
        if (mx < 0 || mx >= stride) return -1;
        at = (uint16_t)(s.ram16(MAP) + mx + (row >> 1) * stride);
    }
    const uint8_t mt = bank_rd8(s, bank, at);
    return bank_rd16(s, bank, (uint16_t)(s.ram16(METATILES) + mt * 8 + (row & 1) * 4 + ((wx >> 3) & 1) * 2)) & 0x1FFF;
}

// "A Whole New World" (stage 7) reloads tiles 259-374 with each of its five sections' graphics as the camera passes
// $400 / $B00 / $1400 / $1B00 ($31EE: the lists in bank $1A, 8 bytes an entry: source, VRAM address, tile count).
// The cells of a section use only its own list's tiles, and none of them lie within 320 px after a threshold (the
// view while the next list loads). The view's sides can show a section whose graphics are not (or no longer) in
// VRAM: those cells take their patterns from a copy of the section's tiles made from ROM.
// Colours: sections 0-3 use the stage's palette ($B3E4 table, $B4D8); once section 4 (the night sky) is under way
// ($C6A6 = 4) the stars twinkle in entries 10-13 ($3950, from $3979) and its list's end loads the background palette
// $33A7 ($3243) - the city's colours are gone while its last buildings are still in the left side. Cells of the
// other palette's sections are drawn with that palette (SmsVdp::alt_cram).
constexpr int S7_FIRST = 259, S7_TILES = 116, S7_SECTIONS = 5, S7_NIGHT = 4;
constexpr uint16_t S7_SECTION = 0xC6A6, PALETTES = 0xB3E4, S7_NIGHT_PALETTE = 0x33A7;

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

// The camera's range: the original view shows world x [lo, hi + 256). A map can end before the camera's limit
// allows (stages 0, 1 and 8, Jafar's room): the map's end counts.
void level_bounds(const Sms& s, int& lo, int& hi) {
    lo = s.ram16(CAM_MIN);
    hi = s.ram16(CAM_MAX);
    const int stage = s.ram8(STAGE);
    if (stage > 9) return;
    if (paged_map(stage)) hi = std::min(hi, map_pages(s, stage) * 256 - 256);
    else hi = std::min(hi, lo + s.ram16(MAP_STRIDE) * 16 - 256);
}

// module_state: [0..3] each sprite table entry's 256-px page as displayed (2 bits each, see sprite_x), [4] the
// camera X at the last VBlank (the view of the next frame; bits 16-23 the frame counter $C20E then), [5] the same
// for the frame being drawn, [6] the objects ($CA00 table) the slot allocator had no room for this frame (bit per
// object), [7] PLACED_AT_WAKE (the waiting objects of the queue stages are placed as they wake, wake_decide); a
// state of an earlier version has the width they were placed for when loaded (old_wake_shift) instead.
constexpr int MS_CAM_NEXT = 4, MS_CAM = 5, MS_UNDRAWN = 6, MS_PLACEMENT = 7;
constexpr uint32_t PLACED_AT_WAKE = 0x40000000;
int shown_cam(const Sms& s) { return (int)(s.module_state[MS_CAM] & 0xFFFF); }
// The camera a line of the camera's band was drawn with: the one whose low byte its scroll register (-camera) has,
// next to the frame's (a line interrupt can load it from the next frame's logic)
int band_camera(const Sms& s, int hs) {
    const int cam = shown_cam(s);
    return cam + ((((-hs - cam) & 0xFF) + 128) & 0xFF) - 128;
}

// The view's slide near a level's ends: the picture is drawn vs px further right than the game's camera puts it
// (the camera itself stays the original's - Aladdin's position is an 8-bit screen X). The view's left edge (world
// x) is camera - E, kept within the level [lo, hi + 256): the view holds still at the level's first / last pixel
// while the camera covers the E px next to the end, then follows it. A level narrower than the view is centred (the
// rest masked). Jafar's room keeps the original camera (its sides are dimmed scenery). From the camera alone, so
// save states and width changes need nothing more.
int view_slide(const Sms& s, int cam, int E) {
    if (!in_stage(s) || s.ram8(STAGE) == ARENA_STAGE || E == 0) return 0;
    int lo, hi;
    level_bounds(s, lo, hi);
    const int end = hi + 256, vw = 256 + 2 * E;
    const int left = end - lo >= vw ? std::clamp(cam - E, lo, end - vw) : lo + (end - lo - vw) / 2;
    return left - (cam - E);
}

// A waiting object of the queue stages (ix = o) at the wake test ($3E46, CAVE_WAKE_DECIDE): hl = $FFFF to wake,
// 0 not yet; the object is placed for the view of the frame (the logic's camera; s = its slide):
// * one that moves with the camera and waits beyond the right edge (screen X 232 or more, mostly 256): it wakes
//   once the view's right edge, E + s px past the original's, reaches its X - at the same place of the level (its X
//   moved by X - camera; one that comes late, as the original's do, at its own X).
// * one inside the original screen (stage 5's bursts of fire, ...): when the original wakes it, where it does.
// * A Whole New World's birds that fly in from the left (screen-space, x -16 / -32; 1 px every 4 frames / every
//   frame) start as far beyond the view's left edge as the original's beyond its own (E - s further out), as much
//   earlier as they take to fly that far (the carpet's camera moves 2 px a frame): from then on they are where the
//   original has them. One the list loads too late for that starts at the view's edge all the same and catches up
//   (bird_catch_up).
// * other screen-space objects: as the original.
int left_entry_frames_per_px(int type) { return type == 0x46 ? 4 : type == 0x3B ? 1 : 0; }

// A Whole New World's birds from the left (types $46 / $3B, bank $12 $B1FC / $B374; still flying in: +$3E / +$2F
// = 0, screen-space) that are not yet where the original starts them (x -16 / -32) when the original would have
// (the camera at their X): 3 px a frame more until they are. (A PC hook on their handlers.)
void bird_catch_up(Sms& m, int start_x, uint8_t state) {
    if (m.bank[2] != 0x12 || !in_stage(m) || m.ram8(STAGE) != 7) return;
    const uint16_t o = m.z80.ix;
    if (m.ram8((uint16_t)(o + state)) || m.ram8((uint16_t)(o + 0x20))) return;
    const int x = (int16_t)m.ram16((uint16_t)(o + 2));
    if (x < start_x && m.ram16(CAM_X) >= m.ram16((uint16_t)(o + 0x28)))
        m.set_ram16((uint16_t)(o + 2), (uint16_t)(x + std::min(3, start_x - x)));
}

void wake_decide(Sms& m) {
    const uint16_t o = m.z80.ix;
    const int cam = m.ram16(CAM_X), X = m.ram16((uint16_t)(o + 0x28)), E = m.vdp.ext;
    const int x0 = (int16_t)m.ram16((uint16_t)(o + 2));
    const int s = view_slide(m, cam, E);
    auto place = [&](int x) { m.set_ram16((uint16_t)(o + 2), (uint16_t)x); };
    m.z80.hl = (uint16_t)cam;   // (the original test)
    if (m.ram8((uint16_t)(o + 0x20))) {
        if (x0 < 232) return;
        if (cam + E + s < X) { m.z80.hl = 0; return; }
        if (X > cam) place(x0 + (X - cam));
        m.z80.hl = 0xFFFF;
        return;
    }
    const int fpp = left_entry_frames_per_px(m.ram8((uint16_t)(o + 0x15))), speed = 2;
    if (x0 >= 0 || !fpp || m.ram8(STAGE) != 7) return;
    const int ahead = speed * fpp;   // camera px per px it flies
    if (cam + ahead * (E - s) < X) { m.z80.hl = 0; return; }
    place(x0 - (E - s));
    m.z80.hl = 0xFFFF;
}

// Increments of a band that moves 1 px on the frames whose counter c has (c & mask) == 0, over the next n frames
// (n < 0: taken back over the last -n).
int band_steps(int c, int n, int mask) {
    int k = 0;
    if (n >= 0) for (int j = 1; j <= n; j++) k += ((c + j) & mask) == 0;
    else for (int j = n + 1; j <= 0; j++) k -= ((c + j) & mask) == 0;
    return k;
}


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
            if (y < 0 || y >= 0xD0 || x <= -8 - 2 * E || x >= 256 + 2 * E) continue;
            v.extra[v.extra_count++] = {(int16_t)x, (uint8_t)(y - 1), bank_rd8(s, 0x12, (uint16_t)(p + 2))};
        }
    }
}

// Whether all of an object's pieces are above or below the screen (its frame as $0C45 draws it; not for a frame
// without pieces): it shows nowhere in the view, whatever its X.
bool off_screen_vertically(const Sms& s, uint16_t o) {
    const int oy = (int16_t)s.ram16(o);
    const uint16_t fp = s.ram16((uint16_t)(o + 0x0F));
    const int n = bank_rd8(s, 0x12, fp);
    if (n == 0 || n > 32) return false;
    for (int k = 0; k < n; k++) {
        const int y = oy + (int8_t)bank_rd8(s, 0x12, (uint16_t)(fp + 1 + 3 * k));
        if (y > -16 && y < 192) return false;
    }
    return true;
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
    // The title as the intro story leads to it (~40 s from power-on, run unseen), then the attract loop. Skipping the
    // story with button 1 isn't kept: the title's music then ends after one pass and the attract demo never comes.
    int preview_start() const override { return 2450; }

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

    // The attract mode's demos replay recorded input against the original game: any change to the code they run
    // changes their course - the windows of the widescreen patches and the cheats, but also just the number of
    // instructions, as the game's random numbers take the Z80's R register ($09B8, $0A0E, $0A64). While a demo
    // plays the ROM is the original (but for the developer menu's title change), and the demo is shown at 4:3
    // (in_stage). The ROM switches at the frame a demo starts or ends (pre_frame): the title's fade lies between
    // the demo flag and the stage's start.
    static std::vector<RomPatch> demo_patch_list(const GameConfig& cfg) {
        return cfg.cheat("dev_menu") ? kDevMenu : std::vector<RomPatch>();
    }
    static std::vector<RomPatch> rom_for(const Sms& s, const GameConfig& cfg) {
        return demo_playing(s) ? demo_patch_list(cfg) : patch_list(cfg);
    }

    bool can_configure(const Sms& s, const GameConfig& cfg) const override { return patches_safe(s, rom_for(s, cfg)); }

    void pre_frame(Sms& s, const GameConfig& cfg) override {
        if (demo_playing(s) == demo_rom_) return;
        const std::vector<RomPatch> rom = rom_for(s, cfg);
        if (!patches_safe(s, rom)) return;   // (a later frame)
        apply_rom(s, rom, false);   // (between the title and a stage: no objects to keep)
        demo_rom_ = !demo_rom_;
    }

    // The ROM for a configuration; the queue stages' slots follow their number (QUEUE_WIDE / QUEUE_SLOTS) in the
    // middle of a stage.
    static void apply_rom(Sms& s, const std::vector<RomPatch>& rom, bool keep_objects = true) {
        const int was = s.rom8(QUEUE_LOOP_COUNT);
        apply_patches(s, rom);
        const int now = s.rom8(QUEUE_LOOP_COUNT);
        if (keep_objects && was != now && in_stage(s) && queue_objects(s.ram8(STAGE)))
            resize_queue(s, was, now);
    }

    // More slots: the new ones take the next records of the list at once (left behind the view: $3EB8 loads the
    // next one). Fewer: the objects move into the first slots - those awake first, then those waiting, earliest
    // first; any that don't fit go back to the list (the latest loaded: the record index steps back), and slots
    // left over load the next ones.
    static void resize_queue(Sms& s, int was, int now) {
        auto slot = [](int i) { return (uint16_t)(OBJECTS + i * OBJ_SIZE); };
        auto empty = [&](int i) {
            for (int b = 0; b < OBJ_SIZE; b++) s.set_ram8((uint16_t)(slot(i) + b), 0);
            s.set_ram8((uint16_t)(slot(i) + 0x14), 1);
            s.set_ram16((uint16_t)(slot(i) + 2), 0x8000);
        };
        if (now > was) {
            for (int i = was; i < now; i++) empty(i);
            return;
        }
        std::vector<std::vector<uint8_t>> awake, waiting;
        std::vector<int> wait_x;
        for (int i = 0; i < was; i++) {
            const uint16_t o = slot(i);
            if (s.ram8((uint16_t)(o + 0x14))) continue;
            std::vector<uint8_t> rec(OBJ_SIZE);
            for (int b = 0; b < OBJ_SIZE; b++) rec[b] = s.ram8((uint16_t)(o + b));
            if (s.ram8((uint16_t)(o + 0x1D))) awake.push_back(rec);
            else waiting.push_back(rec);
        }
        std::stable_sort(waiting.begin(), waiting.end(), [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
            return (a[0x28] | a[0x29] << 8) < (b[0x28] | b[0x29] << 8);
        });
        std::vector<std::vector<uint8_t>> keep = awake;
        if ((int)keep.size() > now) keep.resize(now);
        int back = 0;
        for (const auto& r : waiting) {
            if ((int)keep.size() < now) keep.push_back(r);
            else back++;
        }
        s.set_ram16(RECORD_INDEX, (uint16_t)(s.ram16(RECORD_INDEX) - back));
        for (int i = 0; i < QUEUE_WIDE; i++) {
            if (i < (int)keep.size()) {
                for (int b = 0; b < OBJ_SIZE; b++) s.set_ram8((uint16_t)(slot(i) + b), keep[i][b]);
            } else if (i < now) {
                empty(i);
            }
        }
    }

    void configure(Sms& s, const GameConfig& cfg) override {
        apply_rom(s, rom_for(s, cfg));
        demo_rom_ = demo_playing(s);
        unshift_old_state(s);
        sms_ = &s;
        hud_anchor_ = cfg.hud_anchor;
        s.clear_pc_hooks();
        // A demo that ends in a game over (a cheat can still change its course) would wait on TRY AGAIN, its input
        // being the recording's: back to the title instead, as a demo that loses Aladdin goes ($352C).
        s.add_pc_hook(GAME_OVER_ENTRY, [](Sms& m) { if (demo_playing(m)) m.z80.pc = DEMO_END; });
        SmsVdp& v = s.vdp;
        v.hook_host = this;
        const bool wide = cfg.ext > 0;
        v.hw_x0 = wide ? 8 : 0;
        v.hw_x1 = 256;
        // (the partial cell at the right shows the table column the game is about to rewrite, sometimes a frame
        // late: a stale sliver in the middle of a wide picture. It comes from the map like the sides.)
        v.hw_right_partial_out = wide;
        v.bg_entry = wide ? bg_entry : nullptr;
        v.sprite_x = wide ? sprite_x : nullptr;
        v.line_hook = wide ? line_hook : nullptr;
        s.on_vblank = nullptr;
        s.on_frame_start = nullptr;
        v.sprite_shift = 0;
        v.line_shift = wide ? line_shift : nullptr;
        v.alt_patterns = nullptr;   // (bg_entry points them at stage 7's copies when it uses them)
        v.alt_cram = nullptr;
        std::memset(v.bg_shift, 0, sizeof(v.bg_shift));
        // (a width change within a stage keeps what the hooks recorded for the frames under way)
        if (!wide || !wide_ || !in_stage(s)) {
            v.extra_count = 0;
            std::memset(page_, 0, sizeof(page_));
        }
        wide_ = wide;
        if (!wide) return;
        // (until the next VBlank: the camera of the frame just drawn)
        s.module_state[MS_CAM_NEXT] = s.module_state[MS_CAM] = (uint32_t)(s.ram16(CAM_X) | (s.ram8(FRAMES) << 16));
        apply_view(s);
        // the camera the VBlank shows next (the scroll registers are written from it right after line $C1) and the
        // one the frame being drawn uses
        s.on_vblank = [](Sms& m) { m.module_state[MS_CAM_NEXT] = m.ram16(CAM_X) | ((uint32_t)m.ram8(FRAMES) << 16); };
        s.on_frame_start = [this](Sms& m) { apply_view(m); };
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
        // the objects' margins around the view of the frame (the logic's camera: the one the next frame shows)
        auto slide_now = [](const Sms& m) { return view_slide(m, m.ram16(CAM_X), m.vdp.ext); };
        s.add_pc_hook(CAVE_WAKE_DECIDE, [](Sms& m) { wake_decide(m); });
        s.add_pc_hook(0xB1FC, [](Sms& m) { bird_catch_up(m, -16, 0x3E); });   // (types $46 / $3B, bank $12)
        s.add_pc_hook(0xB374, [](Sms& m) { bird_catch_up(m, -32, 0x2F); });
        // (one that shows nowhere - flown off the top, fallen off the bottom - leaves where the original lets it go,
        // freeing its slot for the next of the list as there)
        s.add_pc_hook(CAVE_GONE_DE, [slide_now](Sms& m) {
            m.z80.de = (uint16_t)(off_screen_vertically(m, m.z80.ix) ? 32 : 32 + m.vdp.ext - slide_now(m));
        });
        s.add_pc_hook(CAVE_KEEP_A_DE, [slide_now](Sms& m) { m.z80.de = (uint16_t)(64 + m.vdp.ext - slide_now(m)); });
        s.add_pc_hook(CAVE_BOULDER_DE, [slide_now](Sms& m) { m.z80.de = (uint16_t)(64 + m.vdp.ext - slide_now(m)); });
        s.add_pc_hook(CAVE_FULL_A_WIDE, undrawn);
        s.add_pc_hook(CAVE_FULL_B_WIDE, undrawn);
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
        // What lies outside the level: only where it is narrower than the view (view_slide centres it), and the sides
        // of Jafar's room (shown dimmed)
        const int cam = shown_cam(s), view = cam - E + view_slide(s, cam, E);
        int lo, hi;
        level_bounds(s, lo, hi);
        const int left = std::clamp(lo - view, 0, E), right = std::clamp(view + 256 + 2 * E - (hi + 256), 0, E);
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

    // Save states (rewind too): besides module_state, what the hooks keep from one frame to the next - the sprite
    // pages the writers left for the next table upload (the game's logic runs across the frame's end), the extra
    // sprites the last upload made for the frame being drawn, and the frame counter at the camera's last move.
    std::vector<uint8_t> save_ext() const override {
        std::vector<uint8_t> d;
        if (!sms_ || !wide_) return d;
        const SmsVdp& v = sms_->vdp;
        d.push_back(EXT_VERSION);
        d.insert(d.end(), page_, page_ + 64);
        d.push_back((uint8_t)moved_at_);
        d.push_back((uint8_t)v.extra_count);
        for (int i = 0; i < v.extra_count; i++) {
            const SmsVdp::ExtraSprite& e = v.extra[i];
            d.insert(d.end(), {(uint8_t)e.x, (uint8_t)(e.x >> 8), e.y, e.pattern});
        }
        return d;
    }

    void load_ext(const uint8_t* d, size_t n) override {
        if (!sms_) return;
        unshift_old_state(*sms_);
        SmsVdp& v = sms_->vdp;
        if (n < 67 || d[0] != EXT_VERSION || n < 67u + 4u * d[66] || d[66] > SmsVdp::kMaxExtra) {
            // (a state without them: as after a width change)
            std::memset(page_, 0, sizeof(page_));
            v.extra_count = 0;
            return;
        }
        std::memcpy(page_, d + 1, 64);
        moved_at_ = d[65];
        v.extra_count = d[66];
        for (int i = 0; i < v.extra_count; i++) {
            const uint8_t* e = d + 67 + 4 * i;
            v.extra[i] = {(int16_t)(e[0] | (e[1] << 8)), e[2], e[3]};
        }
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies, shots, grabs and carpet crashes can't hurt Aladdin; pits and lava can."},
            {"inf_health", "Infinite Health", "Hits never take a gem away. Grabs, carpet crashes, pits and lava still cost a life."},
            {"inf_rocks", "Infinite Stones", "Throwing a stone never uses one up."},
            {"dev_menu", "Developer Menu", "PASSWORD opens TEST MODE: stages, sound test (hold Left/Right, 2 plays), scenes."},
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
    enum { S_FIRED };
    // run bits (they travel with save states): a run is going (a stage was entered), it began with GAME START, a game
    // over happened; and the stage being played: an attempt from its start, and no gem lost in it so far
    enum : uint32_t { R_RUN = 1, R_NEWGAME = 2, R_GAME_OVER = 4, R_ATTEMPT = 8, R_CLEAN = 16 };

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
        auto set = [&](uint32_t bit, bool on) { c.run_bits = on ? c.run_bits | bit : c.run_bits & ~bit; };
        if (demo(s) || demo(p)) { set(R_ATTEMPT, false); return; }   // the attract mode's recorded play
        // runs: GAME START (the opening scene from the title), a password, a game over, back to the title
        if (pm == 0 && sm == M_SCENE && s.b(SCENE) == 0) c.run_bits = R_RUN | R_NEWGAME;
        if (pm == M_PASSWORD && stage_mode(sm)) c.run_bits = R_RUN;
        if (sm == M_GAME_OVER && pm != M_GAME_OVER) c.run_bits |= R_GAME_OVER;
        if (sm == 0 && pm != 0) c.run_bits = 0;
        // stage clears: the stage number goes up as the stage hands over to its story scene / the next stage
        if (stage_mode(pm) && pstage <= 8 && stage == pstage + 1 && (c.run_bits & R_ATTEMPT)) {
            fire(A_MARKET + pstage);
            if (c.run_bits & R_CLEAN) {
                fire(A_NOSCRATCH);
                c.marked.push_back({A_FLAWLESS, pstage});
            }
            set(R_ATTEMPT, false);
        }
        // a stage attempt: from its first frame (entered - also straight from the stage before, as the lamp's chamber
        // is -, restarted after a game over, or by the level select)
        const bool selected = c.level_select && stage_mode(sm);
        c.level_select = false;
        if ((stage_mode(sm) && sm != pm) || selected) {
            set(R_ATTEMPT, true);
            set(R_CLEAN, true);
        }
        if (stage_mode(sm) && sm == pm && s.b(HEALTH) < p.b(HEALTH)) set(R_CLEAN, false);
        // Jafar: eight hits ($C6AD) and the stage hands over to the ending's first scene ($20)
        if (pm == 0x0B && sm == M_SCENE && s.b(SCENE) == 0x20) {
            fire(A_JAFAR);
            if ((c.run_bits & R_ATTEMPT) && (c.run_bits & R_CLEAN)) {
                fire(A_NOSCRATCH);
                fire(A_JAFAR_FLAWLESS);
                c.marked.push_back({A_FLAWLESS, 9});
            }
            set(R_ATTEMPT, false);
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
    // A state of an earlier version (module_state[7] = a width, or 0: also a 4:3 one, or the tools' raw states): its
    // waiting objects were placed when loaded, by that width - they are put back where the original loads them
    // (wake_decide places them as they wake) - and it kept 7 slots: with more in the ROM, the others take the next
    // records of the list.
    static void unshift_old_state(Sms& s) {
        const uint32_t was = s.module_state[MS_PLACEMENT];
        if (!(was & PLACED_AT_WAKE) && in_stage(s) && queue_objects(s.ram8(STAGE))) {
            const int lo = s.ram16(CAM_MIN);
            for (int i = 0; i < QUEUE_SLOTS && was > 0 && was <= 255; i++) {
                const uint16_t o = (uint16_t)(OBJECTS + i * OBJ_SIZE);
                if (s.ram8((uint16_t)(o + 0x14)) || s.ram8((uint16_t)(o + 0x1D)) || !s.ram8((uint16_t)(o + 0x20))) continue;
                const int k = old_wake_shift(s.ram16((uint16_t)(o + 0x28)), lo, (int)was);
                s.set_ram16((uint16_t)(o + 2), (uint16_t)(s.ram16((uint16_t)(o + 2)) - k));
            }
            if (s.rom8(QUEUE_LOOP_COUNT) > QUEUE_SLOTS) resize_queue(s, QUEUE_SLOTS, s.rom8(QUEUE_LOOP_COUNT));
        }
        s.module_state[MS_PLACEMENT] = PLACED_AT_WAKE;
    }

    Sms* sms_ = nullptr;
    bool hud_anchor_ = true;
    static constexpr uint8_t EXT_VERSION = 1;   // (save_ext's layout)
    uint8_t page_[64] = {};   // each sprite table buffer entry's page as the writers left it (uploaded at the VBlank)
    SectionTiles s7_;    // stage 7's sections' reloaded tiles (made from ROM when first needed)
    bool s7_built_ = false;
    uint8_t s7_day_[32] = {}, s7_night_[32] = {};   // stage 7's palettes before / after section 4 (apply_view)
    // the view's slide (apply_view): the parallax bands' first lines (line interrupts), the camera's band, and the
    // frame counter at the camera's last move (stage 1's bands move on its phase; kept in save states)
    int band_start_[3] = {999, 999, 999};
    int cam_band_ = 0;
    // each line's band as drawn: a band starts where its line interrupt writes the scroll - one the game takes late
    // (its logic holding interrupts off) starts a few lines lower than $C499.. say (line_shift)
    int8_t line_band_[SmsVdp::kMaxLines] = {};
    int moved_at_ = 0;
    bool wide_ = false;   // (configured with widescreen: the hooks keep page_ and the extra sprites)
    bool demo_rom_ = false;   // the ROM holds the demos' patches (demo_patch_list), not patch_list

    // The parallax band of a line: 0 up to the first line interrupt's change, 1.. after each one
    int band_of(int line) const {
        int k = 0;
        while (k < 3 && line >= band_start_[k]) k++;
        return k;
    }

    // The view's shift for the frame about to be drawn (Sms::on_frame_start): sprites, and the lines' background
    // through line_shift. The bands come from the line interrupts ($C499-$C49C: the counter reloads, so the second
    // interrupt is at line 2·$C499 + 1 and its handler's scroll shows from 2·$C499 + 3, then + $C49A + 1, + $C49B + 1):
    // stage 0's floor ($C491 / $C492 after the camera's band), stage 1's sky and city ($C495, $C494, $C493 before it);
    // every other stage scrolls as one.
    void apply_view(Sms& m) {
        SmsVdp& v = m.vdp;
        const int before = shown_cam(m);   // (the frame before's)
        m.module_state[MS_CAM] = m.module_state[MS_CAM_NEXT];
        const int E = v.ext, cam = shown_cam(m), stage = m.ram8(STAGE);
        v.sprite_shift = view_slide(m, cam, E);
        for (int& b : band_start_) b = 999;
        cam_band_ = 0;
        if (in_stage(m) && (stage == 0 || stage == 1)) {
            int at = 2 * m.ram8(0xC499) + 3;
            for (int k = 0; k < 3 && at < 192; k++) {
                band_start_[k] = at;
                const int n = m.ram8((uint16_t)(0xC49A + k));
                if (n == 0xFF) break;
                at += n + 1;
            }
            cam_band_ = stage == 0 ? 0 : 3;
        }
        if (cam != before) moved_at_ = (int)((m.module_state[MS_CAM] >> 16) & 0xFF);
        if (stage == 7) {
            // stage 7's two background palettes (from ROM; the sprites' half as it is now)
            const uint16_t day = (uint16_t)(m.rom_orig8(PALETTES + 2 * 7) | (m.rom_orig8(PALETTES + 2 * 7 + 1) << 8));
            for (int i = 0; i < 16; i++) {
                s7_day_[i] = m.rom_orig8((uint32_t)day + i);
                s7_night_[i] = m.rom_orig8((uint32_t)S7_NIGHT_PALETTE + i);
            }
            std::memcpy(s7_day_ + 16, v.cram + 16, 16);
            std::memcpy(s7_night_ + 16, v.cram + 16, 16);
        }
    }

    // A line's background shift, as it is drawn: the status bar (lines 0-15) and the rows below the playfield
    // (176-191: blank, or a story moment's text box) stay; the level's lines slide by the view's shift. The band
    // values a line interrupt loads are read from RAM as it happens - the game's logic for the next frame may have
    // moved them (and the camera) already: each band holds still against the view by what it moves while the
    // camera covers the rest of the slide. Stage 0 (camera 1 px a frame) moves its floor 2 and 3 px for each of the
    // camera's; stage 1 (2 px a frame) moves $C493 1 px every frame, $C494 on even frames, $C495 every fourth ($2CC7:
    // the frame counter's phase at the camera's last move). The logic's lead is taken out without a slide too, so
    // the bands don't jump by it where a hold ends (and keep step with the camera's band and the sprites).
    static int line_shift(void* host, int line) {
        SmsAladdin& mod = *(SmsAladdin*)host;
        const Sms& s = *mod.sms_;
        const int vs = s.vdp.sprite_shift;
        const bool banded = mod.band_start_[0] < 999;   // (stages 0 and 1)
        // (the band: the scroll writes since the frame's first line - the VBlank wrote the first band's)
        int k = mod.band_of(line);
        if (banded && line >= 0 && line < SmsVdp::kMaxLines)
            k = std::min(3, s.vdp.line_hscroll_writes[line] - s.vdp.line_hscroll_writes[0]);
        if (line >= 0 && line < SmsVdp::kMaxLines) mod.line_band_[line] = (int8_t)k;
        if ((vs == 0 && !banded) || line < 16 || line >= 176) return 0;
        const int cam = shown_cam(s);
        if (k == mod.cam_band_) return vs - (band_camera(s, s.vdp.reg[8]) - cam);
        const int live = (int16_t)(s.ram16(CAM_X) - cam);    // how far the logic has moved the camera since
        if (s.ram8(STAGE) == 0) return (k == 1 ? 2 : 3) * (vs - live);
        static const int mask[3] = {3, 1, 0};
        // ($C495 is loaded at the VBlank with the camera; the others by line interrupts, maybe from newer logic)
        const int d = k == 0 ? 0 : live / 2;
        return band_steps(mod.moved_at_ + d, vs / 2 - d, mask[k]);
    }

    void piece_x(Sms& m, int slot, bool parked = false) {
        if (slot < 0 || slot >= 64) return;
        if (!in_stage(m)) { page_[slot] = PAGE_0; return; }
        const int x = (int16_t)m.z80.hl, E = m.vdp.ext;
        if (x >= 0 && x < 256) {
            page_[slot] = PAGE_0;
        } else if (x > -8 - 2 * E && x < 256 + 2 * E && !parked) {   // (as far as the sliding view reaches)
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
        if (vrow < 2) return v.name_entry(0, vrow) & 0x1FFF;
        if (vrow >= 22) return -1;
        const SmsAladdin& mod = *(const SmsAladdin*)host;
        if (mod.line_band_[line % SmsVdp::kMaxLines] != mod.cam_band_) return -1;   // a parallax band: it repeats every 256 px
        const int hs = v.reg[8];
        int wx = band_camera(s, hs) + hs + vcol * 8;
        if (s.ram8(STAGE) == ARENA_STAGE) {   // past the arena's ends: its pillars carry on (dimmed by post_frame)
            const int lo = s.ram16(CAM_MIN), end = lo + s.ram16(MAP_STRIDE) * 16;
            if (wx < lo || wx >= end) wx = lo + ARENA_FROM + (((wx - lo - ARENA_FROM) % ARENA_PERIOD) + ARENA_PERIOD) % ARENA_PERIOD;
        }
        int e = map_entry(s, wx, vrow - 2);
        const int tile = e & 0x1FF;
        if (e >= 0 && s.ram8(STAGE) == 7) {
            SmsAladdin& m7 = *(SmsAladdin*)host;
            if (!m7.s7_built_) { m7.s7_.build(s); m7.s7_built_ = true; }
            SmsVdp& vv = m7.sms_->vdp;
            const int sec = m7.s7_.section(wx);
            if (tile >= S7_FIRST && tile < S7_FIRST + S7_TILES) {
                vv.alt_patterns = m7.s7_.tiles[sec];
                vv.alt_first = S7_FIRST;
                vv.alt_count = S7_TILES;
                e |= 0x8000;
            }
            const bool night = sec >= S7_NIGHT;
            if (night != (s.ram8(S7_SECTION) >= S7_NIGHT)) {   // (the other palette's part of the flight)
                vv.alt_cram = night ? m7.s7_night_ : m7.s7_day_;
                e |= 0x4000;
            }
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
