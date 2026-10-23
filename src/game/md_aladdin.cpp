// Aladdin (Mega Drive / Genesis, Virgin Games 1993): widescreen, HUD, cheats, level select, sound, achievements.
// Addresses and techniques: docs/re_md_game.md, docs/re_md_wide.md.
#include "game.h"
#include "md_common.h"
#include "../core/md.h"
#include <algorithm>
#include <cstring>
#include <iterator>

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
constexpr uint32_t DEV_CHEAT = 0xFF7E20, DEV_MAP_VIEW = 0xFF7273;
constexpr uint32_t SCRIPT_START = 0x4082;

// ROM code the module watches (PC hooks)
constexpr uint32_t PC_MAIN_LOOP = 0x1A8C16;   // top of the gameplay loop
constexpr uint32_t PC_PAUSE_LOOP = 0x1A9230;  // the pause loop
constexpr uint32_t PC_LEVEL_END = 0x1A8E5C;   // a level is over: next level / ending
constexpr uint32_t PC_DEATH = 0x1A8F82;       // the death screen (Aladdin, Abu and the carpet on black)
constexpr uint32_t PC_PIT = 0x1A902E;         // fell below the map: a life lost without the death screen
constexpr uint32_t PC_LEVEL_INIT = 0x1AA484;  // level init (start_level waits for it)

// module_state: [0] frame (+1) the gameplay loop last ran in, 0 after it was left
enum { MS_PLAY = 0 };

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

// ---- widescreen (docs/re_md_wide.md) -------------------------------------------------------------------
// The level map is streamed into a 512-px plane one 16-px block column at a time as the camera crosses 16 px: the
// plane holds map x [L, L+368) (L = $FF7E06), the 4:3 view [L+16+d, L+336+d), |d| < 16. The widened view's extra
// columns are drawn per line straight from the RAM map (line_hook), so the plane needs no patch. Objects are part of
// the map (object blocks, $FFAE87 / $4154) and spawn when their column or row is streamed: the caves spawn the
// columns the widened view reaches as well; the despawn window and the sprite clip are widened to match.
constexpr uint32_t CAVE = 0x7000;   // the $FF fill at $69E0-$FFFF (no references), reachable with jsr abs.w
constexpr uint32_t CAVE_LSPAWN = CAVE, CAVE_RSPAWN = CAVE + 0x80, CAVE_ROWSPAWN = CAVE + 0x100, CAVE_CLIP = CAVE + 0x180;

// An object stored in block column c appears at screen x 16c - (camX + 16): the original streams column L/16 (left,
// screen x -16-d) and L/16+22 (right, 336-d), |d| < 16, just outside its view. Spawning k = E/16 (rounded up) columns
// further out keeps the same margin outside the widened view.
int ext_blocks(int E) { return (E + 15) / 16; }
int sprite_bias(int E) { return E > 96 ? 64 : 0; }          // object sprites drawn this far right in VDP X (see sprite_xoff)

