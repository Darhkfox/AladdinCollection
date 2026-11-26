// Aladdin (Master System, SIMS 1994, European release) - enhancement module.
// Addresses and techniques: docs/re_sms_game.md, docs/re_sms_wide.md.
#include "game.h"
#include "sms_common.h"
#include "../core/sms.h"
#include <algorithm>
#include <climits>
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
constexpr uint16_t STAGE_INIT = 0xC912;  // set while a stage loads (and around the VDP writes of most frames: $0B20)
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
// A stage's picture fading out: the game sets the next mode (the stage number too, at an exit) before it fades the
// stage out ($0913). The picture is the stage's until the screen is black (fade_hooks): module_state[6] bit 28, with
// bits 24-27 the stage shown.
constexpr int MS_SHOWN = 6, SHOWN_STAGE_SHIFT = 24;
constexpr uint32_t SHOWN_STAGE_BITS = 0xFu << SHOWN_STAGE_SHIFT, FADING = 1u << 28;
bool fading(const Sms& s) { return (s.module_state[MS_SHOWN] & FADING) != 0 && s.vdp.ext > 0; }
// Any other screen fading out after the next mode is set (a story scene into its stage, the title into a scene, a
// password into its stage...): module_state[6] bits 29-31, the kind of screen still shown (story screens, below).
constexpr int STORY_FADE_SHIFT = 29;
constexpr uint32_t STORY_FADE_BITS = 7u << STORY_FADE_SHIFT;
int story_fade(const Sms& s) { return s.vdp.ext > 0 ? (int)((s.module_state[MS_SHOWN] & STORY_FADE_BITS) >> STORY_FADE_SHIFT) : 0; }
bool shows_stage(const Sms& s) { return fading(s) || (in_stage(s) && !story_fade(s)); }
int shown_stage(const Sms& s) {
    return fading(s) ? (int)((s.module_state[MS_SHOWN] & SHOWN_STAGE_BITS) >> SHOWN_STAGE_SHIFT) : s.ram8(STAGE);
}

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
    // the Market's pursuer (stage 0's second figure, bank 12): its handler ends in `jp $B6D0`, the catch test, which
    // takes Aladdin to his death ($C438 = $FF at $B74E) without a look at $C467: it returns there instead
    {0x32984, {0xC3, 0xD0, 0xB6}, {0xC9, 0x00, 0x00}},
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
                   CAVE_KEEP_A = 0x7B10, CAVE_KEEP_B = 0x7B30, CAVE_BOULDER = 0x7B50, CAVE_AWAKE43 = 0x7B70,
                   CAVE_ROLL_HOME = 0x7BA0;
// the caves' last instructions, where the module's hooks put the values for the view of the frame (see configure)
constexpr uint16_t CAVE_WAKE_DECIDE = CAVE_WAKE + 11, CAVE_GONE_DE = CAVE_GONE + 19, CAVE_KEEP_A_DE = CAVE_KEEP_A + 19,
                   CAVE_BOULDER_DE = CAVE_BOULDER + 4, CAVE_ROLL_HOME_DE = CAVE_ROLL_HOME + 4,
                   CAVE_ROLL_HOME_DE2 = CAVE_ROLL_HOME + 8;
