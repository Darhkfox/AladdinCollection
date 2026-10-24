// Aladdin (Mega Drive / Genesis, Virgin Games 1993): widescreen, HUD, cheats, level select, sound, achievements.
// Addresses and techniques: docs/re_md_game.md, docs/re_md_wide.md.
#include "game.h"
#include "md_common.h"
#include "../core/md.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

using namespace md_game;

namespace {

// ---- RAM (docs/re_md_game.md section 1) ------------------------------------------------------------
constexpr uint32_t CAM_X = 0xFF7DF6, CAM_Y = 0xFF7DF8;        // camera (map x of screen x 0 = CAM_X + 16)
constexpr uint32_t MAP_W = 0xFF7DB6, MAP_H = 0xFF7DBA;        // level map size in 16-px blocks
constexpr uint32_t MAP_DEFS = 0xFF7DBE;                       // block definitions (4 name-table words each)
constexpr uint32_t MAP_ROWS = 0xFF9884;                       // row pointers, one long per 4 px of map y
constexpr uint32_t STREAM_X = 0xFF7E06, STREAM_Y = 0xFF7E08;  // the plane holds map x [L, L+368), y [Y, Y+256)
constexpr uint32_t DIFFICULTY = 0xFF7E21;                     // 0 Practice, 1 Normal, 2 Difficult
constexpr uint32_t LEVEL = 0xFF7E26;                          // level id (kLevels)
constexpr uint32_t LIVES = 0xFF7E3C;                          // ASCII digit
constexpr uint32_t CONTINUES = 0xFF7E3F;
constexpr uint32_t APPLES = 0xFFEFE0, GEMS = 0xFFEFE2;        // two ASCII digits each
constexpr uint32_t HEALTH = 0xFFEFFA, HEALTH_MAX = 0xFFEFFB;
constexpr uint32_t SWAP_PLANES = 0xFFF165;                    // the level map is on plane B (Sultan's Dungeon)
constexpr uint32_t SCRIPT = 0xFFF572;                         // play-order script position (ROM $4082)
constexpr uint32_t DEV_CHEAT = 0xFF7E20, DEV_MAP_VIEW = 0xFF7273, DEV_FREEZE = 0xFF7E25;   // the debug screen's options
constexpr uint32_t SCORE = 0xFF7E29;                          // six ASCII digits
constexpr uint32_t ALADDIN = 0xFF7E40;                        // the player object ($83 while Aladdin is in play)
constexpr uint32_t PAUSED = 0xFFF158;                         // $FF in the pause loop
constexpr uint32_t GENIE_TOKENS = 0xFFF003;                   // a spin of the Genie's wheel each, after the level
constexpr uint32_t LEVEL_END = 0xFFF0E9;                      // the level is over (countdown, $FF = at once)
constexpr uint32_t DEMO = 0xFFF57C;                           // 1 in the attract demos
constexpr uint32_t RUG_CRASHES = 0xFFF006;                    // Rug Ride lives lost: the third crash ends the ride
constexpr uint32_t RIDE_DONE = 0xFFF005;                      // set by the Rug Ride's end (its script's $F2 entry)
constexpr uint32_t SHAKE_X = 0xFFF082;                        // earthquake shake (Escape from the Cave): planes and sprites
constexpr uint32_t SCRIPT_START = 0x4082;

// ROM code the module watches (PC hooks)
constexpr uint32_t PC_MAIN_LOOP = 0x1A8C16;   // top of the gameplay loop
constexpr uint32_t PC_PAUSE_LOOP = 0x1A9230;  // the pause loop
constexpr uint32_t PC_LEVEL_END = 0x1A8E5C;   // a level is over: next level / ending
constexpr uint32_t PC_DEATH = 0x1A8F82;       // the death screen (Aladdin, Abu and the carpet on black)
constexpr uint32_t PC_PIT = 0x1A902E;         // fell below the map: a life lost without the death screen
constexpr uint32_t PC_FADE_IN = 0x1A8BFC;     // a level set up: its palette fades in ($1AE1A0), then play
constexpr uint32_t PC_RESPAWN_FADE = 0x1A91B8;  // the same after a life lost (the level rebuilt at the restart point)
constexpr uint32_t PC_LEVEL_LOAD = 0x1A8B50;  // the level loop: the level's story card, then its set-up
constexpr uint32_t TITLE_RET = 0xFFEFD4;      // the return address of the main entry's calls (the title's while it runs)
constexpr uint32_t PC_MAIN_ENTRY = 0x1A8A4A;  // power-on, and after a game: SP = $FFEFD8, the set-up, the title
constexpr uint32_t PC_TITLE_RET = 0x1A8B32;   // the title routine returns here: a game starts (or a demo)
constexpr uint32_t PC_LEVEL_INIT = 0x1AA484;  // level init (after the story card)
constexpr uint32_t PC_FRAME_WAIT = 0x1B249E;  // the frame wait (VBlank), the gameplay loop's at $1A8CD2

// module_state: [0] frame (+1) the gameplay loop last ran in, 0 after it was left, bit 31 (PLAY_BUSY) while that
// iteration hasn't reached its frame wait yet (in_play); [1] the developer flags the cheats set (bit 0 cheat mode,
// bit 1 map view), to clear them when they are switched off, and in bits 2-23 the boss framing (boss_frame: bit 2 a
// fight framed, 3-13 its arena's block column sum, 14-23 the easing remainder, signed; 0 in older states: none), bit 24
// the Cave's carpet due, its idol just beaten (CARPET_DUE, carpet_due);
// [2] a level's fade-in is running (shown as play: the picture is already the level's, so it starts wide and slid
// instead of jumping when play begins); [3..6] boss tunes held back (boss_tune_held); [7] palette line 2 loads held
// back (line2_held)
enum { MS_PLAY = 0, MS_DEV = 1, MS_FADE = 2, MS_BOSS = 3, MS_BOSS_N = 4, MS_PAL2 = 7 };
constexpr uint32_t PLAY_BUSY = 0x80000000u, PLAY_FRAME = 0x7FFFFFFFu;

enum : int { L_ROOFTOPS = 0, L_MARKET, L_ABU_AGRABAH, L_DESERT, L_DUNGEON, L_CAVE, L_ABU_CAVE, L_ESCAPE, L_RUG_RIDE,
             L_LAMP, L_PALACE, L_JAFAR_PALACE, L_JAFAR_QUARTERS };

struct Level { int id; const char* group; const char* name; };
// Play order (the game's script at $4082); the Abu bonus rounds follow the level whose Abu token leads there.
const Level kLevels[] = {
    {L_MARKET, "Agrabah", "Agrabah Market"},
    {L_ABU_AGRABAH, "Agrabah", "Bonus - Abu in Agrabah"},
    {L_DESERT, "Agrabah", "The Desert"},
    {L_ROOFTOPS, "Agrabah", "Agrabah Rooftops"},
    {L_DUNGEON, "The Palace Dungeon", "Sultan's Dungeon"},
    {L_CAVE, "The Cave of Wonders", "Cave of Wonders"},
    {L_ABU_CAVE, "The Cave of Wonders", "Bonus - Abu in the Cave"},
    {L_ESCAPE, "The Cave of Wonders", "Escape from the Cave"},
    {L_RUG_RIDE, "The Cave of Wonders", "Rug Ride"},
    {L_LAMP, "The Genie", "Inside the Lamp"},
    {L_PALACE, "The Palace", "Sultan's Palace"},
    {L_JAFAR_QUARTERS, "The Palace", "Jafar's Quarters"},
    {L_JAFAR_PALACE, "The Palace", "Jafar's Palace"},
};

// The play-order script position after `level`'s entry (pairs: command, level; command 1 = only with Abu's token)
uint32_t script_after(const MegaDrive& md, int level) {
    if (level == L_MARKET) return SCRIPT_START;
    for (uint32_t a = SCRIPT_START; md.rom_orig16(a) != 0xFF && a < SCRIPT_START + 0x80; a += 4)
        if (md.rom_orig16(a + 2) == level) return a + 4;
    return SCRIPT_START;
}

// ---- cheats (docs/re_md_game.md section 3) -----------------------------------------------------------
// Aladdin is hit: $1AE4F8 (knock-back and the health loss, 1/2/3 times by difficulty) -> rts
const std::vector<RomPatch> kInvincible = {{0x1AE4F8, {0x4A39, 0x00FF, 0xF0E7}, {0x4E75, 0x00FF, 0xF0E7}}};
// the health loss itself ($1B041A subq.b #1, $FFEFFA)
const std::vector<RomPatch> kInfHealth = {{0x1B041A, {0x5339, 0x00FF, 0xEFFA}, {0x4E71, 0x4E71, 0x4E71}}};
// a life lost ($1A90BE subq.b #1, d0 on the ASCII digit)
const std::vector<RomPatch> kInfLives = {{0x1A90BE, {0x5300}, {0x4E71}}};
// an apple thrown ($1B0360) / gems spent at the peddler ($1B03BE): the decrement routines return at once
const std::vector<RomPatch> kInfApples = {{0x1B0360, {0x0C79, 0x3030}, {0x4E75, 0x3030}}};
const std::vector<RomPatch> kInfGems = {{0x1B03BE, {0x0C79, 0x3030}, {0x4E75, 0x3030}}};
// a continue used ($1B0A1C subq.b #1, $FF7E3F)
const std::vector<RomPatch> kInfContinues = {{0x1B0A1C, {0x5339, 0x00FF, 0x7E3F}, {0x4E71, 0x4E71, 0x4E71}}};
// the pause screen's level-skip code ($4128: pad pairs A B B A A B B A, $FF ends) cut to its first press, A
const std::vector<RomPatch> kLevelSkip = {{0x00412A, {0x0010}, {0xFF00}}};

// ---- widescreen (docs/re_md_wide.md) -------------------------------------------------------------------
// The level map is streamed into a 512-px plane one 16-px block column at a time as the camera crosses 16 px: the
// plane holds map x [L, L+368) (L = $FF7E06), the 4:3 view [L+16+d, L+336+d), |d| < 16. The widened view's other
// columns are drawn per line straight from the RAM map (line_hook), so the plane needs no patch. Near a level's ends the
// picture slides (VDP::view_shift, s px) instead of showing what lies outside the level: the game's camera stays the
// original (Aladdin's walking limits are in screen coordinates), so the view covers up to [-2E, 320+2E] of the 4:3
// screen. Objects are part of the map (object blocks, $FFAE87 / $4154) and spawn when their column or row is streamed:
// the caves spawn the columns the slid view reaches as well; the despawn window and the sprite clip cover +-2E.
constexpr uint32_t CAVE = 0x7000;   // the $FF fill at $69E0-$FFFF (no references), reachable with jsr abs.w
constexpr uint32_t CAVE_LSPAWN = CAVE, CAVE_RSPAWN = CAVE + 0x100, CAVE_ROWSPAWN = CAVE + 0x200, CAVE_CLIP = CAVE + 0x280,
                   CAVE_SCRCLIP = CAVE + 0x2C0, CAVE_REACH = CAVE + 0x300, CAVE_DESPAWN = CAVE + 0x380,
                   CAVE_REFRESH = CAVE + 0x400, CAVE_CARPET = CAVE + 0x480;

// The 4:3 view only ever shows map x [16, W*16 - 18): camX in [0, W*16 - 354] (the camera's clamps, docs section 1).
constexpr int VIEW_LEFT = 16, VIEW_RIGHT_GAP = 18, CAM_SPAN_GAP = 354;

// The view's slide for camera X `camx` (the map x at screen x 0 is camx + 16) in a map W blocks wide: none in the middle
// of a level; near an end, as far as keeps the widened view inside what the 4:3 game shows; in a level narrower than
// the widened view, centred. CAVE_REACH does the same sum on the 68000 for the spawn caves.
int view_slide(int camx, int W, int E) {
    const int span = W * 16 - CAM_SPAN_GAP;   // the largest camera X
    int s;
    if (span < 2 * E) s = span / 2 - camx;
    else if (camx < E) s = E - camx;
    else if (camx > span - E) s = span - E - camx;
    else s = 0;
    return std::clamp(s, -E, E);
}

// Boss fights (docs/re_md_wide.md section 5). The 4:3 camera never locks: in a fight it follows Aladdin as anywhere
// else, and the arena is wider than the 4:3 view (the Cave of Wonders' golden idol: camera 1821-2178 between the
// arena's walls; Jafar's hall: 252-947), so at either end of it the widened view showed as much outside the arena on
// the outer side as the 4:3 game, and the boss at the far other edge. While a boss fight is on (Boss) the view slides
// toward the arena's centre instead - camera `centre` frames it at 4:3 - as far as |s| <= E and the level's ends allow:
// held still while the camera is within E of that, following it beyond, and, once the camera is more than
// E + ARENA_REACH away (Aladdin leaving the arena), back to the level's own slide over the next 2E px of camera (the view
// catching up at 1.5 times the camera's pace). (The level's ends keep the view inside what the 4:3 game shows; at the
// Rooftops' end the arena is the level's last screen and stays where the 4:3 game has it.)
constexpr int ARENA_REACH = 320;
struct Boss { bool on = false; int centre = 0; };

int slide_target(int camx, int W, int E, const Boss& b) {
    const int span = W * 16 - CAM_SPAN_GAP;
    const int lo = std::max(-E, E - camx), hi = std::min(E, span - E - camx);
    const int d = b.centre - camx, far = std::abs(d) - E - ARENA_REACH;
    if (!b.on || span < 2 * E || lo > hi || far >= 2 * E) return view_slide(camx, W, E);
    const int s = far <= 0 ? std::clamp(d, -E, E) : (d > 0 ? E - far / 2 : far / 2 - E);
    return std::clamp(s, lo, hi);
}

// The slide in force: the target plus `e`, what is left of a change of target (a fight starting or ending) as it
// eases out, inside the level.
int slide_total(int camx, int W, int E, const Boss& b, int e) {
    const int span = W * 16 - CAM_SPAN_GAP;
    const int lo = std::max(-E, E - camx), hi = std::min(E, span - E - camx);
    if (span < 2 * E || lo > hi) return view_slide(camx, W, E);
    return std::clamp(slide_target(camx, W, E, b) + e, lo, hi);
}

// An object stored in block column c appears at screen x 16c - (camX + 16): the original streams column L/16 (left,
// screen x -16-d) and L/16+22 (right, 336-d), |d| < 16, just outside its view. Spawning ceil((E -+ s) / 16) columns
// further out keeps that margin outside the slid view, and the despawn window keeps the original's margin outside it.
// Object sprites may start this far left of VDP X 0 (as negative X, read by the VDP's 10-bit mode): a 32-px piece just
// entering the fully slid view's left edge at screen x -2E.
int sprite_left(int E) { return std::max(0, 2 * E - 96); }

// Words with labels: branch displacements filled in once the target is known.
struct Asm {
    std::vector<uint16_t> w;
    Asm& operator<<(std::initializer_list<uint16_t> v) { w.insert(w.end(), v); return *this; }
    size_t here() const { return w.size(); }
    size_t branch(uint16_t op) { w.push_back(op); return w.size() - 1; }   // a short branch, displacement later
    void land(size_t at) { w[at] |= (uint16_t)(((w.size() - at - 1) * 2) & 0xFF); }
    void back(uint16_t op, size_t to) { w.push_back((uint16_t)(op | (((int)to - (int)w.size() - 1) * 2 & 0xFF))); }
};

std::vector<RomPatch> wide_patches(int E) {
    const uint16_t o = (uint16_t)sprite_left(E);
    auto ff = [](size_t n) { return std::vector<uint16_t>(n, 0xFFFF); };
    // d0 = left reach kl = ceil((E - s)/16), d1 = right reach kr = ceil((E + s)/16) in blocks: set by the module's PC hook
    // on this rts (reach_hook: s is the slide in force, a boss fight's framing included)
    const std::vector<uint16_t> reach = {0x4E75};
    // After the left column writer's own spawn (column L/16), the columns out to L/16 - kl (those >= 1): the spawner entered
    // past its set-up ($1AE42C) with the column's x ($FF7DB0, its scratch) and map pointer (a0), L itself untouched (a
    // frame can end inside, and the view is worked out from L). Every column of the band each time, not only the
    // farthest: an object that couldn't get sprite tiles ($1AD3E8, a pool of $74 shared by all objects) when its column
    // came first is tried again, instead of waiting for the 4:3 edge.
    Asm lspawn;
    lspawn << std::initializer_list<uint16_t>{0x4EB9, 0x001A, 0xE3FC,   // jsr $1AE3FC (the replaced call)
                                              0x4EB8, (uint16_t)CAVE_REACH,   // jsr reach
                                              0x3600};                        // move.w d0, d3 (j = kl .. 1)
    const size_t l_none = lspawn.branch(0x6700);                          // beq.s done
    const size_t l_loop = lspawn.here();
    lspawn << std::initializer_list<uint16_t>{0x3439, 0x00FF, 0x7E06, 0xE84A, 0xB443};   // d2 = L/16; cmp.w d3, d2
    const size_t l_out = lspawn.branch(0x6300);                           // bls.s next (column < 1)
    lspawn << std::initializer_list<uint16_t>{0x3403, 0xE94A, 0x4442,                   // d2 = -16j
                                              0xD479, 0x00FF, 0x7E06, 0x33C2, 0x00FF, 0x7DB0,   // x = L - 16j
                                              0x2079, 0x00FF, 0x7DAC,                   // a0 = map pointer
                                              0x3403, 0x48C2, 0xD482, 0x91C2,           //      - 2j
                                              0x3F03,                                   // (j kept on the stack)
                                              0x4EB9, 0x001A, 0xE42C,                   // jsr $1AE42C
                                              0x361F};
    lspawn.land(l_out);
    lspawn << std::initializer_list<uint16_t>{0x5343};                    // next: subq.w #1, d3
    lspawn.back(0x6600, l_loop);                                          // bne.s loop
    lspawn.land(l_none);
    lspawn << std::initializer_list<uint16_t>{0x4E75};
    // after the right column writer's own spawn (column L/16 + 22), the columns out to L/16 + 22 + kr (inside the map)
    Asm rspawn;
    rspawn << std::initializer_list<uint16_t>{0x4EB9, 0x001A, 0xE406,   // jsr $1AE406
                                              0x4EB8, (uint16_t)CAVE_REACH,   // jsr reach
                                              0x3601};                        // move.w d1, d3 (j = kr .. 1)
    const size_t r_none = rspawn.branch(0x6700);
    const size_t r_loop = rspawn.here();
    rspawn << std::initializer_list<uint16_t>{0x3439, 0x00FF, 0x7E06, 0xE84A, 0xD443,   // d2 = L/16 + j
                                              0x0642, 0x0016, 0xB479, 0x00FF, 0x7DB6};  // + 22; cmp.w W, d2
    const size_t r_out = rspawn.branch(0x6400);                           // bcc.s next
    rspawn << std::initializer_list<uint16_t>{0x3403, 0xE94A,                           // d2 = 16j
                                              0xD479, 0x00FF, 0x7E06, 0x33C2, 0x00FF, 0x7DB0,   // x = L + 16j
                                              0x2079, 0x00FF, 0x7DAC,                   // a0 = map pointer
                                              0x3403, 0x48C2, 0xD482, 0xD1C2,           //      + 2j
                                              0x3F03,
                                              0x4EB9, 0x001A, 0xE42C,
                                              0x361F};
    rspawn.land(r_out);
    rspawn << std::initializer_list<uint16_t>{0x5343};
    rspawn.back(0x6600, r_loop);
    rspawn.land(r_none);
    rspawn << std::initializer_list<uint16_t>{0x4E75};
    // Despawn ($1ADEB6): object x outside [camX - $50 - 16kl, camX + $190 + 16kr] - the original's margin outside the
    // view as slid (more would keep objects that hold sprite tiles a new one needs). Carry set: outside.
    Asm despawn;
    despawn << std::initializer_list<uint16_t>{0x48E7, 0x6000,           // movem.l d1-d2, -(a7)
                                               0x4EB8, (uint16_t)CAVE_REACH,
                                               0xE948, 0x0640, 0x0050,  // d0 = 16kl + $50
                                               0xD069, 0x0002,          // + x
                                               0xB079, 0x00FF, 0x7DF6}; // cmp.w camX, d0
    const size_t d_out1 = despawn.branch(0x6500);                         // bcs.s out
    despawn << std::initializer_list<uint16_t>{0xE949, 0x0641, 0x0190,   // d1 = 16kr + $190
                                               0xD279, 0x00FF, 0x7DF6,   // + camX
                                               0xB269, 0x0002};          // cmp.w x, d1
    const size_t d_out2 = despawn.branch(0x6500);                         // bcs.s out
    despawn << std::initializer_list<uint16_t>{0x4CDF, 0x0006, 0x023C, 0x00FE, 0x4E75};   // keep: carry clear
    despawn.land(d_out1);
    despawn.land(d_out2);
    despawn << std::initializer_list<uint16_t>{0x4CDF, 0x0006, 0x003C, 0x0001, 0x4E75};   // out: carry set
    // a streamed row spawns blocks [L/16 - kl, L/16 + 22 + kr] inside the map instead of [L/16, L/16 + 22]: the loop's
    // a0 (map pointer), d6 (x), d4 (count - 1), a1, a2
    std::vector<uint16_t> rowspawn = {
        0x4EB8, (uint16_t)CAVE_REACH,              // jsr reach (d0 = kl, d1 = kr)
        0x3C39, 0x00FF, 0x7E06,                    // move.w $FF7E06, d6
        0xE84E,                                    // lsr.w #4, d6
        0x3806, 0x3A06,                            // move.w d6, d4; move.w d6, d5
        0x9C40,                                    // sub.w d0, d6
        0x6A02, 0x7C00,                            // bpl.s +2; moveq #0, d6
        0xD841, 0x0644, 0x0016,                    // add.w d1, d4; addi.w #22, d4
        0x3239, 0x00FF, 0x7DB6,                    // move.w $FF7DB6, d1
        0x5341,                                    // subq.w #1, d1
        0xB841, 0x6F02, 0x3801,                    // cmp.w d1, d4; ble.s +2; move.w d1, d4
        0x9846,                                    // sub.w d6, d4
        0x2079, 0x00FF, 0x7DAC,                    // movea.l $FF7DAC, a0
        0x3206, 0x9245,                            // move.w d6, d1; sub.w d5, d1
        0xD0C1, 0xD0C1,                            // adda.w d1, a0 (x2)
        0xE94E,                                    // lsl.w #4, d6
        0x43F8, 0x4154,                            // lea $4154.w, a1
        0x45F9, 0x00FF, 0xAE87,                    // lea $FFAE87, a2
        0x4E75};
    // Object sprite clip: VDP X in [-o, 448+2E) instead of [0, 447) (the compare on a copy biased by o: the X written
    // stays the piece's own, negative left of X 0). Map objects clear bit 15 of the entry's size / link word (unused by
    // the VDP), screen-fixed ones set it: sprite_xoff keeps those where the game put them on the slid view.
    std::vector<uint16_t> clip = {0x0241, 0x7FFF, 0x9647};   // andi.w #$7FFF, d1; sub.w d7, d3 (the replaced one)
    if (o) clip.insert(clip.end(), {0x3F03, 0x0657, o, 0x0C5F, (uint16_t)(448 + 2 * E + o)});   // move.w d3, -(a7);
    else clip.insert(clip.end(), {0x0C43, (uint16_t)(448 + 2 * E)});                           // addi / cmpi (a7)+
    clip.push_back(0x4E75);
    const std::vector<uint16_t> scrclip = {0x0443, 0x0080, 0x0041, 0x8000, 0x0C43, (uint16_t)(448 + 2 * E), 0x4E75};
    // Block columns spawned as a column step would, from the gameplay loop: while a boss fight's framing is in force the
    // view can move further than the camera (easing in or out), and the band with it, between column steps. Entered from
    // the module's hook at $1A8CE8 (jsr $1B315C) with the return past it and two column ranges (words: first, last,
    // first, last) pushed: each column through the column spawner ($1AE42C, past its set-up: $FF7DB0 = 16c, a0 = the
    // map at column c, row Y/16; $FFF150 / $FFF152 = -16 / $F0, as the left writer's), all registers kept; then the
    // replaced call.
    Asm refresh;
    refresh << std::initializer_list<uint16_t>{0x48E7, 0xFFFE,                    // movem.l d0-d7/a0-a6, -(a7)
                                               0x2F39, 0x00FF, 0xF150,            // move.l $FFF150, -(a7)
                                               0x33FC, 0xFFF0, 0x00FF, 0xF150,    // move.w #-16, $FFF150
                                               0x33FC, 0x00F0, 0x00FF, 0xF152,    // move.w #$F0, $FFF152
                                               0x3C2F, 0x0040, 0x3E2F, 0x0042};   // move.w 64(a7), d6; 66(a7), d7
    const size_t call1 = refresh.branch(0x6100);                                  // bsr.s columns
    refresh << std::initializer_list<uint16_t>{0x3C2F, 0x0044, 0x3E2F, 0x0046};   // move.w 68(a7), d6; 70(a7), d7
    const size_t call2 = refresh.branch(0x6100);                                  // bsr.s columns
    refresh << std::initializer_list<uint16_t>{0x23DF, 0x00FF, 0xF150,            // move.l (a7)+, $FFF150
                                               0x4CDF, 0x7FFF,                    // movem.l (a7)+, d0-d7/a0-a6
                                               0x508F,                            // addq.l #8, a7 (the ranges)
                                               0x4EF9, 0x001B, 0x315C};           // jmp $1B315C
    refresh.land(call1);   // columns: d6 .. d7
    refresh.land(call2);
    refresh << std::initializer_list<uint16_t>{0xBC47};                           // cmp.w d7, d6
    const size_t none = refresh.branch(0x6E00);                                   // bgt.s done
    const size_t col = refresh.here();
    refresh << std::initializer_list<uint16_t>{0x3F06, 0x3F07,                    // move.w d6, -(a7); d7, -(a7)
                                               0x41F9, 0x00FF, 0x9884,            // lea $FF9884, a0 (row pointers)
                                               0x3A39, 0x00FF, 0x7E08, 0x0245, 0xFFF0,   // move.w $FF7E08, d5; andi.w #$FFF0
                                               0xDA45, 0xDA45, 0x2070, 0x5000,    // add.w d5, d5 (x2); movea.l (a0, d5.w), a0
                                               0x3A06, 0xDA45, 0xD0C5,            // + 2c
                                               0xE94E, 0x33C6, 0x00FF, 0x7DB0,    // lsl.w #4, d6; move.w d6, $FF7DB0
                                               0x4EB9, 0x001A, 0xE42C,            // jsr $1AE42C
                                               0x3E1F, 0x3C1F, 0x5246, 0xBC47};   // d7, d6 back; addq.w #1, d6; cmp.w d7, d6
    refresh.back(0x6F00, col);                                                    // ble.s column
    refresh.land(none);
    refresh << std::initializer_list<uint16_t>{0x4E75};
    // One map block spawned as the column spawner would spawn it (the Cave's carpet at the idol's kill, carpet_due):
    // entered from the module's hook at $1A8CE8 with the return past the replaced jsr and the block pushed (long: the
    // address of its map word; words: 16c, then 16r on top); the column spawner's loop ($1AE44A) for that block alone
    // (its handler gets x = 16c - 16, y = 16r + 240 from $FF7DB0 / $FF7DB2 and $FFF150 / $FFF152), all registers kept;
    // then the replaced call.
    const std::vector<uint16_t> carpet = {
        0x48E7, 0xFFFE,                    // movem.l d0-d7/a0-a6, -(a7)
        0x2F39, 0x00FF, 0xF150,            // move.l $FFF150, -(a7)
        0x33FC, 0xFFF0, 0x00FF, 0xF150,    // move.w #-16, $FFF150
        0x33FC, 0x00F0, 0x00FF, 0xF152,    // move.w #$F0, $FFF152
        0x33EF, 0x0042, 0x00FF, 0x7DB0,    // move.w 66(a7), $FF7DB0 (16c)
        0x3C2F, 0x0040,                    // move.w 64(a7), d6 (16r)
        0x206F, 0x0044,                    // movea.l 68(a7), a0 (the map word)
        0x43F8, 0x4154,                    // lea $4154.w, a1
        0x45F9, 0x00FF, 0xAE87,            // lea $FFAE87, a2
        0x7800, 0x7A00,                    // moveq #0, d4 (one block); moveq #0, d5
        0x4EB9, 0x001A, 0xE44A,            // jsr $1AE44A
        0x23DF, 0x00FF, 0xF150,            // move.l (a7)+, $FFF150
        0x4CDF, 0x7FFF,                    // movem.l (a7)+, d0-d7/a0-a6
        0x508F,                            // addq.l #8, a7 (the block)
        0x4EF9, 0x001B, 0x315C};           // jmp $1B315C (the refresh cave before it: 64 words, $7400-$747F)
    return {
        {0x1AB446, {0x6100, 0x2FB4}, {0x4EB8, (uint16_t)CAVE_LSPAWN}},
        {0x1AB554, {0x6100, 0x2EB0}, {0x4EB8, (uint16_t)CAVE_RSPAWN}},
        {0x1AE4A8, {0x2079, 0x00FF, 0x7DAC, 0x43F8, 0x4154, 0x3C39, 0x00FF, 0x7E06, 0x0206, 0x00F0, 0x45F9, 0x00FF,
                    0xAE87, 0x383C, 0x0016},
                   {0x4EB8, (uint16_t)CAVE_ROWSPAWN, 0x6018, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71,
                    0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71}},
        {CAVE_REACH, ff(reach.size()), reach},
        {CAVE_REFRESH, ff(refresh.w.size()), refresh.w},
        {CAVE_CARPET, ff(carpet.size()), carpet},
        {CAVE_LSPAWN, ff(lspawn.w.size()), lspawn.w},
        {CAVE_RSPAWN, ff(rspawn.w.size()), rspawn.w},
        {CAVE_ROWSPAWN, ff(rowspawn.size()), rowspawn},
        // despawn window (the x test of $1ADE84): jsr CAVE_DESPAWN; bcc.w keep; bra.w despawn
        {0x1ADEB6, {0x3029, 0x0002, 0x0640, 0x0050, 0xB079, 0x00FF, 0x7DF6, 0x6510, 0x3039, 0x00FF, 0x7DF6, 0x0640,
                    0x0190, 0xB069, 0x0002, 0x6410},
                   {0x4EB8, (uint16_t)CAVE_DESPAWN, 0x6400, 0x002A, 0x6000, 0x0016, 0x4E71, 0x4E71, 0x4E71, 0x4E71,
                    0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71}},
        {CAVE_DESPAWN, ff(despawn.w.size()), despawn.w},
        // sprite clip: map objects (sub.w d7, d3; cmpi.w #$1BF, d3 -> jsr CAVE_CLIP; nop) and screen-fixed ones
        // (subi.w #$80, d3; cmpi.w #$1BF, d3 -> jsr CAVE_SCRCLIP; nop; nop), the bcc after them unchanged
        {0x1ABAD0, {0x9647, 0x0C43, 0x01BF}, {0x4EB8, (uint16_t)CAVE_CLIP, 0x4E71}},
        {CAVE_CLIP, ff(clip.size()), clip},
        {0x1ABA8C, {0x0443, 0x0080, 0x0C43, 0x01BF}, {0x4EB8, (uint16_t)CAVE_SCRCLIP, 0x4E71, 0x4E71}},
        {CAVE_SCRCLIP, ff(scrclip.size()), scrclip},
    };
}

// The attract mode: from power-on (or the main entry after a game) through the title routine (logos, title, OPTIONS,
// the debug screen), and a demo playing. The main entry ($1A8A4A) sets SP to $FFEFD8 and calls the set-up and then the
// title from that top level, so while they run their return address lies at $FFEFD4: $1A8A58 ... $1A8B32 (the title's).
// The first call after the title (a game) overwrites it; before the main entry the word is still 0 (a game is never
// under way in the first seconds). Stateless, so a save state of any version tells it.
bool attract(const MegaDrive& md) {
    const uint32_t ret = md.ram32(TITLE_RET);
    return md.ram8(DEMO) != 0 || (ret > PC_MAIN_ENTRY && ret <= PC_TITLE_RET) || (ret == 0 && md.frame_count < 120);
}

// The gameplay loop ran lately (in play or paused), in a game. One iteration of the loop can take several frames: the
// game draws nothing new meanwhile and the picture holds, as at 4:3 - hitting two of the Cave of Wonders' monkey
// statues runs a scan of the whole level map ($1AC386 / $1AC3BC, clearing the blocks of a wall elsewhere) that keeps
// the 68000 busy for 9 frames. Those frames are play: the iteration is under way from the loop's top
// (PC_MAIN_LOOP sets PLAY_BUSY) until its frame wait (PC_FRAME_WAIT clears it). (Up to a second of it: a way out of
// the loop not hooked as leaving it still ends play.)
constexpr uint32_t PLAY_RECENT = 4, PLAY_BUSY_MAX = 60;
bool in_play(const MegaDrive& md) {
    const uint32_t ms = md.module_state[MS_PLAY];
    if (ms == 0 || attract(md)) return false;
    const uint32_t age = ((uint32_t)md.frame_count + 1 - ms) & PLAY_FRAME;
    return age <= PLAY_RECENT || ((ms & PLAY_BUSY) && age <= PLAY_BUSY_MAX);
}

// A level on screen in a game: in play, or fading in (its first frames, before the gameplay loop runs).
bool in_gameplay(const MegaDrive& md) { return (md.module_state[MS_FADE] != 0 && !attract(md)) || in_play(md); }

// Gameplay shown wide: all but Abu's bonus rounds (a 336-px room), which stay 4:3.
bool wide_view(const MegaDrive& md) {
    const int level = md.ram8(LEVEL);
    return in_gameplay(md) && level != L_ABU_AGRABAH && level != L_ABU_CAVE;
}

// The Rug Ride (docs/re_md_wide.md section 7). The carpet flies right at the ride's own pace (the camera moves `speed`
// px an iteration, 6 at the start and one more every 310 iterations) and Aladdin stays at screen x 120; the obstacles
// are sent in by a script every 192 px of the ride at the 4:3 view's right edge (screen x 304) and stay put in the cave
// as it scrolls past. Anything shown right of screen x 320 would show nothing of what is coming there, and what came
// would pop up in the middle of the view. So the ride is shown with all the extra width behind the carpet: the view
// slides by -E (screen x -2E .. 319), the 4:3 picture is its right part exactly, the right edge is the 4:3 game's (the
// same look ahead: the same ride), and the left part shows the cave behind - what was passed, and the lava wave that
// chases the carpet. The game runs on the 4:3 game's ROM (MdAladdin::rom_for); the module draws what the 4:3 game
// doesn't show there (RideView). The planes are 512-px loops the VDP wraps; plane B's lava wave, the one picture in them
// that isn't a repeating pattern, never shows twice (its copy 512 px on lies right of the view).
bool rug_ride(const MegaDrive& md) { return md.ram8(LEVEL) == L_RUG_RIDE; }

// The status bar's sprites (written first, $1AB7C4): the lamp and its smoke trail (tiles $680-$6DF, VDP X $92 up to
// $C2 + 8 x health - the last pieces pass X $100), Aladdin's face ($6EA) and lives digit (X $AA) bottom left; the apple and
// gem icons ($6F3 / $6F7) and their digits (X $16A up) bottom right; the score ($7C0-$7E7) top right. Nothing else uses
// these tiles (checked in every level). -1 left group, 1 right group, 0 not the HUD.
int hud_side(int tile, int x) {
    if (tile >= 0x7C0 && tile < 0x7E8) return 1;
    if (tile < 0x680 || tile >= 0x700) return 0;
    if (tile == 0x6F3 || tile == 0x6F7) return 1;
    if (tile >= 0x6E0 && tile < 0x6EA) return x < 0x100 ? -1 : 1;   // digits: lives left, apples / gems right
    return -1;
}

// The sprite's "screen-fixed object" mark (CAVE_SCRCLIP): bit 7 of the SAT entry's size byte.
bool screen_fixed(const VDP& v, int index) {
    const uint32_t sat = (v.reg[5] & 0x7E) << 9;
    return (v.vram[(sat + index * 8 + 2) & 0xFFFF] & 0x80) != 0;
}

// Sprites move with the slid view (the renderer subtracts view_shift), but the HUD stays anchored to the screen's
// edges and messages ("FIND THE SCARAB", PAUSED, ...) stay where the game puts them on the screen. (Only in a frame
// drawn wide.)
// (The Rug Ride's view is slid for good, its 4:3 screen the view's right part: its screen-fixed sprites - the Genie's
// hand signs - and the HUD unanchored stay on that part, where the 4:3 game has them.)
int sprite_xoff(const MegaDrive& md, int index, uint16_t attr, int x, int y) {
    (void)y;
    const VDP& v = md.vdp;
    const bool anchor = (v.hook_flags & 1) != 0, ride = rug_ride(md);
    const int side = hud_side(attr & 0x7FF, x);
    if (side) return anchor ? v.view_shift + side * v.ext : ride ? 0 : v.view_shift;
    return screen_fixed(v, index) && !ride ? v.view_shift : 0;
}

// HUD sprites stay visible over the level-edge masks.
bool sprite_is_hud(const MegaDrive& md, int index) {
    const VDP& v = md.vdp;
    const uint32_t sat = (v.reg[5] & 0x7E) << 9;
    return hud_side(v.vram16(sat + index * 8 + 4) & 0x7FF, v.vram16(sat + index * 8 + 6) & 0x1FF) != 0;
}

// The line's horizontal scroll for a plane (H scroll table, reg 11 mode).
int hscroll_offset(const VDP& v, int line) {
    switch (v.reg[11] & 3) {
    case 0: return 0;
    case 1: return (line & 7) * 4;
    case 2: return (line & ~7) * 4;
    default: return line * 4;
    }
}
int line_hscroll(const VDP& v, int line, bool plane_b) {
    const uint32_t hsb = (v.reg[13] & 0x3F) << 10;
    return v.vram16(hsb + hscroll_offset(v, line) + (plane_b ? 2 : 0)) & 0x3FF;
}

// The map plane's scroll on a line: the map x shown at screen x 0 (before the slide) and the map y of the line. The
// plane holds map x [L, L+368) and y [Y, Y+256): the map position a screen pixel shows is the one, among those
// congruent mod 512 / 256 with its plane position, inside that range.
struct LineView { int mx0, my; };
LineView line_view(const MegaDrive& md, int line, bool plane_b) {
    const VDP& v = md.vdp;
    const int hs = line_hscroll(v, line, plane_b);
    const int vs = v.vsram[plane_b ? 1 : 0] & 0x3FF;
    const int L = md.ram16(STREAM_X), Y = md.ram16(STREAM_Y);
    LineView lv;
    lv.mx0 = L + ((-hs - L) & 511);
    if (lv.mx0 >= L + 384) lv.mx0 -= 512;
    lv.my = Y + ((line + vs - Y) & 255);
    return lv;
}

// Pixels of the map plane outside the columns the game keeps valid ([L, L+368)) drawn from the RAM map; buf index x
// shows map x mx0 + x - E (mx0 including the slide).
void draw_map_ext(const MegaDrive& md, int mx0, int my, uint8_t* buf) {
    const VDP& v = md.vdp;
    const int E = v.ext, TW = 320 + 2 * E;
    const int W = md.ram16(MAP_W), H = md.ram16(MAP_H), L = md.ram16(STREAM_X);
    if (my < 0 || my >= H * 16) return;
    const uint32_t row = md.ram32(MAP_ROWS + 64 * (my >> 4)) & 0xFFFFFF;
    const uint32_t defs = md.ram32(MAP_DEFS) & 0xFFFFFF;
    const int cell_row = ((my >> 3) & 1) * 4;
    uint8_t px[8];
    // the screen run [a, b) that the plane holds; draw left and right of it
    const int a = std::clamp(L - mx0 + E, 0, TW), b = std::clamp(L + 368 - mx0 + E, 0, TW);
    for (int side = 0; side < 2; side++) {
        const int x0 = side ? b : 0, x1 = side ? TW : a;
        int x = x0;
        while (x < x1) {
            const int mx = mx0 + x - E;
            const int n = std::min(8 - (mx & 7), x1 - x);
            if (mx >= 0 && mx < W * 16) {
                const uint16_t blk = md.ram16(row + 2 * (mx >> 4));
                const uint32_t ad = defs + blk + cell_row + ((mx >> 3) & 1) * 2;
                const uint16_t tile = ad >= 0xFF0000 ? md.ram16(ad) : md.rom16(ad);
                v.pattern_row(tile, my & 7, px);
                memcpy(buf + x, px + (mx & 7), n);
            }
            x += n;
        }
    }
}

// The level's scroll routine (header +$34, $FF7DA4) run on a copy of RAM with the camera moved, on a CPU of its own whose
// VDP writes only fill a copy of the H scroll table: the values the game would write for that camera. Plane B's
// parallax rows (and Sultan's Dungeon's picture on plane A) then slide by exactly what they would move if the camera
// were there, so they hold still with the level while the view slides.
class ScrollProbe {
public:
    static constexpr int kWords = VDP::kMaxLines * 2;   // every line the VDP can show (240) x 2 words (A, B)
    uint16_t table[kWords];