std::vector<RomPatch> wide_patches(int E) {
    const uint16_t kl = (uint16_t)ext_blocks(E), kr = kl, o = (uint16_t)sprite_bias(E);
    auto ff = [](size_t n) { return std::vector<uint16_t>(n, 0xFFFF); };
    // after the left column writer's own spawn (column L), the column L-16kl (if >= 1)
    std::vector<uint16_t> lspawn = {
        0x4EB9, 0x001A, 0xE3FC,                    // jsr $1AE3FC (the replaced call)
        0x3039, 0x00FF, 0x7E06,                    // move.w $FF7E06, d0
        0x0C40, (uint16_t)(16 * (kl + 1)),         // cmpi.w #16(kl+1), d0
        0x652A,                                    // bcs.s done
        0x0479, (uint16_t)(16 * kl), 0x00FF, 0x7E06,   // subi.w #16kl, $FF7E06
        0x04B9, 0x0000, (uint16_t)(2 * kl), 0x00FF, 0x7DAC,   // subi.l #2kl, $FF7DAC (the column's map pointer)
        0x4EB9, 0x001A, 0xE3FC,                    // jsr $1AE3FC
        0x06B9, 0x0000, (uint16_t)(2 * kl), 0x00FF, 0x7DAC,
        0x0679, (uint16_t)(16 * kl), 0x00FF, 0x7E06,
        0x4E75};                                   // done: rts
    // after the right column writer's own spawn (column L+22), the column L+22+kr (if inside the map)
    std::vector<uint16_t> rspawn = {
        0x4EB9, 0x001A, 0xE406,                    // jsr $1AE406
        0x3039, 0x00FF, 0x7E06,                    // move.w $FF7E06, d0
        0xE848,                                    // lsr.w #4, d0
        0x0640, (uint16_t)(22 + kr),               // addi.w #22+kr, d0
        0xB079, 0x00FF, 0x7DB6,                    // cmp.w $FF7DB6, d0 (map width)
        0x642A,                                    // bcc.s done
        0x0679, (uint16_t)(16 * kr), 0x00FF, 0x7E06,
        0x06B9, 0x0000, (uint16_t)(2 * kr), 0x00FF, 0x7DAC,
        0x4EB9, 0x001A, 0xE406,
        0x04B9, 0x0000, (uint16_t)(2 * kr), 0x00FF, 0x7DAC,
        0x0479, (uint16_t)(16 * kr), 0x00FF, 0x7E06,
        0x4E75};
    // a streamed row spawns blocks [L/16-kl, L/16+22+kr] inside the map instead of [L/16, L/16+22]: the loop's
    // a0 (map pointer), d6 (x), d4 (count - 1), a1, a2
    std::vector<uint16_t> rowspawn = {
        0x3C39, 0x00FF, 0x7E06,                    // move.w $FF7E06, d6
        0xE84E,                                    // lsr.w #4, d6
        0x3806,                                    // move.w d6, d4
        0x3006,                                    // move.w d6, d0
        0x0446, kl,                                // subi.w #kl, d6
        0x6A02, 0x7C00,                            // bpl.s +2; moveq #0, d6
        0x0644, (uint16_t)(22 + kr),               // addi.w #22+kr, d4
        0x3239, 0x00FF, 0x7DB6,                    // move.w $FF7DB6, d1
        0x5341,                                    // subq.w #1, d1
        0xB841, 0x6F02, 0x3801,                    // cmp.w d1, d4; ble.s +2; move.w d1, d4
        0x9846,                                    // sub.w d6, d4
        0x2079, 0x00FF, 0x7DAC,                    // movea.l $FF7DAC, a0
        0x3206, 0x9240,                            // move.w d6, d1; sub.w d0, d1
        0xD0C1, 0xD0C1,                            // adda.w d1, a0 (x2)
        0xE94E,                                    // lsl.w #4, d6
        0x43F8, 0x4154,                            // lea $4154.w, a1
        0x45F9, 0x00FF, 0xAE87,                    // lea $FFAE87, a2
        0x4E75};
    // object sprite clip: VDP X in [0, 448+E) (+ the bias)
    std::vector<uint16_t> clip = {0x9647};         // sub.w d7, d3 (the replaced instruction)
    if (o) { clip.push_back(0x0643); clip.push_back(o); }
    clip.push_back(0x0C43);
    clip.push_back((uint16_t)(448 + E + o));
    clip.push_back(0x4E75);
    return {
        {0x1AB446, {0x6100, 0x2FB4}, {0x4EB8, (uint16_t)CAVE_LSPAWN}},
        {0x1AB554, {0x6100, 0x2EB0}, {0x4EB8, (uint16_t)CAVE_RSPAWN}},
        {0x1AE4A8, {0x2079, 0x00FF, 0x7DAC, 0x43F8, 0x4154, 0x3C39, 0x00FF, 0x7E06, 0x0206, 0x00F0, 0x45F9, 0x00FF,
                    0xAE87, 0x383C, 0x0016},
                   {0x4EB8, (uint16_t)CAVE_ROWSPAWN, 0x6018, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71,
                    0x4E71, 0x4E71, 0x4E71, 0x4E71, 0x4E71}},
        {CAVE_LSPAWN, ff(lspawn.size()), lspawn},
        {CAVE_RSPAWN, ff(rspawn.size()), rspawn},
        {CAVE_ROWSPAWN, ff(rowspawn.size()), rowspawn},
        // despawn window: object x in [camX - $50 - 16kl, camX + $190 + 16kr]
        {0x1ADEBA, {0x0640, 0x0050}, {0x0640, (uint16_t)(0x50 + 16 * kl)}},
        {0x1ADECC, {0x0640, 0x0190}, {0x0640, (uint16_t)(0x190 + 16 * kr)}},
        // sprite clip: map objects (sub.w d7, d3; cmpi.w #$1BF, d3 -> jsr CAVE_CLIP; nop) and screen-fixed ones
        {0x1ABAD0, {0x9647, 0x0C43, 0x01BF}, {0x4EB8, (uint16_t)CAVE_CLIP, 0x4E71}},
        {CAVE_CLIP, ff(clip.size()), clip},
        {0x1ABA8C, {0x0443, 0x0080, 0x0C43, 0x01BF}, {0x0443, (uint16_t)(0x80 - o), 0x0C43, (uint16_t)(448 + E + o)}},
    };
}