// (CAVE_AWAKE43's `ret`, where awake43 sets a; the cycles it takes more than the `ld a,(ix+$1D)` it replaces)
constexpr uint16_t CAVE_AWAKE43_RET = CAVE_AWAKE43 + 3;
constexpr int CAVE_AWAKE43_COST = 46 - 19;
constexpr uint16_t CAVE_FULL_A_WIDE = CAVE_FULL_A + 16, CAVE_FULL_B_WIDE = CAVE_FULL_B + 16;   // (their wide paths)
// the Z80 cycles the wake and leave tests' caves take more than the instructions they replace (given back: slot_begin)
constexpr int CAVE_WAKE_COST = 75 - 16, CAVE_GONE_COST = 97 - 10, CAVE_KEEP_COST = 97 - 10;
constexpr uint16_t CAVE_KEEP_B_END = CAVE_KEEP_B + 16;   // (its `pop af`, both paths)
constexpr uint8_t QUEUE_SLOTS = 7, QUEUE_WIDE = 12;   // the queue stages' object slots: original, wide (see below)
constexpr uint16_t QUEUE_LOOP_COUNT = 0x3D14, RECORD_INDEX = 0xC680;
constexpr uint16_t OBJECTS = 0xCA00;     // 7 (stages 0/1/5/7/8) or up to 16 objects, $40 bytes each
constexpr int OBJ_SIZE = 0x40, MAX_OBJECTS = 16;

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
    // The Treasure Room's bats (type $0D, bank $12 $AC16) come back to their home once it is off the screen (`ld a,h;
    // or a; ret z` with hl = home - camera: not within x 0..255) - just past the original's left edge, under its
    // blanked column, they popped up inside a wide view's left side. Off the view now, and on the left past the keep
    // window's edge: not within [-64 - E + s, 256 + E + s).
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
    // The free-scrolling stages' fixed hazards run on the original's keep window, x in [-64, 304) ($3E1F), not the
    // wide one: what they do while awake starts, and what they do once asleep (re-arm, come back home) happens, at
    // the moment the 4:3 game does it, relative to Aladdin - at every width. The wide window had them act E + s px of
    // camera travel earlier and re-arm only that much further away: a ceiling boulder that had fallen stayed on the
    // floor when Aladdin came back (the 4:3 game had put it back at the ceiling: it falls again). They stay awake in
    // the wide window for the drawing (the flag the VDP and the touch test read). CAVE_AWAKE43 replaces their
    // `ld a,(ix+$1D)` (each followed by `or a`); the hook at its `ret` (awake43) clears a outside the original's
    // window (in the free-scrolling stages: the queue stages' objects wake and leave by their own rules, section 3).
    //   bank $12 $A77A  type $0C, the Caverns' geysers: erupt every few seconds while awake (sound $CC)
    //   bank $12 $A862  type $12, spikes: rise / retract as Aladdin nears; asleep: back to their first phase
    //   bank $12 $AA53  types $09 / $1E (handler $A9D9: the Caverns' ceiling boulders, the Sultan's Palace's ceiling
    //                   drops): fall once x is in [64, 160); landed, they lie there while awake and go back to the
    //                   ceiling once asleep
    //   bank $12 $AA7D  type $0A, rolling boulders: roll left from home while awake, come home once asleep
    //   bank $12 $AB12  type $0E, ledges that crumble under Aladdin: asleep, back home
    const std::vector<uint8_t> awake43 = {
        0xDD, 0x7E, 0x1D,        // ld a,(ix+$1D)       (the original's)
        0xC9,                    // ret                 CAVE_AWAKE43_RET: a as the original's window has it
    };
    // The rolling boulders (type $0A, bank $12 $AADE) come back home after their fall once home is off the original
    // screen (the same `ld a,h; or a; ret z`): on the right, home x 256 or more, as the 4:3 game does - so a boulder
    // that rolls again and again from a home just past the 4:3 edge does so at every width (seen coming home in the
    // right side). On the left not within the wide keep window's edge, -64 - E + s (a home there came back under the
    // original's blanked column - it popped up inside a wide view's left side - and rolls away left, out of play):
    // home not within [-64 - E + s, 256).
    const std::vector<uint8_t> roll_home = {
        0xE5,                    // push hl
        0x11, lo(64 + E), hi(64 + E),               // ld de,64+E
        0x19,                    // add hl,de           CAVE_ROLL_HOME_DE: de = 64 + E - the slide
        0x11, lo(320 + E), hi(320 + E),             // ld de,320+E
        0xB7,                    // or a                CAVE_ROLL_HOME_DE2: de = 320 + E - the slide
        0xED, 0x52,              // sbc hl,de
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
        // (the other loops over them: stage 8's flying figure drops what waits for it - pots, type $51 - once the
        // camera reaches it (bank $0C $AE96: past the seventh slot they waited at x 256 for good, awake, in the right
        // side); the Market's pursuer runs into what is on the screen (bank $0C $B5C0))
        {0x32E9A, {0x06, QUEUE_SLOTS}, {0x06, QUEUE_WIDE}},
        {0x335C4, {0x06, QUEUE_SLOTS}, {0x06, QUEUE_WIDE}},
        {0x4AAEB, {0x7C, 0xB7, 0xC8}, {0xCD, lo(CAVE_ROLL_HOME), hi(CAVE_ROLL_HOME)}},
        {CAVE_ROLL_HOME, zeros(roll_home.size()), roll_home},
        // (the Treasure Room's bats, type $0D, bank $12 $AC16: one that flew off came home at x -2, the top left corner
        // of the original screen, and swooped out of the wide view's left side)
        {0x4AC23, {0x7C, 0xB7, 0xC8}, {0xCD, lo(CAVE_BOULDER), hi(CAVE_BOULDER)}},
        {CAVE_BOULDER, zeros(boulder.size()), boulder},
        {0x4A77A, {0xDD, 0x7E, 0x1D}, {0xCD, lo(CAVE_AWAKE43), hi(CAVE_AWAKE43)}},
        {0x4A862, {0xDD, 0x7E, 0x1D}, {0xCD, lo(CAVE_AWAKE43), hi(CAVE_AWAKE43)}},
        {0x4AA53, {0xDD, 0x7E, 0x1D}, {0xCD, lo(CAVE_AWAKE43), hi(CAVE_AWAKE43)}},
        {0x4AA7D, {0xDD, 0x7E, 0x1D}, {0xCD, lo(CAVE_AWAKE43), hi(CAVE_AWAKE43)}},
        {0x4AB12, {0xDD, 0x7E, 0x1D}, {0xCD, lo(CAVE_AWAKE43), hi(CAVE_AWAKE43)}},
        {CAVE_AWAKE43, zeros(awake43.size()), awake43},
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
    const int stage = shown_stage(s);
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

// Tiles the game animates with the camera (stages 0 and 1). Stage 0's sky gaps have side walls drawn in perspective -
// the left wall (B: tiles 265-272, world x 64 mod 512) shows its face while it is left of the screen's middle, the right
// wall (A: 257-264, x 176 mod 512) while right of it - and a brick wall between them (tile 273) that moves slower than
// the camera; stage 1's roofs end in faces the same way at its gaps (B 265-272 at x 96 mod 512, A 257-264 at 128). $30F6 /
// $3159 compute the uploads for the camera's phase - stage 0: (camera - 40) & $1FC, 4-px steps, tables bank $0A $8000
// (A), bank 2 $A324 (B) and $0A $8400 (the bricks, 8 steps of 32 px); stage 1: (camera - 40) & $1F8, 8-px steps, bank
// $0A $9850 / $9A50; an entry is source, VRAM address, tile count, mostly a part of the tiles - and the VBlank job ($32DE /
// $32EA, every fourth frame) copies them from bank $3311[stage]. So the tiles show the one wall of each kind near the
// original screen as it looks at its place there; where the original never shows one they hold whatever suited the
// tables: a right wall in the right side showed no face until 8 px from the 4:3 picture and then all of it at once, a left
// wall just past the left edge scraps of other tiles, a left wall in the right side bricks out of step with the wall
// beside it, and stage 1's roof ends far in a side another gap's look. The module keeps the tiles' state for every phase
// (CamTiles, from ROM: a whole cycle of uploads in order) and gives the VDP each frame the tiles (SmsVdp::bg_alt) as for
// the wall of each kind in view at a place where the tables are right for it (kAnimGroups): one wholly off the original
// screen and past those places looks as it does at their end - in stage 0 a whole number of 32-px brick steps away, so
// its bricks keep step with the wall's. While the view holds still at a stage's start or end (the slide) the tiles are
// those of the view's camera (camera + slide), the 4:3 part's too: walls and bricks hold still with the picture.
constexpr int ANIM_FIRST = 257, ANIM_TILES = 17;   // tiles 257-273 (stage 1: 257-272)
struct AnimGroup {
    int stage, first, xref;          // its 8 tiles; the wall's world x (mod 512) the tables' phases are for
    int left_lo, left_hi, right_lo, right_hi;   // where the tables are right for a wall wholly off the original screen
    int left_win, right_win;         // else it is shown as at [win, win + step) (stage 0: a brick step, 32 px; else 1)
};
const AnimGroup kAnimGroups[] = {
    {0, 257, 176, -240, -8, 256, 263, -240, 232},   // stage 0, right walls: all face from 232 on, none left of 100
    {0, 265, 64, -148, -20, 1, 0, -52, 197},        // left walls: all face -148..-20; right of 196: bricks in step
    {1, 257, 128, -128, -24, 256, 375, -128, 375},  // stage 1's roof ends (right of the gap), as far as the tables go
    {1, 265, 96, -160, -24, 256, 343, -160, 343},   // (left of the gap)
};

struct CamTiles {
    int stage = -1, phases = 0;
    uint16_t map = 0;
    std::vector<uint8_t> state;      // [phase][tile - ANIM_FIRST][32 bytes]
    uint8_t bricks[8][32] = {};      // stage 0's tile 273 by brick step
    struct Wall { int group, x0, w; };
    std::vector<Wall> walls;
    int phase(int cam) const { return stage == 0 ? ((cam - 40) & 0x1FC) >> 2 : ((cam - 40) & 0x1F8) >> 3; }
    const uint8_t* tiles(int cam) const { return &state[(size_t)phase(cam) * ANIM_TILES * 32]; }
    void build(const Sms& s, int st);
};

void CamTiles::build(const Sms& s, int st) {
    stage = st;
    map = s.ram16(MAP);
    phases = st == 0 ? 128 : 64;
    state.assign((size_t)phases * ANIM_TILES * 32, 0);
    const int src_bank = s.rom_orig8(0x3311 + st);
    uint8_t cur[ANIM_TILES * 32] = {};
    auto upload = [&](uint16_t src, uint16_t vram, int n) {
        const int t0 = (vram & 0x3FFF) / 32 - ANIM_FIRST;
        for (int i = 0; i < n; i++)
            if (t0 + i >= 0 && t0 + i < ANIM_TILES)
                for (int b = 0; b < 32; b++) cur[(t0 + i) * 32 + b] = bank_rd8(s, src_bank, (uint16_t)(src + 32 * i + b));
    };
    auto entry = [&](int bank, uint16_t at) {
        upload(bank_rd16(s, bank, at), bank_rd16(s, bank, (uint16_t)(at + 2)), bank_rd8(s, bank, (uint16_t)(at + 4)));
    };
    // (two cycles of the job's uploads in order: the second leaves each phase's state)
    for (int q = 0; q < 2 * phases; q++) {
        const int p = q % phases;
        if (st == 0) {
            upload(bank_rd16(s, 0x0A, (uint16_t)(0x8400 + 2 * (p & 7))), 0x6220, 1);   // (tile 273, $32DE)
            entry(0x0A, (uint16_t)(0x8000 + 8 * p));
            entry(0x02, (uint16_t)(0xA324 + 8 * p));
        } else {
            entry(0x0A, (uint16_t)(0x9850 + 8 * p));
            entry(0x0A, (uint16_t)(0x9A50 + 8 * p));
        }
        if (q >= phases) std::memcpy(&state[(size_t)p * ANIM_TILES * 32], cur, sizeof(cur));
    }
    for (int k = 0; k < 8; k++)
        for (int b = 0; b < 32; b++) bricks[k][b] = bank_rd8(s, src_bank, (uint16_t)(bank_rd16(s, 0x0A, (uint16_t)(0x8400 + 2 * k)) + b));
    // the walls: the map's columns with the animated tiles, by group
    walls.clear();
    const int end = map_pages(s, st) * 256;
    for (int g = 0; g < (int)std::size(kAnimGroups); g++) {
        const AnimGroup& G = kAnimGroups[g];
        if (G.stage != st) continue;
        int from = -1, last = -1;
        for (int wx = 0; wx <= end; wx += 8) {
            bool used = false;
            for (int row = 0; row < 20 && !used && wx < end; row++) {
                const int t = map_entry(s, wx, row) & 0x1FF;
                used = t >= G.first && t < G.first + 8;
            }
            if (used && from < 0) from = wx;
            if (used) last = wx;
            if (!used && from >= 0) {
                walls.push_back({g, from, last + 8 - from});
                from = -1;
            }
        }
    }
}

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
    const int stage = shown_stage(s);
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
// module_state[7] also holds the queue loop's slot being timed (slot_time): bits 0-19 the Z80 cycle (in its frame,
// + SLOT_BIAS) at which it began, bit 20 that frame's parity, bit 21 a slot is being timed, bit 22 its time is free
constexpr uint32_t SLOT_START = 0xFFFFF, SLOT_PARITY = 1u << 20, SLOT_OPEN = 1u << 21, SLOT_FREE = 1u << 22;
constexpr int SLOT_BIAS = 0x40000, SLOT_CREDIT = 0x8000;
// module_state[7] bits 23 / 24: the palace's neighbouring guard may be drawn at its place on the left / right
// (palace_guards)
constexpr uint32_t GUARD_ARMED_LEFT = 1u << 23, GUARD_ARMED_RIGHT = 1u << 24;
// module_state[7] bit 25: a widened story scene's table, uploaded reversed (odd frames), is drawn in the buffer's order
// (SmsVdp::sprite_rev; without the sprite limit only): see StorySprites::upload
constexpr uint32_t STORY_ORDER = 1u << 25;
// module_state[6]: bits 0-15 the undrawn objects, bits 16-23 how far Aladdin is left behind past the original
// screen's left edge (push_left)
constexpr uint32_t UNDRAWN_BITS = 0xFFFF;
constexpr int BEHIND_SHIFT = 16;
constexpr uint16_t AL_X = 0xC402, PIT = 0xC461;
int behind(const Sms& m) { return (int)((m.module_state[MS_UNDRAWN] >> BEHIND_SHIFT) & 0xFF); }
void set_behind(Sms& m, int d) {
    m.module_state[MS_UNDRAWN] = (m.module_state[MS_UNDRAWN] & ~(0xFFu << BEHIND_SHIFT)) | ((uint32_t)d << BEHIND_SHIFT);
}
// The parallax bands' line interrupts ($01E9: jp ($C91D), the chain $01F3 -> $0205 -> $0222 -> $023F): the k-th
// handler with a scroll (k = 1..3) writes reg 8 = -($C4A7 + k).
constexpr uint16_t BAND_SCROLL = 0xC4A7, BAND_NEXT = 0xC91D;
constexpr uint16_t BAND_CHAIN[4] = {0x01F3, 0x0205, 0x0222, 0x023F};
// How many lines past its own the drawing-only correction still draws a split that has not landed (line_scroll):
// the game takes a line interrupt a few lines late now and then (its logic holding interrupts off).
constexpr int SPLIT_LATE = 6;

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
    if (!shows_stage(s) || shown_stage(s) == ARENA_STAGE || E == 0) return 0;
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
// = 0, screen-space) that are behind the original's once the original's has started (the camera at their X): 1 px
// a frame more until they are where it is by then - on its path from then on, without a dash across the side.
// (A PC hook on their handlers.)
constexpr int BIRD_CATCH_UP = 1;
void bird_catch_up(Sms& m, int start_x, uint8_t state) {
    if (m.bank[2] != 0x12 || !in_stage(m) || m.ram8(STAGE) != 7) return;
    const uint16_t o = m.z80.ix;
    if (m.ram8((uint16_t)(o + state)) || m.ram8((uint16_t)(o + 0x20))) return;
    const int x = (int16_t)m.ram16((uint16_t)(o + 2)), cam = m.ram16(CAM_X), X = m.ram16((uint16_t)(o + 0x28));
    if (cam < X) return;
    // (where the original's is by now: it started at start_x when the camera reached X, the carpet's camera moving
    // 2 px a frame, and it flies 1 px every fpp frames)
    const int fpp = left_entry_frames_per_px(m.ram8((uint16_t)(o + 0x15)));
    const int target = start_x + (fpp ? (cam - X) / (2 * fpp) : 0);
    if (x < start_x || x < target - 1) m.set_ram16((uint16_t)(o + 2), (uint16_t)(x + std::min(BIRD_CATCH_UP, target - x)));
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

// A fixed hazard's awake test (ix = the object; a = its +$1D, CAVE_AWAKE43_RET): in the free-scrolling stages awake
// only inside the original's keep window, x in [-64, 304) ($3E1F), so it acts, re-arms and comes home when the 4:3
// game has it do so (wide_patches). The queue stages' objects (stage 8's ceiling drops, type $1E) wake at their
// X and leave by the leave test: as before.
void awake43(Sms& m) {
    if (queue_objects(m.ram8(STAGE)) || demo_playing(m)) return;
    const int x = (int16_t)m.ram16((uint16_t)(m.z80.ix + 2));
    if (x < -64 || x >= 304) m.z80.af &= 0x00FF;   // (a = 0: the caller's `or a` reads it)
}

// The objects' CPU time. A wide view keeps more objects awake (they wake earlier and leave later), the queue stages
// (0/1/5/7/8) in more slots (QUEUE_WIDE): the objects' loop no longer fit the frame, and the auto-scrolling stages
// lagged (the logic missed frames: the scroll stalled, the Rooftops took 18 % longer at 21:9). A pass of the loop (a
// slot: queue stages $3D15..$3D39, the others $3CBC..$3CE4) the original would not have made takes no time: an
// object awake outside the original's window (queue stages: x beyond 256, where it would still wait, or past -32,
// where it would have left; the others: outside [-64, 304)), and a waiting or empty slot past the original's seven.
// The pass runs with the clock stopped: slot_begin gives the Z80 a credit of cycles (wait_cycles, negative), so the
// run goes on without the lines or the interrupts moving on, and slot_end takes back what the pass did not use. The
// caves' extra instructions are taken off the same way (CAVE_*_COST). The original's own work keeps its cost, and
// its slowdowns.
int cycle_now(const Sms& m) { return m.line_cycle() + m.current_line() * Sms::kCyclesPerLine; }

void slot_begin(Sms& m, bool queue) {
    uint32_t& st = m.module_state[MS_PLACEMENT];
    st &= ~(SLOT_START | SLOT_PARITY | SLOT_OPEN | SLOT_FREE);
    if (!in_stage(m) || queue_objects(m.ram8(STAGE)) != queue) return;
    const uint16_t o = m.z80.ix;
    const int slot = (o - OBJECTS) / OBJ_SIZE;
    const bool awake = !m.ram8((uint16_t)(o + 0x14)) && m.ram8((uint16_t)(o + 0x1D));
    const int x = (int16_t)m.ram16((uint16_t)(o + 2));
    bool original;
    if (queue) original = awake ? x >= -32 && x <= 256 : slot < QUEUE_SLOTS;
    else original = !awake || (x >= -64 && x < 304);   // (the other stages keep all their objects: $3E1F's window)
    if (original) return;
    st |= SLOT_OPEN | SLOT_FREE | ((m.frame_count & 1) ? SLOT_PARITY : 0) | ((uint32_t)(cycle_now(m) + SLOT_BIAS) & SLOT_START);
    m.z80.wait_cycles -= SLOT_CREDIT;   // (the run goes on, the lines and the interrupts wait)
}

void slot_end(Sms& m) {
    uint32_t& st = m.module_state[MS_PLACEMENT];
    if (!(st & SLOT_OPEN)) return;
    const bool same_frame = ((st & SLOT_PARITY) != 0) == ((m.frame_count & 1) != 0);
    const int start = (int)(st & SLOT_START) - SLOT_BIAS;
    st &= ~(SLOT_START | SLOT_PARITY | SLOT_OPEN | SLOT_FREE);
    // (the clock back where the pass began: what is left of the credit; none if the pass took it all and the frame
    // went on)
    const int back = start - cycle_now(m);
    if (same_frame && back > 0) { m.z80.wait_cycles += back; }
}

// Left behind by the scroll (stages 1 and 8, $34A5 each frame): while the scroll pushes Aladdin ($C41C: standing
// still, or held by a wall) his X goes down 2 px a frame, and once it would go below 0 the game counts him lost (the pit
// flag $C461, health 0, $34BC) and carries him on to x -32 ($34C7), where he waits until TRY AGAIN - under the
// original's blanked column, out of sight; in a wide view 70 px inside the picture. His X is an 8-bit screen position
// for the game, so in a wide view it stays at 0 while the module counts how much further left the scroll carries him
// (behind: the sprite writer's hook draws him so much further left) and the game takes him once he is as far past the
// view's left edge as the original takes him past its own (its blanked column: 8 px). A step right takes it back
// first (push_left_frame).
constexpr int BEHIND_MARGIN = 8;
void push_left_frame(Sms& m) {   // ($34A5's entry)
    const int d = behind(m);
    if (!d || m.ram8(PIT)) return;
    const int real = (int16_t)m.ram16(AL_X) - d;
    m.set_ram16(AL_X, (uint16_t)std::max(real, 0));
    set_behind(m, std::max(-real, 0));
}
void push_left(Sms& m) {         // ($34B4: hl = his X less 2, the test whether that is below 0)
    const int nx = (int16_t)m.z80.hl, E = m.vdp.ext;
    if (nx >= 0 || !in_stage(m)) return;
    const int real = nx - behind(m), edge = -(E - view_slide(m, m.ram16(CAM_X), E));
    if (real < edge - BEHIND_MARGIN || -real > 0xFF) return;   // lost, as in the original (and drawn as far left)
    m.set_ram16(AL_X, 0);
    set_behind(m, -real);
    m.z80.pc = 0x34BB;   // (ret)
}
// ($36B8, the pad's walk, when nothing pushes him: below x 24 the game puts him at 24, a rescue that never reaches
// further than the blanked column there; one left behind further catches up at 4 px a frame instead, unless he walks
// right himself)
void push_left_release(Sms& m) {
    const int d = behind(m);
    if (!d || m.ram8(PIT) || m.ram8(0xC41C) || (m.ram8(0xC206) & 0x08)) return;
    set_behind(m, std::max(d - 4, 0));
    m.z80.pc = 0x36C9;   // (ret)
}

// Increments of a band that moves 1 px on the frames whose counter c has (c & mask) == 0, over the next n frames
// (n < 0: taken back over the last -n).
int band_steps(int c, int n, int mask) {
    int k = 0;
    if (n >= 0) for (int j = 1; j <= n; j++) k += ((c + j) & mask) == 0;
    else for (int j = n + 1; j <= 0; j++) k -= ((c + j) & mask) == 0;
    return k;
}

// The pieces of an object the sprite table had no room for, drawn by the VDP after the table's 64 (as $0C45 would
// have: frame data in bank $12 at +$0F, count then dy / dx / tile per piece; the 16-bit Y / X at +$00 / +$02).
void extra_sprites(Sms& s) {
    SmsVdp& v = s.vdp;
    v.extra_count = 0;
    const uint32_t undrawn = s.module_state[MS_UNDRAWN] & UNDRAWN_BITS;
    s.module_state[MS_UNDRAWN] &= ~UNDRAWN_BITS;
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

// ---- the Sultan's palace guards (stage 6's second figure, bank $0C; docs/re_sms_wide.md section 3) ---------------
// The guard is the second figure ($C698 type, $C687 Y, $C689 screen X, moved with the camera by $B58D; drawn by
// $3C13 into table entries 20-36, its pose's 17 tiles streamed from ROM). Which guard a stretch has is a table by
// sub-area ($C49F) and the camera's 256-px page ($B4D0: $B521[sub][page]); a new type is put at its place ($B551:
// world X, animation, Y). Types 3 and 12 (handler $AC53) wait there unseen (Y $C0) and wake once their screen X is
// in [-48, -41] ($ACA2: X & $F8 = $FFD0): 41-48 px past the original screen's left edge, inside a wide view - they
// popped up in the left side and ran from there. The others (4-8, 13: $A9BA) patrol at their place from the moment
// their page is reached: type 6 at x 256, inside the right side. Their logic stays the original's; the module draws
// what the 4:3 game has off its screen there (extra sprites, the poses from ROM):
// * a waiting guard right of its wake (X > -41, the camera coming towards it) runs in from beyond the wide view's
//   left edge on the path that meets the game's at the wake: the mirror of its place about the wake (screen x = -80 -
//   X: 2 px of the level per camera px, as fast as the guard runs while Aladdin walks), in the game's own run cycle
//   (the hidden guard's animation runs on); it stands, facing him, while the camera has been still for a few frames,
//   and runs back out (left) as the camera turns back. At the wake it is where the game puts it, on the same pose.
// * one left of its wake (X < -48: the camera coming back towards it) stands at its place, facing right, until the
//   game wakes it there;
// * the guard of a neighbouring page whose place is in the view stands there in its first pose until the game puts
//   it there (type 6 when the camera reaches $800 in sub-area 1). Coming back, it is drawn there again once the
//   game has taken it away (at most 48 px from where its patrol had it).
// Drawing only, from RAM and ROM alone: save states and rewind need nothing.
constexpr int PALACE_STAGE = 6, FIG_BANK = 0x0F, FIG_PIECES = 17, FIG_BASE = 18, FIG_SLOT = 20;
constexpr uint16_t FIG_Y = 0xC687, FIG_X = 0xC689, FIG_TYPE = 0xC698, FIG_POSE = 0xC690, FIG_STATE = 0xC6A3;
constexpr uint16_t FIG_ANIMS = 0x3C76, GUARD_PAGES = 0xB521, GUARD_TYPES = 0xB551, GUARD_PATROL = 0xA9BA;
constexpr uint16_t GUARD_HANDLERS = 0xA95C, SUB_AREA = 0xC49F, AL_STATE = 0xC406, AL_FACING = 0xC404;
constexpr int GUARD_WAKE_LO = -48, GUARD_WAKE_HI = -41, GUARD_Y = 0x68;
constexpr uint16_t GUARD_STAND = 0x80C6;   // (anim 0, facing right, bank $0F)
constexpr int GUARD_STILL = 4;            // (frames without a camera move: the camera is still)
constexpr int GUARD_LEFT = 7, GUARD_RIGHT = 56;   // (its poses' pieces lie within x + 7 .. x + 55)

// a pose of the second figure (its record in bank $0F: tile bank, duration, tile address, 4 more; the piece list
// after it: 17 Y offsets, then 17 X offset / pattern pairs) as $3C13 places it, at screen x, y
void add_figure_pose(Sms& s, uint16_t rec, int x, int y) {
    SmsVdp& v = s.vdp;
    const int E = v.ext;
    const uint16_t list = (uint16_t)(rec + 8);
    const uint32_t tiles = (uint32_t)bank_rd8(s, FIG_BANK, rec) * 0x4000 + (bank_rd16(s, FIG_BANK, (uint16_t)(rec + 2)) & 0x3FFF);
    for (int i = 0; i < FIG_PIECES && v.extra_count < SmsVdp::kMaxExtra; i++) {
        int a = bank_rd8(s, FIG_BANK, (uint16_t)(list + i));
        if (a == 0xC0) continue;
        a = (a + y) & 0xFF;
        if (a < 0xF8 && a >= 0xC0) continue;
        const int px = x + (int8_t)bank_rd8(s, FIG_BANK, (uint16_t)(list + FIG_PIECES + 2 * i));
        const uint8_t pat = bank_rd8(s, FIG_BANK, (uint16_t)(list + FIG_PIECES + 2 * i + 1));
        if (pat < FIG_BASE || pat >= FIG_BASE + FIG_PIECES || px <= -8 - 2 * E || px >= 256 + 2 * E) continue;
        const uint8_t* data = s.rom_orig_ptr(tiles + (uint32_t)(pat - FIG_BASE) * 32, 32);
        if (data) v.extra[v.extra_count++] = {(int16_t)px, (uint8_t)(a - 1), pat, data};
    }
}

// the first record of the figure's animation `anim` facing right (+$3C76: the table used with $C697 set) or left
uint16_t figure_record(const Sms& s, int type, int anim, bool right, int frame = 0) {
    const uint16_t tab = (uint16_t)(s.rom_orig8(FIG_ANIMS + 4 * type + (right ? 0 : 2)) |
                                    (s.rom_orig8(FIG_ANIMS + 4 * type + (right ? 1 : 3)) << 8));
    const uint16_t a = bank_rd16(s, FIG_BANK, (uint16_t)(tab + 2 * anim));
    return bank_rd16(s, FIG_BANK, (uint16_t)(a + 2 * frame));
}

// moving: the camera moved in the last few frames
void palace_guards(Sms& s, bool moving) {
    const int E = s.vdp.ext;
    if (E == 0 || s.ram8(STAGE) != PALACE_STAGE || !in_stage(s)) return;
    const int type = s.ram8(FIG_TYPE), cam = s.ram16(CAM_X);
    if (type == 0 || type > 15) return;
    // the table as uploaded shows the guard (the writer has drawn it since it woke): nothing to add
    bool shown = false;
    for (int i = FIG_SLOT; i < FIG_SLOT + FIG_PIECES; i++) {
        const uint8_t y = s.ram8((uint16_t)(0xC300 + i));
        if (y != 0xBF && y != 0xC0) shown = true;
    }
    if ((type == 3 || type == 12) && s.ram8(FIG_STATE) == 0 && !shown) {
        const int x = (int16_t)s.ram16(FIG_X);
        if (s.ram8(FIG_Y) != 0xC0) {
            // woken since the table was written (the writer's pass still to come): where the game has it
            add_figure_pose(s, (uint16_t)(s.ram16(FIG_POSE) - 8), x, s.ram8(FIG_Y));
        } else if (x > GUARD_WAKE_HI) {
            const int gx = 2 * GUARD_WAKE_HI + 2 - x;   // (-80 - X: at the wake, X + 2)
            const bool back = moving && s.ram8(AL_STATE) == 1 && !s.ram8(AL_FACING);
            const uint16_t rec = !moving ? GUARD_STAND
                                 : back ? figure_record(s, type, 2, false, (s.ram8(0xC692) + 5) % 6)
                                        : (uint16_t)(s.ram16(FIG_POSE) - 8);
            if (gx + GUARD_RIGHT > -2 * E) add_figure_pose(s, rec, gx, GUARD_Y);
        } else if (x < GUARD_WAKE_LO) {
            add_figure_pose(s, GUARD_STAND, x, GUARD_Y);
        }
    }
    // the guard of the page next to the camera's, standing at its place until the game puts it there: once its place
    // has been beyond the view's edge on that side since the game last had it (GUARD_ARMED: else, the camera coming
    // back past the page's edge, the game has just taken it away - there it would come out of nothing)
    const int sub = s.ram8(SUB_AREA), page = cam >> 8, last = s.ram16(CAM_MAX) >> 8;
    const int slide = view_slide(s, cam, E);
    uint32_t& st = s.module_state[MS_PLACEMENT];
    for (int side = 0; side < 2; side++) {
        const uint32_t bit = side ? GUARD_ARMED_RIGHT : GUARD_ARMED_LEFT;
        const int p = side ? page + 1 : page - 1;
        int t = 0;
        if (sub <= 3 && p >= 0 && p <= last) t = bank_rd8(s, 0x0C, (uint16_t)(bank_rd16(s, 0x0C, (uint16_t)(GUARD_PAGES + 2 * sub)) + p));
        if (t == 0 || t > 15 || t == type || bank_rd16(s, 0x0C, (uint16_t)(GUARD_HANDLERS + 2 * t)) != GUARD_PATROL) {
            st &= ~bit;
            continue;
        }
        const uint16_t d = bank_rd16(s, 0x0C, (uint16_t)(GUARD_TYPES + 2 * t));
        const int y = bank_rd8(s, 0x0C, (uint16_t)(d + 3)), x = bank_rd16(s, 0x0C, d) - cam;
        if (side ? x + GUARD_LEFT >= 256 + E + slide : x + GUARD_RIGHT <= -E + slide) st |= bit;
        if (!(st & bit) || y == 0xC0 || (x > -GUARD_RIGHT && x < 256 - GUARD_LEFT)) continue;   // (inside the original screen the game has it)
        add_figure_pose(s, figure_record(s, t, bank_rd8(s, 0x0C, (uint16_t)(d + 2)), true), x, y);
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

// Screen-fixed sprites: READY. A stage's setup ($3486: every stage but the Lamp, at its start and at TRY AGAIN, not in
// the demos) puts table slots 57-61 at Y $40 with the X / tile pairs at $3834 (x 108-147, the 256-px screen's middle)
// and leaves them until the objects' writers take the slots. It belongs to the screen, not the level: it neither
// slides with the view near a level's ends (it is shown at the start, where the view has slid by E) nor takes the
// page a piece left in those slots. Told by what the VDP's table holds, so nothing to keep.
constexpr int READY_SLOT = 57, READY_LEN = 5;
constexpr uint8_t READY_Y = 0x40;
constexpr uint8_t READY_XT[READY_LEN][2] = {{108, 0xF4}, {116, 0xF2}, {124, 0xF0}, {132, 0xF1}, {140, 0xF7}};
bool ready_sprite(const SmsVdp& v, int index, int x, int y, int pattern) {
    const int k = index - READY_SLOT;
    if (k < 0 || k >= READY_LEN || y != READY_Y || (pattern & 0xFF) != READY_XT[k][1]) return false;
    return x + ((v.reg[0] & 0x08) ? 8 : 0) == READY_XT[k][0];
}

// ---- story screens: the cut scenes, the intro and title, the ending (docs/re_sms_wide.md section 5a) ------------------
// Everything but the ten stage modes used to be pillarboxed. The story screens are widened where their picture allows:
// * Story scenes (mode $13, scene $C11F = 0..$23, script table $5F42): a 256x160 picture of its own, 16x10 metatiles
//   ($4E92: bank $1C $8000 holds a word per scene, metatile bank << 8 | map bank; the scene's map pointer at ($C48F) +
//   2 * scene in the map bank, rows 16 bytes apart; metatiles at ($C49D)), copied to table rows 0-19 with the hardware
//   scroll at 0 (a shake moves it by 2 px), characters as sprites and some drawn into the table (framed pictures, the
//   Genie, Jafar's snake), the text box on rows 20-23 (blank entry $11BF). The sides repeat part of the scene's own map
//   from ROM (kSceneRules: per band of rows a range of source columns for each side - the whole picture where its
//   edges meet, a stretch of sky, wall or floor elsewhere), so nothing the game draws into the table is repeated. The
//   scenes whose edges can't be carried on (a rock, rock walls, a roof's slope or a lit cave cut by them) stay 4:3.
// * The intro (the Genie with his framed pictures), the title and its menu (mode 0): table entry 0 at every edge
//   column, a plain background: the sides are entry 0.
// * The ending (mode $12): the stars are sprites (buffer slots 20-42, 1 px left every 4 frames, $7686: they wrap at 256
//   px), the sea's rows 16-21 repeat every 32 px (scrolled per line), the credits are written on rows 6-13. The sides
//   show the table's own wrap but blank ($0100) on the text rows 2-15, and every star again 256 px left and right.
// The characters of a scene and the ending's carpet come from the game's sprite writers ($23C2, $3C63, $50D4), which
// park a piece at X = 0 when its 16-bit X is off the 256 px. On a widened screen those pieces are drawn at their true X
// as extra sprites (and the parked entries hidden), so the game's sprite table - its RAM - stays as it is.
// A screen still fades out after the next mode is set ($0913): the kind of screen is kept for the fade (STORY_FADE).
enum StoryKind { STORY_NONE = 0, STORY_SCENE = 1, STORY_TITLE = 2, STORY_ENDING = 3, STORY_OTHER = 4 };
constexpr int M_ENDING = 0x12;
constexpr uint16_t SCENE_SELECT = 0xC120, SCENE_RUNNING = 0xCF2E;   // (developers' scene select; set by $601B)
constexpr int SCENES = 0x24, SCENE_ROWS = 20;
constexpr uint16_t SCENE_BLANK = 0x11BF, ENDING_BLANK = 0x0100;
constexpr uint16_t SCENE_MAPS = 0x8048;   // ($C48F) while a scene is shown: its map pointers' table ($4E92)
constexpr uint16_t SAT_Y = 0xC300;
// a band of a scene's cell rows [r0, r1): the left side repeats source columns [la, lb) (the one next to the picture's
// edge is lb - 1), the right side [ra, rb) (ra next to the edge); an empty range: the scene's blank background (tile
// $100, as its picture has it beyond what it draws); a rule without bands: the scene stays 4:3. col0: rows whose
// hidden column 0 (blanked on the hardware, shown in a widened scene) takes the left side's rule, not the table's cell.
// side0: rows whose left side begins at the hidden column (it shows lb - 1, column -1 lb - 2, ...: a stretch that ends
// in a piece of its own there, the floor's end in $A). sub0, sub1, sub: the sides show source columns [sub0, sub1) as
// the ones from sub on (a prop the picture has once, where the columns are otherwise alike: $1B's table).
struct SceneBand { uint8_t r0, r1, la, lb, ra, rb; };
struct SceneRule { uint8_t n; SceneBand band[3]; uint32_t col0, side0; uint8_t sub0, sub1, sub; };
constexpr uint16_t SCENE_VOID = 0x0100;
constexpr SceneBand WHOLE = {0, 20, 0, 32, 0, 32};   // the picture repeated: its edges meet
const SceneRule kSceneRules[SCENES] = {
    {1, {WHOLE}},   // 0 the market: Aladdin takes the bread
    {1, {WHOLE}},   // 1 the market: the guard and Jasmine (stalls meet across the edges)
    {1, {WHOLE}},   // 2 the rooftops: Aladdin and Jasmine run (the tower repeats)
    {1, {WHOLE}},   // 3 the rooftops: the pole vault
    // 4 the desert hills over Agrabah: the hidden column's two bottom cells are bare rock (never shown), under the
    // ridge that runs into it from the left side: the left side's (the ridge goes on to the hill at column 1; also
    // where the sides repeat column 0)
    {1, {WHOLE}, 0x3u << 18, 0},
    {1, {WHOLE}},   // 5 the palace wall: the guards take Aladdin
    {1, {WHOLE}},   // 6 the palace: Jafar and Jasmine (pillars and windows)
    {1, {WHOLE}},   // 7 the dungeon
    // 8 the Cave of Wonders at night: the tiger head's jaw and fangs cut by the right edge; drawn at the right
    // (scene_at_right: the wide edge cuts the head where the 4:3 edge did). Left side: the sky's stars (columns 0-24,
    // 200 px) and the dunes (rows 14-19, columns 0-11: their 6-column waves in phase); right (only a shake's 2 px):
    // the head's last column
    {2, {{0, 14, 0, 25, 31, 32}, {14, 20, 0, 12, 31, 32}}},
    {1, {WHOLE}},   // 9 the treasure room
    // $A the lamp's cave: the claw at the left edge isn't repeated; the rock floor (rows 16-19, cols 2-25: the ledge
    // and the slab, its rounded end at 25; small rocks on row 15) is cut at the claw's foot (col 1 dark, col 0 a black
    // block in the table), and ends at the right in the lava (cols 26-29) with a rock at the edge. Widened, the floor
    // runs on unbroken out of the left edge (kSceneFills: the gap under the claw's foot), the hidden column col 16 and
    // the side cols 15 down to 5, repeating every 12 (the slab's two- and four-column steps in phase; row 15's rocks
    // from cols 4-15, whole); the right side the lava
    {3, {{0, 15, 8, 16, 4, 32}, {15, 16, 4, 16, 26, 30}, {16, 20, 5, 17, 26, 30}}, 0, 0x1F << 15},
    {1, {WHOLE}},   // $B the escape: the lava's drips (its sides: kSceneSides)
    {1, {WHOLE}},   // $C, $D the fall: the shaft's walls carried on (kSceneSides)
    {1, {WHOLE}},
    {1, {WHOLE}},   // $E the Genie appears (hills at both edges)
    {1, {WHOLE}},   // $F the Genie in the oasis
    // $10 the throne room: pillars on the left (not the throne); the picture is drawn at the right (scene_at_right:
    // the door ends the room at the wide picture's right edge), so its right side is never shown
    {1, {{0, 20, 0, 9, 0, 28}}},
    {3, {{0, 6, 0, 13, 0, 13}, {6, 8, 0, 13, 24, 32}, {8, 20, 0, 13, 28, 32}}},   // $11 night: sky, the balcony carries on
    {1, {{0, 20, 0, 16, 0, 16}}},   // $12 the roof at night: sky and railing
    {3, {{0, 6, 0, 13, 0, 13}, {6, 8, 0, 13, 24, 32}, {8, 20, 0, 13, 28, 32}}},   // $13 = $11's map
    // $14 the garden at night: the roof's slope and the bushes cut by the left edge, the wall at the right: its sides
    // laid out (kSceneSides, rows 0-17); the floor's rows 18-19 repeat columns 0-1 / 30-31
    {1, {{0, 20, 0, 2, 30, 32}}},
    {2, {{0, 8, 0, 13, 0, 13}, {8, 20, 0, 13, 24, 32}}},   // $15 the cliff at night: sky, the cliff carries on
    {1, {WHOLE}},   // $16 the Genie's cave under the water: its sides laid out (kSceneSides: the left mirrored, the rock)
    {2, {{0, 8, 0, 13, 0, 13}, {8, 20, 0, 13, 24, 32}}},   // $17 = $15's map
    {1, {{0, 20, 0, 9, 0, 28}}},   // $18 = $10's map
    // $19 the palace hall: the post at the right edge (columns 28-31, in front: the priority bit) is drawn at the right
    // (scene_at_right); the left side carries the plain wall on without it - columns 12-27 (the framed panel at 1-10
    // isn't repeated: the wide edge would cut it to a stray strip of its frame at some widths), the frieze's 4-column
    // steps and the floor in phase (column 27 next to the edge)
    {1, {{0, 20, 12, 28, 28, 32}}},
    {1, {{0, 20, 0, 9, 0, 28}}},   // $1A = $10's map
    // $1B the Sultan's room: the lamp's table (rows 14-19, cols 10-17) in front of the wall isn't repeated (the right
    // side reached it at 21:9 square): the same columns of the next frame along the wall (22-29), frieze in step
    {1, {WHOLE}, 0, 0, 10, 18, 22},
    {2, {{0, 14, 0, 1, 0, 1}, {14, 20, 0, 1, 28, 32}}},   // $1C the balcony by day: sky, the balcony carries on
    {1, {WHOLE}},   // $1D the sky over the hills
    {1, {{0, 20, 0, 16, 0, 16}}},   // $1E Jafar's room: the throne at the right edge isn't repeated
    {1, {{0, 20, 0, 8, 24, 32}}},   // $1F Jafar's room: the hourglass
    {1, {WHOLE}},   // $20 Jafar's room: the snake (the floor)
    {1, {WHOLE}},   // $21 Jafar the genie
    {1, {WHOLE}},   // $22 Jafar's room, red
    {1, {WHOLE}},   // $23 the palace balcony: the end (towers)
};
// Cells inside the 4:3 picture a widened scene draws with a floor behind them (a gap the sides' floor runs across):
// scene, column, rows [r0, r1), source column. The picture's own cell stays in front: where its pattern has pixels
// (not colour 0, the background) they are its, elsewhere the source cell's show through - a cell of its own pattern
// made at the frame's start (StoryFills, SmsVdp::alt_patterns, bit 15), with the picture cell's priority, flips and
// palette; a blank picture cell (tile $100) is the source cell as it is. The VDP asks bg_entry for the columns left of
// hw_x0, which the frame's start moves past them (and the shake's 2 px) for such a scene. At 4:3 and widescreen off
// the table is drawn as it is.
// $A: the floor stops at the claw. Its slab (rows 18-19) begins flat at col 2, col 1 dark; the claw's base (cols 1-3,
// rows 16-17: its foot's outline over the dark, the ledge's tip in col 3) has the floor's top rows (16-17) only from
// col 4 on. The claw is the same in stage 4's first screen (the same tiles in the same columns: there the slab begins
// under it with a rounded end, 132b / 132c, nothing beyond). Widened, the floor runs on behind it: col 1's slab from col
// 17 (1105 / 1109, before the slab's 1106 / 110a at col 2), and rows 16-17 of cols 1-3 behind the claw from cols 7-9
// (143 143 144 / 128 128 125, before the 140 / 125 at col 4); every pixel of the claw is kept.
struct SceneFill { uint8_t scene, col, r0, r1, src; };
constexpr SceneFill kSceneFills[] = {
    {0x0A, 1, 18, 20, 17}, {0x0A, 1, 16, 18, 7}, {0x0A, 2, 16, 18, 8}, {0x0A, 3, 16, 18, 9},
};
// The fills' cells made for the frame: the picture cell's pattern over the source's (by the picture cell's pattern)
struct StoryFills {
    static constexpr int MAX = 16;
    uint8_t tiles[MAX * 32] = {};
    int first = 0, count = 0;
    static void pixels(const uint8_t* p, int e, uint8_t out[8][8]) {   // (screen orientation)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                const int ty = (e & 0x400) ? 7 - y : y, tx = (e & 0x200) ? 7 - x : x;
                const uint8_t* r = p + ty * 4;
                const int b = 7 - tx;
                out[y][x] = (uint8_t)(((r[0] >> b) & 1) | (((r[1] >> b) & 1) << 1) | (((r[2] >> b) & 1) << 2) |
                                      (((r[3] >> b) & 1) << 3));
            }
    }
};
int scene_cell(const Sms& s, int sc, int col, int row);
// a fill's cell: the picture's own over the source's; *composite: whether it is a made cell (pattern = the picture's)
int fill_cell(const Sms& s, const SceneFill& f, int row, bool* composite) {
    const int fg = scene_cell(s, f.scene, f.col, row), bg = scene_cell(s, f.scene, f.src, row);
    const uint8_t* p = &s.vdp.vram[(fg & 0x1FF) * 32];
    bool blank = true;
    for (int i = 0; i < 32; i++) blank = blank && !p[i];
    *composite = !blank && ((fg ^ bg) & 0x800) == 0;
    return blank ? bg : fg;
}
int scene_fill_x(int sc) {   // (the screen x up to which bg_entry is asked: 8 for none)
    int x = 8;
    for (const SceneFill& f : kSceneFills)
        if (f.scene == sc) x = std::max(x, (f.col + 1) * 8 + 2);
    return x;
}
// Sides laid out from a scene's own pieces where no stretch of its columns carries it on (kSceneSides: cell rows 0-11,
// 13 columns out from each edge, 0 = the blank background; rows 12-19 blank).
// $B (the escape): the lava's drips hang in a cluster at the picture's left (cols 0-11, rows 0-11), cut by its edge,
// and sparsely at the top of its right part. Repeated whole, the right side was the cluster again next to the empty
// right part and the left side the empty right part - the user's "save state two cut scene doesnt look right". The
// drips are arcs of two rows, six cells (116d 116e 116f 136f 136e 136d over 1171 1172 1173 1173 1372 1371) or four
// (1165-1168 over 1169 1124 1124 1124), with the priority bit as in the picture. Left: the arcs the edge cuts made
// whole (rows 3-4 a four-arc ending at col 0, 5-6 a six-arc ending at col 2, 9-10 a six-arc ending at col 0, 11 a
// four-arc's top ending at col 1), and the cluster thinning out with whole arcs further left; right: two arcs, at the
// top as the picture's right part has them.
// $C and $D (the fall that follows): a shaft between rock walls - lobes outlined in blue and red (1130 1131 over 1139
// 113a, flipped for the left wall), the rock's mass black (114c) beyond them. They stayed 4:3 before ("rock walls cut
// by both edges": no stretch of the picture's columns repeats past a wall); the sides carry the walls on with the
// walls' own art. Right: the mass beyond the right wall, black. Left (cells composed from the pictures' own): the
// shaft's left wall is the scene's right wall mirrored (its tiles flipped, as the game draws its left walls), in the
// order it has them, not a row of one repeated tile. $C: the picture's left wall begins at row 10 (an outcrop, its top
// a lobe at columns 1-3) with the shaft open above it to the edge; the mirrored right wall (rows 0-11, columns 19-31
// about column 19) stands two columns out, its ledge at rows 8-9 reaching over the outcrop's top, which the hidden
// column completes (1130 / 1139 with the picture's 1131 / 113a); the mass below row 12. $D: the mass on the left but
// for the picture's opening in the left wall (rows 10-15), whose back is the right wall's face there mirrored (rows 9-16,
// columns 22-31 about column 22), one to three columns out, joining the picture's wall at rows 9 and 16.
// hidden: the column the hardware blanks, where the table has the blank $11BF ($C rows 0-13: 0 = the table's own).
constexpr int SIDE_COLS = 13, SIDE_ROWS = 20;
// rows: the rows the tables are for (0: all; the others follow the scene's rule).
// $F (the oasis): its picture's edges meet but on rows 18-19, where a bush stands over the sand at the bottom right
// (cols 28-31: 010f 0121 0325 0126 over 0102 0109 0115 012c, cut by the edge) and the sand runs on at the left: with
// the whole picture repeated the bush ended in a straight edge over the sand at the right side's start, and the left
// side's copy of it at the 4:3 picture's left edge (the user's "margin tilesets not correct"). There the bush is
// finished with its own tiles mirrored, as the game mirrors its bushes: on the right its right half, on the left the
// whole bush ending at the picture's edge; the sand beyond.
struct SceneSides { uint8_t scene; uint16_t left[SIDE_ROWS][SIDE_COLS], right[SIDE_ROWS][SIDE_COLS]; uint16_t hidden[SIDE_ROWS]; uint32_t rows; };
const SceneSides kSceneSides[] = {
    {0x0F,
     {
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0x030f, 0x0321, 0x0125, 0x0326, 0x0126, 0x0325, 0x0121, 0x010f, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d},
         {0x0302, 0x0309, 0x0315, 0x032c, 0x012c, 0x0115, 0x0109, 0x0102, 0x0107, 0x0107, 0x0107, 0x0107, 0x0107},
     },
     {
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0x0326, 0x0125, 0x0321, 0x030f, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d, 0x010d},
         {0x032c, 0x0315, 0x0309, 0x0302, 0x0107, 0x0107, 0x0107, 0x0107, 0x0107, 0x0107, 0x0107, 0x0107, 0x0107},
     },
     {}, 0x3u << 18},
    {0x0C,
     {
         {0x0100, 0x0100, 0x1330, 0x1331, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x0100, 0x1339, 0x133a, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x0100, 0x1330, 0x1331, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x1332, 0x1333, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x1334, 0x1335, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x1336, 0x1337, 0x1338, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x0100, 0x1330, 0x1331, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x0100, 0x1339, 0x133a, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x133b, 0x133c, 0x133d, 0x133e, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x133f, 0x1340, 0x1341, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x1342, 0x1333, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x1334, 0x1335, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
     },
     {
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
     },
     {0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x1130, 0x1139, 0x114c, 0x114c, 0, 0, 0, 0, 0, 0}},
    {0x0D,
     {
         {0x1333, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x034b, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0345, 0x0346, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0347, 0x0348, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x0349, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x034a, 0x034b, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x0349, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x034a, 0x034b, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x0100, 0x1344, 0x1345, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
     },
     {
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c, 0x114c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
         {0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c, 0x014c},
     },
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {0x0B,
     {   // left: [row][k], k = 0 the column left of the picture's (-1), out to -13
         {0, 0, 0, 0, 0, 0, 0, 0x1371, 0x1372, 0x1173, 0x1173, 0x1172, 0x1171},
         {0, 0, 0x136d, 0x136e, 0x136f, 0x116f, 0x116e, 0x116d, 0, 0, 0, 0, 0},
         {0, 0, 0x1371, 0x1372, 0x1173, 0x1173, 0x1172, 0x1171, 0, 0, 0, 0, 0},
         {0x1167, 0x1166, 0x1165, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0x1124, 0x1124, 0x1169, 0, 0, 0x136d, 0x136e, 0x136f, 0x116f, 0x116e, 0x116d, 0, 0},
         {0x116f, 0x116e, 0x116d, 0, 0, 0x1371, 0x1372, 0x1173, 0x1173, 0x1172, 0x1171, 0, 0},
         {0x1173, 0x1172, 0x1171, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0x1168, 0x1167, 0x1166, 0x1165, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0x1124, 0x1124, 0x1124, 0x1169, 0, 0, 0},
         {0x136e, 0x136f, 0x116f, 0x116e, 0x116d, 0, 0, 0, 0, 0, 0, 0, 0},
         {0x1372, 0x1173, 0x1173, 0x1172, 0x1171, 0, 0, 0, 0, 0x1168, 0x1167, 0x1166, 0x1165},
         {0x1166, 0x1165, 0, 0, 0, 0, 0, 0, 0, 0x1124, 0x1124, 0x1124, 0x1169},
     },
     {   // right: [row][k], k = 0 the column right of the picture's (32), out to 44
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0x116d, 0x116e, 0x116f, 0x136f, 0x136e, 0x136d, 0, 0, 0, 0, 0},
         {0, 0, 0x1171, 0x1172, 0x1173, 0x1173, 0x1372, 0x1371, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0x1165, 0x1166, 0x1167, 0x1168, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0x1169, 0x1124, 0x1124, 0x1124, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
     },
     {}},
    // $14 (the garden at night): left, the roof's slope (133 134 over its fill 132, the sky 100 above) rising on two
    // columns a row, and the bushes (rows 10-16) mirrored about the picture's left edge - the game mirrors its bushes
    // (335 is 135 flipped): the cluster cut there is finished by its mirror image and the roof's fill goes on beyond;
    // row 17 the lawn's edge. Right: the wall (128). Rows 18-19 by the rule.
    {0x14,
     {
         {0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100},
         {0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0134, 0x0133},
         {0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0134, 0x0133, 0x0132, 0x0132},
         {0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0134, 0x0133, 0x0132, 0x0132, 0x0132, 0x0132},
         {0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0134, 0x0133, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132},
         {0x0100, 0x0100, 0x0100, 0x0134, 0x0133, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132},
         {0x0100, 0x0134, 0x0133, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132},
         {0x0133, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132},
         {0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132},
         {0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132, 0x0132},
         {0x0332, 0x0332, 0x0332, 0x0135, 0x0338, 0x0331, 0x0335, 0x0332, 0x0332, 0x0332, 0x0332, 0x0332, 0x0332},
         {0x0335, 0x0135, 0x0338, 0x0331, 0x0339, 0x0331, 0x0331, 0x0335, 0x0332, 0x0332, 0x0332, 0x0332, 0x0332},
         {0x0336, 0x033b, 0x0339, 0x0331, 0x0339, 0x0331, 0x0338, 0x0331, 0x0335, 0x0332, 0x0332, 0x0332, 0x0332},
         {0x0331, 0x0331, 0x0339, 0x0331, 0x033a, 0x0331, 0x0339, 0x0331, 0x0331, 0x0332, 0x0332, 0x0332, 0x0332},
         {0x0331, 0x0331, 0x033a, 0x0331, 0x0331, 0x033c, 0x0339, 0x0331, 0x0331, 0x0332, 0x0332, 0x0332, 0x0332},
         {0x0336, 0x033b, 0x0331, 0x033c, 0x0331, 0x013b, 0x0337, 0x0337, 0x0136, 0x0335, 0x0332, 0x0332, 0x0332},
         {0x0331, 0x0331, 0x0336, 0x033b, 0x0331, 0x0331, 0x0331, 0x0331, 0x013b, 0x0136, 0x0335, 0x0332, 0x0332},
         {0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d, 0x012d},
         {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
         {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
     },
     {
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128, 0x0128},
         {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
         {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
     },
     {}, 0x3FFFF},
    // $16 (the Genie's cave under the water): right, the rock's black mass (10a: the rock wall's face runs down to the
    // left inside the picture, its mass solid to the right edge); left, the picture's columns 0-12 mirrored about its
    // left edge - the light shafts, the weed and the rock lump cut there finished by their mirror image (the Genie and
    // the lamp are drawn into the table, never from ROM: not repeated).
    {0x16,
     {
         {0x0303, 0x0302, 0x0308, 0x0301, 0x0308, 0x0308, 0x0308, 0x0308, 0x0308, 0x0308, 0x0308, 0x0308, 0x0308},
         {0x0304, 0x0303, 0x0308, 0x0302, 0x0301, 0x0301, 0x0301, 0x0301, 0x0308, 0x0308, 0x0308, 0x0308, 0x0308},
         {0x0300, 0x0300, 0x0308, 0x0303, 0x0302, 0x0302, 0x0302, 0x0302, 0x0302, 0x0302, 0x0302, 0x0302, 0x0302},
         {0x0300, 0x0300, 0x0308, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0300, 0x0300, 0x0302, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0300, 0x0300, 0x0303, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0300, 0x0300, 0x031d, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x031d, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0305, 0x030b, 0x0300, 0x030b, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0305, 0x030b, 0x0300, 0x030c, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0306, 0x030d, 0x0300, 0x030d, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0307, 0x0306, 0x0305, 0x0305, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0304, 0x0307, 0x0306, 0x0306, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0309, 0x0304, 0x0307, 0x0307, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300, 0x0300},
         {0x0315, 0x0316, 0x0317, 0x031a, 0x011a, 0x0306, 0x0305, 0x0305, 0x0305, 0x0305, 0x0300, 0x0300, 0x0305},
         {0x0311, 0x0312, 0x031b, 0x0310, 0x0310, 0x0310, 0x0310, 0x0311, 0x0310, 0x0310, 0x031b, 0x0310, 0x0314},
         {0x030f, 0x030e, 0x030e, 0x030e, 0x030e, 0x030f, 0x030f, 0x030f, 0x030e, 0x030e, 0x030e, 0x030e, 0x030e},
     },
     {
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
         {0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a, 0x010a},
     },
     {}, 0},
};
const SceneSides* scene_sides(int sc) {
    for (const SceneSides& d : kSceneSides)
        if (d.scene == sc) return &d;
    return nullptr;
}
const SceneRule* scene_rule(int sc) { return sc >= 0 && sc < SCENES && kSceneRules[sc].n ? &kSceneRules[sc] : nullptr; }

// The screen shown, as far as widening it goes (not a stage: STORY_NONE)
int story_screen(const Sms& s) {
    if (shows_stage(s)) return STORY_NONE;
    if (const int k = story_fade(s)) return k;
    const int m = s.ram8(MODE);
    if (m == M_SCENE) return s.ram8(SCENE_SELECT) && !s.ram8(SCENE_RUNNING) ? STORY_OTHER : STORY_SCENE;
    if (m == 0) return STORY_TITLE;
    if (m == M_ENDING) return STORY_ENDING;
    return STORY_OTHER;
}
// ... and whether it is widened
int story_wide(const Sms& s) {
    if (s.vdp.ext == 0) return STORY_NONE;
    const int k = story_screen(s);
    if (k == STORY_SCENE) return scene_rule(s.ram8(SCENE)) ? k : STORY_NONE;
    return k == STORY_TITLE || k == STORY_ENDING ? k : STORY_NONE;
}

// A fade-out ($0913) begins with the next mode set: the screen it fades, by the caller (the return address). The
// stages' own (FADING) are told apart before this.
void story_fade_begin(Sms& m, uint16_t ret) {
    int k = STORY_OTHER;   // (password, game over, menus, the end of a demo: pillarboxed)
    if (ret == 0x66F7) k = STORY_SCENE;                       // $66D7: a scene's end (its stage, the ending)
    else if (ret == 0x4BD2 || ret == 0x4BF4 || ret == 0x1101) k = STORY_TITLE;   // the title: GAME START, PASSWORD, a
                                                              // demo; a pad code entered on it ($10DC)
    else if (ret == 0x75EC) k = STORY_ENDING;                 // the ending, back to the title
    else if (ret == 0x1059) k = STORY_NONE;                   // ($1055 sets the mode after the fade: the screen's own)
    m.module_state[MS_SHOWN] = (m.module_state[MS_SHOWN] & ~STORY_FADE_BITS) | ((uint32_t)k << STORY_FADE_SHIFT);
}

// A cell of scene `sc`'s picture from ROM (as $4E92 builds it): column 0..31, row 0..19
int scene_cell(const Sms& s, int sc, int col, int row) {
    const uint16_t w = bank_rd16(s, 0x1C, (uint16_t)(0x8000 + 2 * sc));
    const int map_bank = w & 0xFF, mt_bank = w >> 8;
    const uint16_t map = bank_rd16(s, map_bank, (uint16_t)(s.ram16(MAP) + 2 * sc));
    const uint8_t mt = bank_rd8(s, map_bank, (uint16_t)(map + (col >> 1) + (row >> 1) * 16));
    return bank_rd16(s, mt_bank, (uint16_t)(s.ram16(METATILES) + mt * 8 + (row & 1) * 4 + (col & 1) * 2)) & 0x1FFF;
}

// The rows of a scene's picture the scene has drawn over with another (Jafar's spell in the throne room, $10: the
// table goes dark but for the pillars' outlines in the spell's colours): most of a row's 32 cells differ from the
// picture's own. Their sides repeat the table's cells instead (bit per row 0-19; at the frame's start).
uint32_t scene_replaced_rows(const Sms& s) {
    if (story_wide(s) != STORY_SCENE || s.ram16(MAP) != SCENE_MAPS) return 0;
    const int sc = s.ram8(SCENE);
    uint32_t rows = 0;
    for (int row = 0; row < SCENE_ROWS; row++) {
        int differ = 0;
        for (int col = 0; col < 32; col++)
            differ += (s.vdp.name_entry(col, row) & 0x1FFF) != scene_cell(s, sc, col, row);
        if (differ > 24) rows |= 1u << row;
    }
    return rows;
}

// The background beyond the 256-px table on a story screen: the entry, or -1 for the table's own (wrapped)
// hs: the line's scroll. The scenes that shake horizontally ($5205: 8, $A, $C, $1C) move the whole picture 2 px with
// a scroll of 2 or 254; counted from 254 the sides' columns came out 32 columns off - the right side the table's own
// left part (in $A the claw and the floor again, in $1C sky over the balcony) on every shake to the left, a flash every
// 8 frames. There 254 is 2 px to the left. ($20's upper part, lines 0-143, slides 4..88 px left over its floor while
// Jafar's snake comes in from the right edge - the table's column 0 belongs at the right there: its sides as before.)
bool scene_shakes(int sc) { return sc == 0x08 || sc == 0x0A || sc == 0x0C || sc == 0x1C; }
// The throne room ($10, $18, $1A: one picture) ends at its right edge: the Sultan's throne stands against the room's
// door there (the pink and blue strip, column 31), and the scenes bring their characters in and out through it, from
// just beyond the 4:3 edge (Jasmine at 264 in $10, Aladdin at 268 in $1A, Jafar off at 259 in $18). Centred, the
// right side carried the room on past the door (pillars again) and the comings and goings at the door happened in the
// middle of a wide picture (the user's "the room is too centred ... the door should be on the far right so Jasmine
// walks in properly"). Widened, the whole picture (lines 0-159, its sprites with it) is drawn E px to the right: the
// door is the wide picture's right edge at every width, every character keeps its place against the others and the
// throne, and whoever the game brings in or takes out beyond the 4:3 edge comes and goes at the door, as in the
// original; the left side, 2E px, is the hall's pillars (the rule's columns 0-8). The text box (lines 160-191) stays
// centred. Those coming from the left walk the longer hall (kSceneWalks: $10 and $18's Aladdin).
// $12 (the roof at night: Aladdin and Jasmine on the carpet by the railing) likewise: its statue - a curled finial on
// the railing at the picture's right edge, columns 29-31, cut by it - stood cut in half beside the sky and railing the
// right side carried on (the user's "the statue is cut in half, maybe move it to far right"). Drawn at the right, the
// wide edge cuts the statue where the 4:3 edge did; the two sit as far from it as at 4:3 and nothing moves in or out;
// the left side, 2E px, is the sky and railing (columns 0-15, a post every 64 px).
bool scene_throne(int sc) { return sc == 0x10 || sc == 0x18 || sc == 0x1A; }
// Scene 8 (Aladdin and the old man - Jafar disguised - at the Cave of Wonders at night): the tiger head's jaw (columns
// 25-31, rows 11-19) and its fangs at the top (columns 28-31) are cut by the right edge, the dunes and the sky run on to
// the left; Aladdin walks into the jaw and off the right edge. Drawn at the right: the wide edge cuts the head where the
// 4:3 edge did and he walks out of the picture there; the left side is the sky and the dunes (kSceneRules).
// $19 (the palace hall: Jafar finds the lamp with Aladdin) likewise: the post at its right edge stood in the middle of
// the wide picture, and repeated in the left side, the room's panels beyond it on the right (the user's "the wall on
// the left shouldn't be there and the wall on the right should be far right").
bool scene_at_right(int sc) { return scene_throne(sc) || sc == 0x12 || sc == 0x08 || sc == 0x19; }
// how far a widened scene's picture is drawn to the right (px; 0: centred)
int story_shift(const Sms& s) {
    return story_wide(s) == STORY_SCENE && s.ram16(MAP) == SCENE_MAPS && scene_at_right(s.ram8(SCENE)) ? s.vdp.ext : 0;
}
constexpr int SCENE_LINES = 8 * SCENE_ROWS;   // (the picture's lines; the text box below)
int story_bg_entry(const Sms& s, int vcol, int vrow, uint32_t replaced, int hs) {
    if (hs >= 256 - 8 && story_wide(s) == STORY_SCENE && scene_shakes(s.ram8(SCENE))) vcol += 32;
    const SceneRule* rule = story_wide(s) == STORY_SCENE && s.ram16(MAP) == SCENE_MAPS && vrow % 28 < SCENE_ROWS
                                ? scene_rule(s.ram8(SCENE)) : nullptr;
    if (vcol > 0 && vcol < 32 && rule && !(replaced >> (vrow % 28) & 1))
        for (const SceneFill& f : kSceneFills)   // (inside the picture: a gap the sides' floor runs across)
            if (f.scene == s.ram8(SCENE) && f.col == vcol && vrow % 28 >= f.r0 && vrow % 28 < f.r1) {
                bool comp = false;
                const int e = fill_cell(s, f, vrow % 28, &comp);
                return comp && s.vdp.alt_patterns && (e & 0x1FF) >= s.vdp.alt_first &&
                               (e & 0x1FF) < s.vdp.alt_first + s.vdp.alt_count ? e | 0x8000 : e;
            }
    if (vcol > 0 && vcol < 32) return -1;   // (the table)
    const bool side0 = rule && (rule->side0 >> (vrow % 28) & 1);
    if (vcol == 0) {   // (the column the hardware blanks: the table's, or the left side's where the rule says)
        if (const SceneSides* d = rule ? scene_sides(s.ram8(SCENE)) : nullptr)
            if (d->hidden[vrow % 28]) return d->hidden[vrow % 28];
        if (!rule || !((rule->col0 | rule->side0) >> (vrow % 28) & 1)) return -1;
        vcol = -1;
    } else if (vcol < 0 && side0) {
        vcol--;   // (the side begins at the hidden column)
    }
    switch (story_wide(s)) {
    case STORY_TITLE:
        return 0;
    case STORY_ENDING:
        vrow %= 28;
        return vrow >= 2 && vrow < 16 ? ENDING_BLANK : -1;   // (no credits in the sides; stars are sprites)
    case STORY_SCENE: {
        vrow %= 28;
        if (vrow >= 24) return -1;
        if (vrow >= SCENE_ROWS) return SCENE_BLANK;   // the text box's rows
        // (until $4E92 builds a scene's picture ($C48F the scenes' map table) the pointers are another screen's - the
        // title's are 0, a stage's its map - and would make garbage of the sides: the plain entry 0, as on the title)
        if (s.ram16(MAP) != SCENE_MAPS) return 0;
        const int sc = s.ram8(SCENE);
        const SceneRule& r = *scene_rule(sc);
        if (const SceneSides* d = scene_sides(sc); d && (!d->rows || (d->rows >> vrow & 1))) {   // (laid out from the scene's pieces)
            const int k = vcol < 0 ? -1 - vcol : vcol - 32;
            const int e = vrow < SIDE_ROWS && k < SIDE_COLS ? (vcol < 0 ? d->left : d->right)[vrow][k] : 0;
            return e ? e : SCENE_VOID;
        }
        for (int i = 0; i < r.n; i++) {
            const SceneBand& b = r.band[i];
            if (vrow < b.r0 || vrow >= b.r1) continue;
            const int la = vcol < 0 ? b.la : b.ra, lb = vcol < 0 ? b.lb : b.rb;
            if (la == lb) return SCENE_VOID;
            int col = vcol < 0 ? lb - 1 - ((-1 - vcol) % (lb - la)) : la + ((vcol - 32) % (lb - la));
            // (the picture's column 0 repeated in a side: where the rule replaces the hidden column, its copies too -
            // the column the left side has next to the picture's edge)
            if (col == 0 && (r.col0 >> vrow & 1) && b.la != b.lb) col = b.lb - 1;
            // (the throne room's far left, drawn at the right: the hall's thin pillar, column 8, one cell with its
            // outline at its left, cut in half by the view's edge - 21:9 CRT, the edge at -148 - showed as a bare
            // lavender strip with its base's fragment; there the gap beside it, column 0: the wall and its band go on)
            if (vcol < 0 && col == 8 && scene_throne(sc)) {
                const int lx = -s.vdp.ext - story_shift(s);   // (the view's left edge in the picture)
                if ((lx & 7) && vcol == (lx >> 3)) col = 0;
            }
            if (col >= r.sub0 && col < r.sub1) col = r.sub + col - r.sub0;
            if (replaced >> vrow & 1) return s.vdp.name_entry(col, vrow) & 0x1FFF;
            return scene_cell(s, sc, col, vrow);
        }
        return SCENE_BLANK;
    }
    default:
        return -1;
    }
}

// ---- walks at the sides (docs/re_sms_wide.md section 5a, "Walks") ---------------------------------------------------
// The scenes move their characters by a clock of their own, -($CF79): $6100 counts it on every pass of the scene's
// logic, none while text is written (so a button pressed to hurry the text changes no tick of it). A character the
// script keeps waiting off the 256 px for its cue, or puts just beyond an edge to walk in, used to come in at the 4:3
// picture's edge; one the script removes once it has walked off vanished in the side. From the scene as the game
// plays it (tools/re/sms_scene_walks.py) each such walk has a tick t0 and the steps of its walk cycle (the pose - the
// piece list its writer draws - and the move per tick): before t0 the module draws the character walking in on that
// cycle played backwards from where the game has it at t0 (WALK_IN: the game's own frames and speed, ending where
// and when the game takes over); from t0 on it draws one removed going on with the cycle of the ticks before
// (WALK_OUT); WALK_SHOWN draws a character standing within reach of a side as the game keeps it there, t0 to t1.
// Aladdin's and the second figure's tiles are streamed from ROM to VRAM while the game shows a pose (17 from the
// pose's record, 8 bytes before its piece list: bank, -, address), so their poses are drawn from ROM; the list's
// characters use tiles the scene keeps in VRAM. Nothing of the game's changes: drawing only.
enum WalkKind : uint8_t { WALK_IN, WALK_OUT, WALK_SHOWN };
struct WalkStep { uint16_t ptr; int8_t dx, dy; };   // the pose at a tick, the move to the next (IN) / into it (OUT)
struct SceneWalk {
    uint8_t scene, ch;          // the character: 0 Aladdin ($2379), 1 the second figure ($3C13), 2 + k the list's ($5099)
    WalkKind kind;
    uint16_t t0, t1;            // IN: drawn before t0, counted shown from t0 to t1; OUT: drawn from t0 to t1; SHOWN
    int16_t x, y;               // IN: the character at t0; OUT: at t0 - 1, where the game last drew it
    uint8_t bank, n, base;      // its writer's pieces: bank, count, first pattern of the streamed tiles (0: VRAM's)
    uint8_t len;                // the cycle: IN from t0 on, OUT the ticks t0 - len .. t0 - 1
    const WalkStep* cycle;
    // IN: drawn from this tick only (NO_TS: from the fade-in); where the game's speed can't bring the character from
    // beyond the view's edge between ts and t0 (the throne room's hall, drawn at the right, is 2E wide) the walk is
    // played faster, its steps and its cycle alike, so it still comes from the edge and is the game's at t0
    int16_t ts = NO_TS;
    static constexpr int16_t NO_TS = INT16_MIN;
};
// (generated by tools/re/sms_scene_walks.py)
const WalkStep kWalk0[] = {
    {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8458, 1, 0},
    {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0},
    {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0},
    {0x848D, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0},
    {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0},
    {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0},
    {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0},
};
const WalkStep kWalk1[] = {
    {0x84C6, 2, 0}, {0x84C6, 2, 0}, {0x84C6, 2, 0}, {0x84C6, 2, 0}, {0x8501, 2, 0}, {0x8501, 2, 0}, {0x8501, 2, 0},
    {0x8501, 2, 0}, {0x83DA, 2, 0}, {0x83DA, 2, 0}, {0x83DA, 2, 0}, {0x83DA, 2, 0}, {0x8415, 2, 0}, {0x8415, 2, 0},
    {0x8415, 2, 0}, {0x8415, 2, 0}, {0x8450, 2, 0}, {0x8450, 2, 0}, {0x8450, 2, 0}, {0x8450, 2, 0}, {0x848B, 2, 0},
    {0x848B, 2, 0}, {0x848B, 2, 0}, {0x848B, 2, 0},
};
const WalkStep kWalk2[] = {
    {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0},
    {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x83F8, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0},
    {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8427, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0},
    {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x8458, 1, 0}, {0x848D, 1, 0},
    {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0}, {0x848D, 1, 0},
    {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0}, {0x84BC, 1, 0},
    {0x84BC, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0}, {0x84ED, 1, 0},
};
const WalkStep kWalk3[] = {
    {0xA759, 1, 0}, {0xA759, 1, 0}, {0xA720, 1, 0}, {0xA720, 1, 0}, {0xA720, 1, 0}, {0xA720, 1, 0}, {0xA720, 1, 0},
    {0xA733, 1, 0}, {0xA733, 1, 0}, {0xA733, 1, 0}, {0xA733, 1, 0}, {0xA733, 1, 0}, {0xA746, 1, 0}, {0xA746, 1, 0},
    {0xA746, 1, 0}, {0xA746, 1, 0}, {0xA746, 1, 0}, {0xA759, 1, 0}, {0xA759, 1, 0}, {0xA759, 1, 0},
};
const WalkStep kWalk4[] = {
    {0x8D50, 1, 0}, {0x8D50, 1, 0}, {0x8D50, 1, 0}, {0x8D50, 1, 0}, {0x8D81, 1, 0}, {0x8D81, 1, 0}, {0x8D81, 1, 0},
    {0x8D81, 1, 0}, {0x8D81, 1, 0}, {0x8D81, 1, 0}, {0x8D81, 1, 0}, {0x8D81, 1, 0}, {0x8DAC, 1, 0}, {0x8DAC, 1, 0},
    {0x8DAC, 1, 0}, {0x8DAC, 1, 0}, {0x8DAC, 1, 0}, {0x8DAC, 1, 0}, {0x8DAC, 1, 0}, {0x8DAC, 1, 0}, {0x8DDB, 1, 0},
    {0x8DDB, 1, 0}, {0x8DDB, 1, 0}, {0x8DDB, 1, 0}, {0x8DDB, 1, 0}, {0x8DDB, 1, 0}, {0x8DDB, 1, 0}, {0x8DDB, 1, 0},
    {0x8CF6, 1, 0}, {0x8CF6, 1, 0}, {0x8CF6, 1, 0}, {0x8CF6, 1, 0}, {0x8CF6, 1, 0}, {0x8CF6, 1, 0}, {0x8CF6, 1, 0},
    {0x8CF6, 1, 0}, {0x8D21, 1, 0}, {0x8D21, 1, 0}, {0x8D21, 1, 0}, {0x8D21, 1, 0}, {0x8D21, 1, 0}, {0x8D21, 1, 0},
    {0x8D21, 1, 0}, {0x8D21, 1, 0}, {0x8D50, 1, 0}, {0x8D50, 1, 0}, {0x8D50, 1, 0}, {0x8D50, 1, 0},
};
const WalkStep kWalk5[] = {
    {0xB193, 5, 2},
};
const WalkStep kWalk6[] = {
    {0x9C47, 4, 1},
};
const WalkStep kWalk7[] = {
    {0xB193, 4, 1},
};
const WalkStep kWalk8[] = {
    {0x8368, 1, 0}, {0x8368, 1, 0}, {0x8368, 1, 0}, {0x8368, 1, 0}, {0x8368, 1, 0}, {0x8368, 1, 0}, {0x839F, 1, 0},
    {0x839F, 1, 0}, {0x839F, 1, 0}, {0x839F, 1, 0}, {0x839F, 1, 0}, {0x839F, 1, 0}, {0x839F, 1, 0}, {0x839F, 1, 0},
    {0x83D0, 1, 0}, {0x83D0, 1, 0}, {0x83D0, 1, 0}, {0x83D0, 1, 0}, {0x83D0, 1, 0}, {0x83D0, 1, 0}, {0x83D0, 1, 0},
    {0x83D0, 1, 0}, {0x8403, 1, 0}, {0x8403, 1, 0}, {0x8403, 1, 0}, {0x8403, 1, 0}, {0x8403, 1, 0}, {0x8403, 1, 0},
    {0x8403, 1, 0}, {0x8403, 1, 0}, {0x8302, 1, 0}, {0x8302, 1, 0}, {0x8302, 1, 0}, {0x8302, 1, 0}, {0x8302, 1, 0},
    {0x8302, 1, 0}, {0x8302, 1, 0}, {0x8302, 1, 0}, {0x8335, 1, 0}, {0x8335, 1, 0}, {0x8335, 1, 0}, {0x8335, 1, 0},
    {0x8335, 1, 0}, {0x8335, 1, 0}, {0x8335, 1, 0}, {0x8335, 1, 0}, {0x8368, 1, 0}, {0x8368, 1, 0},
};
const WalkStep kWalk9[] = {
    {0x8EF1, -1, 0}, {0x8EF1, -1, 0}, {0x8E0C, -1, 0}, {0x8E0C, -1, 0}, {0x8E0C, -1, 0}, {0x8E0C, -1, 0},
    {0x8E0C, -1, 0}, {0x8E0C, -1, 0}, {0x8E0C, -1, 0}, {0x8E0C, -1, 0}, {0x8E37, -1, 0}, {0x8E37, -1, 0},
    {0x8E37, -1, 0}, {0x8E37, -1, 0}, {0x8E37, -1, 0}, {0x8E37, -1, 0}, {0x8E37, -1, 0}, {0x8E37, -1, 0},
    {0x8E66, -1, 0}, {0x8E66, -1, 0}, {0x8E66, -1, 0}, {0x8E66, -1, 0}, {0x8E66, -1, 0}, {0x8E66, -1, 0},
    {0x8E66, -1, 0}, {0x8E66, -1, 0}, {0x8E97, -1, 0}, {0x8E97, -1, 0}, {0x8E97, -1, 0}, {0x8E97, -1, 0},
    {0x8E97, -1, 0}, {0x8E97, -1, 0}, {0x8E97, -1, 0}, {0x8E97, -1, 0}, {0x8EC2, -1, 0}, {0x8EC2, -1, 0},
    {0x8EC2, -1, 0}, {0x8EC2, -1, 0}, {0x8EC2, -1, 0}, {0x8EC2, -1, 0}, {0x8EC2, -1, 0}, {0x8EC2, -1, 0},
    {0x8EF1, -1, 0}, {0x8EF1, -1, 0}, {0x8EF1, -1, 0}, {0x8EF1, -1, 0}, {0x8EF1, -1, 0}, {0x8EF1, -1, 0},
};
const WalkStep kWalk10[] = {
    {0x853A, -2, 0}, {0x853A, -2, 0}, {0x853A, -2, 0}, {0x853A, -2, 0}, {0x8575, -2, 0}, {0x8575, -2, 0},
    {0x8575, -2, 0}, {0x8575, -2, 0}, {0x85B0, -2, 0}, {0x85B0, -2, 0}, {0x85B0, -2, 0}, {0x85B0, -2, 0},
    {0x85EB, -2, 0}, {0x85EB, -2, 0}, {0x85EB, -2, 0}, {0x85EB, -2, 0}, {0x8626, -2, 0}, {0x8626, -2, 0},
    {0x8626, -2, 0}, {0x8626, -2, 0}, {0x865F, -2, 0}, {0x865F, -2, 0}, {0x865F, -2, 0}, {0x865F, -2, 0},
};
const WalkStep kWalk11[] = {
    {0x8572, 2, 0}, {0x8572, 2, 0}, {0x8572, 2, 0}, {0x8572, 2, 0}, {0x85A5, 2, 0}, {0x85A5, 2, 0}, {0x85A5, 2, 0},
    {0x85A5, 2, 0}, {0x85DC, 2, 0}, {0x85DC, 2, 0}, {0x85DC, 2, 0}, {0x85DC, 2, 0}, {0x8615, 2, 0}, {0x8615, 2, 0},
    {0x8615, 2, 0}, {0x8615, 2, 0}, {0x8648, 2, 0}, {0x8648, 2, 0}, {0x8648, 2, 0}, {0x8648, 2, 0}, {0x867F, 2, 0},
    {0x867F, 2, 0}, {0x867F, 2, 0}, {0x867F, 2, 0},
};
const WalkStep kWalk12[] = {
    {0x911D, -2, 0}, {0x911D, -2, 0}, {0x911D, -2, 0}, {0x911D, -2, 0}, {0x9038, -2, 0}, {0x9038, -2, 0},
    {0x9038, -2, 0}, {0x9038, -2, 0}, {0x9063, -2, 0}, {0x9063, -2, 0}, {0x9063, -2, 0}, {0x9063, -2, 0},
    {0x9092, -2, 0}, {0x9092, -2, 0}, {0x9092, -2, 0}, {0x9092, -2, 0}, {0x90C3, -2, 0}, {0x90C3, -2, 0},
    {0x90C3, -2, 0}, {0x90C3, -2, 0}, {0x90EE, -2, 0}, {0x90EE, -2, 0}, {0x90EE, -2, 0}, {0x90EE, -2, 0},
};
const SceneWalk kSceneWalks[] = {
    {0x00, 0, WALK_IN, 58, 90, -16, 137, 0x0E, 17, 1, 48, kWalk0},   // the market: Aladdin walks in to the bread stall
    {0x00, 1, WALK_IN, 494, 525, -46, 89, 0x0F, 17, 18, 24, kWalk1},   // the market: the guard runs in after him
    {0x01, 0, WALK_IN, 365, 389, -8, 137, 0x0E, 17, 1, 48, kWalk2},   // the market: Aladdin comes back for Jasmine
    {0x01, 3, WALK_OUT, 348, 421, 353, 121, 0x1D, 7, 0, 20, kWalk3},   // the market: Abu runs off
    {0x06, 1, WALK_IN, 66, 113, -31, 72, 0x0F, 17, 18, 48, kWalk4},   // the palace: Jasmine walks in
    {0x0B, 2, WALK_IN, 1, 14, -52, 74, 0x1D, 13, 0, 1, kWalk5},   // the escape: the carpet swoops in (from the fade-in on)
    {0x0C, 0, WALK_IN, 49, 73, -78, 59, 0x0E, 17, 1, 1, kWalk6},   // the fall: Aladdin flies in on the carpet
    {0x0C, 6, WALK_IN, 49, 153, -78, 59, 0x1D, 13, 0, 1, kWalk7},   // the fall: the carpet flies in under him
    {0x10, 0, WALK_IN, 18, 49, -15, 136, 0x0D, 17, 1, 48, kWalk8},   // the throne room: Aladdin walks in down the hall (waits at -16 walking on the spot)
    {0x11, 1, WALK_IN, 808, 839, 255, 40, 0x0F, 17, 18, 48, kWalk9},   // night: Jasmine walks in on the balcony
    {0x14, 1, WALK_IN, 298, 305, 238, 96, 0x0F, 17, 18, 24, kWalk10},   // the garden: a guard runs in from the right (put at 240)
    {0x18, 0, WALK_IN, 84, 97, -10, 136, 0x0D, 17, 1, 24, kWalk11, 18},   // the throne room: Aladdin runs in down the hall (waits at -16; from tick 18)
    {0x20, 1, WALK_IN, 114, 129, 254, 74, 0x0F, 17, 18, 24, kWalk12},   // Jafar's room: Abu runs in (his run off later)
    {0x23, 4, WALK_SHOWN, 0, 850, 0, 0, 0x00, 0, 0, 0, nullptr},   // the balcony: Iago on the tower
};

// The scene's clock (section "walks" above)
int scene_tick(const Sms& m) { return (uint16_t)(0 - m.ram16(0xCF79)); }

// Characters the game draws only as far as its 4:3 picture needs them, at its right edge. Rajah in $11 (the night
// balcony, list character 2, bank $1D, poses at $B92C: $B934 head raised, $B94D head down, $B964 roaring) is the
// tiger's head and shoulders in every pose, 28-32 px, his body cut flat at the right (no other art of him in the ROM):
// the game keeps him at x >= 232, lying beyond the screen (256), creeping in to 232 head down and roaring there with
// the screen's right edge across his shoulders (his cut at 264 never shown), creeping back and padding off beyond it.
// Widened, he showed in the side cut in half (the user's "the tiger is cut in half"); a back half made from his own
// shoulders mirrored didn't look right (the user's "can it not just appear closer to the screen edge"). He is drawn E
// px further right, every piece (on the 256 px or parked; the table's own entries hidden): the wide picture's right
// edge crosses him where the 4:3 screen's does, so he is shown exactly as much as at 4:3 and moves as there. Nothing
// else touches him (Aladdin on the carpet is 110 px away when he roars; Jasmine walks in after he has gone).
// Jafar in $14 (the garden at night, list character 2 from tick 419 - before, the carpet): he creeps in from -17 to
// x 0 at 1 px in 4 and stays there, his body at the 4:3 picture's left edge (x 0-7 blanked: he shows from x 8 on).
// Widened he stood whole in the left side, or popped up there as he first reached the 256 px. He is drawn E + 8 px
// further left: the wide picture's left edge crosses him where the 4:3 screen's visible edge (x 8) does.
// side: +1 drawn E px further right, -1 E + 8 px further left; from tick t0 to t1 (the character's list slot may
// hold another before).
struct SceneOut { uint8_t scene, ch; int8_t side; uint16_t t0, t1; };
constexpr SceneOut kSceneOuts[] = {
    {0x11, 2, +1, 0, 0xFFFF},     // night: Rajah at the balcony's right edge
    {0x14, 2, -1, 419, 0xFFFF},   // the garden: Jafar at its left edge
};
// how far character ch is drawn out at tick t (0: as the game has it)
int scene_out(int sc, int ch, int t, int E) {
    for (const SceneOut& o : kSceneOuts)
        if (o.scene == sc && o.ch == ch && t >= o.t0 && t <= o.t1) return o.side > 0 ? E : -(E + 8);
    return 0;
}

// The sprites of a widened story screen. The writers' pieces (hooks before their "off screen" test: hl = the piece's
// 16-bit X, bc = its character's) are kept per slot of the sprite table buffer with the slot's contents as uploaded:
// the scenes run their writers on some frames only (not while text is written), so a piece stays as it was while its
// slot does. A scene keeps characters waiting off the screen for their cue (a guard at x -48 for 550 frames, Aladdin
// at -16 for 1000): a character is drawn beyond the 256 px only once it has been on them (its X within 0..255, or one
// of its pieces shown there) since it was put where it is (a jump of more than 16 px) - one leaving walks on into the
// side, one waiting stays unseen - unless kSceneWalks has it walk in, walk on or stand there.
struct StorySprites {
    static constexpr int16_t NONE = INT16_MIN;
    static constexpr int CHARS = 9;   // Aladdin's writer ($2390), the second figure ($3C13), the scene's list ($5099, 7)
    int16_t x[64];          // the piece's 16-bit X as written, or NONE
    uint8_t who[64];        // its character
    uint8_t fresh[64];      // written since the last upload
    uint8_t snap[64][3];    // the slot's Y, X, pattern at the upload after it was written
    uint8_t valid[64];      // (and unchanged since)
    int16_t at[CHARS];      // each character's X when last drawn (NONE: not yet)
    uint8_t seen[CHARS];    // it has been on the 256 px since
    uint16_t shown = 0;     // the screen they belong to (kind << 8 | scene): another one starts them afresh
    int32_t tick = -1;      // the scene's clock at the writers' last pass on this screen (-1: none yet)
    int16_t fade = 0;       // uploads since the scene's picture is up with its clock at 0 (the fade-in: FADE_IN)
    // The clock stands at 0 through a scene's fade-in, FADE_IN uploads from its picture's being up to tick 1. A walk in
    // is placed by the ticks the fade-in has left (negative ticks): one whose cue comes soon after (the carpet in $B at
    // tick 1, Aladdin in scene 0 at 58) comes in from the wide edge while the scene fades in, rather than standing in a
    // side until the clock starts.
    static constexpr int FADE_IN = 26;
    StorySprites() { clear(); }
    void clear() {
        for (int i = 0; i < 64; i++) { x[i] = NONE; who[i] = fresh[i] = valid[i] = 0; snap[i][0] = snap[i][1] = snap[i][2] = 0; }
        forget();
        shown = 0;
    }
    void forget() {
        for (int c = 0; c < CHARS; c++) { at[c] = NONE; seen[c] = 0; }
        tick = -1;
        fade = 0;
    }
    // the screen the pieces belong to (kind << 8 | scene; kept through a screen's fade-out)
    static uint16_t screen_key(const Sms& m) { return (uint16_t)(story_screen(m) << 8 | m.ram8(SCENE)); }
    static bool hidden_y(uint8_t y) { return y >= 0xBF && y < 0xE0; }   // (below the picture: the writers' $C0 - 1)
    // the end of the column the hardware blanks (x 0..7 on the screen), as a table X (reg 0 bit 3 shifts sprites 8 left)
    static int blanked_end(const Sms& m) { return (m.vdp.reg[0] & 0x08) ? 16 : 8; }
    void piece(const Sms& m, int slot, int c) {
        if (slot < 0 || slot >= 64 || c < 0 || c >= CHARS) return;
        const int px = (int16_t)m.z80.hl;
        x[slot] = (int16_t)px;
        who[slot] = (uint8_t)c;
        fresh[slot] = 1;
        // (a new screen starts here, not only at the next upload: its clock is the pass's, run after the scene's init
        // has cleared it - a scene following another keeps the last one's number of ticks until then)
        const uint16_t now = screen_key(m);
        if (now != shown) { forget(); shown = now; }
        tick = scene_tick(m);
        const int cx = (int16_t)m.z80.bc;
        if (at[c] == NONE || std::abs(cx - at[c]) > 16) seen[c] = 0;
        // (the writer has put the piece's Y in the buffer already: a piece shown on the 256 px shows its character -
        // past the blanked column x 0..7, where the hardware shows nothing: scene 4's guard waits at x -36 for the
        // whole scene with two pieces at x 4, unseen in the original)
        if ((cx >= 0 && cx < 256) || (px >= blanked_end(m) && px < 256 && !hidden_y(m.ram8((uint16_t)(SAT_Y + slot)))))
            seen[c] = 1;
        at[c] = (int16_t)cx;
    }
    // the list's character whose X ($CF37 + 4k + 2) the writer at $50D4 adds ($CF33 points at it)
    static int list_char(const Sms& m) { return 2 + std::clamp((m.ram16(0xCF33) - 0xCF39) / 4, 0, 6); }
    // A walk's character as the module draws it at tick t: its X, Y and pose (false: not drawn)
    static bool walk_at(const SceneWalk& w, int t, int& gx, int& gy, uint16_t& ptr) {
        const int L = w.len;
        if (!L || t <= INT_MIN / 4) return false;   // (walk_tick: not yet)
        int sx = 0, sy = 0;
        if (w.kind == WALK_IN) {
            if (t >= w.t0) return false;
            const int k = w.t0 - t;   // (ticks before the game's own; the cycle played backwards from t0)
            for (int i = 0; i < L; i++) { sx += w.cycle[i].dx; sy += w.cycle[i].dy; }
            sx *= k / L;
            sy *= k / L;
            for (int j = 1; j <= k % L; j++) { sx += w.cycle[L - j].dx; sy += w.cycle[L - j].dy; }
            gx = w.x - sx;
            gy = w.y - sy;
            ptr = w.cycle[(L - k % L) % L].ptr;
            return true;
        }
        if (w.kind == WALK_OUT) {
            if (t < w.t0 || t > w.t1) return false;
            const int m = t - w.t0;   // (the cycle of the ticks before t0 going on)
            for (int i = 0; i < L; i++) { sx += w.cycle[i].dx; sy += w.cycle[i].dy; }
            sx *= (m + 1) / L;
            sy *= (m + 1) / L;
            for (int j = 0; j < (m + 1) % L; j++) { sx += w.cycle[j].dx; sy += w.cycle[j].dy; }
            gx = w.x + sx;
            gy = w.y + sy;
            ptr = w.cycle[m % L].ptr;
            return true;
        }
        return false;
    }
    // The tick a walk is drawn at, at tick t (SceneWalk::ts): before ts none (INT_MIN: walk_at draws nothing); from
    // ts, if the game's speed can't bring the character from beyond the view's edge (vs: the view's shift) by t0, the
    // walk played faster so that it does
    static int walk_tick(const SceneWalk& w, int t, int E, int vs) {
        if (w.kind != WALK_IN || w.ts == SceneWalk::NO_TS) return t;
        if (t < w.ts) return INT_MIN / 2;
        int sx = 0;
        for (int i = 0; i < w.len; i++) sx += w.cycle[i].dx;
        if (!sx || w.t0 <= w.ts) return t;
        const int edge = sx > 0 ? -E + vs - 24 : 256 + E + vs + 24;   // (a character's width beyond the edge)
        const int need = (std::abs(w.x - edge) * w.len + std::abs(sx) - 1) / std::abs(sx), avail = w.t0 - w.ts;
        return need > avail ? w.t0 - (w.t0 - t) * need / avail : t;
    }
    // At the VBlank's table upload ($02D9): which entries are parked pieces (hidden: module_state[0..3]), and the extra
    // sprites (true X) for those in the view; the walks of kSceneWalks; on the ending the stars again 256 px left and right.
    void upload(Sms& m) {
        SmsVdp& v = m.vdp;
        v.extra_count = 0;
        const int kind = story_wide(m);
        const uint16_t now = screen_key(m);
        if (now != shown) { forget(); shown = now; }
        for (int slot = 0; slot < 64; slot++) {   // (follow the slots on every screen: a widened one may come next)
            const uint8_t cur[3] = {m.ram8((uint16_t)(SAT_Y + slot)), m.ram8((uint16_t)(SAT_XT + 2 * slot)),
                                    m.ram8((uint16_t)(SAT_XT + 2 * slot + 1))};
            if (fresh[slot]) {
                std::memcpy(snap[slot], cur, 3);
                valid[slot] = 1;
                fresh[slot] = 0;
            } else if (valid[slot] && std::memcmp(snap[slot], cur, 3) != 0) {
                valid[slot] = 0;
            }
        }
        if (kind != STORY_SCENE && kind != STORY_ENDING) return;
        const bool rev = m.ram8(FRAMES) & 1;   // (the upload's order: reversed on odd frames)
        // The game reverses its table every other frame (the 8 sprites a line shared out by turns): where sprites
        // overlap they swap places every frame - Aladdin's feet and the carpet in $11 as he rises on it, 20 px
        // flickering, the same at 4:3 (the user's "parts of Aladdin flash"). Without the sprite limit nothing is
        // shared out by it: a widened scene's reversed table is drawn back in the buffer's order (the writers': Aladdin
        // first, in front), steady. With the limit the turns stay, as on the hardware.
        if (kind == STORY_SCENE && rev) m.module_state[MS_PLACEMENT] |= STORY_ORDER;
        v.sprite_rev = (m.module_state[MS_PLACEMENT] & STORY_ORDER) != 0;
        const int E = v.ext, shift = (v.reg[0] & 0x08) ? 8 : 0;
        const int vs = -story_shift(m);   // (the view in the picture's X: a scene drawn at the right shows -2E..255)
        auto add = [&](int sx, uint8_t y, uint8_t pat, const uint8_t* data) {
            if (hidden_y(y)) return;
            if (sx <= -8 - E + vs || sx >= 256 + E + vs || v.extra_count >= SmsVdp::kMaxExtra) return;
            v.extra[v.extra_count++] = {(int16_t)sx, y, pat, data};
        };
        // the walks: characters drawn here (shown) or by the module (walking, their own pieces hidden)
        uint8_t show[CHARS], walk[CHARS];
        std::memcpy(show, seen, CHARS);
        std::memset(walk, 0, CHARS);
        const SceneWalk* walks[CHARS] = {};
        int wtick[CHARS] = {};   // (the tick each walking character is drawn at: walk_tick)
        const int t = std::max(tick, 0);
        // Only on the scene's own picture: built ($4E92 has set ($C48F) to the scenes' maps, as the sides require) and
        // its init run (the writers' first pass on this screen, `$626E` calls them after `$6010` has cleared the clock;
        // the fade-in follows). Before that the screen is still another one's - the title waits 20 frames after GAME
        // START with the scene's mode already set (a state saved there by an earlier version has no fade kind to tell
        // it), a stage fades out, a scene that follows another has its number before its init - and nothing of the
        // scene's may be drawn over it. (Not the display bit: the VBlank's upload turns the display off around its VRAM
        // writes on some frames.)
        const bool scene_up = m.ram16(MAP) == SCENE_MAPS && tick >= 0;
        if (kind == STORY_SCENE && scene_up && tick == 0 && fade < 0x7FFF) fade++;
        // (the walks in: the clock still at 0, the ticks the fade-in has left - see FADE_IN)
        const int tin = tick == 0 ? std::min(0, fade - FADE_IN) : t;
        if (kind == STORY_SCENE && scene_up && !(v.reg[1] & 0x02)) {   // (8x8 sprites, as all scenes have them)
            for (const SceneWalk& w : kSceneWalks) {
                if (w.scene != m.ram8(SCENE) || w.ch >= CHARS) continue;
                int gx, gy;
                uint16_t ptr;
                const int tw = walk_tick(w, w.kind == WALK_IN ? tin : t, E, vs);
                if (w.kind == WALK_SHOWN ? t >= w.t0 && t <= w.t1 : w.kind == WALK_IN && t >= w.t0 && t <= w.t1)
                    show[w.ch] = 1;
                else if (walk_at(w, tw, gx, gy, ptr) && gx > -E - 80 + vs && gx < 256 + E + 80 + vs) {   // (in reach of a side)
                    walk[w.ch] = 1;
                    walks[w.ch] = &w;
                    wtick[w.ch] = tw;
                }
            }
        }
        for (int w = 0; w < 4; w++) m.module_state[w] = 0;
        const int blanked = blanked_end(m);
        const int sc = m.ram8(SCENE);
        for (int i = 0; i < 64; i++) {
            const int slot = rev ? 63 - i : i;
            if (!valid[slot] || x[slot] == NONE) continue;
            if (const int out = kind == STORY_SCENE && scene_up ? scene_out(sc, who[slot], t, E) : 0) {   // (kSceneOuts)
                m.module_state[i >> 4] |= (uint32_t)PAGE_HIDDEN << ((i & 15) * 2);
                add(x[slot] + out - shift, snap[slot][0], snap[slot][2], nullptr);   // (every piece: cut by the view)
                continue;
            }
            if (x[slot] >= 0 && x[slot] < 256) {
                // (a piece of a character not shown, under the column the hardware blanks: hidden, as there - the
                // widened scene shows that column, and scene 4's waiting guard showed his turban in it)
                if (x[slot] < blanked && !show[who[slot]] && (kind != STORY_SCENE || scene_up))
                    m.module_state[i >> 4] |= (uint32_t)PAGE_HIDDEN << ((i & 15) * 2);
                continue;
            }
            m.module_state[i >> 4] |= (uint32_t)PAGE_HIDDEN << ((i & 15) * 2);
            if (show[who[slot]] && !walk[who[slot]] && (kind != STORY_SCENE || scene_up))
                add(x[slot] - shift, snap[slot][0], snap[slot][2], nullptr);
        }
        // a walking character: its writer's pieces for the pose, as the writer places them ($2390 / $3C31 / $5099)
        for (int c = 0; c < CHARS; c++) {
            if (!walk[c]) continue;
            const SceneWalk& w = *walks[c];
            int gx, gy;
            uint16_t ptr;
            walk_at(w, wtick[c], gx, gy, ptr);
            const uint16_t rec = (uint16_t)(ptr - 8);   // (the pose's record: tile bank, -, tile address)
            const uint32_t tiles = (uint32_t)bank_rd8(m, w.bank, rec) * 0x4000 + (bank_rd16(m, w.bank, (uint16_t)(rec + 2)) & 0x3FFF);
            for (int i = 0; i < w.n; i++) {
                int a = bank_rd8(m, w.bank, (uint16_t)(ptr + i));
                if (a == 0xC0) continue;
                a = (a + gy) & 0xFF;
                if (a < 0xF8 && a >= 0xC0) a = 0xC0;
                const int px = gx + (int8_t)bank_rd8(m, w.bank, (uint16_t)(ptr + w.n + 2 * i));
                const uint8_t pat = bank_rd8(m, w.bank, (uint16_t)(ptr + w.n + 2 * i + 1));
                const uint8_t* data = nullptr;
                if (w.base) {
                    if (pat < w.base || pat >= w.base + 17) continue;
                    data = m.rom_orig_ptr(tiles + (uint32_t)(pat - w.base) * 32, 32);
                    if (!data) continue;
                }
                add(px - shift, (uint8_t)(a - 1), pat, data);
            }
        }
        if (kind == STORY_ENDING) {
            for (int slot = 20; slot <= 42; slot++) {
                const uint8_t y = m.ram8((uint16_t)(SAT_Y + slot));
                if (y == 0xD0) break;
                const int sx = m.ram8((uint16_t)(SAT_XT + 2 * slot)) - shift;
                const uint8_t pat = m.ram8((uint16_t)(SAT_XT + 2 * slot + 1));
                add(sx - 256, y, pat, nullptr);
                add(sx + 256, y, pat, nullptr);
            }
        }
    }
    // save states: the slots' and characters' records, the clock and the fade-in's count (version 3; version 2 has no
    // count, version 1 no clock)
    static constexpr uint8_t TAG = 'S', VERSION = 3;
    static constexpr size_t SIZE1 = 2 + 64 * 8 + CHARS * 3 + 2, SIZE2 = SIZE1 + 4, SIZE = SIZE2 + 2;
    void save(std::vector<uint8_t>& d) const {
        d.push_back(TAG);
        d.push_back(VERSION);
        for (int i = 0; i < 64; i++)
            d.insert(d.end(), {(uint8_t)x[i], (uint8_t)((uint16_t)x[i] >> 8), who[i], fresh[i], valid[i], snap[i][0], snap[i][1], snap[i][2]});
        for (int c = 0; c < CHARS; c++) d.insert(d.end(), {(uint8_t)at[c], (uint8_t)((uint16_t)at[c] >> 8), seen[c]});
        d.insert(d.end(), {(uint8_t)shown, (uint8_t)(shown >> 8)});
        const uint32_t tk = (uint32_t)tick;
        d.insert(d.end(), {(uint8_t)tk, (uint8_t)(tk >> 8), (uint8_t)(tk >> 16), (uint8_t)(tk >> 24)});
        d.insert(d.end(), {(uint8_t)fade, (uint8_t)((uint16_t)fade >> 8)});
    }
    // -> the bytes read (0: none)
    size_t load(const uint8_t* d, size_t n) {
        clear();
        if (n < SIZE1 || d[0] != TAG || d[1] < 1 || d[1] > VERSION || (d[1] == 2 && n < SIZE2) || (d[1] == 3 && n < SIZE))
            return 0;
        for (int i = 0; i < 64; i++) {
            const uint8_t* e = d + 2 + 8 * i;
            x[i] = (int16_t)(e[0] | (e[1] << 8));
            who[i] = (uint8_t)std::min<int>(e[2], CHARS - 1);
            fresh[i] = e[3];
            valid[i] = e[4];
            std::memcpy(snap[i], e + 5, 3);
        }
        for (int c = 0; c < CHARS; c++) {
            const uint8_t* e = d + 2 + 64 * 8 + 3 * c;
            at[c] = (int16_t)(e[0] | (e[1] << 8));
            seen[c] = e[2];
        }
        shown = (uint16_t)(d[SIZE1 - 2] | (d[SIZE1 - 1] << 8));
        if (d[1] == 1) return SIZE1;
        tick = (int32_t)(d[SIZE1] | (d[SIZE1 + 1] << 8) | (d[SIZE1 + 2] << 16) | ((uint32_t)d[SIZE1 + 3] << 24));
        if (d[1] == 2) { fade = tick == 0 ? FADE_IN : 0; return SIZE2; }   // (a version-2 state: as if the fade-in were done)
        fade = (int16_t)(d[SIZE2] | (d[SIZE2 + 1] << 8));
        return SIZE;
    }
};

// A sprite of a widened story screen: the parked pieces are hidden (their extra sprites stand in)
int story_sprite_x(const Sms& s, int index, int x) {
    const int k = story_wide(s);
    if (k != STORY_SCENE && k != STORY_ENDING) return x;
    return ((s.module_state[index >> 4] >> ((index & 15) * 2)) & 3) == PAGE_HIDDEN ? -4096 : x;
}

// A line of a story screen after its layers are drawn: the blanked left column. Widened scenes and the ending show the
// table's column there (part of the picture) with its sprites (the parked ones are hidden); the title shows the plain
// background without sprites; screens that stay 4:3 the backdrop colour, as the hardware.
void story_line_hook(const Sms& s, uint8_t* bg, uint8_t* spr) {
    const SmsVdp& v = s.vdp;
    if (!(v.reg[0] & 0x20)) return;
    const int E = v.ext, k = story_wide(s);
    if (k == STORY_SCENE || k == STORY_ENDING) return;
    for (int i = E; i < E + 8; i++) {
        if (k != STORY_TITLE) bg[i] = (uint8_t)(0x10 | (v.reg[7] & 0x0F));
        spr[i] = 0;
    }
}

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
        v.sprite_rev = false;
        v.line_shift = wide ? line_shift : nullptr;
        v.line_scroll = line_scroll;   // (every width, 4:3 too: a visual fix to the original, see line_scroll)
        std::memset(line_early_, -1, sizeof(line_early_));
        v.alt_patterns = nullptr;   // (bg_entry points them at stage 7's copies when it uses them)
        v.alt_cram = nullptr;
        v.bg_alt = nullptr;         // (apply_view: stages 0 and 1's camera-animated tiles)
        std::memset(v.bg_shift, 0, sizeof(v.bg_shift));
        // (a width change within a stage keeps what the hooks recorded for the frames under way)
        if (!wide || !wide_ || !in_stage(s)) {
            v.extra_count = 0;
            std::memset(page_, 0, sizeof(page_));
        }
        if (!wide || !wide_) story_.clear();
        story_shift_ = 0;
        wide_ = wide;
        if (!wide) {
            s.module_state[MS_PLACEMENT] &= ~(SLOT_START | SLOT_PARITY | SLOT_OPEN | SLOT_FREE);
            set_behind(s, 0);
            s.module_state[MS_SHOWN] &= ~(FADING | STORY_FADE_BITS);
            return;
        }
        // (until the next VBlank: the camera of the frame just drawn)
        s.module_state[MS_CAM_NEXT] = s.module_state[MS_CAM] = (uint32_t)(s.ram16(CAM_X) | (s.ram8(FRAMES) << 16));
        apply_view(s);
        // the camera the VBlank shows next (the scroll registers are written from it right after line $C1) and the
        // one the frame being drawn uses
        s.on_vblank = [](Sms& m) { m.module_state[MS_CAM_NEXT] = m.ram16(CAM_X) | ((uint32_t)m.ram8(FRAMES) << 16); };
        s.on_frame_start = [this](Sms& m) {
            apply_view(m);
            m.vdp.sprite_rev = (m.module_state[MS_PLACEMENT] & STORY_ORDER) != 0;   // (the table uploaded last)
            story_rows_ = shows_stage(m) ? 0 : scene_replaced_rows(m);   // (a scene's rows drawn over: section 5a)
            // (a widened scene with cells filled inside its picture: the VDP asks bg_entry for them, kSceneFills)
            // (a shaking scene: the table's column 0 shows at x 8-9 on a shake to the right - the column the module draws
            // in its place, as the sides, takes those 2 px too: $C's hidden column, the blank $11BF there, showed as a
            // black line in the shaft)
            m.vdp.hw_x0 = story_wide(m) == STORY_SCENE
                              ? std::max(scene_fill_x(m.ram8(SCENE)), scene_shakes(m.ram8(SCENE)) ? 10 : 8) : 8;
            make_fills(m);
        };
        // sprite pieces: the player ($23C2), the large second figure ($3C63) and the objects ($0C99), each with
        // hl = the piece's 16-bit screen X just before the "off screen" test (`ld a,h`)
        s.add_pc_hook(0x23C2, [this](Sms& m) {
            m.z80.hl = (uint16_t)(m.z80.hl - behind(m));   // (left behind by the scroll: see push_left)
            piece_x(m, (m.z80.de - SAT_XT) >> 1);
            if (!in_stage(m)) story_.piece(m, (m.z80.de - SAT_XT) >> 1, 0);   // (story screens: StorySprites)
        });
        s.add_pc_hook(0x3C63, [this](Sms& m) {
            piece_x(m, (m.z80.de - SAT_XT) >> 1);
            if (!in_stage(m)) story_.piece(m, (m.z80.de - SAT_XT) >> 1, 1);
        });
        // (the story scenes' other characters, $5099: the table pointer on the stack)
        s.add_pc_hook(0x50D4, [this](Sms& m) {
            story_.piece(m, ((m.peek(m.z80.sp) | (m.peek((uint16_t)(m.z80.sp + 1)) << 8)) - SAT_XT) >> 1, StorySprites::list_char(m));
        });
        s.add_pc_hook(0x0C99, [this](Sms& m) {
            // stack: the piece's table pointer, two counters, the object ($0C45 pushed ix)
            auto word = [&](int o) { return (uint16_t)(m.peek((uint16_t)(m.z80.sp + o)) | (m.peek((uint16_t)(m.z80.sp + o + 1)) << 8)); };
            const uint16_t obj = word(6);
            // An object parked at the window's left end (x = -64, not moving with the camera: Jafar's room keeps two
            // there) is never seen in the original: its pieces stay hidden (only there: elsewhere it is one passing by)
            const bool parked = m.ram8(STAGE) == ARENA_STAGE && m.ram16(obj + 2) == 0xFFC0 && !m.ram8((uint16_t)(obj + 0x20));
            piece_x(m, (word(0) - SAT_XT) >> 1, parked);
        });
        // an object the slot allocator has no room for (the caves of wide_patches): drawn as extra sprites
        auto undrawn = [](Sms& m) {
            const int i = (m.z80.ix - OBJECTS) / OBJ_SIZE;
            if (i >= 0 && i < MAX_OBJECTS) m.module_state[MS_UNDRAWN] |= 1u << i;
        };
        // the objects' margins around the view of the frame (the logic's camera: the one the next frame shows)
        auto slide_now = [](const Sms& m) { return view_slide(m, m.ram16(CAM_X), m.vdp.ext); };
        s.add_pc_hook(CAVE_WAKE_DECIDE, [](Sms& m) { wake_decide(m); m.z80.wait_cycles -= CAVE_WAKE_COST; });
        s.add_pc_hook(0xB1FC, [](Sms& m) { bird_catch_up(m, -16, 0x3E); });   // (types $46 / $3B, bank $12)
        s.add_pc_hook(0xB374, [](Sms& m) { bird_catch_up(m, -32, 0x2F); });
        // (one that shows nowhere - flown off the top, fallen off the bottom - leaves where the original lets it go,
        // freeing its slot for the next of the list as there)
        s.add_pc_hook(CAVE_GONE_DE, [slide_now](Sms& m) {
            m.z80.de = (uint16_t)(off_screen_vertically(m, m.z80.ix) ? 32 : 32 + m.vdp.ext - slide_now(m));
            m.z80.wait_cycles -= CAVE_GONE_COST;
        });
        s.add_pc_hook(CAVE_KEEP_A_DE, [slide_now](Sms& m) {
            m.z80.de = (uint16_t)(64 + m.vdp.ext - slide_now(m));
            m.z80.wait_cycles -= CAVE_KEEP_COST;
        });
        s.add_pc_hook(CAVE_KEEP_B_END, [](Sms& m) { m.z80.wait_cycles -= CAVE_KEEP_COST; });
        s.add_pc_hook(CAVE_BOULDER_DE, [slide_now](Sms& m) { m.z80.de = (uint16_t)(64 + m.vdp.ext - slide_now(m)); });
        s.add_pc_hook(CAVE_ROLL_HOME_DE, [slide_now](Sms& m) { m.z80.de = (uint16_t)(64 + m.vdp.ext - slide_now(m)); });
        s.add_pc_hook(CAVE_ROLL_HOME_DE2, [slide_now](Sms& m) { m.z80.de = (uint16_t)(320 + m.vdp.ext - slide_now(m)); });
        // the fixed hazards' awake test as the original's keep window has it (wide_patches, CAVE_AWAKE43)
        s.add_pc_hook(CAVE_AWAKE43_RET, [](Sms& m) { awake43(m); m.z80.wait_cycles -= CAVE_AWAKE43_COST; });
        s.add_pc_hook(CAVE_FULL_A_WIDE, undrawn);
        s.add_pc_hook(CAVE_FULL_B_WIDE, undrawn);
        // the objects' loops, a pass per object (queue stages $3D15..$3D39, the others $3CBC..$3CE4): CPU time the
        // original would not have spent is given back (slot_begin)
        s.add_pc_hook(0x3D15, [](Sms& m) { slot_begin(m, true); });
        s.add_pc_hook(0x3D39, [](Sms& m) { slot_end(m); });
        s.add_pc_hook(0x3CBC, [](Sms& m) { slot_begin(m, false); });
        s.add_pc_hook(0x3CE4, [](Sms& m) { slot_end(m); });
        // a stage fading out after its end (the next mode set: a story scene, the next stage, TRY AGAIN): still the
        // stage's picture, wide, until the fade ($0913) is over; any other screen likewise its own (story_fade_begin)
        s.add_pc_hook(0x0913, [](Sms& m) {
            static const uint16_t ends[] = {0x1523, 0x167B, 0x35D7},   // (the stage's exits: the stage number is up)
                lost[] = {0x1503, 0x1652, 0x1C44, 0x20DC, 0x34F7, 0x36A9, 0x37BF, 0x394B, 0x2794};   // (TRY AGAIN, Jafar)
            const uint16_t ret = (uint16_t)(m.peek(m.z80.sp) | (m.peek((uint16_t)(m.z80.sp + 1)) << 8));
            int st = -1;
            for (uint16_t a : ends) if (ret == a) st = m.ram8(STAGE) - 1;
            for (uint16_t a : lost) if (ret == a) st = m.ram8(STAGE);
            // (the lamp's chamber into its scene, $351E: the stage number is up; a demo's end there goes to the title)
            if (ret == 0x3536 && m.ram8(MODE) == M_SCENE) st = m.ram8(STAGE) - 1;
            if (st < 0 || st > 9 || demo_playing(m)) {
                story_fade_begin(m, ret);
                return;
            }
            m.module_state[MS_SHOWN] = (m.module_state[MS_SHOWN] & ~SHOWN_STAGE_BITS) | FADING | ((uint32_t)st << SHOWN_STAGE_SHIFT);
        });
        s.add_pc_hook(0x092D, [](Sms& m) { m.module_state[MS_SHOWN] &= ~(FADING | STORY_FADE_BITS); });
        // Two screens set the next mode and wait before their fade: the title's GAME START / PASSWORD ($4BC9: mode $13
        // or $F, then `ld b,$14; call $0B4D`, 20 frames) and a password taken ($7494: the stage's mode, 30 frames).
        // They stay themselves from the mode's write (before, the title was taken for scene 0, its sides drawn from
        // the title's null map pointers, and the password screen for the stage, its sides from the stage's map).
        s.add_pc_hook(0x4BCA, [](Sms& m) { story_fade_begin(m, 0x4BD2); });
        s.add_pc_hook(0x7497, [](Sms& m) { story_fade_begin(m, 0x74A4); });
        // left behind by the scroll: lost at the view's left edge
        s.add_pc_hook(0x34A5, [](Sms& m) { push_left_frame(m); });
        s.add_pc_hook(0x34B4, [](Sms& m) { push_left(m); });
        s.add_pc_hook(0x36B8, [](Sms& m) { push_left_release(m); });
        // (the auto-scrolling stages' handler: a stage (re)starting, TRY AGAIN too. $C912 doesn't tell: the game sets it
        // around its VRAM writes in the middle of most frames, not only while a stage loads)
        s.add_pc_hook(0x33B7, [](Sms& m) { set_behind(m, 0); });
        // the VBlank's sprite table upload ($02D9): in reverse order on odd frames (the game's flicker)
        s.add_pc_hook(0x02D9, [this](Sms& m) {
            const bool rev = m.ram8(FRAMES) & 1;
            for (int w = 0; w < 4; w++) m.module_state[w] = 0;
            for (int i = 0; i < 64; i++)
                m.module_state[i >> 4] |= (uint32_t)page_[rev ? 63 - i : i] << ((i & 15) * 2);
            m.module_state[MS_PLACEMENT] &= ~STORY_ORDER;   // (set again by a widened scene's upload)
            if (shows_stage(m)) {
                extra_sprites(m);
                palace_guards(m, ((m.ram8(FRAMES) - moved_at_) & 0xFF) <= GUARD_STILL);
            } else story_.upload(m);   // (a story screen: its own extra sprites and hidden entries)
        });
    }

    void post_frame(Sms& s, const GameConfig& cfg, FrameInfo& info) override {
        const int E = cfg.ext;
        info.gameplay = shows_stage(s);
        if (!info.gameplay) {
            info.gameplay = story_wide(s) != STORY_NONE;   // (a widened story screen: no masks)
            return;
        }
        if (E == 0) return;
        // What lies outside the level: only where it is narrower than the view (view_slide centres it), and the sides
        // of Jafar's room (shown dimmed)
        const int cam = shown_cam(s), view = cam - E + view_slide(s, cam, E);
        int lo, hi;
        level_bounds(s, lo, hi);
        const int left = std::clamp(lo - view, 0, E), right = std::clamp(view + 256 + 2 * E - (hi + 256), 0, E);
        if (shown_stage(s) == ARENA_STAGE) {
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
    static constexpr uint32_t FLIPPED = 0xFE000000u;   // (an earlier version's mark of a flipped tile: none now)
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
        story_.save(d);   // (after the stage's part: the story screens' sprite records)
        // the extra sprites' own pattern data (a story scene's walk: tiles from ROM), as ROM offsets
        const uint8_t* rom0 = sms_->rom_orig_ptr(0, 1);
        d.push_back('D');
        for (int i = 0; i < v.extra_count; i++) {
            const uint32_t o = v.extra[i].data && rom0 ? (uint32_t)(v.extra[i].data - rom0) : 0xFFFFFFFFu;
            d.insert(d.end(), {(uint8_t)o, (uint8_t)(o >> 8), (uint8_t)(o >> 16), (uint8_t)(o >> 24)});
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
            story_.clear();
            return;
        }
        std::memcpy(page_, d + 1, 64);
        moved_at_ = d[65];
        v.extra_count = d[66];
        for (int i = 0; i < v.extra_count; i++) {
            const uint8_t* e = d + 67 + 4 * i;
            v.extra[i] = {(int16_t)(e[0] | (e[1] << 8)), e[2], e[3]};
        }
        const size_t at = 67u + 4u * d[66];
        const size_t used = story_.load(d + at, n - at);   // (none in older states: cleared)
        const size_t da = at + used;
        if (used && n >= da + 1 + 4u * v.extra_count && d[da] == 'D') {
            for (int i = 0; i < v.extra_count; i++) {
                const uint8_t* e = d + da + 1 + 4 * i;
                const uint32_t o = e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t)e[3] << 24);
                v.extra[i].data = o == 0xFFFFFFFFu ? nullptr
                                  : (o & 0xFFFFFE00u) == FLIPPED ? nullptr   // (VRAM's for a frame)
                                  : sms_->rom_orig_ptr(o, 32);
            }
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
        // (the slot being timed, slot_begin, stays: it is the frame's; so do the palace guards' bits)
        s.module_state[MS_PLACEMENT] = PLACED_AT_WAKE | ((was & PLACED_AT_WAKE) ? was & (SLOT_START | SLOT_PARITY | SLOT_OPEN | SLOT_FREE | GUARD_ARMED_LEFT | GUARD_ARMED_RIGHT | STORY_ORDER) : 0);
    }

    Sms* sms_ = nullptr;
    bool hud_anchor_ = true;
    static constexpr uint8_t EXT_VERSION = 1;   // (save_ext's layout)
    uint8_t page_[64] = {};   // each sprite table buffer entry's page as the writers left it (uploaded at the VBlank)
    StorySprites story_;      // the story screens' sprite pieces (story_.upload at the VBlank)
    uint32_t story_rows_ = 0; // a widened scene's rows the scene has drawn over (scene_replaced_rows, each frame)
    int story_shift_ = 0;     // the frame's scene drawn at the right, px (story_shift, apply_view)
    StoryFills fills_;        // the fills' made cells (kSceneFills, make_fills at the frame's start)
    // The made cells of a widened scene's fills, from VRAM as it is (the scenes reload no tiles; a fade changes colours
    // only): the picture cell's pixels, the source cell's where they are 0. alt_patterns points at them while such a
    // scene is shown (stage 7 points it at its own copies when it uses them).
    void make_fills(Sms& m) {
        SmsVdp& v = m.vdp;
        fills_.count = 0;
        const int sc = m.ram8(SCENE);
        if (story_wide(m) == STORY_SCENE && m.ram16(MAP) == SCENE_MAPS)
            for (const SceneFill& f : kSceneFills) {
                if (f.scene != sc) continue;
                for (int row = f.r0; row < f.r1; row++) {
                    bool comp = false;
                    const int fg = fill_cell(m, f, row, &comp);
                    if (!comp) continue;
                    const int pat = fg & 0x1FF;
                    const int lo = fills_.count ? std::min(fills_.first, pat) : pat;
                    const int hi = fills_.count ? std::max(fills_.first + fills_.count, pat + 1) : pat + 1;
                    if (hi - lo > StoryFills::MAX) continue;   // (out of the buffer's reach: drawn as the picture's)
                    if (fills_.count && lo < fills_.first) {   // (the range grows down: move what is made)
                        std::memmove(fills_.tiles + (fills_.first - lo) * 32, fills_.tiles, fills_.count * 32);
                        std::memset(fills_.tiles, 0, (fills_.first - lo) * 32);
                    }
                    fills_.first = lo;
                    fills_.count = hi - lo;
                    const int bg = scene_cell(m, f.scene, f.src, row);
                    uint8_t a[8][8], b[8][8];
                    StoryFills::pixels(&v.vram[pat * 32], fg, a);
                    StoryFills::pixels(&v.vram[(bg & 0x1FF) * 32], bg, b);
                    uint8_t* out = fills_.tiles + (pat - lo) * 32;
                    for (int ty = 0; ty < 8; ty++) {   // (in the picture cell's own orientation)
                        uint8_t r[4] = {};
                        for (int tx = 0; tx < 8; tx++) {
                            const int y = (fg & 0x400) ? 7 - ty : ty, x = (fg & 0x200) ? 7 - tx : tx;
                            const uint8_t c = a[y][x] ? a[y][x] : b[y][x];
                            for (int k = 0; k < 4; k++) r[k] |= (uint8_t)(((c >> k) & 1) << (7 - tx));
                        }
                        std::memcpy(out + ty * 4, r, 4);
                    }
                }
            }
        if (fills_.count) {
            v.alt_patterns = fills_.tiles;
            v.alt_first = fills_.first;
            v.alt_count = fills_.count;
        } else if (v.alt_patterns == fills_.tiles) {
            v.alt_patterns = nullptr;
        }
    }
    SectionTiles s7_;    // stage 7's sections' reloaded tiles (made from ROM when first needed)
    CamTiles cam_tiles_;   // stages 0 and 1's camera-animated tiles by phase, and their walls (made when first needed)
    uint8_t anim_buf_[ANIM_TILES * 32] = {};   // the frame's tiles 257-273 for the VDP (anim_tiles)
    bool s7_built_ = false;
    uint8_t s7_day_[32] = {}, s7_night_[32] = {};   // stage 7's palettes before / after section 4 (apply_view)
    // the view's slide (apply_view): the parallax bands' first lines (line interrupts), the camera's band, and the
    // frame counter at the camera's last move (stage 1's bands move on its phase; kept in save states)
    int band_start_[3] = {999, 999, 999};
    int cam_band_ = 0;
    // each line's band as drawn: a band starts where its line interrupt writes the scroll - one the game takes late
    // (its logic holding interrupts off) starts a few lines lower than $C499.. say (line_shift)
    int8_t line_band_[SmsVdp::kMaxLines] = {};
    // each line's band when line_scroll drew it with a split that had not landed yet (-1: as the registers say)
    int8_t line_early_[SmsVdp::kMaxLines] = {};
    // the frame's last line interrupt as line_scroll found it the line after: its line, the handler it runs, the
    // scroll writes so far (within a frame only: SmsVdp::line_irq_line starts each frame at -1)
    int irq_line_ = -1;
    uint16_t irq_handler_ = 0, irq_writes_ = 0;
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
        const int E = v.ext, cam = shown_cam(m), stage = shown_stage(m);
        if (!shows_stage(m)) set_behind(m, 0);   // (and at a stage's (re)start: the hook at $33B7)
        v.sprite_shift = view_slide(m, cam, E);
        for (int& b : band_start_) b = 999;
        cam_band_ = 0;
        if (shows_stage(m) && (stage == 0 || stage == 1)) {
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
        anim_tiles(m, stage, cam, v.sprite_shift);
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
        // a story scene drawn at the right (the throne room: scene_at_right): its picture's lines through line_shift,
        // its sprites all
        story_shift_ = shows_stage(m) ? 0 : story_shift(m);
        if (story_shift_) v.sprite_shift = -story_shift_;
    }

    // Stages 0 and 1's camera-animated tiles for the frame (see CamTiles): the walls in view as at a place where the
    // tables are right for them, all of it for the view's camera while the view slides.
    void anim_tiles(Sms& m, int stage, int cam, int slide) {
        SmsVdp& v = m.vdp;
        v.bg_alt = nullptr;
        if (!shows_stage(m) || (stage != 0 && stage != 1)) return;
        CamTiles& ct = cam_tiles_;
        if (ct.stage != stage || ct.map != m.ram16(MAP)) ct.build(m, stage);
        const int count = stage == 0 ? ANIM_TILES : ANIM_TILES - 1, step = stage == 0 ? 32 : 1;
        // the view's camera; without a slide the one whose tiles are in VRAM (the job copies them every fourth frame:
        // stage 0's bricks tell their step) - a wall where the tables are right is VRAM's then
        int ce = cam + slide;
        const bool vram = slide == 0;
        if (stage == 0 && vram) {
            for (int k = 0; k < 8; k++) {
                if (std::memcmp(&v.vram[(ANIM_FIRST + 16) * 32], ct.bricks[k], 32)) continue;
                const int d = ((ct.phase(cam) & 7) - k) & 7;
                ce = d <= 4 ? cam - 4 * d : cam + 4 * (8 - d);
                break;
            }
        }
        bool any = !vram;
        for (int i = 0; i < count * 32; i++) anim_buf_[i] = v.vram[ANIM_FIRST * 32 + i];
        if (!vram && stage == 0) std::memcpy(&anim_buf_[16 * 32], ct.tiles(ce) + 16 * 32, 32);   // (the bricks)
        for (int g = 0; g < (int)std::size(kAnimGroups); g++) {
            const AnimGroup& G = kAnimGroups[g];
            if (G.stage != stage) continue;
            // the wall of the kind nearest the view's middle (they lie 512 px or more apart: one in view at most)
            const CamTiles::Wall* w = nullptr;
            for (const CamTiles::Wall& c : ct.walls)
                if (c.group == g && (!w || std::abs(c.x0 + c.w / 2 - ce - 128) < std::abs(w->x0 + w->w / 2 - ce - 128))) w = &c;
            int cg = ce;
            if (w) {
                const int x = w->x0 - ce;
                const bool left = x + w->w <= 8, right = x >= 256;   // (wholly off the original screen)
                if (left || right) {
                    const int xt = x + ((G.xref - w->x0) % 512 + 512 + 256) % 512 - 256;   // (as the tables have it)
                    const int lo = left ? G.left_lo : G.right_lo, hi = left ? G.left_hi : G.right_hi;
                    const int win = left ? G.left_win : G.right_win;
                    // (shown as at win + ((xt - win) mod step): the camera that puts it there)
                    if (xt < lo || xt > hi) cg = ce + (xt - win) - (((xt - win) % step) + step) % step;
                }
            }
            if (cg == ce && vram) continue;
            std::memcpy(&anim_buf_[(G.first - ANIM_FIRST) * 32], ct.tiles(cg) + (G.first - ANIM_FIRST) * 32, 8 * 32);
            any = true;
        }
        if (!any) return;
        v.bg_alt = anim_buf_;
        v.bg_alt_first = ANIM_FIRST;
        v.bg_alt_count = count;
    }

    // A visual fix to the original, at every width (4:3 too, widescreen off included): the parallax bands' line
    // interrupts land one line late. The counter underflows at the end of the line above the band's first tile row
    // (stage 1's rooftops from line 128: at the end of line 127), and the handler's scroll write lands during that
    // row's first line, so the row's top pixel line is drawn with the band above's scroll (1-2: the roofs' sky-orange
    // top row shifted sideways into the brown city by the two scrolls' gap). A line due a band whose write has not
    // landed yet is drawn with the scroll that write brings (-($C4A7 + k), read as the handler will read it): the
    // split as if on time. The same a few lines further when the game takes the interrupt late (SPLIT_LATE). Only the
    // picture changes: the registers, the line interrupts and everything the game reads stay the original's.
    // The write is known to be coming from the VDP and the CPU: an interrupt raised at the end of a line above
    // (SmsVdp::line_irq_line), still pending or its handler under way, with a handler that writes a band's scroll
    // ($C91D as the line after the raise finds it: no handler has run so soon) and no scroll write since. (A status
    // read outside the handler, $09A5 at a mode change, can lose one: then nothing is corrected.)
    static int line_scroll(void* host, int line) {
        SmsAladdin& mod = *(SmsAladdin*)host;
        const Sms& s = *mod.sms_;
        const SmsVdp& v = s.vdp;
        if (line < 0 || line >= SmsVdp::kMaxLines) return -1;
        mod.line_early_[line] = -1;
        if (v.line_irq_line < 0) return -1;
        if (line == v.line_irq_line + 1) {   // (the handler to run, before it can have moved the chain on)
            mod.irq_line_ = v.line_irq_line;
            mod.irq_handler_ = v.line_pending ? s.ram16(BAND_NEXT) : 0;
            mod.irq_writes_ = v.hscroll_writes;
        }
        if (line < 16 || line >= 176 || !(v.reg[0] & 0x10)) return -1;   // (the playfield; line interrupts on)
        if (mod.irq_line_ != v.line_irq_line || line - 1 - mod.irq_line_ > SPLIT_LATE || v.hscroll_writes != mod.irq_writes_)
            return -1;
        const uint16_t pc = s.z80.pc;
        const bool under_way = (pc >= 0x0038 && pc < 0x0045) || (pc >= 0x01E9 && pc < 0x0258);   // ($0038, $01E9..)
        if (!v.line_pending && !under_way) return -1;
        int due = 0;
        for (int k = 1; k < 4; k++)
            if (mod.irq_handler_ == BAND_CHAIN[k]) due = k;
        const int done = (uint16_t)(v.hscroll_writes - v.line_hscroll_writes[0]);   // (the frame's writes so far)
        if (!due || done != due - 1) return -1;
        mod.line_early_[line] = (int8_t)due;
        return (-s.ram8((uint16_t)(BAND_SCROLL + due))) & 0xFF;
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
        if (mod.story_shift_) return line < SCENE_LINES ? -mod.story_shift_ : 0;   // (a scene drawn at the right)
        const Sms& s = *mod.sms_;
        const int vs = s.vdp.sprite_shift;
        const bool banded = mod.band_start_[0] < 999;   // (stages 0 and 1)
        // (the band: the scroll writes since the frame's first line - the VBlank wrote the first band's)
        int k = mod.band_of(line);
        if (banded && line >= 0 && line < SmsVdp::kMaxLines)
            k = mod.line_early_[line] >= 0 ? mod.line_early_[line]   // (drawn with its split on time: line_scroll)
                                           : std::min(3, s.vdp.line_hscroll_writes[line] - s.vdp.line_hscroll_writes[0]);
        if (line >= 0 && line < SmsVdp::kMaxLines) mod.line_band_[line] = (int8_t)k;
        if ((vs == 0 && !banded) || line < 16 || line >= 176) return 0;
        const int cam = shown_cam(s);
        if (k == mod.cam_band_) return vs - (band_camera(s, s.vdp.reg[8]) - cam);
        const int live = (int16_t)(s.ram16(CAM_X) - cam);    // how far the logic has moved the camera since
        if (shown_stage(s) == 0) return (k == 1 ? 2 : 3) * (vs - live);
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
        if (!shows_stage(s))
            return story_bg_entry(s, vcol, vrow, ((const SmsAladdin*)host)->story_rows_, v.hscroll_for(line));
        // (the table's 28 rows wrap: Jafar's turn into the snake shakes the screen with the vertical scroll, 222 / 224)
        vrow %= 28;
        // the status bar (rows 0-1): blank beyond the 256 px; below the playfield: see line_hook
        if (vrow < 2) return v.name_entry(0, vrow) & 0x1FFF;
        if (vrow >= 22) return -1;
        const SmsAladdin& mod = *(const SmsAladdin*)host;
        if (mod.line_band_[line % SmsVdp::kMaxLines] != mod.cam_band_) return -1;   // a parallax band: it repeats every 256 px
        const int hs = v.reg[8];
        int wx = band_camera(s, hs) + hs + vcol * 8;
        if (shown_stage(s) == ARENA_STAGE) {   // past the arena's ends: its pillars carry on (dimmed by post_frame)
            const int lo = s.ram16(CAM_MIN), end = lo + s.ram16(MAP_STRIDE) * 16;
            if (wx < lo || wx >= end) wx = lo + ARENA_FROM + (((wx - lo - ARENA_FROM) % ARENA_PERIOD) + ARENA_PERIOD) % ARENA_PERIOD;
        }
        int e = map_entry(s, wx, vrow - 2);
        const int tile = e & 0x1FF;
        if (e >= 0 && shown_stage(s) == 7) {
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
        const Sms& s = *((SmsAladdin*)host)->sms_;
        if (!shows_stage(s)) return story_sprite_x(s, index, x);
        if (ready_sprite(s.vdp, index, x, y, pattern)) return x + s.vdp.sprite_shift;   // (the screen's: no slide)
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
        if (!shows_stage(s)) {
            story_line_hook(s, bg, spr);   // (the blanked left column of the other screens)
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