    ScrollProbe() {
        cpu.ctx = this;
        cpu.rd8 = [](void*, uint32_t) -> uint8_t { return 0; };      // (VDP status, Z80 bus: free at once)
        cpu.rd16 = [](void*, uint32_t) -> uint16_t { return 0; };
        cpu.wr8 = [](void*, uint32_t, uint8_t) {};
        cpu.wr16 = [](void* c, uint32_t a, uint16_t v) { ((ScrollProbe*)c)->write(a, v); };
        cpu.trace_fn = [](void* c, uint32_t pc) { if ((pc & 0xFFFFFF) == 0) ((M68k*)c)->end_run(); };
        cpu.trace_ctx = &cpu;
    }

    // The table the routine writes with camera X `camx`; false if it didn't return.
    bool run(const MegaDrive& md, int camx) {
        memcpy(ram, md.ram, sizeof(ram));
        ram[CAM_X & 0xFFFF] = (uint8_t)(camx >> 8);
        ram[(CAM_X & 0xFFFF) + 1] = (uint8_t)camx;
        base = (md.vdp.reg[13] & 0x3F) << 10;
        for (int i = 0; i < kWords; i++) table[i] = md.vdp.vram16(base + 2 * i);
        for (int p = 0; p < 256; p++) cpu.page_read[p] = cpu.page_write[p] = nullptr;
        for (int p = 0; p < 0x40 && !md.rom.empty(); p++) cpu.page_read[p] = const_cast<uint8_t*>(&md.rom[((size_t)p << 16) % md.rom.size()]);
        for (int p = 0xE0; p < 0x100; p++) cpu.page_read[p] = cpu.page_write[p] = ram;
        inc = 2; pending = false; code = 0; addr = 0;
        cpu.set_sr(0x2700);
        cpu.stopped = false;
        cpu.irq_level = 0;
        cpu.r[15] = 0xFFEF00;
        cpu.write32(cpu.r[15] -= 4, 0);   // returns to address 0 (trace_fn stops there)
        cpu.pc = md.ram32(0xFF7DA4) & 0xFFFFFF;
        for (int used = 0; (cpu.pc & 0xFFFFFF) != 0 && used < 200000;) {
            const int n = cpu.run(2000);
            used += n;
            if (n == 0) break;
        }
        return (cpu.pc & 0xFFFFFF) == 0;
    }

private:
    M68k cpu;
    uint8_t ram[0x10000];
    uint32_t base = 0xF000, addr = 0;
    int inc = 2;
    bool pending = false;
    uint16_t first = 0;
    uint8_t code = 0;