bool in_gameplay(const MegaDrive& md) {
    return md.module_state[MS_PLAY] != 0 && (uint32_t)md.frame_count + 1 - md.module_state[MS_PLAY] <= 4;
}

// The status bar's sprites (the lamp and its smoke, Aladdin's face and lives top-left / bottom-left; apples, gems
// and the score on the right) use tiles $680-$6FF and $7C0-$7E7, which nothing else does (checked in every level).
bool hud_tile(int tile) { return (tile >= 0x680 && tile < 0x700) || (tile >= 0x7C0 && tile < 0x7E8); }

int sprite_xoff(void* host, int index, uint16_t attr, int x, int y) {
    (void)index; (void)y;
    const MegaDrive& md = *(const MegaDrive*)host;
    if (!in_gameplay(md)) return 0;
    const int E = md.vdp.ext;
    if (hud_tile(attr & 0x7FF)) {
        if (!(md.vdp.hook_flags & 1)) return 0;
        return x < 0x100 ? -E : E;
    }
    return -sprite_bias(E);
}

// The line's horizontal scroll for a plane (H scroll table, reg 11 mode).
int line_hscroll(const VDP& v, int line, bool plane_b) {
    const uint32_t hsb = (v.reg[13] & 0x3F) << 10;
    int off;
    switch (v.reg[11] & 3) {
    case 0: off = 0; break;
    case 1: off = (line & 7) * 4; break;
    case 2: off = (line & ~7) * 4; break;
    default: off = line * 4; break;
    }
    return v.vram16(hsb + off + (plane_b ? 2 : 0)) & 0x3FF;
}

// The map plane's scroll on a line: the map x shown at screen x 0 and the map y of the line. The plane holds map x
// [L, L+368) and y [Y, Y+256): the map position a screen pixel shows is the one, among those congruent mod 512 / 256
// with its plane position, inside that range.
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

// The 4:3 view only ever shows map x [16, W*16 - 18) (the camera's clamps, docs/re_md_wide.md section 4).
constexpr int VIEW_LEFT = 16, VIEW_RIGHT_GAP = 18;

