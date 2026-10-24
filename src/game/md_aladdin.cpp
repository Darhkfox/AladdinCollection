// Aladdin (Mega Drive / Genesis, Virgin Games 1993): widescreen, HUD, cheats, level select, sound, achievements.
// Addresses and techniques: docs/re_md_game.md, docs/re_md_wide.md.
#include "game.h"
#include "md_common.h"
#include "../core/md.h"
#include <algorithm>
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

// module_state: [0] frame (+1) the gameplay loop last ran in, 0 after it was left; [1] the developer flags the cheats
// set (bit 0 cheat mode, bit 1 map view), to clear them when they are switched off; [2] a level's fade-in is running
// (shown as play: the picture is already the level's, so it starts wide and slid instead of jumping when play begins);
// [3..6] boss tunes held back (boss_tune_held)
enum { MS_PLAY = 0, MS_DEV = 1, MS_FADE = 2, MS_BOSS = 3, MS_BOSS_N = 4 };

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
                   CAVE_SCRCLIP = CAVE + 0x2C0, CAVE_REACH = CAVE + 0x300, CAVE_DESPAWN = CAVE + 0x380;

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
    const uint16_t e = (uint16_t)E, o = (uint16_t)sprite_left(E);
    auto ff = [](size_t n) { return std::vector<uint16_t>(n, 0xFFFF); };
    // d0 = left reach kl = ceil((E - s)/16), d1 = right reach kr = ceil((E + s)/16) in blocks (s: view_slide); uses d2
    Asm reach;
    reach << std::initializer_list<uint16_t>{0x3039, 0x00FF, 0x7DF6,   // move.w $FF7DF6, d0 (camX)
                                             0x3239, 0x00FF, 0x7DB6,   // move.w $FF7DB6, d1 (W)
                                             0xE949, 0x0441, (uint16_t)CAM_SPAN_GAP,   // lsl.w #4, d1; subi.w #354, d1
                                             0x0C41, (uint16_t)(2 * E)};               // cmpi.w #2E, d1
    const size_t to_wide = reach.branch(0x6C00);                         // bge.s wide
    reach << std::initializer_list<uint16_t>{0xE241, 0x9240};            // asr.w #1, d1; sub.w d0, d1 (narrow: centred)
    const size_t narrow_done = reach.branch(0x6000);                     // bra.s clamp
    reach.land(to_wide);
    reach << std::initializer_list<uint16_t>{0x343C, e, 0x9440};          // wide: move.w #E, d2; sub.w d0, d2
    const size_t to_left = reach.branch(0x6A00);                         // bpl.s left (camX <= E)
    reach << std::initializer_list<uint16_t>{0x9240, 0x0441, e};          // sub.w d0, d1; subi.w #E, d1 (span - E - camX)
    const size_t right_done = reach.branch(0x6B00);                      // bmi.s clamp (near the right end)
    reach << std::initializer_list<uint16_t>{0x7200};                    // moveq #0, d1
    const size_t mid_done = reach.branch(0x6000);                        // bra.s clamp
    reach.land(to_left);
    reach << std::initializer_list<uint16_t>{0x3202};                    // left: move.w d2, d1
    reach.land(narrow_done);
    reach.land(right_done);
    reach.land(mid_done);
    reach << std::initializer_list<uint16_t>{0x0C41, e, 0x6F04, 0x323C, e,                         // clamp: s <= E
                                             0x0C41, (uint16_t)-E, 0x6C04, 0x323C, (uint16_t)-E,   //        s >= -E
                                             0x303C, (uint16_t)(E + 15), 0x9041, 0xE840,           // d0 = (E+15-s) >> 4
                                             0x0641, (uint16_t)(E + 15), 0xE841, 0x4E75};          // d1 = (E+15+s) >> 4
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
    return {
        {0x1AB446, {0x6100, 0x2FB4}, {0x4EB8, (uint16_t)CAVE_LSPAWN}},
        {0x1AB554, {0x6100, 0x2EB0}, {0x4EB8, (uint16_t)CAVE_RSPAWN}},
        {0x1AE4A8, {0x2079, 0x00FF, 0x7DAC, 0x43F8, 0x4154, 0x3C39, 0x00FF, 0x7E06, 0x0206, 0x00F0, 0x45F9, 0x00FF,
                    0xAE87, 0x383C, 0x0016},
                   {0x4EB8, (uint16_t)CAVE_ROWSPAWN, 0x6018, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71,
                    0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71}},
        {CAVE_REACH, ff(reach.w.size()), reach.w},
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

// The gameplay loop ran lately (in play or paused), in a game.
bool in_play(const MegaDrive& md) {
    return md.module_state[MS_PLAY] != 0 && (uint32_t)md.frame_count + 1 - md.module_state[MS_PLAY] <= 4 && !attract(md);
}

// A level on screen in a game: in play, or fading in (its first frames, before the gameplay loop runs).
bool in_gameplay(const MegaDrive& md) { return (md.module_state[MS_FADE] != 0 && !attract(md)) || in_play(md); }

// Gameplay shown wide: all but Abu's bonus rounds (a 336-px room) and the Rug Ride, whose obstacles are sent in at the
// 4:3 view's right edge on a timer (wider, they would pop up in the middle of the right extension). Those stay 4:3.
bool wide_view(const MegaDrive& md) {
    const int level = md.ram8(LEVEL);
    return in_gameplay(md) && level != L_ABU_AGRABAH && level != L_ABU_CAVE && level != L_RUG_RIDE;
}

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
int sprite_xoff(const MegaDrive& md, int index, uint16_t attr, int x, int y) {
    (void)y;
    const VDP& v = md.vdp;
    const int side = hud_side(attr & 0x7FF, x);
    if (side) return v.view_shift + ((v.hook_flags & 1) ? side * v.ext : 0);
    return screen_fixed(v, index) ? v.view_shift : 0;
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

// Per-session widescreen state the VDP hooks work with (VDP::hook_host).
struct Wide {
    MegaDrive* md = nullptr;
    ScrollProbe probe;
    int16_t shift_a[VDP::kMaxLines] = {};   // plane A's slide per line when it isn't the view's (Sultan's Dungeon)
    bool shift_a_on = false;
    bool active = false;                    // this frame is drawn wide (the slide applies)
    int cam = 0;                            // the camera the slide in force was worked out for
    uint16_t t0[ScrollProbe::kWords];
};

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
    const int s = view_slide(camx, md.ram16(MAP_W), v.ext);
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

// Before each frame is drawn (MegaDrive::on_frame_start): whether it is drawn wide (for the whole frame), and its slide
// from line 0's scroll.
void update_view(Wide& w) {
    MegaDrive& md = *w.md;
    VDP& v = md.vdp;
    v.view_shift = 0;
    v.view_shift_b_on = false;
    w.shift_a_on = false;
    w.active = v.ext != 0 && wide_view(md);
    if (w.active) apply_slide(w, line_camera(md, 0, md.ram8(SWAP_PLANES) != 0), 0);
}

// Before each line is drawn: the slide follows the scroll line by line. On a lag frame the game writes the scroll (and
// the sprite table) while the frame is drawn, and a slide worked out once at the frame's start would hold the old
// camera against the new scroll: a one-frame jump of the picture the slide holds still.
void pre_line_hook(void* host, int line) {
    Wide& w = *(Wide*)host;
    if (!w.active) return;
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

int sprite_xoff_hook(void* host, int index, uint16_t attr, int x, int y) {
    const Wide& w = *(Wide*)host;
    return w.active ? sprite_xoff(*w.md, index, attr, x, y) : 0;
}
bool sprite_is_hud_hook(void* host, int index) { return sprite_is_hud(*((Wide*)host)->md, index); }

void line_hook(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) {
    (void)sprites;
    Wide& w = *(Wide*)host;
    MegaDrive& md = *w.md;
    VDP& v = md.vdp;
    v.edge_mask_l = v.edge_mask_r = 0;
    // (The Rug Ride doesn't stream: plane A is one 512-px loop of the lava cave the camera runs along, its "map" nominal.)
    if (!w.active) return;
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
    static std::vector<RomPatch> rom_for(const MegaDrive& md, const GameConfig& cfg) {
        return attract(md) ? std::vector<RomPatch>() : patch_list(cfg);
    }

    bool can_configure(const MegaDrive& md, const GameConfig& cfg) const override { return patches_safe(md, rom_for(md, cfg)); }

    void configure(MegaDrive& md, const GameConfig& cfg) override {
        apply_patches(md, rom_for(md, cfg));
        rom_attract_ = attract(md);
        md.clear_pc_hooks();
        auto play = [](MegaDrive& m) { m.module_state[MS_PLAY] = (uint32_t)m.frame_count + 1; m.module_state[MS_FADE] = 0; };
        auto leave = [](MegaDrive& m) { m.module_state[MS_PLAY] = 0; m.module_state[MS_FADE] = 0; };
        md.add_pc_hook(PC_MAIN_LOOP, play);
        md.add_pc_hook(PC_PAUSE_LOOP, play);
        md.add_pc_hook(PC_LEVEL_END, leave);
        md.add_pc_hook(PC_DEATH, leave);
        md.add_pc_hook(PC_PIT, leave);
        auto fade = [](MegaDrive& m) { m.module_state[MS_FADE] = 1; };
        md.add_pc_hook(PC_FADE_IN, fade);
        md.add_pc_hook(PC_RESPAWN_FADE, fade);
        const bool wide = cfg.ext > 0;
        // (with the widescreen patches only: in the attract mode and at 4:3 the pool is the original's)
        if (wide) {
            md.add_pc_hook(PC_TILES_FULL, [this](MegaDrive& m) { if (!rom_attract_) tile_pool_make_room(m); });
            for (const SlotSearch& ss : kSlotSearches)
                md.add_pc_hook(ss.rts, [this, &ss](MegaDrive& m) { if (!rom_attract_ && in_play(m)) slot_search_done(m, ss); });
            for (const BossHandler& h : kBossHandlers)
                md.add_pc_hook(h.at, [this, &h](MegaDrive& m) { if (!rom_attract_) boss_tune_spawned(m, h); });
            md.add_pc_hook(PC_STREAMED, [](MegaDrive& m) { boss_tune_held(m); });
        }
        wide_->md = &md;
        md.vdp.line_hook = wide ? line_hook : nullptr;
        md.vdp.pre_line_hook = wide ? pre_line_hook : nullptr;
        md.vdp.sprite_xoff = wide ? sprite_xoff_hook : nullptr;
        md.vdp.sprite_is_hud = wide ? sprite_is_hud_hook : nullptr;
        md.vdp.edge_mask_l = md.vdp.edge_mask_r = 0;
        md.vdp.sprite_x10 = false;   // (per frame: post_frame)
        md.vdp.hook_host = wide_.get();
        md.vdp.hook_flags = cfg.hud_anchor ? 1 : 0;
        Wide* w = wide_.get();
        md.on_frame_start = wide ? std::function<void(MegaDrive&)>([w](MegaDrive&) { update_view(*w); }) : nullptr;
        md.vdp.view_shift = 0;
        md.vdp.view_shift_b_on = false;
        if (wide) update_view(*w);   // (the frame about to be drawn after a width change or a loaded state)
    }

    void pre_frame(MegaDrive& md, const GameConfig& cfg) override {
        if (attract(md) != rom_attract_) {
            const std::vector<RomPatch> rom = rom_for(md, cfg);
            if (patches_safe(md, rom)) {   // (else a later frame)
                apply_patches(md, rom);
                rom_attract_ = !rom_attract_;
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
        // Sprite X read as 10 bits only in play (the other screens, shown 4:3, may leave junk in the upper bits)
        const int E = cfg.ext;
        md.vdp.sprite_x10 = E > 0 && in_gameplay(md) && (448 + 2 * E > 512 || sprite_left(E) > 0);
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
        // a level is over: by the game, or by the pause screen's level-skip code (never counted, and the run is over)
        if (!s.b(LEVEL_END) || p.b(LEVEL_END)) return;
        if (s.b(PAUSED) || p.b(PAUSED)) { c.run_bits |= R_DEV; c.scratch[S_ATT] = 0; return; }
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
    bool rom_attract_ = false;   // the ROM is the attract mode's (original)

};

}  // namespace

GameModule* create_md_aladdin() { return new MdAladdin; }