    void write(uint32_t a, uint16_t v) {
        a &= 0xFFFFFF;
        if (a < 0xC00000 || a > 0xC00007) return;
        if (a >= 0xC00004) {                                           // control port
            if (!pending && (v & 0xC000) == 0x8000) { if (((v >> 8) & 0x1F) == 15) inc = v & 0xFF; return; }
            if (!pending) { first = v; pending = true; return; }
            pending = false;
            addr = (first & 0x3FFF) | ((uint32_t)(v & 3) << 14);
            code = (uint8_t)(((first >> 14) & 3) | ((v >> 2) & 0x3C));
            return;
        }
        if ((code & 0x0F) == 1 && addr >= base && addr < base + 2 * kWords) table[(addr - base) >> 1] = v;   // VRAM
        addr = (addr + inc) & 0xFFFF;
    }
};

struct RideView;

// Per-session widescreen state the VDP hooks work with (VDP::hook_host).
struct Wide {
    MegaDrive* md = nullptr;
    ScrollProbe probe;
    int16_t shift_a[VDP::kMaxLines] = {};   // plane A's slide per line when it isn't the view's (Sultan's Dungeon)
    bool shift_a_on = false;
    bool active = false;                    // this frame is drawn wide (the slide applies)
    int cam = 0;                            // the camera the slide in force was worked out for
    Boss boss;                              // this frame's boss framing (boss_frame; kept in module_state)
    int ease = 0;                           // and what is left of its last change (slide_total's e)
    uint16_t t0[ScrollProbe::kWords];
    RideView* ride = nullptr;               // the Rug Ride's (ride_frame, ride_line)
};

// The slide in force for camera camx (this frame's boss framing)
int wide_slide(const Wide& w, int camx) {
    return slide_total(camx, w.md->ram16(MAP_W), w.md->vdp.ext, w.boss, w.ease);
}

void boss_frame(Wide& w, bool advance);
void ride_frame(Wide& w);
void ride_line(Wide& w, int line, uint8_t* plane_b, uint8_t* sprites);

// The camera a line was scrolled with: from the map plane's H scroll (the camera in RAM may already be the next
// frame's), less the earthquake shake the Escape adds to the planes and the sprites alike ($FFF082, written with the
// scroll): the slide follows the camera, the shake stays on screen as in the original.
int line_camera(const MegaDrive& md, int line, bool swapped) {
    return line_view(md, line, swapped).mx0 - 16 - (int16_t)md.ram16(SHAKE_X);
}

// The slide for camera camx from line `from` on: view_shift for plane A and the sprites, and the other plane's per line
// from the scroll probe (the scroll the game writes at camx + s against at camx: anything but the camera is the same for
// both runs).
void apply_slide(Wide& w, int camx, int from) {
    MegaDrive& md = *w.md;
    VDP& v = md.vdp;
    const bool swapped = md.ram8(SWAP_PLANES) != 0;
    const int s = wide_slide(w, camx);
    w.cam = camx;
    v.view_shift = s;
    bool probed = s != 0 && w.probe.run(md, camx);
    if (probed) {
        memcpy(w.t0, w.probe.table, sizeof(w.t0));
        probed = w.probe.run(md, camx + s);
    }
    auto slide = [&](int line, bool plane_b) {
        if (!probed) return s;
        const int k = hscroll_offset(v, line) / 2 + (plane_b ? 1 : 0);
        if (k < 0 || k >= ScrollProbe::kWords) return s;
        int d = (w.probe.table[k] - w.t0[k]) & 0x1FF;   // the H scroll falls as the plane moves right
        if (d >= 256) d -= 512;
        return -d;
    };
    for (int ln = from; ln < VDP::kMaxLines; ln++) {
        v.view_shift_b[ln] = (int16_t)(swapped ? s : slide(ln, true));
        w.shift_a[ln] = (int16_t)(swapped ? slide(ln, false) : s);
    }
    v.view_shift_b_on = true;
    w.shift_a_on = swapped;
}

// Before each frame is drawn (MegaDrive::on_frame_start, `advance`): the boss framing moves on a frame; whether the
// frame is drawn wide (for the whole frame), and its slide from line 0's scroll. (Without `advance`, after a width change
// or a loaded state: the same frame again.)
void update_view(Wide& w, bool advance) {
    MegaDrive& md = *w.md;
    VDP& v = md.vdp;
    boss_frame(w, advance);
    v.view_shift = 0;
    v.view_shift_b_on = false;
    w.shift_a_on = false;
    w.active = v.ext != 0 && wide_view(md);
    if (w.active && rug_ride(md)) v.view_shift = -v.ext;   // (every plane and line alike: the 4:3 picture as it is)
    else if (w.active) apply_slide(w, line_camera(md, 0, md.ram8(SWAP_PLANES) != 0), 0);
    ride_frame(w);
}

// Before each line is drawn: the slide follows the scroll line by line. On a lag frame the game writes the scroll (and
// the sprite table) while the frame is drawn, and a slide worked out once at the frame's start would hold the old
// camera against the new scroll: a one-frame jump of the picture the slide holds still.
void pre_line_hook(void* host, int line) {
    Wide& w = *(Wide*)host;
    if (!w.active || rug_ride(*w.md)) return;
    MegaDrive& md = *w.md;
    const int c = line_camera(md, line, md.ram8(SWAP_PLANES) != 0);
    if (c != w.cam) apply_slide(w, c, line);
}

// ---------------------------------------------------------------------------------------------------------------------
// The boss tune. Four map object types start BOSS TUNE (50) from their spawn handler ($4154 table), right after
// spawning: $18 -> $1B6C5A (Agrabah Rooftops' end), $28 -> $1B6D1E (Rooftops, while Aladdin's Y is in [$2AD, $2F4)),
// $4D -> $1B6CCE (Jafar's Palace), $F5 -> $1B6D90 (the Cave of Wonders' golden idol: the pedestal at block column 129,
// row 29). The 4:3 game spawns them as their block enters its plane window - columns L/16 .. L/16 + 22, rows Y/16 ..
// Y/16 + 15 - so the tune starts as the arena is about to come into its view. The widened spawning (the caves' band
// columns, the widened rows) brings that moment up to 128 px earlier at 21:9, and from places the 4:3 view never
// starts it: in the Cave of Wonders, up on the ledges right of and above the idol's arena, as the camera came down a
// little the widened row reached the pedestal at the bottom-left corner of the view and the boss tune started with
// Aladdin nowhere near the fight. So a boss object spawned outside the 4:3 window keeps everything else (it is there,
// with its palette and its animation, as the wider view needs) but its tune is held (the handler's stop-all and start
// skipped) until its block lies in the 4:3 window - when the 4:3 game would have spawned it and started the tune.
// Then the module queues the same commands (stop all; BOSS TUNE if the music option is on, $FFF57F) right after the
// gameplay loop's streaming ($1A8CE8, after `bsr $1AAA2A`), in the frame the 4:3 game's would have spawned it. Held entries (slot, block column / row) live in module_state, so save states and rewind keep them.
constexpr uint32_t OBJECTS = 0xFF7E40, OBJ_TYPES = 0xFFAE87;   // the object table (32 x $42), the block type table
constexpr int OBJ_SIZE = 0x42;
struct BossHandler { uint32_t at, skip; };   // the handler's stop-all (movem before it) and the code after the start
const BossHandler kBossHandlers[] = {{0x1B6C64, 0x1B6C94}, {0x1B6CEC, 0x1B6D1C}, {0x1B6D52, 0x1B6D82}, {0x1B6DA4, 0x1B6DD4}};
constexpr uint32_t MUSIC_ON = 0xFFF57F;
constexpr uint32_t PC_STREAMED = 0x1A8CE8;   // the gameplay loop, right after its column / row streaming ($1AAA2A)
constexpr uint8_t BOSS_TUNE = 50;
bool boss_type(uint8_t t) { return t == 0x18 || t == 0x28 || t == 0x4D || t == 0xF5; }

// The GEMS command FIFO in Z80 RAM ($1B40, 64 bytes, write index at $0036): $FF, command, arguments ($1E57F4).
void gems_command(MegaDrive& md, std::initializer_list<uint8_t> bytes) {
    uint8_t i = md.zram[0x0036];
    md.zram[0x1B40 + i] = 0xFF;
    i = (i + 1) & 0x3F;
    for (uint8_t b : bytes) {
        md.zram[0x1B40 + i] = b;
        i = (i + 1) & 0x3F;
    }
    md.zram[0x0036] = i;
}

bool in_43_window(const MegaDrive& md, int col, int row) {
    const int L = md.ram16(STREAM_X) >> 4, Y = md.ram16(STREAM_Y) >> 4;
    return col >= L && col <= L + 22 && row >= Y && row <= Y + 15;
}

// At a boss handler's stop-all (a5 = the object it spawned at its block: x = 16 col - 16, y = 16 row + 240)
void boss_tune_spawned(MegaDrive& md, const BossHandler& h) {
    const uint32_t o = md.cpu.r[13] & 0xFFFFFF;
    const int col = ((int16_t)md.ram16(o + 2) + 16) >> 4, row = ((int16_t)md.ram16(o + 4) - 240) >> 4;
    if (o < OBJECTS || o >= OBJECTS + 32 * OBJ_SIZE || in_43_window(md, col, row)) return;   // (as the 4:3 game)
    for (int k = 0; k < MS_BOSS_N; k++) {
        uint32_t& e = md.module_state[MS_BOSS + k];
        if (e) continue;
        e = 0x80000000u | (uint32_t)((o - OBJECTS) / OBJ_SIZE) << 24 | (uint32_t)(row & 0xFF) << 16 | (uint32_t)(col & 0xFFFF);
        md.cpu.pc = h.skip;
        return;
    }
}

// After the gameplay loop's streaming: a held tune whose block has come into the 4:3 window starts; one whose object is
// gone (despawned, the level left) is dropped.
void boss_tune_held(MegaDrive& md) {
    for (int k = 0; k < MS_BOSS_N; k++) {
        uint32_t& e = md.module_state[MS_BOSS + k];
        if (!e) continue;
        const uint32_t o = OBJECTS + ((e >> 24) & 0x1F) * OBJ_SIZE;
        if (!md.ram8(o) || !boss_type(md.ram8(o + 0x34))) { e = 0; continue; }
        if (!in_43_window(md, (int)(e & 0xFFFF), (int)((e >> 16) & 0xFF))) continue;
        gems_command(md, {0x16});
        if (md.ram8(MUSIC_ON)) {
            gems_command(md, {0x12, BOSS_TUNE});
            gems_command(md, {0x10, BOSS_TUNE});
        }
        e = 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Palette line 2. Several kinds of map object draw their sprites in CRAM line 2 and load their colours into it from
// their spawn handler ($1B2650, the source kept at $FF726A for fades; not while a level start or a restart fills the
// plane, $FFF175): the bats and the like ($1292B2), the Cave of Wonders' golden idol and its pedestals ($1292F2),
// Jafar's Palace's statues ($1294F2), ... Whichever spawned last colours them all; the 4:3 game's levels are laid out so
// that it is the one on screen. The widened spawning brings objects in up to 2E px earlier (and from rows the 4:3 game
// doesn't reach), so one could load its colours while another kind is on screen: in the idol's arena at 21:9, moving
// left brings the bat at block (109, 36) into the spawn band, and the idol and its pedestals took the bat's dark red,
// yellow and magenta for the rest of the fight (the 4:3 game's window never reaches that bat from the arena). So when a
// map object spawned outside the 4:3 window would load other colours while an object of the kind that loaded the
// line's colours is on screen (a boss: alive), the handler's call is skipped and the load held; the object is drawn
// in the line's colours meanwhile. Right after the gameplay loop's streaming (as the boss tune) a held load is done when
// the object's block comes into the 4:3 window - when the 4:3 game would spawn it and load them - or once no object of
// the line's kind is alive any more (a boss's colours: none on screen, so the boss doesn't come in in another's colours
// while one of those lingers in view). A load let through outside the 4:3 window is noted too and done again at the
// object's 4:3 moment if another kind has loaded its colours since (as the 4:3 game, which loads them in that order):
// the Cave's leaping fish ($3E) loaded early under the arena, a bat's held load done at its 4:3 moment, and the fish
// stayed in the bat's colours. Held and early loads: module_state[MS_PAL2] (line2_entry).
struct Line2Load { uint32_t ret; uint32_t pal; uint8_t types[5]; };   // a spawn handler's call (return address)
const Line2Load kLine2Loads[] = {
    {0x1B6C2C, 0x1292B2, {0x23, 0x24}}, {0x1B6C4C, 0x1292B2, {0x25}}, {0x1B75F4, 0x1292B2, {0x2D, 0x2E, 0x2F, 0x4E, 0x4F}},
    {0x1B6DE8, 0x1292F2, {0xF5}}, {0x1B6E14, 0x1292F2, {0xF6}}, {0x1B6E40, 0x1292F2, {0xF7}}, {0x1B6E6C, 0x1292F2, {0xF8}},
    {0x1B6FAC, 0x129532, {0x3E}}, {0x1B6FD8, 0x1294F2, {0x3C}}, {0x1B6CCC, 0x1295F2, {0x1F, 0x81}},
    {0x1B6CE6, 0x1295D2, {0x4D}}, {0x1B6792, 0x129612, {0x8F}}, {0x1B6D52, 0x129152, {0x28}},
};
const uint32_t kLine2Pals[8] = {0x1292B2, 0x1292F2, 0x129532, 0x1294F2, 0x1295F2, 0x1295D2, 0x129612, 0x129152};
constexpr uint32_t PC_LOAD_LINE2 = 0x1B2650, LINE2_SRC = 0xFF726A;

// Whether two of those palettes are the same colours ($1294F2 and $129532 are)
bool same_line2(const MegaDrive& md, uint32_t a, uint32_t b) {
    if (a == b) return true;
    if (a + 32 > md.rom.size() || b + 32 > md.rom.size()) return false;
    for (int i = 0; i < 16; i++) if (md.rom16(a + 2 * i) != md.rom16(b + 2 * i)) return false;
    return true;
}

// The colours a map object type loads into line 2 (0: none)
uint32_t line2_pal(uint8_t type) {
    for (const Line2Load& l : kLine2Loads)
        for (uint8_t t : l.types) if (t && t == type) return l.pal;
    return 0;
}

// A boss's colours: the golden idol (and its pedestals), Jafar in his palace, the Rooftops' boss
bool line2_boss(uint32_t pal) { return pal == 0x1292F2 || pal == 0x1295D2 || pal == 0x129152; }

int line2_index(uint32_t pal) {
    for (int i = 0; i < 8; i++) if (kLine2Pals[i] == pal) return i;
    return -1;
}

// The objects whose colours go into line 2 at another moment than the 4:3 game's: three entries of 10 bits in
// module_state[MS_PAL2] - slot (>= 1), palette index << 5, bit 8: loaded already (before its 4:3 moment), else held.
constexpr int L2_N = 3;
struct Line2Entry { uint32_t obj = 0, pal = 0; bool early = false; };
Line2Entry line2_entry(uint32_t st, int k) {
    const uint32_t e = (st >> (10 * k)) & 0x3FF;
    Line2Entry r;
    if (e & 31) { r.obj = OBJECTS + (e & 31) * OBJ_SIZE; r.pal = kLine2Pals[(e >> 5) & 7]; r.early = (e & 0x100) != 0; }
    return r;
}
void line2_set(uint32_t& st, int k, uint32_t o, uint32_t pal, bool early) {
    const uint32_t e = o ? ((o - OBJECTS) / OBJ_SIZE | (uint32_t)line2_index(pal) << 5 | (early ? 0x100u : 0u)) : 0u;
    st = (st & ~(0x3FFu << (10 * k))) | e << (10 * k);
}
bool line2_held(uint32_t st, uint32_t o) {
    for (int k = 0; k < L2_N; k++) {
        const Line2Entry e = line2_entry(st, k);
        if (e.obj == o && !e.early) return true;
    }
    return false;
}

void load_line2(MegaDrive& md, uint32_t pal) {   // as $1B2650: CRAM 32-47 (stored BGR333), the source at $FF726A
    for (int i = 0; i < 16; i++) {
        const uint16_t c = md.rom16(pal + 2 * i);
        md.vdp.cram[32 + i] = (uint16_t)((((c >> 9) & 7) << 6) | (((c >> 5) & 7) << 3) | ((c >> 1) & 7));
    }
    md.set_ram32(LINE2_SRC, pal);
}

// Whether a piece of the object is in the sprite table being shown and inside the view as slid.
bool object_in_view(const MegaDrive& md, uint32_t o) {
    const uint32_t lo = md.ram32(o + 0x2E), hi = lo + (md.ram8(o + 0x29) + 1u) * 0x80;
    if (!lo) return false;
    const VDP& v = md.vdp;
    const int E = v.ext, s = v.view_shift;
    const uint32_t sat = (uint32_t)(v.reg[5] & 0x7E) << 9;
    for (int e = 0, n = 0; n < 80; n++) {
        const uint32_t a = sat + e * 8;
        const uint32_t t = (uint32_t)(v.vram16(a + 4) & 0x7FF) * 32;
        if (t >= lo && t < hi) {
            const uint8_t size = v.vram[(a + 2) & 0xFFFF];
            int x = v.vram16(a + 6) & (v.sprite_x10 ? 0x3FF : 0x1FF);
            if (v.sprite_x10 && x >= 0x300) x -= 0x400;
            x -= 128;
            const int y = (v.vram16(a) & 0x3FF) - 128, w = (((size >> 2) & 3) + 1) * 8, h = ((size & 3) + 1) * 8;
            if (x + w > -E + s && x < 320 + E + s && y + h > 0 && y < 224) return true;
        }
        e = v.vram[(a + 3) & 0xFFFF] & 0x7F;
        if (!e) break;
    }
    return false;
}

// Whether an object alive (other than the held ones) is of a kind that loaded these colours - and, with `shown`, has a
// piece in the view.
bool line2_in_use(const MegaDrive& md, uint32_t pal, uint32_t st, bool shown = false) {
    for (int i = 1; i < 32; i++) {
        const uint32_t o = OBJECTS + i * OBJ_SIZE;
        const uint32_t p = line2_pal(md.ram8(o + 0x34));
        if (!md.ram8(o) || !p || !same_line2(md, p, pal) || line2_held(st, o)) continue;
        if (!shown || object_in_view(md, o)) return true;
    }
    return false;
}

// The map block an object was spawned from: its block word (+$32 = word / 2) is unique in the map (row-major at $FF0000).
bool object_block(const MegaDrive& md, uint32_t o, int& col, int& row) {
    const uint16_t w = (uint16_t)(md.ram16(o + 0x32) * 2);
    const int W = md.ram16(MAP_W), H = md.ram16(MAP_H);
    for (int i = 0; w && i < W * H && i < 0x8000; i++)
        if (md.ram16(0xFF0000 + 2 * i) == w) { col = i % W; row = i / W; return true; }
    return false;
}

// At $1B2650 (a0 = the colours, a5 = the object), from a map object's spawn handler outside the 4:3 window with other
// colours than the line's: with an object of the kind that loaded those on screen (a boss's: alive) the call is skipped
// and the load held, else it loads now and is noted as early, to be done again at its 4:3 moment if the line has other
// colours by then.
void line2_load(MegaDrive& md) {
    const uint32_t sp = md.cpu.r[15] & 0xFFFFFF, ret = md.ram32(sp) & 0xFFFFFF;
    const uint32_t o = md.cpu.r[13] & 0xFFFFFF, pal = md.cpu.r[8] & 0xFFFFFF;
    const Line2Load* site = nullptr;
    for (const Line2Load& l : kLine2Loads) if (l.ret == ret) site = &l;
    if (!site || pal != site->pal || o <= OBJECTS || o >= OBJECTS + 32 * OBJ_SIZE || (o - OBJECTS) % OBJ_SIZE) return;
    uint32_t& st = md.module_state[MS_PAL2];
    int k = -1;   // its entry (a new object in a slot an entry was left for: that one is over), else a free one
    for (int i = 0; i < L2_N && k < 0; i++) if (line2_entry(st, i).obj == o) k = i;
    if (k >= 0) line2_set(st, k, 0, 0, false);
    for (int i = 0; i < L2_N && k < 0; i++) if (!line2_entry(st, i).obj) k = i;
    const int col = ((int16_t)md.ram16(o + 2) + 16) >> 4, row = ((int16_t)md.ram16(o + 4) - 240) >> 4;
    const uint32_t cur = md.ram32(LINE2_SRC) & 0xFFFFFF;
    if (in_43_window(md, col, row) || same_line2(md, pal, cur) || k < 0) return;   // (as the 4:3 game)
    if (!line2_in_use(md, cur, st, !line2_boss(cur))) { line2_set(st, k, o, pal, true); return; }
    line2_set(st, k, o, pal, false);
    md.cpu.pc = ret;            // rts
    md.cpu.r[15] = sp + 4;
}

// After the gameplay loop's streaming. Entries whose object is gone are dropped (a held one whose colours the line has
// counts as loaded early). One whose block has come into the 4:3 window - when the 4:3 game would spawn the object and
// load its colours - loads them if the line has others, and is done. Then a held one whose line is free (no object of
// the line's kind alive; for a boss's colours, none on screen) loads, noted as early.
void line2_moments(MegaDrive& md) {
    uint32_t& st = md.module_state[MS_PAL2];
    if (!st) return;
    auto tidy = [&]() {
        const uint32_t cur = md.ram32(LINE2_SRC) & 0xFFFFFF;
        for (int k = 0; k < L2_N; k++) {
            const Line2Entry e = line2_entry(st, k);
            if (!e.obj) continue;
            if (!md.ram8(e.obj) || line2_pal(md.ram8(e.obj + 0x34)) != e.pal) line2_set(st, k, 0, 0, false);
            else if (!e.early && same_line2(md, e.pal, cur)) line2_set(st, k, e.obj, e.pal, true);
        }
    };
    tidy();
    for (int k = 0; k < L2_N; k++) {
        const Line2Entry e = line2_entry(st, k);
        int col, row;
        if (!e.obj || !object_block(md, e.obj, col, row) || !in_43_window(md, col, row)) continue;
        if (!same_line2(md, e.pal, md.ram32(LINE2_SRC) & 0xFFFFFF)) load_line2(md, e.pal);
        line2_set(st, k, 0, 0, false);
        tidy();
    }
    for (int k = 0; k < L2_N; k++) {
        const Line2Entry e = line2_entry(st, k);
        if (!e.obj || e.early || line2_in_use(md, md.ram32(LINE2_SRC) & 0xFFFFFF, st, line2_boss(e.pal))) continue;
        load_line2(md, e.pal);
        line2_set(st, k, e.obj, e.pal, true);
        tidy();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Boss fights framed (slide_target). A fight is on while a boss object is alive - one of the four types whose spawn
// starts BOSS TUNE (boss_type), the game's own mark of a boss fight - and its tune is not held back (its block has come
// into the 4:3 window, or it spawned there: when the 4:3 game starts the fight). Its arena: the boss's map block and
// those of the kinds that share its colours within a plane window of it - the golden idol's pedestal ($F5, block
// 129,29: the fight's left end) and the other one ($F6, 143,32), between which the idol flies; Jafar alone; at the
// Rooftops' end the boss's block. The arena's centre (map x 8 x (first + last block column)) is framed as the 4:3
// camera would centre it: camera 8 x (first + last) - 176 (the idol: 2000, the middle of the 4:3 camera's 1821-2178 in
// the fight; Jafar: 640, his hall 252-947). A fight starting or ending changes the slide's target at once; the
// difference is kept and eased out (at most 3 px a frame), so the view doesn't lurch. module_state[MS_DEV] bits 2-23.
constexpr uint32_t BF_ON = 1u << 2, BF_MASK = 0x00FFFFFCu;
constexpr int BF_SUM = 3, BF_EASE = 14;

bool boss_held(const MegaDrive& md, int slot) {
    for (int k = 0; k < MS_BOSS_N; k++) {
        const uint32_t e = md.module_state[MS_BOSS + k];
        if (e && (int)((e >> 24) & 0x1F) == slot) return true;
    }
    return false;
}

// The boss fight on (from RAM), and its arena's block column sum
Boss boss_arena(const MegaDrive& md, int& sum) {
    Boss b;
    for (int i = 1; i < 32 && !b.on; i++) {
        const uint32_t o = OBJECTS + i * OBJ_SIZE;
        const uint8_t type = md.ram8(o + 0x34);
        int col, row;
        if (!md.ram8(o) || !boss_type(type) || boss_held(md, i) || !object_block(md, o, col, row)) continue;
        int lo = col, hi = col, top = row, bottom = row;
        auto add = [&](int c, int r) { lo = std::min(lo, c), hi = std::max(hi, c), top = std::min(top, r), bottom = std::max(bottom, r); };
        const uint32_t pal = line2_pal(type);
        auto kin = [&](uint8_t t) { const uint32_t p = line2_pal(t); return pal && p && same_line2(md, p, pal); };
        auto close_by = [&](int c, int r) { return std::abs(c - col) <= 23 && std::abs(r - row) <= 16; };
        if (pal) {
            for (int k = 1; k < 32; k++) {   // its kin alive (a spawned object's block type is cleared while it lives)
                const uint32_t q = OBJECTS + k * OBJ_SIZE;
                int c, r;
                if (q != o && md.ram8(q) && kin(md.ram8(q + 0x34)) && object_block(md, q, c, r) && close_by(c, r)) add(c, r);
            }
            const int W = md.ram16(MAP_W), H = md.ram16(MAP_H);   // and still in the map
            for (int r = std::max(0, row - 16); r <= std::min(H - 1, row + 16); r++) {
                const uint32_t rp = md.ram32(MAP_ROWS + 64 * r) & 0xFFFFFF;
                for (int c = std::max(0, col - 23); c <= std::min(W - 1, col + 23); c++) {
                    const uint16_t wd = md.ram16(rp + 2 * c);
                    if (wd && kin(md.ram8(OBJ_TYPES + wd / 2))) add(c, r);
                }
            }
        }
        // only with the camera at the arena's height (within half a screen): the boss stays alive with Aladdin fallen
        // into the Cave's river 300 px below, where nothing of the fight is in view
        const int camy = (int16_t)md.ram16(CAM_Y);
        if (camy + 240 <= 16 * top - 112 || camy + 16 >= 16 * bottom + 16 + 112) continue;
        sum = std::min(lo + hi, 0x7FF);
        b.on = true;
        b.centre = 8 * sum - 176;
    }
    return b;
}

// Once a frame (advance) the framing is worked out again and a change of target is kept in the remainder, which eases
// out; this frame's framing for the slide (Wide::boss / ease).
void boss_frame(Wide& w, bool advance) {
    MegaDrive& md = *w.md;
    uint32_t& st = md.module_state[MS_DEV];
    Boss b;
    b.on = (st & BF_ON) != 0;
    int sum = (int)((st >> BF_SUM) & 0x7FF);
    b.centre = 8 * sum - 176;
    int e = (int)((st >> BF_EASE) & 0x3FF);
    if (e & 0x200) e -= 0x400;
    const int E = md.vdp.ext;
    if (advance && E > 0) {
        int sum2 = 0;
        const Boss now = attract(md) ? Boss() : boss_arena(md, sum2);
        const int W = md.ram16(MAP_W), cam = line_camera(md, 0, md.ram8(SWAP_PLANES) != 0);
        e = std::clamp(e + slide_target(cam, W, E, b) - slide_target(cam, W, E, now), -2 * E, 2 * E);
        const int step = std::clamp((std::abs(e) + 15) / 16, 1, 3);
        e = e > 0 ? std::max(0, e - step) : std::min(0, e + step);
        b = now;
        sum = now.on ? sum2 : 0;
        st = (st & ~BF_MASK) | (b.on ? BF_ON : 0u) | (uint32_t)sum << BF_SUM | ((uint32_t)e & 0x3FF) << BF_EASE;
    }
    w.boss = b;
    w.ease = e;
}

// The spawn caves' reach (CAVE_REACH): kl / kr block columns from the slide in force
void band_reach(const MegaDrive& md, const Wide& w, int& kl, int& kr) {
    const int E = md.vdp.ext, s = wide_slide(w, (int16_t)md.ram16(CAM_X));
    kl = (E + 15 - s) >> 4;
    kr = (E + 15 + s) >> 4;
}
void reach_hook(MegaDrive& md, const Wide& w) {
    int kl, kr;
    band_reach(md, w, kl, kr);
    md.cpu.r[0] = (md.cpu.r[0] & 0xFFFF0000u) | (uint32_t)kl;
    md.cpu.r[1] = (md.cpu.r[1] & 0xFFFF0000u) | (uint32_t)kr;
}

// While a boss framing is in force (or easing out) the view can run ahead of the camera, and the band's outer edge with
// it, between column steps: right after the gameplay loop's streaming ($1A8CE8) the two outermost columns of the band on
// each side are spawned as a column step would spawn them (CAVE_REFRESH, the return past the replaced jsr pushed with
// the two column ranges; blocks already spawned are cleared or alive, so this only brings in what is missing).
void band_refresh(MegaDrive& md, const Wide& w) {
    if (!w.boss.on && w.ease == 0) return;
    int kl, kr;
    band_reach(md, w, kl, kr);
    const int L = md.ram16(STREAM_X) >> 4, W = md.ram16(MAP_W);
    const int l1 = std::max(1, L - kl), l2 = std::min(L - 1, L - kl + 1);
    const int r1 = std::max(L + 23, L + 21 + kr), r2 = std::min(W - 1, L + 22 + kr);
    uint32_t sp = md.cpu.r[15];
    md.set_ram32(sp -= 4, PC_STREAMED + 6);
    md.set_ram16(sp -= 2, (uint16_t)r2);
    md.set_ram16(sp -= 2, (uint16_t)r1);
    md.set_ram16(sp -= 2, (uint16_t)l2);
    md.set_ram16(sp -= 2, (uint16_t)l1);
    md.cpu.r[15] = sp;
    md.cpu.pc = CAVE_REFRESH;
}

// ---------------------------------------------------------------------------------------------------------------------
// Objects that keep their map block. Most spawn handlers spawn through $1B526C, which clears the object's map block
// (and the despawn puts it back), so a column streamed again spawns nothing twice. Three kinds spawn through $1B52A0,
// which leaves it: the stepping stones ($EF: the Cave of Wonders' river, the Escape's lava rocks; $F4, the one the monkey
// statue $35 raises) and the apples that come while Aladdin has none ($61, four from one block). A stone sinks when
// stood on and is gone (its object cleared), to come back when its column is streamed again. The 4:3 game streams a
// column as it comes into its plane window at an edge, just outside the view (and spawns a second stone over one still
// there when the camera crosses that edge back and forth). The widened spawning goes over the whole band beyond the 4:3
// window at every column step (objects that found no room are tried again), so such an object came again at every step
// its column spent in the band: in the river at 21:9, up to 10 stones on one block, the object slots full (the stone at
// block 156,59 never came: in the QA's screenshot nothing beyond the rock outcrop, where it belongs), and a stone that
// sank could rise again in the extension. So:
// - a block whose object is alive is not spawned again (the spawners' handler call is skipped: $1AE46C, $1AE4E8);
// - a column spawn through $1B52A0 goes ahead only where its column lies entirely outside the view as slid, as the 4:3
//   game's edge columns do - not in the band's columns inside the view - except while a level start or a restart
//   fills the plane ($FFF175: the camera runs 23 columns right and back, every block once, deduplicated);
// - room is never made by evicting one (it would only come back at its column's next entry).
constexpr uint32_t PC_COL_CALL = 0x1AE46C, PC_COL_RET = 0x1AE46E;   // the column spawner's jsr (a4) and what follows it
constexpr uint32_t PC_ROW_CALL = 0x1AE4E8, PC_ROW_RET = 0x1AE4EA;   // the row spawner's
constexpr uint32_t PC_SPAWN_KEEP = 0x1B52A0, PC_SPAWN_RTS = 0x1B529E;
constexpr uint32_t SPAWN_X = 0xFF7DB0, SPAWN_DX = 0xFFF150, FILLING = 0xFFF175;

// An object spawned from this block (+$32: the block word / 2, +$34 its type) alive
bool block_alive(const MegaDrive& md, uint16_t blk, uint8_t type) {
    for (int i = 1; i < 32; i++) {
        const uint32_t o = OBJECTS + i * OBJ_SIZE;
        if (md.ram8(o) && md.ram16(o + 0x32) == blk && md.ram8(o + 0x34) == type) return true;
    }
    return false;
}

// A spawned object whose block stays in the map (alive, its block's type still there)
bool keeps_block(const MegaDrive& md, uint32_t o) {
    return md.ram8(o) && md.ram8(o + 0x34) && md.ram8(OBJ_TYPES + (uint32_t)(int16_t)md.ram16(o + 0x32)) != 0;
}

// At a spawner's handler call (d2 = the block word / 2, d3 = its type; the call skipped by going on to the restore)
void spawn_call(MegaDrive& md, uint32_t restore) {
    if (block_alive(md, (uint16_t)md.cpu.r[2], (uint8_t)md.cpu.r[3])) md.cpu.pc = restore;
}

// At $1B52A0: from the column spawner (its handler call's return on the stack, under a helper's for $61), outside a
// fill, inside the view: not spawned (NE, as when no slot is free)
void spawn_keep(MegaDrive& md, const Wide& w) {
    if (md.ram8(FILLING)) return;
    const uint32_t sp = md.cpu.r[15] & 0xFFFFFF;
    bool column = false;
    for (uint32_t a = sp; a < sp + 16; a += 4) {
        const uint32_t v = md.ram32(a) & 0xFFFFFF;
        if (v == PC_ROW_RET) return;
        if (v == PC_COL_RET) { column = true; break; }
    }
    if (!column) return;
    const int x = ((int16_t)md.ram16(SPAWN_X) + (int16_t)md.ram16(SPAWN_DX) + 16) & ~15;   // the block column's map x
    const int camx = (int16_t)md.ram16(CAM_X), E = md.vdp.ext, s = wide_slide(w, camx);
    if (x + 16 <= camx + 16 - E + s || x >= camx + 336 + E + s) return;
    md.cpu.set_sr((uint16_t)(md.cpu.get_sr() & ~4));
    md.cpu.pc = PC_SPAWN_RTS;
}

// Palette line 2 and the fill. While a level start or a restart fills the plane ($FFF175) the spawn handlers load no
// colours (they test it before $1B2650); the 4:3 game fills its own window, and an object it spawns later loads its
// colours then. The widened band's fill spawns objects beyond that window, which so never loaded theirs: after a death
// in the Cave's river at 21:9, the leaping fish ($3E, block 139,59) came with the restart and leapt in a bat's colours
// (25 frames in the 4:3 part on the arena run from above). So, right after a handler call during a fill
// ($1AE46E / $1AE4EA, the spawner's registers still on the stack: d2 the block word / 2, d3 its type), an object of a
// line-2 kind spawned outside the 4:3 window is noted as a held load (line2_held): its colours go in at its 4:3 moment,
// or once the line is free (line2_moments).
void line2_filled(MegaDrive& md) {
    if (!md.ram8(FILLING)) return;
    const uint32_t sp = md.cpu.r[15] & 0xFFFFFF;
    const uint16_t blk = md.ram16(sp + 10);
    const uint8_t type = md.ram8(sp + 15);
    const uint32_t pal = line2_pal(type);
    if (!pal) return;
    uint32_t& st = md.module_state[MS_PAL2];
    for (int i = 1; i < 32; i++) {
        const uint32_t o = OBJECTS + i * OBJ_SIZE;
        if (!md.ram8(o) || md.ram16(o + 0x32) != blk || md.ram8(o + 0x34) != type) continue;
        const int col = ((int16_t)md.ram16(o + 2) + 16) >> 4, row = ((int16_t)md.ram16(o + 4) - 240) >> 4;
        if (in_43_window(md, col, row)) return;
        int k = -1;
        for (int j = 0; j < L2_N; j++) {
            if (line2_entry(st, j).obj == o) return;
            if (k < 0 && !line2_entry(st, j).obj) k = j;
        }
        if (k >= 0) line2_set(st, k, o, pal, false);
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The Cave of Wonders' carpet after the golden idol (docs/re_md_wide.md section 5, After the fight). The carpet is a map
// object ($F9, block 144,29, right of the far pedestal: two linked objects) whose spawn handler ($1B6B5C) spawns it only
// once the idol is beaten ($FFF112, set by its tenth hit at $1AEB36); until then its column spawns nothing and its block
// stays. The 4:3 game brings it as that column next enters its plane window: beaten from the arena's left half (camera
// below about 1952) it streams in at the right edge as Aladdin walks right; beaten beside the far pedestal its column is
// already inside the window, its place in view, and the carpet comes only once Aladdin has walked left far enough for the
// column to leave the window and back. The widened view shows that place from anywhere in the arena (at 21:9 the whole
// arena), and an empty place where the carpet belongs, until Aladdin steps right (21:9, from the left: then popping in
// 150 px inside the view) or walks away and back, read as the carpet not coming. So with the widescreen ROM the idol's
// tenth hit (PC_IDOL_BEATEN, module_state[MS_DEV] bit 24) has the carpet's block alone spawned right after the same
// iteration's streaming ($1A8CE8, CAVE_CARPET: the column spawner's loop for that block, its handler as the streaming
// calls it), wherever the view or its spawn band covers the block - not whole columns, which would bring back the
// river's stepping stones and the other objects that keep their map block. Spawned, the block's type is cleared as
// always (a later streaming of its column spawns nothing), and the spawner's hook skips a block whose object is alive.
// At 4:3 nothing changes: the original's own way.
constexpr uint32_t PC_IDOL_BEATEN = 0x1AEB36;   // st.b $FFF112: the idol's tenth hit
constexpr uint32_t IDOL_BEATEN = 0xFFF112;
constexpr uint32_t CARPET_DUE = 1u << 24;
constexpr uint8_t CARPET = 0xF9;

// After the streaming: the carpet due (cleared either way) and its block not spawned yet, in the view's or the band's
// columns and the plane's rows: push the block and the return past the replaced call, enter CAVE_CARPET. Returns
// whether it did (band_refresh waits a frame then).
bool carpet_due(MegaDrive& md, const Wide& w) {
    uint32_t& dev = md.module_state[MS_DEV];
    if (!(dev & CARPET_DUE)) return false;
    dev &= ~CARPET_DUE;
    if (!md.ram8(IDOL_BEATEN)) return false;
    int kl, kr;
    band_reach(md, w, kl, kr);
    const int W = md.ram16(MAP_W), H = md.ram16(MAP_H), L = md.ram16(STREAM_X) >> 4, Y = md.ram16(STREAM_Y) >> 4;
    for (int r = std::max(0, Y); r <= std::min(H - 1, Y + 15); r++) {
        const uint32_t row = md.ram32(MAP_ROWS + 64 * r) & 0xFFFFFF;
        for (int c = std::max(0, L - kl); c <= std::min(W - 1, L + 22 + kr); c++) {
            const uint16_t word = md.ram16(row + 2 * c);
            if (!word || md.ram8(OBJ_TYPES + word / 2) != CARPET || block_alive(md, word / 2, CARPET)) continue;
            uint32_t sp = md.cpu.r[15];
            md.set_ram32(sp -= 4, PC_STREAMED + 6);
            md.set_ram32(sp -= 4, row + 2 * c);
            md.set_ram16(sp -= 2, (uint16_t)(16 * c));
            md.set_ram16(sp -= 2, (uint16_t)(16 * r));
            md.cpu.r[15] = sp;
            md.cpu.pc = CAVE_CARPET;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Room for objects. The widened view keeps more objects alive than the game was sized for: its object slots (32 at
// $FF7E40, $42 bytes; each kind of object looks for a free one in its own range) and its sprite-tile pool (below) can
// run out where the 4:3 game's never do. An object that finds no room is not spawned (a free-slot search fails: its
// map block stays, and it comes at a later column step) or dropped (no tiles: its block is put back) - by then it can
// be well inside the view (or, an object already in view that needs new tiles for its next animation, vanish). So
// when room runs out, the module takes it from objects the view doesn't show: alive, farther outside the view as slid
// than the one asking (an object to be placed by a free-slot search: past the spawn band), and either at least 32 px
// out (the spawn band reaches 31 px) or with no sprite piece in the sprite table being shown; one the game would bring
// back from its map block (+6 bit 5, +$34: despawned, it returns when its column or row is streamed again), not
// screen-fixed (+6 bit 3), not one the game never despawns (+$3C bit 1) and not one the code at hand works on (a0-a6)
// - the farthest first, despawned the game's way ($1AE0B0, mirrored: object cleared, tiles freed, map block back, the
// same for its linked object at +$3E). Inside the view nothing changes.
constexpr int ROOM_MARGIN = 32;

// how far an object lies outside the view as slid (0: inside or touching it)
int outside_view(const MegaDrive& md, uint32_t o) {
    const int E = md.vdp.ext, s = md.vdp.view_shift;
    const int x = (int16_t)md.ram16(o + 2) - (int16_t)md.ram16(CAM_X) - 16;
    const int y = (int16_t)md.ram16(o + 4) - (int16_t)md.ram16(CAM_Y) - 240;
    return std::max({-E + s - x, x - (320 + E + s), -y, y - 224, 0});
}

// Whether any of the object's sprite tiles is in the sprite table being shown (the builder leaves out what's off the
// view, so a piece there is a piece on screen).
bool on_screen(const MegaDrive& md, uint32_t o) {
    const uint32_t lo = md.ram32(o + 0x2E), hi = lo + (md.ram8(o + 0x29) + 1u) * 0x80;
    if (!lo) return false;
    const VDP& v = md.vdp;
    const uint32_t sat = (uint32_t)(v.reg[5] & 0x7E) << 9;
    for (int e = 0, n = 0; n < 80; n++) {
        const uint32_t t = (uint32_t)(v.vram16(sat + e * 8 + 4) & 0x7FF) * 32;
        if (t >= lo && t < hi) return true;
        e = v.vram[(sat + e * 8 + 3) & 0xFFFF] & 0x7F;
        if (!e) break;
    }
    return false;
}

// An object that may be despawned to make room for one `than` px outside the view (0: inside it): farther out than
// that, and either 32 px out (past the spawn band) or with nothing of it on screen.
bool evictable(const MegaDrive& md, uint32_t o, int than) {
    if (o == OBJECTS || !md.ram8(o) || boss_type(md.ram8(o + 0x34))) return false;   // (a respawn: the tune again)
    if (keeps_block(md, o)) return false;   // (it would come back only as its column next comes in)
    const uint8_t f6 = md.ram8(o + 6);
    if ((f6 & 8) || !(f6 & 0x20) || !md.ram8(o + 0x34) || (md.ram8(o + 0x3C) & 2)) return false;
    for (int r = 8; r < 15; r++) if ((md.cpu.r[r] & 0xFFFFFF) == o) return false;
    const uint32_t link = md.ram32(o + 0x3E) & 0xFFFFFF;
    for (int r = 8; r < 15; r++) if (link && (md.cpu.r[r] & 0xFFFFFF) == link) return false;
    const int out = outside_view(md, o);
    return out > than && (out >= ROOM_MARGIN || (out > 0 && !on_screen(md, o) && !(link && on_screen(md, link))));
}

void free_tiles(MegaDrive& md, uint32_t o) {   // $1AE372
    const uint32_t p = md.ram32(o + 0x2A) & 0xFFFFFF;
    if (!p) return;
    md.set_ram32(o + 0x2A, 0);
    md.set_ram32(o + 0x2E, 0);
    const int n = md.ram8(o + 0x29) + 1;
    md.set_ram8(o + 0x29, 0);
    for (int k = 0; k < n; k++) md.set_ram8(p + k, 0);
}

void despawn(MegaDrive& md, uint32_t o) {   // $1AE0B0
    auto one = [&](uint32_t a) {
        md.set_ram8(a, 0);
        free_tiles(md, a);
        if ((md.ram8(a + 6) & 0x20) && md.ram8(a + 0x34))   // $1AE0D4: the map block back
            md.set_ram8(OBJ_TYPES + (uint32_t)(int16_t)md.ram16(a + 0x32), md.ram8(a + 0x34));
    };
    one(o);
    const uint32_t link = md.ram32(o + 0x3E) & 0xFFFFFF;
    if (link) one(link);
}

// The farthest evictable object among slots [first, first + count) (that holds tiles, if `tiles`), or 0.
uint32_t eviction_candidate(const MegaDrive& md, int first, int count, bool tiles, int than) {
    uint32_t best = 0;
    int far = -1;
    for (int i = first; i < first + count && i < 32; i++) {
        const uint32_t o = OBJECTS + (uint32_t)(i * OBJ_SIZE);
        if (!evictable(md, o, than) || (tiles && !(md.ram32(o + 0x2A) & 0xFFFFFF))) continue;
        const int d = outside_view(md, o);
        if (d > far) far = d, best = o;
    }
    return best;
}

// The free-slot searches ($1AE262 ...): a5 walks a range of slots; at their rts Z is set (and a5 the free slot) or not.
// Not found: an evictable object of the range makes room, a5 = its slot, Z set.
struct SlotSearch { uint32_t rts; int first, count; };
const SlotSearch kSlotSearches[] = {
    {0x1AE278, 3, 20}, {0x1AE290, 1, 24}, {0x1AE2A8, 1, 24}, {0x1AE2C0, 1, 20}, {0x1AE2D8, 1, 31}, {0x1AE2F0, 25, 6},
};

void slot_search_done(MegaDrive& md, const SlotSearch& ss) {
    if (md.cpu.get_sr() & 4) return;   // found
    // (the object to come isn't placed yet: only what lies past the spawn band)
    const uint32_t o = eviction_candidate(md, ss.first, ss.count, false, ROOM_MARGIN - 1);
    if (!o) return;
    despawn(md, o);
    md.cpu.r[13] = o;                  // a5
    md.cpu.set_sr((uint16_t)(md.cpu.get_sr() | 4));
}

// ---------------------------------------------------------------------------------------------------------------------
// The sprite-tile pool. Every object takes a run of consecutive slots ($1AD3E8: $74 slots at $FFF008, one byte each,
// $FF = taken; slot k = 4 tiles at VRAM $8600 + $80k, table $11F500): +$29 the run's length - 1, +$2A its first byte,
// +$2E its VRAM address, which the animation pass ($1AC7D2) uploads a frame's tiles to (a DMA queue at $FF769A, 14
// bytes an entry, count $FFEFEE, flushed after the sprite table is built) and the sprite builder takes the pattern
// from. First fit: objects come and go in any order and the free slots get scattered, and the wider view keeps more
// objects alive than the pool was sized for, so a guard ($11: 16 slots) can find 24 free in runs of 13 and 11 - and
// is dropped until its column comes again, by then inside the view. When the allocation fails ($1AD406) the module
// makes room: with too few slots free at all, it evicts objects the view can't show (above) until there are; then it
// moves allocations to make a run - the fewest slots moved into free ones, or failing that all packed to the pool's
// start - and the asker gets the run as the allocator would give it (with no room to be had, the original failure). Objects keep their state and nothing on screen
// changes. The pass runs while the frame is being drawn (before the VBlank wait; the sprite table and the uploads
// follow it), so the moved tiles are copied in VRAM at once and the sprite table in VRAM - the one being shown - is
// pointed at them, as are this frame's queued uploads and +$2A / +$2E; then the pool and the picture agree at every
// moment (and in a save state taken before the frame's uploads).
constexpr uint32_t POOL = 0xFFF008, POOL_TILES = 0x11F500, DMA_QUEUE = 0xFF769A, DMA_COUNT = 0xFFEFEE;
constexpr int POOL_SLOTS = 0x74;
constexpr uint32_t PC_TILES_FULL = 0x1AD406, PC_ALLOC_DONE = 0x1AD62C;

bool tile_pool_make_room(MegaDrive& md) {
    const uint32_t asker = md.cpu.r[9] & 0xFFFFFF;   // a1
    const int need = md.ram8(asker + 0x29) + 1;
    for (;;) {   // too few free at all: evict
        int free_slots = 0;
        for (int k = 0; k < POOL_SLOTS; k++) free_slots += md.ram8(POOL + k) == 0;
        if (free_slots >= need) break;
        const uint32_t o = eviction_candidate(md, 1, 31, true, outside_view(md, asker));
        if (!o || o == asker) return false;
        despawn(md, o);
    }
    struct Run { uint32_t obj; int start, size, to; };
    std::vector<Run> runs;
    bool used[POOL_SLOTS];
    int taken = 0, owned = 0;
    for (int k = 0; k < POOL_SLOTS; k++) taken += (used[k] = md.ram8(POOL + k) != 0);
    const uint32_t vram_lo = md.rom32(POOL_TILES), vram_hi = vram_lo + POOL_SLOTS * 0x80;
    for (int i = 0; i < 32; i++) {
        const uint32_t o = 0xFF7E40 + i * 0x42;
        if (o == asker) continue;
        const uint32_t p = md.ram32(o + 0x2A) & 0xFFFFFF, v = md.ram32(o + 0x2E);
        if (!p) {
            if (v >= vram_lo && v < vram_hi) return false;   // (tiles in the pool without a run: not understood)
            continue;
        }
        const int start = (int)p - (int)POOL, size = md.ram8(o + 0x29) + 1;
        if (start < 0 || start + size > POOL_SLOTS || v != md.rom32(POOL_TILES + 4 * start)) return false;
        for (int k = start; k < start + size; k++) if (!used[k]) return false;
        runs.push_back({o, start, size, start});
        owned += size;
    }
    if (owned != taken || POOL_SLOTS - taken < need || runs.empty()) return false;
    // the uploads queued this frame (to be redirected with the runs they write to)
    const int queued = md.ram16(DMA_COUNT);
    if (queued > 64) return false;
    auto queue_dest = [&](int e) {
        const uint32_t q = DMA_QUEUE + 14 * e;
        return (uint32_t)((md.ram16(q + 10) & 0x3FFF) | ((md.ram16(q + 12) & 3) << 14));
    };
    // The window for the asker: the one whose runs fit into the free slots elsewhere with the fewest slots moved
    std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) { return a.start < b.start; });
    int best = -1, best_cost = POOL_SLOTS + 1;
    std::vector<int> best_to;
    for (int s = 0; s + need <= POOL_SLOTS; s++) {
        std::vector<int> over;
        int cost = 0;
        for (int j = 0; j < (int)runs.size(); j++)
            if (runs[j].start < s + need && runs[j].start + runs[j].size > s) { over.push_back(j); cost += runs[j].size; }
        if (cost >= best_cost) continue;
        bool free2[POOL_SLOTS];
        for (int k = 0; k < POOL_SLOTS; k++) free2[k] = !used[k] && (k < s || k >= s + need);
        std::sort(over.begin(), over.end(), [&](int a, int b) { return runs[a].size > runs[b].size; });
        std::vector<int> to(runs.size(), -1);
        bool ok = true;
        for (int j : over) {
            int at = -1;
            for (int k = 0, len = 0; k < POOL_SLOTS && at < 0; k++) {
                len = free2[k] ? len + 1 : 0;
                if (len == runs[j].size) at = k - len + 1;
            }
            if (at < 0) { ok = false; break; }
            for (int k = at; k < at + runs[j].size; k++) free2[k] = false;
            to[j] = at;
        }
        if (!ok) continue;
        best = s, best_cost = cost, best_to = to;
    }
    if (best >= 0) {
        for (size_t j = 0; j < runs.size(); j++) if (best_to[j] >= 0) runs[j].to = best_to[j];
    } else {   // all packed to the start, the asker's run after them (moving down, in order: memmove is enough)
        int at = 0;
        for (Run& r : runs) { r.to = at; at += r.size; }
        best = at;
    }
    // tiles, queued uploads, slots, objects
    VDP& v = md.vdp;
    auto tiles = [&](int slot) { return md.rom32(POOL_TILES + 4 * slot); };
    for (int e = 0; e < queued; e++) {
        const uint32_t d = queue_dest(e);
        for (const Run& r : runs) {
            const uint32_t lo = tiles(r.start), hi = lo + r.size * 0x80;
            if (r.to == r.start || d < lo || d >= hi) continue;
            const uint32_t nd = d - lo + tiles(r.to), q = DMA_QUEUE + 14 * e;
            md.set_ram16(q + 10, (uint16_t)((md.ram16(q + 10) & 0xC000) | (nd & 0x3FFF)));
            md.set_ram16(q + 12, (uint16_t)((md.ram16(q + 12) & ~3) | (nd >> 14)));
            break;
        }
    }
    // the sprite table being shown: patterns in a moved run follow it
    const uint32_t sat = (uint32_t)(v.reg[5] & 0x7E) << 9;
    for (int e = 0; e < 80; e++) {
        const uint32_t a = (sat + e * 8 + 4) & 0xFFFF;
        const uint16_t w = v.vram16(a);
        const uint32_t t = (uint32_t)(w & 0x7FF) * 32;
        for (const Run& r : runs) {
            const uint32_t lo = tiles(r.start);
            if (r.to == r.start || t < lo || t >= lo + r.size * 0x80) continue;
            const uint16_t nw = (uint16_t)((w & 0xF800) | ((t - lo + tiles(r.to)) / 32));
            v.vram[a] = (uint8_t)(nw >> 8);
            v.vram[a + 1] = (uint8_t)nw;
            break;
        }
    }
    for (const Run& r : runs) {   // (ascending: a packed run moves down, over nothing still to be read)
        if (r.to == r.start) continue;
        std::memmove(&v.vram[tiles(r.to) & 0xFFFF], &v.vram[tiles(r.start) & 0xFFFF], (size_t)r.size * 0x80);
        for (int k = r.start; k < r.start + r.size; k++) md.set_ram8(POOL + k, 0);
    }
    for (const Run& r : runs) {
        if (r.to == r.start) continue;
        for (int k = r.to; k < r.to + r.size; k++) md.set_ram8(POOL + k, 0xFF);
        md.set_ram32(r.obj + 0x2A, POOL + r.to);
        md.set_ram32(r.obj + 0x2E, tiles(r.to));
    }
    for (int k = best; k < best + need; k++) md.set_ram8(POOL + k, 0xFF);
    md.set_ram32(asker + 0x2A, POOL + best);
    md.set_ram32(asker + 0x2E, tiles(best));
    md.cpu.r[14] = POOL_TILES;   // (a6, as the allocator's success path leaves it)
    md.cpu.pc = PC_ALLOC_DONE;   // movem.l (a7)+, d0-d1; rts
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The Rug Ride wide (docs/re_md_wide.md section 7). The ride runs on the 4:3 game's ROM (the cheats, none of the
// widescreen patches: MdAladdin::rom_for), so its RAM and timing are the 4:3 game's to the cycle; the view is slid by
// -E (update_view) and the module only draws. Two things the 4:3 game never shows come into the view behind the carpet:
// * Objects leaving on the left. The game despawns an object once it is $50 px left of the camera ($1ADE84: screen x
//   -96), out of its own view but inside the wider one. The module keeps drawing it there - a ghost: its record as the
//   game left it, its frame drawn from the ROM data the game draws it from, its animation stepped by the game's own code
//   (on a CPU and a RAM copy of the module's) on every second iteration, as the game steps its objects - until it leaves
//   the view or its animation ends it. (Nothing the ride sends moves in the cave: its treasures, walls and pickups
//   stand; a lava splash erupts and sinks where it is - at speed the game despawns one half way.)
// * Plane B's lava wave: a crest cut off straight at its back (ride_wave).
constexpr uint32_t PC_WINDOW_DESPAWN = 0x1ADEDE;   // the object loop: a1 outside the window, despawned (bsr $1AE0B0)
constexpr uint32_t PC_ANIMATE = 0x1AC796;          // the animation pass, odd iterations: every object a step
constexpr uint32_t PC_ANIM_OBJECT = 0x1AC7DC, PC_ANIM_NEXT = 0x1AC842, PC_ANIM_DONE = 0x1AC846;   // its loop, per object
constexpr uint32_t ANIM_RET = 0xFF7D9A, ANIM_SKIP = 0xFF7D9E, ANIM_MOVES = 0xFF7DA2, ANIM_COMMANDS = 0x4954;
constexpr uint32_t SHAKE_Y = 0xFFF080;
constexpr size_t GHOSTS_MAX = 16;

// The game's animation step for one object ($1AC7DC with a1 = it, d4 = 0: its script at +$20 - frames, waits, loops,
// jumps, calls - and the end that removes it), on a CPU of its own over a copy of RAM: what the object would do next had
// the game kept it. Nothing it writes reaches the game.
class AnimProbe {
public:
    AnimProbe() {
        cpu.ctx = this;
        cpu.rd8 = [](void*, uint32_t) -> uint8_t { return 0; };      // (I/O: VDP status, Z80 bus free at once)
        cpu.rd16 = [](void*, uint32_t) -> uint16_t { return 0; };
        cpu.wr8 = [](void*, uint32_t, uint8_t) {};
        cpu.wr16 = [](void*, uint32_t, uint16_t) {};
        cpu.trace_fn = [](void* c, uint32_t pc) { if ((pc & 0xFFFFFF) == 0) ((M68k*)c)->end_run(); };
        cpu.trace_ctx = &cpu;
    }

    // One step for the record `obj` of the object at `addr`; false when it ended (or the step didn't return).
    bool step(const MegaDrive& md, uint32_t addr, uint8_t* obj) {
        memcpy(ram, md.ram, sizeof(ram));
        memcpy(ram + (addr & 0xFFFF), obj, OBJ_SIZE);
        put32(ANIM_RET, PC_ANIM_NEXT);   // (as the pass sets them: commands return through these)
        put32(ANIM_SKIP, PC_ANIM_DONE);
        ram[ANIM_MOVES & 0xFFFF] = 0;
        for (int p = 0; p < 256; p++) cpu.page_read[p] = cpu.page_write[p] = nullptr;
        for (int p = 0; p < 0x40 && !md.rom.empty(); p++) cpu.page_read[p] = const_cast<uint8_t*>(&md.rom[((size_t)p << 16) % md.rom.size()]);
        for (int p = 0xE0; p < 0x100; p++) cpu.page_read[p] = cpu.page_write[p] = ram;
        cpu.set_sr(0x2700);
        cpu.stopped = false;
        cpu.irq_level = 0;
        cpu.r[4] = 0;                    // d4: the loop's last object
        cpu.r[9] = addr;                 // a1
        cpu.r[11] = DMA_QUEUE;           // a3: the frame's tile uploads (into the copy)
        cpu.r[12] = ANIM_COMMANDS;       // a4
        cpu.r[15] = 0xFFEF00;
        cpu.write32(cpu.r[15] -= 4, 0);  // returns to address 0 (trace_fn stops there)
        cpu.pc = PC_ANIM_OBJECT;
        for (int used = 0; (cpu.pc & 0xFFFFFF) != 0 && used < 100000;) {
            const int n = cpu.run(2000);
            used += n;
            if (n == 0) break;
        }
        if ((cpu.pc & 0xFFFFFF) != 0) return false;
        memcpy(obj, ram + (addr & 0xFFFF), OBJ_SIZE);
        return obj[0] != 0;
    }

private:
    M68k cpu;
    uint8_t ram[0x10000];
    void put32(uint32_t a, uint32_t v) { for (int i = 0; i < 4; i++) ram[(a + i) & 0xFFFF] = (uint8_t)(v >> (24 - 8 * i)); }
};

struct RideView {
    struct Ghost { uint32_t addr; uint8_t obj[OBJ_SIZE]; };   // the object's record and where it lived
    std::vector<Ghost> ghosts;                                // (kept in save states: MdAladdin::save_ext)
    struct Piece { int x, y, w, h; uint8_t attr; bool hf, vf; size_t data; };   // screen x / y, cells
    std::vector<Piece> pieces;                                // this frame's, and the tiles they draw from
    std::vector<uint8_t> tiles;
    AnimProbe probe;
};

uint32_t obj32(const uint8_t* o, int k) { return (uint32_t)o[k] << 24 | o[k + 1] << 16 | o[k + 2] << 8 | o[k + 3]; }
int obj16(const uint8_t* o, int k) { return (int16_t)(o[k] << 8 | o[k + 1]); }

// An object's sprite pieces as the game would place them with camera (camx, camy). Its frame (+$14, ROM) is a word
// (pieces - 1), 4 bytes, then 12 bytes a piece: its descriptor (a short address: +4 its tiles' bytes, +6 the SAT size
// << 8, +8 / +9 its width / height), x / y offsets (bytes; flipped, -offset - width / height, still a byte), the DMA
// source of its tiles (VDP registers 21-23 at +4 / +6 / +8) and the tiles the builder steps on by (+$A). The frame's
// upload ($1AC6D0) writes the pieces' tiles one after another into the object's block; the builder ($1ABA0A) puts piece k
// at VDP x offset + x - camera X, y offset + y - camera Y - $100 (+ the earthquake's shake), its pattern the block's plus
// the tiles of the pieces before it, palette and priority from +$1E (+ the block's tile index), flipped by +9 / +$35.
// Returns the screen x right of its rightmost piece (INT_MIN: nothing to draw).
int ghost_pieces(const MegaDrive& md, const uint8_t* o, int camx, int camy, RideView& r) {
    const uint32_t frame = obj32(o, 0x14) & 0xFFFFFF;
    if (!frame || (o[7] & 0x20) || frame + 6 > md.rom.size()) return INT_MIN;   // (no frame, hidden)
    const int n = md.rom16(frame) + 1;
    if (n > 32 || frame + 6 + 12u * n > md.rom.size()) return INT_MIN;
    const size_t block = r.tiles.size();
    auto byte = [&](uint32_t a) -> uint8_t {
        a &= 0xFFFFFF;
        return a < md.rom.size() ? md.rom[a] : a >= 0xFF0000 ? md.ram[a & 0xFFFF] : 0;
    };
    for (int k = 0; k < n; k++) {   // the block, as the upload writes it
        const uint32_t e = frame + 6 + 12 * k, desc = md.rom16(e);
        const uint32_t src = (uint32_t)(md.rom16(e + 8) & 0x7F) << 17 | (uint32_t)(md.rom16(e + 6) & 0xFF) << 9 |
                             (uint32_t)(md.rom16(e + 4) & 0xFF) << 1;
        const int bytes = std::min<int>(md.rom16(desc + 4), 0x1000);
        for (int i = 0; i < bytes; i++) r.tiles.push_back(byte(src + i));
    }
    const uint16_t attr = (uint16_t)(((obj32(o, 0x2E) >> 5) & 0xFFFF) + (uint16_t)obj16(o, 0x1E));
    const int x0 = (int16_t)(obj16(o, 2) - camx) - (int16_t)md.ram16(SHAKE_X) - 128;   // (16 bits: the ride's camera wraps)
    const int y0 = (int16_t)(obj16(o, 4) - camy) - 0x100 + (int16_t)md.ram16(SHAKE_Y) - 128;
    int right = INT_MIN;
    size_t tile = 0;
    for (int k = 0; k < n; k++) {   // the pieces, as the builder places them
        const uint32_t e = frame + 6 + 12 * k, desc = md.rom16(e);
        const int size = (md.rom16(desc + 6) >> 8) & 0x0F;
        RideView::Piece p;
        p.w = ((size >> 2) & 3) + 1;
        p.h = (size & 3) + 1;
        uint8_t dx = md.rom[e + 2], dy = md.rom[e + 3];
        p.hf = o[9] != 0;
        p.vf = o[0x35] != 0;
        if (p.hf) dx = (uint8_t)(-dx - md.rom[desc + 8]);
        if (p.vf) dy = (uint8_t)(-dy - md.rom[desc + 9]);
        p.x = x0 + dx;
        p.y = y0 + dy;
        p.attr = (uint8_t)(((attr & 0x8000) ? 0x80 : 0) | ((attr >> 13) & 3) << 4);
        p.data = block + tile * 32;
        if (p.data + (size_t)p.w * p.h * 32 <= r.tiles.size()) {
            r.pieces.push_back(p);
            right = std::max(right, p.x + 8 * p.w);
        }
        tile += md.rom16(e + 0xA);
    }
    return right;
}

// Before each frame: the ghosts' pieces, placed with the camera the frame's sprites were placed with - the floor's
// scroll (plane A's bottom 56 lines: -(camera X + 16)) next to the camera in RAM, which can be the next iteration's
// already; a ghost entirely left of the view is gone. No ghosts outside the ride's play.
void ride_frame(Wide& w) {
    RideView* r = w.ride;
    if (!r) return;
    MegaDrive& md = *w.md;
    r->pieces.clear();
    r->tiles.clear();
    if (!w.active || !rug_ride(md)) {
        r->ghosts.clear();
        return;
    }
    const int ram_x = (int16_t)md.ram16(CAM_X);
    int d = (ram_x + 16 + line_hscroll(md.vdp, 216, false)) & 511;
    if (d >= 256) d -= 512;
    const int camx = ram_x - d, camy = (int16_t)md.ram16(CAM_Y), left = -md.vdp.ext + md.vdp.view_shift;
    for (size_t i = 0; i < r->ghosts.size();) {
        const size_t np = r->pieces.size(), nt = r->tiles.size();
        if (ghost_pieces(md, r->ghosts[i].obj, camx, camy, *r) > left) { i++; continue; }
        r->pieces.resize(np);
        r->tiles.resize(nt);
        r->ghosts.erase(r->ghosts.begin() + i);
    }
}

// At $1ADEDE (a1 outside the game's window, about to be despawned, its linked object, +$3E, with it): one leaving on
// the left goes on as a ghost - $50 px or more left of the camera, the game's own test, but as a signed distance: the
// test compares unsigned words, and once the ride's camera X has run past 65535 (some 55,000 px into the ride) objects
// the script sends in at the 4:3 view's right edge are despawned at once; the 4:3 game shows them vanish there, and so
// does the wider view. (The linked object that leaves with it likewise, when it is that far left too; ghosts are only
// ever drawn left of the 4:3 area, so that stays the 4:3 game's.)
void ride_despawn(Wide& w) {
    RideView* r = w.ride;
    MegaDrive& md = *w.md;
    if (!r || !md.vdp.ext || !rug_ride(md) || !in_play(md)) return;
    auto keep = [&](uint32_t a) {
        if (a <= OBJECTS || a >= OBJECTS + 32 * OBJ_SIZE || (a - OBJECTS) % OBJ_SIZE || !md.ram8(a) || (md.ram8(a + 6) & 8)) return;
        if ((int16_t)(md.ram16(a + 2) - md.ram16(CAM_X)) >= -0x50) return;
        if (r->ghosts.size() >= GHOSTS_MAX) r->ghosts.erase(r->ghosts.begin());
        RideView::Ghost g;
        g.addr = a;
        for (int i = 0; i < OBJ_SIZE; i++) g.obj[i] = md.ram8(a + i);
        r->ghosts.push_back(g);
    };
    const uint32_t o = md.cpu.r[9] & 0xFFFFFF;
    keep(o);
    keep(md.ram32(o + 0x3E) & 0xFFFFFF);
}

// At $1AC796 (the animation pass of an odd iteration): the ghosts step on with the game's objects.
void ride_animate(Wide& w) {
    RideView* r = w.ride;
    if (!r) return;
    for (size_t i = 0; i < r->ghosts.size();) {
        if (r->probe.step(*w.md, r->ghosts[i].addr, r->ghosts[i].obj)) i++;
        else r->ghosts.erase(r->ghosts.begin() + i);
    }
}

// Plane B's lava wave (the picture's lines 24-159, scrolled by $FFF0A2: a pixel every 64 frames, so it creeps up behind
// the carpet) is a crest cut off straight at its back (plane x 72, its top there at line 39), which the 4:3 game never
// shows: the cut starts 118 px left of its screen and comes to 28 px short of it by the ride's end. The view behind the
// carpet shows it, so the wave's back goes on from there: its top on down at the crest's own slope (12 px in 27) toward
// the lava, its body the 16 px at the cut repeated, squeezed into the lower height.
constexpr int WAVE_X = 72, WAVE_TOP = 39, WAVE_BOTTOM = 159;
void ride_wave(const MegaDrive& md, int line, uint8_t* lb) {
    const VDP& v = md.vdp;
    if (line < WAVE_TOP || line > WAVE_BOTTOM || (v.vsram[1] & 0x3FF) != 0 || (v.reg[16] & 3) != 1) return;
    const uint32_t nt = (v.reg[4] & 7) << 13;
    uint8_t row[8];
    auto pixel = [&](int x, int y) {   // plane B (64 x 32 cells) at (x, y)
        v.pattern_row(v.vram16(nt + (((y >> 3) & 31) * 64 + ((x >> 3) & 63)) * 2), y & 7, row);
        return row[x & 7];
    };
    if (!(pixel(WAVE_X, 150) & 15) || (pixel(WAVE_X - 1, 150) & 15)) return;   // (not the ride's picture)
    const int cut = ((WAVE_X + line_hscroll(v, line, true)) & 511) - 512, left = -v.ext + v.view_shift;
    for (int x = left; x < cut; x++) {
        const int d = cut - x, top = WAVE_TOP + d * 4 / 9;
        uint8_t& p = lb[x - left];
        if (line < top || top >= WAVE_BOTTOM || (p & 15)) continue;
        p = pixel(WAVE_X + (16 - d % 16) % 16, WAVE_BOTTOM - (WAVE_BOTTOM - line) * (WAVE_BOTTOM - WAVE_TOP) / (WAVE_BOTTOM - top));
    }
}

// A line of the ride: the wave's back on plane B, the ghosts behind the game's sprites, left of the 4:3 area.
void ride_line(Wide& w, int line, uint8_t* plane_b, uint8_t* sprites) {
    const MegaDrive& md = *w.md;
    ride_wave(md, line, plane_b);
    if (!w.ride) return;
    const RideView& r = *w.ride;
    const int TW = std::min(320 + 2 * md.vdp.ext, md.vdp.ext - md.vdp.view_shift), at = md.vdp.ext - md.vdp.view_shift;   // (at: screen x 0)
    for (const RideView::Piece& p : r.pieces) {
        int row = line - p.y;
        if (row < 0 || row >= 8 * p.h) continue;
        if (p.vf) row = 8 * p.h - 1 - row;
        for (int c = 0; c < p.w; c++) {
            const int col = p.hf ? p.w - 1 - c : c;
            const uint8_t* t = &r.tiles[p.data + (size_t)(col * p.h + (row >> 3)) * 32 + (row & 7) * 4];
            for (int k = 0; k < 8; k++) {
                const int i = p.x + 8 * c + k + at;
                if (i < 0 || i >= TW || (sprites[i] & 15)) continue;
                const int q = p.hf ? 7 - k : k;
                const uint8_t c4 = (q & 1) ? (t[q >> 1] & 15) : (t[q >> 1] >> 4);
                if (c4) sprites[i] = p.attr | c4;
            }
        }
    }
}

int sprite_xoff_hook(void* host, int index, uint16_t attr, int x, int y) {
    const Wide& w = *(Wide*)host;
    return w.active ? sprite_xoff(*w.md, index, attr, x, y) : 0;
}
bool sprite_is_hud_hook(void* host, int index) { return sprite_is_hud(*((Wide*)host)->md, index); }

void line_hook(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) {
    Wide& w = *(Wide*)host;
    MegaDrive& md = *w.md;
    VDP& v = md.vdp;
    v.edge_mask_l = v.edge_mask_r = 0;
    if (!w.active) return;
    // (The Rug Ride doesn't stream: its planes are 512-px loops of the lava cave the VDP wraps, its "map" nominal.)
    if (rug_ride(md)) return ride_line(w, line, plane_b, sprites);
    const int E = v.ext, TW = 320 + 2 * E;
    if (w.shift_a_on && w.shift_a[line] != v.view_shift)   // plane A's own slide (the renderer used the view's)
        v.render_plane(plane_a, line, false, -E + w.shift_a[line], 320 + E + w.shift_a[line]);
    const bool swapped = md.ram8(SWAP_PLANES) != 0;
    const LineView lv = line_view(md, line, swapped);
    const int mx0 = lv.mx0 + (swapped && v.view_shift_b_on ? v.view_shift_b[line] : v.view_shift);
    draw_map_ext(md, mx0, lv.my, swapped ? plane_b : plane_a);
    // What lies outside the 4:3 game's range (only in a level narrower than the widened view, centred) is black, the
    // HUD staying over it. (The VDP's masks rather than FrameInfo's: those would cover the HUD anchored there.)
    const int right = md.ram16(MAP_W) * 16 - VIEW_RIGHT_GAP;
    v.edge_mask_l = std::clamp(VIEW_LEFT - mx0 + E, 0, E);
    v.edge_mask_r = std::clamp(TW - (right - mx0 + E), 0, E);
}

// ---- sound (docs/re_md_game.md section 4) ---------------------------------------------------------------
// GEMS sequences (bank $1BAF6F, 114 entries): music and sound effects alike. The names are the developers' own, from the
// OPTIONS screen's SOUND TEST ($12675E: sequence, 14 characters), with where the game plays the tune. Ten tunes the code
// never asks for (and the sound test doesn't list) close the music list; the silent sequences (2, 23-25, ...) are left out.
const SoundEntry kSounds[] = {
    {26, "A Whole New World (Title & Ending)", true},
    {84, "Storyline", true},
    {73, "Prince Ali (Agrabah Market)", true},
    {38, "Camel Jazz (The Desert)", true},
    {20, "One Jump Ahead (Agrabah Rooftops)", true},
    {85, "Drama", true},
    {82, "Arabian Nights (Sultan's Dungeon)", true},
    {35, "Gloomy Tune (Cave of Wonders)", true},
    {16, "Arab Rock 2 (Escape from the Cave)", true},
    {29, "Rug Ride", true},
    {27, "Friend Like Me (Inside the Lamp)", true},
    {22, "Turban Jazz (Sultan's Palace)", true},
    {86, "Mushy One", true},
    {50, "Boss Tune (Jafar's Quarters)", true},
    {15, "Arab Rock 1 (Abu's Bonus, Jafar's Palace)", true},
    {18, "Bonus Tune", true},
    {17, "Level Complete", true},
    {10, "Ne Naw Tune (Continue)", true},
    {21, "Unused Tune 1", true}, {28, "Unused Tune 2", true}, {30, "Unused Tune 3", true},
    {36, "Unused Tune 4", true}, {37, "Unused Tune 5", true}, {51, "Unused Tune 6", true},
    {52, "Unused Tune 7", true}, {53, "Unused Tune 8", true}, {54, "Unused Tune 9", true},
    {74, "Unused Tune 10", true},
    {0, "High Sword", false}, {1, "Low Sword", false}, {3, "Object Throw", false}, {4, "Menu Select", false},
    {5, "Flagpole", false}, {6, "Menu Change", false}, {7, "Head Bop", false}, {8, "Wall Hit", false},
    {9, "Cloud Poof", false}, {11, "Apple Collect", false}, {12, "Apple Slice", false}, {13, "Gem Collect", false},
    {14, "Jafar Laugh", false}, {19, "Scarab Wow", false}, {31, "Balloon Pop", false}, {32, "Sword Spinning", false},
    {33, "Sword Ching", false}, {34, "Apple Splat", false}, {39, "Pole Slide", false}, {41, "Rope Launch", false},
    {42, "Genie Fumes", false}, {43, "Guard's Pants", false}, {44, "Rope Appear", false}, {45, "Iago Squawk", false},
    {46, "Effect 46", false}, {47, "Landing Oof", false}, {49, "Aladdin Hurt", false}, {55, "Skeleton", false},
    {56, "Bones Tinkle", false}, {57, "Clay Pot", false}, {58, "Boxing Bell", false}, {59, "Aladdin Push", false},
    {60, "Splash", false}, {61, "Camel Spit", false}, {62, "Guard Beckon", false}, {63, "Canopy Bounce", false},
    {64, "Flamingo Hey!", false}, {68, "Flamingo Hey! 2", false}, {65, "Guard Hit 1", false}, {66, "Guard Hit 2", false},
    {67, "Geyser", false}, {69, "Stone Rumble", false}, {71, "Spring Doing 1", false}, {70, "Spring Doing 2", false},
    {72, "Cash Register", false}, {75, "Honk", false}, {76, "Fire from Coal", false}, {77, "Stones Crumble", false},
    {78, "Wall Spikes", false}, {79, "Tiptoe", false}, {80, "Rock Bounce", false}, {81, "Outta Apples", false},
    {83, "Earthquake", false}, {90, "Stopping", false}, {91, "Win a Bonus", false}, {92, "Starting Gun", false},
    {93, "Abu Uh-Oh", false}, {94, "Oooh", false}, {95, "Aaah", false}, {96, "Eeeh", false}, {97, "Iiee", false},
    {98, "Extra Health", false}, {99, "Continue Point", false}, {100, "Scarab Pickup", false},
    {101, "Body Crunch", false}, {102, "Yeah", false}, {103, "Flute Pickup", false}, {104, "Jafar Tractor", false},
    {105, "Wow!", false}, {106, "Gazeem Hit 1", false}, {107, "Gazeem Hit 2", false}, {108, "Jafar Snake", false},
    {109, "Peddler Shop", false}, {110, "Abu Waving", false}, {112, "Coming Out", false}, {113, "Honkey Sound", false},
};

bool is_music(int id) {
    for (const SoundEntry& e : kSounds) if (e.id == id) return e.music;
    return false;
}

std::vector<SoundEntry> sound_list() { return std::vector<SoundEntry>(std::begin(kSounds), std::end(kSounds)); }

class MdAladdin : public MdModule {
public:
    const char* id() const override { return "md"; }
    const char* title() const override { return "Aladdin"; }
    const char* subtitle() const override { return "Virgin Games"; }
    const char* year() const override { return "1993"; }
    uint32_t rom_crc32() const override { return 0xED427EA9; }
    uint32_t rom_size() const override { return 2u << 20; }
    const char* rom_hint() const override { return "Aladdin (U) [!].bin"; }
    const char* rom_names() const override { return "ALADDIN"; }
    uint32_t accent_color() const override { return 0xFF5AA8F2; }
    int max_ext() const override { return 128; }
    int preview_start() const override { return 850; }   // the title screen (after the SEGA, Disney and Virgin logos)

    static std::vector<RomPatch> patch_list(const GameConfig& cfg) {
        std::vector<RomPatch> p;
        auto add = [&](const std::vector<RomPatch>& v) { p.insert(p.end(), v.begin(), v.end()); };
        if (cfg.cheat("invincible")) add(kInvincible);
        if (cfg.cheat("inf_health")) add(kInfHealth);
        if (cfg.cheat("inf_lives")) add(kInfLives);
        if (cfg.cheat("inf_apples")) add(kInfApples);
        if (cfg.cheat("inf_gems")) add(kInfGems);
        if (cfg.cheat("inf_continues")) add(kInfContinues);
        if (cfg.cheat("level_skip")) add(kLevelSkip);
        if (cfg.ext > 0) add(wide_patches(cfg.ext));
        return p;
    }

    // The attract mode's demos replay recorded input against the original game: the widescreen patches and the
    // cheats would change their course (objects spawn and despawn elsewhere, Aladdin survives a hit). While the title
    // routine runs or a demo plays the ROM is the original, no cheat touches RAM and the picture is 4:3; the ROM
    // switches at the first frame boundary where it is safe (pre_frame) - the title's own screens lie between a game's
    // start and its first level, and between the demo flag and the demo's level.
    // The Rug Ride runs on the 4:3 game's ROM (the cheats, none of the widescreen patches): its view is widened by
    // drawing only (RideView), so the ride is the 4:3 game's to the cycle. The stories and title cards around it lie
    // between it and the levels on either side.
    enum class Rom { Wide, Attract, Ride };
    static Rom rom_kind(const MegaDrive& md) { return attract(md) ? Rom::Attract : rug_ride(md) ? Rom::Ride : Rom::Wide; }
    static std::vector<RomPatch> rom_for(const MegaDrive& md, const GameConfig& cfg) {
        switch (rom_kind(md)) {
        case Rom::Attract: return {};
        case Rom::Ride: { GameConfig c = cfg; c.ext = 0; return patch_list(c); }
        default: return patch_list(cfg);
        }
    }
    bool wide_rom() const { return rom_ == Rom::Wide; }   // (the widescreen patches' hooks work on them)

    bool can_configure(const MegaDrive& md, const GameConfig& cfg) const override { return patches_safe(md, rom_for(md, cfg)); }

    void configure(MegaDrive& md, const GameConfig& cfg) override {
        apply_patches(md, rom_for(md, cfg));
        rom_ = rom_kind(md);
        md.clear_pc_hooks();
        auto play = [](MegaDrive& m) { m.module_state[MS_PLAY] = ((uint32_t)m.frame_count + 1) & PLAY_FRAME; m.module_state[MS_FADE] = 0; };
        auto leave = [](MegaDrive& m) { m.module_state[MS_PLAY] = 0; m.module_state[MS_FADE] = 0; };
        // (an iteration of the gameplay loop is under way until its frame wait: in_play)
        md.add_pc_hook(PC_MAIN_LOOP, [play](MegaDrive& m) { play(m); m.module_state[MS_PLAY] |= PLAY_BUSY; });
        md.add_pc_hook(PC_FRAME_WAIT, [](MegaDrive& m) { m.module_state[MS_PLAY] &= ~PLAY_BUSY; });
        md.add_pc_hook(PC_PAUSE_LOOP, play);
        md.add_pc_hook(PC_LEVEL_END, leave);
        md.add_pc_hook(PC_DEATH, leave);
        md.add_pc_hook(PC_PIT, leave);
        // (a level's start or a restart: no boss framing carried over)
        auto fade = [](MegaDrive& m) { m.module_state[MS_FADE] = 1; m.module_state[MS_DEV] &= ~(BF_MASK | CARPET_DUE); };
        md.add_pc_hook(PC_FADE_IN, fade);
        md.add_pc_hook(PC_RESPAWN_FADE, fade);
        const bool wide = cfg.ext > 0;
        // (with the widescreen patches only: in the attract mode, the Rug Ride and at 4:3 the pool is the original's)
        if (wide) {
            md.add_pc_hook(PC_TILES_FULL, [this](MegaDrive& m) { if (wide_rom()) tile_pool_make_room(m); });
            for (const SlotSearch& ss : kSlotSearches)
                md.add_pc_hook(ss.rts, [this, &ss](MegaDrive& m) { if (wide_rom() && in_play(m)) slot_search_done(m, ss); });
            for (const BossHandler& h : kBossHandlers)
                md.add_pc_hook(h.at, [this, &h](MegaDrive& m) { if (wide_rom()) boss_tune_spawned(m, h); });
            md.add_pc_hook(PC_LOAD_LINE2, [this](MegaDrive& m) { if (wide_rom()) line2_load(m); });
            // After the streaming: held tunes and colours; the carpet at the idol's kill (carpet_due), else while a boss
            // framing is in force the band's outer columns (band_refresh)
            md.add_pc_hook(PC_STREAMED, [this](MegaDrive& m) {
                boss_tune_held(m);
                if (!wide_rom()) return;
                line2_moments(m);
                if (!carpet_due(m, *wide_)) band_refresh(m, *wide_);
            });
            md.add_pc_hook(PC_LEVEL_LOAD, [](MegaDrive& m) { m.module_state[MS_PAL2] = 0; m.module_state[MS_DEV] &= ~(BF_MASK | CARPET_DUE); });
            md.add_pc_hook(PC_IDOL_BEATEN, [this](MegaDrive& m) { if (wide_rom()) m.module_state[MS_DEV] |= CARPET_DUE; });
            md.add_pc_hook(CAVE_REACH, [this](MegaDrive& m) { reach_hook(m, *wide_); });
            md.add_pc_hook(PC_COL_CALL, [this](MegaDrive& m) { if (wide_rom()) spawn_call(m, PC_COL_RET); });
            md.add_pc_hook(PC_ROW_CALL, [this](MegaDrive& m) { if (wide_rom()) spawn_call(m, PC_ROW_RET); });
            md.add_pc_hook(PC_SPAWN_KEEP, [this](MegaDrive& m) { if (wide_rom()) spawn_keep(m, *wide_); });
            md.add_pc_hook(PC_COL_RET, [this](MegaDrive& m) { if (wide_rom()) line2_filled(m); });
            md.add_pc_hook(PC_ROW_RET, [this](MegaDrive& m) { if (wide_rom()) line2_filled(m); });
            // The Rug Ride: objects despawned behind the carpet go on as ghosts, stepped with the game's (RideView)
            md.add_pc_hook(PC_WINDOW_DESPAWN, [this](MegaDrive&) { if (rom_ == Rom::Ride) ride_despawn(*wide_); });
            md.add_pc_hook(PC_ANIMATE, [this](MegaDrive&) { if (rom_ == Rom::Ride) ride_animate(*wide_); });
        }
        wide_->md = &md;
        wide_->ride = ride_.get();
        md.vdp.line_hook = wide ? line_hook : nullptr;
        md.vdp.pre_line_hook = wide ? pre_line_hook : nullptr;
        md.vdp.sprite_xoff = wide ? sprite_xoff_hook : nullptr;
        md.vdp.sprite_is_hud = wide ? sprite_is_hud_hook : nullptr;
        md.vdp.edge_mask_l = md.vdp.edge_mask_r = 0;
        sprite_x10(md, cfg.ext);   // (per frame: post_frame; here for the first frame after a loaded state)
        md.vdp.hook_host = wide_.get();
        md.vdp.hook_flags = cfg.hud_anchor ? 1 : 0;
        Wide* w = wide_.get();
        md.on_frame_start = wide ? std::function<void(MegaDrive&)>([w](MegaDrive&) { update_view(*w, true); }) : nullptr;
        md.vdp.view_shift = 0;
        md.vdp.view_shift_b_on = false;
        if (wide) update_view(*w, false);   // (the frame about to be drawn after a width change or a loaded state)
    }

    void pre_frame(MegaDrive& md, const GameConfig& cfg) override {
        if (rom_kind(md) != rom_) {
            const std::vector<RomPatch> rom = rom_for(md, cfg);
            if (patches_safe(md, rom)) {   // (else a later frame)
                apply_patches(md, rom);
                rom_ = rom_kind(md);
            }
        }
        // The developers' debug-screen options: set while the cheat is on in a game, cleared once when it is switched
        // off or the attract mode comes (its demos play as recorded).
        uint32_t& dev = md.module_state[MS_DEV];
        const bool game = !attract(md);
        if (game && cfg.cheat("dev_cheat")) { md.set_ram8(DEV_CHEAT, 0xFF); dev |= 1; }
        else if (dev & 1) { md.set_ram8(DEV_CHEAT, 0); dev &= ~1u; }
        if (!game && (dev & 2)) { md.set_ram8(DEV_MAP_VIEW, 0); dev &= ~2u; }
        if (!in_play(md)) return;
        // Map view takes Aladdin out of play every frame ($1A9C9A clears his object); switched off, he is put back
        // where the view is.
        if (cfg.cheat("map_view")) { md.set_ram8(DEV_MAP_VIEW, 0xFF); dev |= 2; }
        else if (dev & 2) {
            md.set_ram8(DEV_MAP_VIEW, 0);
            if (md.ram8(ALADDIN) == 0) md.set_ram8(ALADDIN, 0x83);
            dev &= ~2u;
        }
        // Infinite apples / gems also top a low stock up (the decrement no longer runs, but 0 can't throw / buy).
        if (cfg.cheat("inf_apples") && md.ram16(APPLES) < 0x3130) md.set_ram16(APPLES, 0x3130);
        if (cfg.cheat("inf_gems") && md.ram16(GEMS) < 0x3130) md.set_ram16(GEMS, 0x3130);
    }

    void post_frame(MegaDrive& md, const GameConfig& cfg, FrameInfo& info) override {
        // (the level-edge masks are drawn per line by line_hook, so the HUD stays over them)
        info.gameplay = cfg.ext > 0 ? wide_->active : wide_view(md);   // (as the frame was drawn)
        sprite_x10(md, cfg.ext);
    }

    // Sprite X read as 10 bits only in play (the other screens, shown 4:3, may leave junk in the upper bits), and not
    // in the Rug Ride (the 4:3 game's ROM: X 0..$1BE, as on the real chip)
    static void sprite_x10(MegaDrive& md, int E) {
        md.vdp.sprite_x10 = E > 0 && in_gameplay(md) && !rug_ride(md) && (448 + 2 * E > 512 || sprite_left(E) > 0);
    }

    // Save states (rewind too): besides module_state, the Rug Ride's ghosts - objects the game has despawned behind the
    // carpet that the view still shows (RideView): a version byte, their count, then each one's slot address (4 bytes)
    // and record. Nothing when there are none; an older state (none) has no ghosts.
    static constexpr uint8_t EXT_VERSION = 1;
    std::vector<uint8_t> save_ext() const override {
        std::vector<uint8_t> d;
        if (ride_->ghosts.empty()) return d;
        d.push_back(EXT_VERSION);
        d.push_back((uint8_t)ride_->ghosts.size());
        for (const RideView::Ghost& g : ride_->ghosts) {
            for (int i = 0; i < 4; i++) d.push_back((uint8_t)(g.addr >> (24 - 8 * i)));
            d.insert(d.end(), g.obj, g.obj + OBJ_SIZE);
        }
        return d;
    }

    void load_ext(const uint8_t* d, size_t n) override {
        ride_->ghosts.clear();
        if (n < 2 || d[0] != EXT_VERSION || d[1] > GHOSTS_MAX || n != 2 + (size_t)d[1] * (4 + OBJ_SIZE)) return;
        for (int k = 0; k < d[1]; k++) {
            const uint8_t* e = d + 2 + k * (4 + OBJ_SIZE);
            RideView::Ghost g;
            g.addr = (uint32_t)e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3];
            std::memcpy(g.obj, e + 4, OBJ_SIZE);
            ride_->ghosts.push_back(g);
        }
    }

    std::vector<AchievementDef> achievements() const override {
        return {
            {"md_market", "One Jump Ahead", "Escape the guards of Agrabah Market."},
            {"md_desert", "Half a Scarab", "Cross the Desert and find the first half of the scarab."},
            {"md_rooftops", "Street Rat", "Clear Agrabah Rooftops."},
            {"md_dungeon", "Jailbreak", "Escape the Sultan's Dungeon."},
            {"md_cave", "Touch Nothing but the Lamp", "Find the lamp in the Cave of Wonders."},
            {"md_escape", "Magic Carpet Ride", "Escape from the Cave of Wonders on the carpet."},
            {"md_lamp", "Friend Like Me", "Find your way out of the Genie's lamp."},
            {"md_palace", "Prince Ali", "Clear the Sultan's Palace."},
            {"md_quarters", "Snake in the Grass", "Clear Jafar's Quarters."},
            {"md_jafar", "Defeat Jafar", "Defeat Jafar and finish the game."},
            {"md_abu", "Monkey Business", "Find Abu's token and play one of his bonus rounds."},
            {"md_genie", "Phenomenal Cosmic Power", "Spin the Genie's bonus wheel."},
            {"md_peddler", "It's a Deal", "Buy something from the peddler."},
            {"md_apples", "Apple of My Eye", "Carry 50 apples at once.", false, 50},
            {"md_flawless", "Not a Scratch", "Clear a level without losing any health."},
            {"md_sword", "Sword Play", "Clear a level without throwing a single apple.", true},
            {"md_iron", "Diamond in the Rough", "Finish the game from a new game without losing a life.", true},
            {"md_difficult", "Street Rat Supreme", "Finish the game on Difficult from a new game."},
        };
    }

    // ---- Achievements: RAM only (docs/re_md_game.md section 5). Indices follow achievements().
    enum { A_MARKET, A_DESERT, A_ROOFTOPS, A_DUNGEON, A_CAVE, A_ESCAPE, A_LAMP, A_PALACE, A_QUARTERS, A_JAFAR, A_ABU,
           A_GENIE, A_PEDDLER, A_APPLES, A_FLAWLESS, A_SWORD, A_IRON, A_DIFFICULT };
    enum { S_FIRED, S_ATT, S_ATT_LEVEL, S_ATT_HURT, S_ATT_THROWN };
    // run bits: a game going on since the title (Aladdin first in Agrabah Market with score 0 and full lives), on
    // Difficult, a life lost, a developer option or the level-skip code used
    enum : uint32_t { R_RUN = 1, R_DIFFICULT = 2, R_LIFE_LOST = 4, R_DEV = 8 };

    static int digits(const RamViewBE& r, uint32_t a, int n) {   // ASCII digits as a number
        int v = 0;
        for (int i = 0; i < n; i++) v = v * 10 + (r.b(a + i) - '0');
        return v;
    }

    void achievement_frame(const MegaDrive& md, AchievementCtx& c) const override {
        if (c.prev.size() != 0x10000) return;
        const RamViewBE s{md.ram}, p{c.prev.data()};
        auto fire = [&](int i) {
            if (c.scratch[S_FIRED] & (1 << i)) return;
            c.scratch[S_FIRED] |= 1 << i;
            c.unlocked.push_back(i);
        };
        if (s.b(DEMO) || p.b(DEMO)) { c.scratch[S_ATT] = 0; return; }   // the attract demos are never play
        if (s.b(DEV_CHEAT) || s.b(DEV_MAP_VIEW) || s.b(DEV_FREEZE)) c.run_bits |= R_DEV;
        const int level = s.b(LEVEL);
        const bool here = s.b(ALADDIN) == 0x83, was = p.b(ALADDIN) == 0x83;
        // (a level the port's level select started: no run, an attempt from here)
        const bool selected = c.level_select && here;
        if (c.level_select) { c.level_select = false; c.run_bits = 0; }

        // Aladdin put into a level: a level attempt starts (also after every lost life). A new game: Agrabah Market from
        // the script's start with score 0 and the difficulty's full lives (5 / 3 / 2) - so also a continue there.
        if ((here && !was) || selected) {
            c.scratch[S_ATT] = 1;
            c.scratch[S_ATT_LEVEL] = level;
            c.scratch[S_ATT_HURT] = c.scratch[S_ATT_THROWN] = 0;
            const int diff = s.b(DIFFICULTY);
            const int full = diff == 0 ? 5 : diff == 1 ? 3 : 2;
            if (level == L_MARKET && s.l(SCRIPT) == SCRIPT_START && digits(s, SCORE, 6) == 0 && digits(s, LIVES, 1) == full)
                c.run_bits = R_RUN | (diff == 2 ? R_DIFFICULT : 0);
        }
        // the play-order script moved on to one of Abu's rounds (his token was found)
        if (s.l(SCRIPT) != p.l(SCRIPT) && (level == L_ABU_AGRABAH || level == L_ABU_CAVE)) fire(A_ABU);
        // lives: a death takes one; the last one goes with a continue
        if (digits(s, LIVES, 1) < digits(p, LIVES, 1) || s.b(CONTINUES) < p.b(CONTINUES)) c.run_bits |= R_LIFE_LOST;
        // in play (Aladdin there both frames, not paused)
        if (here && was && !s.b(PAUSED)) {
            if (s.b(HEALTH) < p.b(HEALTH)) c.scratch[S_ATT_HURT] = 1;
            const int apples = digits(s, APPLES, 2), papples = digits(p, APPLES, 2);
            if (apples < papples) c.scratch[S_ATT_THROWN] = 1;
            if (apples > papples) c.progress.push_back({A_APPLES, std::min(apples, 50)});
            if (apples >= 50) fire(A_APPLES);
            if (digits(s, GEMS, 2) < digits(p, GEMS, 2)) fire(A_PEDDLER);
        }
        // a level is over: by the game, or by the pause screen's level-skip code (never counted, and the run is over).
        // The Rug Ride's end (its script's last entry, $F2 at $1B5B3A) sets the flag to 1 only for a moment - the
        // gameplay loop counts it down in the same iteration, so a frame's RAM hardly ever shows it - and $FFF005 for
        // good: that one's rising edge is the end of the ride. (The third crash, the game's mercy, sets the flag to $FF.)
        const bool ride_end = level == L_RUG_RIDE && s.b(RIDE_DONE) && !p.b(RIDE_DONE);
        if (!ride_end && (!s.b(LEVEL_END) || p.b(LEVEL_END))) return;
        if (!ride_end && (s.b(PAUSED) || p.b(PAUSED))) { c.run_bits |= R_DEV; c.scratch[S_ATT] = 0; return; }
        switch (level) {
        case L_MARKET: fire(A_MARKET); break;
        case L_DESERT: fire(A_DESERT); break;
        case L_ROOFTOPS: fire(A_ROOFTOPS); break;
        case L_DUNGEON: fire(A_DUNGEON); break;
        case L_CAVE: fire(A_CAVE); break;
        case L_RUG_RIDE: if (s.b(RUG_CRASHES) < 3) fire(A_ESCAPE); break;   // (not the game's mercy after 3 crashes)
        case L_LAMP: fire(A_LAMP); break;
        case L_PALACE: fire(A_PALACE); break;
        case L_JAFAR_QUARTERS: fire(A_QUARTERS); break;
        default: break;
        }
        // the Genie's wheel turns after the level for each token (not after the Sultan's Palace, $1B16E0)
        if (s.b(GENIE_TOKENS) && level != L_PALACE) fire(A_GENIE);
        if (c.scratch[S_ATT] && c.scratch[S_ATT_LEVEL] == level) {
            const bool bonus = level == L_ABU_AGRABAH || level == L_ABU_CAVE || level == L_RUG_RIDE;
            if (!bonus && !c.scratch[S_ATT_HURT]) fire(A_FLAWLESS);
            // (Jafar's Quarters can't be cleared without apples)
            if (!bonus && level != L_JAFAR_QUARTERS && !c.scratch[S_ATT_THROWN]) fire(A_SWORD);
        }
        c.scratch[S_ATT] = 0;
        // Jafar's Palace is the last level: the ending follows
        if (level == L_JAFAR_PALACE) {
            fire(A_JAFAR);
            if (c.run_counts && (c.run_bits & R_RUN) && !(c.run_bits & R_DEV)) {
                if (!(c.run_bits & R_LIFE_LOST)) fire(A_IRON);
                if (c.run_bits & R_DIFFICULT) fire(A_DIFFICULT);
            }
            c.run_bits = 0;
        }
    }

    std::vector<CheatEntry> cheats() const override {
        return {
            {"invincible", "Invincibility", "Enemies, projectiles and hazards that hit Aladdin don't hurt him. Falling into pits or lava still costs a life."},
            {"inf_health", "Infinite Health", "Hits knock Aladdin back but the lamp's smoke never shrinks."},
            {"inf_lives", "Infinite Lives", "Losing a life never reduces the lives counter."},
            {"inf_apples", "Infinite Apples", "Throwing never uses up apples (and the stock is topped up to 10)."},
            {"inf_gems", "Infinite Gems", "Buying from the peddler never uses up gems (and the stock is topped up to 10)."},
            {"inf_continues", "Infinite Continues", "Game over never uses up a continue."},
            {"dev_cheat", "Developer Cheat Mode", "The CHEAT MODE of the developers' hidden debug screen (A C A C A C A C B B B B on OPTIONS): "
                                                  "Aladdin never loses health or lives."},
            {"map_view", "Developer Map View", "The debug screen's MAP VIEW MODE: Aladdin leaves play and the D-pad flies the view around the "
                                               "level. Switched off, Aladdin is put back at the middle of the view (switch it off over solid ground)."},
            {"level_skip", "Developer Level Skip", "Pause, then press A: the level is over at once. (The developers' own code, A B B A A B B A "
                                                   "while paused, works without this cheat.)"},
        };
    }

    std::vector<LevelEntry> levels() const override {
        std::vector<LevelEntry> v;
        for (const Level& l : kLevels) v.push_back({l.group, l.name});
        return v;
    }
    std::vector<std::string> level_variants() const override { return {"Practice", "Normal", "Difficult"}; }
    int level_variant_default() const override { return 1; }

    // Power-on -> the title (START skips the logos) -> the level. Power-on sets level 1 and the play-order script's
    // start once ($1AA41C) and the title keeps them (the developers' debug screen sets the start level the same way),
    // so the level, its script position and the difficulty are set before START; START is tapped through the title and
    // the new game's instructions screen ($1B2E9A) until the level loop begins ($1A8B50). It returns there, START
    // released: the level's story card then plays as after the level before (the player skips it with START, or it
    // runs out), and the level starts.
    bool start_level(MegaDrive& md, const GameConfig& cfg, int entry, int variant) override {
        if (entry < 0 || entry >= (int)std::size(kLevels)) return false;
        const int level = kLevels[entry].id;
        md.power_on();
        configure(md, cfg);
        for (int i = 0; i < 30; i++) md.run_frame();
        md.set_ram8(LEVEL, (uint8_t)level);
        md.set_ram32(SCRIPT, script_after(md, level));
        md.set_ram8(DIFFICULTY, (uint8_t)std::clamp(variant, 0, 2));
        bool loop = false;
        md.add_pc_hook(PC_LEVEL_LOAD, [&loop](MegaDrive& m) { loop = true; m.set_pad(0, 0); });
        for (int t = 0; t < 200 && !loop; t++) {
            md.set_pad(0, MD_START);
            for (int f = 0; f < 3 && !loop; f++) md.run_frame();
            md.set_pad(0, 0);
            for (int f = 0; f < 17 && !loop; f++) md.run_frame();
        }
        md.set_pad(0, 0);
        configure(md, cfg);   // (drops the hook)
        return loop;
    }

    std::vector<SoundEntry> sounds() const override { return sound_list(); }

    // The GEMS sound driver runs on the Z80 by itself: the 68000 only queues commands. Park the game on the SEGA logo
    // (the driver is set up at power-on, $1AA344) and queue the commands the way the game's library does.
    void jukebox_boot(MegaDrive& md) override {
        md.power_on();
        for (int i = 0; i < 40; i++) md.run_frame();
        freeze_68k(md);
        stop_sound(md);
        for (int i = 0; i < 4; i++) md.run_frame();   // let the driver take the stop request
    }

    // The game stops a sequence before starting it again ($1E58B8, $1E589A); a new tune also stops the one playing.
    void play_sound(MegaDrive& md, int id) override {
        if (is_music(id)) gems_command(md, {0x16});
        else gems_command(md, {0x12, (uint8_t)id});
        gems_command(md, {0x10, (uint8_t)id});
    }

    void stop_sound(MegaDrive& md) override { gems_command(md, {0x16}); }

private:
    std::unique_ptr<Wide> wide_{new Wide};
    std::unique_ptr<RideView> ride_{new RideView};
    Rom rom_ = Rom::Wide;   // the ROM the game runs on now (rom_for)

};

}  // namespace

GameModule* create_md_aladdin() { return new MdAladdin; }