// The extension columns of the map plane, drawn from the RAM map (the plane only holds the 4:3 view's columns).
void draw_map_ext(const MegaDrive& md, const LineView& lv, uint8_t* buf) {
    const VDP& v = md.vdp;
    const int E = v.ext, TW = 320 + 2 * E;
    const int W = md.ram16(MAP_W), H = md.ram16(MAP_H);
    const int my = lv.my;
    if (my < 0 || my >= H * 16) return;
    const uint32_t row = md.ram32(MAP_ROWS + 64 * (my >> 4)) & 0xFFFFFF;
    const uint32_t defs = md.ram32(MAP_DEFS) & 0xFFFFFF;
    const int cell_row = ((my >> 3) & 1) * 4;
    uint8_t px[8];
    for (int side = 0; side < 2; side++) {
        const int x0 = side ? E + 320 : 0, x1 = side ? TW : E;
        int x = x0;
        while (x < x1) {
            const int mx = lv.mx0 + x - E;
            const int n = std::min(8 - (mx & 7), x1 - x);
            if (mx >= 0 && mx < W * 16) {
                const uint16_t blk = md.ram16(row + 2 * (mx >> 4));
                const uint32_t a = defs + blk + cell_row + ((mx >> 3) & 1) * 2;
                const uint16_t tile = a >= 0xFF0000 ? md.ram16(a) : md.rom16(a);
                v.pattern_row(tile, my & 7, px);
                memcpy(buf + x, px + (mx & 7), n);
            }
            x += n;
        }
    }
}

// HUD sprites stay visible over the level-edge masks.
bool sprite_is_hud(void* host, int index) {
    const VDP& v = ((const MegaDrive*)host)->vdp;
    const uint32_t sat = (v.reg[5] & 0x7E) << 9;
    return hud_tile(v.vram16(sat + index * 8 + 4) & 0x7FF);
}

void line_hook(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) {
    (void)sprites;
    MegaDrive& md = *(MegaDrive*)host;
    VDP& v = md.vdp;
    v.edge_mask_l = v.edge_mask_r = 0;
    if (!in_gameplay(md)) return;
    // The Rug Ride doesn't stream: plane A is one 512-px loop of the lava cave the camera runs along (its "map" is
    // nominal), so the VDP's own wrap-around is right.
    if (md.ram8(LEVEL) == L_RUG_RIDE) return;
    const bool swapped = md.ram8(SWAP_PLANES) != 0;
    const LineView lv = line_view(md, line, swapped);
    draw_map_ext(md, lv, swapped ? plane_b : plane_a);
    // Level edges: black (the HUD stays), from this line's own scroll. (The VDP's masks rather than FrameInfo's: those
    // would cover the HUD anchored there.)
    const int E = v.ext, TW = 320 + 2 * E;
    const int right = md.ram16(MAP_W) * 16 - VIEW_RIGHT_GAP;
    v.edge_mask_l = std::clamp(VIEW_LEFT - lv.mx0 + E, 0, E);
    v.edge_mask_r = std::clamp(TW - (right - lv.mx0 + E), 0, E);
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

    static std::vector<RomPatch> patch_list(const GameConfig& cfg) {
        std::vector<RomPatch> p;
        auto add = [&](const std::vector<RomPatch>& v) { p.insert(p.end(), v.begin(), v.end()); };
        if (cfg.cheat("invincible")) add(kInvincible);
        if (cfg.cheat("inf_health")) add(kInfHealth);
        if (cfg.cheat("inf_lives")) add(kInfLives);
        if (cfg.cheat("inf_apples")) add(kInfApples);
        if (cfg.cheat("inf_gems")) add(kInfGems);
        if (cfg.cheat("inf_continues")) add(kInfContinues);
        if (cfg.ext > 0) add(wide_patches(cfg.ext));
        return p;
    }

    bool can_configure(const MegaDrive& md, const GameConfig& cfg) const override { return patches_safe(md, patch_list(cfg)); }

    void configure(MegaDrive& md, const GameConfig& cfg) override {
        apply_patches(md, patch_list(cfg));
        md.clear_pc_hooks();
        auto play = [](MegaDrive& m) { m.module_state[MS_PLAY] = (uint32_t)m.frame_count + 1; };
        auto leave = [](MegaDrive& m) { m.module_state[MS_PLAY] = 0; };
        md.add_pc_hook(PC_MAIN_LOOP, play);
        md.add_pc_hook(PC_PAUSE_LOOP, play);
        md.add_pc_hook(PC_LEVEL_END, leave);
        md.add_pc_hook(PC_DEATH, leave);
        md.add_pc_hook(PC_PIT, leave);
        const bool wide = cfg.ext > 0;
        md.vdp.line_hook = wide ? line_hook : nullptr;
        md.vdp.sprite_xoff = wide ? sprite_xoff : nullptr;
        md.vdp.sprite_is_hud = wide ? sprite_is_hud : nullptr;
        md.vdp.edge_mask_l = md.vdp.edge_mask_r = 0;
        md.vdp.sprite_x10 = wide && 448 + cfg.ext + sprite_bias(cfg.ext) > 512;
        md.vdp.hook_host = &md;
        md.vdp.hook_flags = cfg.hud_anchor ? 1 : 0;
    }

    void pre_frame(MegaDrive& md, const GameConfig& cfg) override {
        if (!in_gameplay(md)) return;
        // Infinite apples / gems also top a low stock up (the decrement no longer runs, but 0 can't throw / buy).
        if (cfg.cheat("inf_apples") && md.ram16(APPLES) < 0x3130) md.set_ram16(APPLES, 0x3130);
        if (cfg.cheat("inf_gems") && md.ram16(GEMS) < 0x3130) md.set_ram16(GEMS, 0x3130);
        if (cfg.cheat("dev_cheat")) md.set_ram8(DEV_CHEAT, 0xFF);
    }

    void post_frame(MegaDrive& md, const GameConfig& cfg, FrameInfo& info) override {
        const int E = cfg.ext;
        const int level = md.ram8(LEVEL);
        // Shown 4:3: Abu's bonus rounds (a 336-px room) and the Rug Ride, whose obstacles are sent in at the 4:3 view's
        // right edge on a timer (wider, they would pop up in the middle of the right extension).
        info.gameplay = in_gameplay(md) && level != L_ABU_AGRABAH && level != L_ABU_CAVE && level != L_RUG_RIDE;
        (void)E;   // (the level-edge masks are drawn per line by line_hook, so the HUD stays over them)
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
    // so the level, its script position and the difficulty are set before START; the game then shows that level's
    // title card and starts it as it would after the one before.
    bool start_level(MegaDrive& md, const GameConfig& cfg, int entry, int variant) override {
        if (entry < 0 || entry >= (int)std::size(kLevels)) return false;
        const int level = kLevels[entry].id;
        md.power_on();
        configure(md, cfg);
        for (int i = 0; i < 30; i++) md.run_frame();
        md.set_ram8(LEVEL, (uint8_t)level);
        md.set_ram32(SCRIPT, script_after(md, level));
        md.set_ram8(DIFFICULTY, (uint8_t)std::clamp(variant, 0, 2));
        bool init = false;
        md.add_pc_hook(PC_LEVEL_INIT, [&init](MegaDrive&) { init = true; });
        for (int t = 0; t < 200 && !init; t++) {
            md.set_pad(0, MD_START);
            for (int f = 0; f < 3; f++) md.run_frame();
            md.set_pad(0, 0);
            for (int f = 0; f < 17 && !init; f++) md.run_frame();
        }
        md.set_pad(0, 0);
        configure(md, cfg);   // (drops the init hook)
        if (!init) return false;
        return run_until(md, [&]() { return in_gameplay(md); }, 600);
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
    // The GEMS command FIFO in Z80 RAM ($1B40, 64 bytes, write index at $0036): $FF, command, arguments ($1E57F4).
    static void gems_command(MegaDrive& md, std::initializer_list<uint8_t> bytes) {
        uint8_t i = md.zram[0x0036];
        md.zram[0x1B40 + i] = 0xFF;
        i = (i + 1) & 0x3F;
        for (uint8_t b : bytes) {
            md.zram[0x1B40 + i] = b;
            i = (i + 1) & 0x3F;
        }
        md.zram[0x0036] = i;
    }
};

}  // namespace

GameModule* create_md_aladdin() { return new MdAladdin; }
