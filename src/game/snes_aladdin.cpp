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
// stay at the picture's middle. In the Lava Cavern, which scrolls by itself, the limits the game sets at its screen's
// edges (the wall ahead, being left behind, its objects' lifetime) follow the picture's edges instead. The staff roll,
// a level the game flies its camera through by itself, is drawn wide the same way, centred, its sprites flying out
// into the sides; its credits (BG3) stay on the game's 256 px.
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
constexpr uint32_t kCamHoldX = 0x1A10, kCamHoldY = 0x1A12;            // nonzero: the camera doesn't follow in x / y
constexpr uint32_t kZoneMaxX = 0x1A42;                                 // a zone's right limit for the camera
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
// STAGE CLEAR: screen $24; its task's waits from the picture's set-up to the fade-out after B
constexpr uint8_t kStageClearScreen = 0x24;
constexpr uint32_t kStageClearFrom = 0x81F7B6, kStageClearTo = 0x81F90C;
// Its Genie (object type $8F, $83:FA45): out of sight at x -32 he waits, then flies right 4 px a frame for $56 frames
// at y 200 ($40 counts them down; his script's frame loop resumes at $83:FA8C), is moved to y 128 at x 312 and flies
// back left as many ($14 set: the second pass), and is deleted at x -32: every end of his path is past the game's
// screen by his own width. His first frame drawn shows tiles his graphics' upload hasn't replaced yet.
constexpr uint8_t kGenieType = 0x8F;
constexpr uint32_t kGenieFly = 0x83FA8C;
// The staff roll: a level of its own (index $1C, stage 8 act 8) the game runs with the attract demos' flag ($0331 = 2)
// from the ending, its main task looping at $82:B447 (JSL $81:830D, the level's frame work: objects, camera, streaming,
// spawner, OAM) and waiting at $82:B44D. Its camera flies right by itself (0.75 px a frame, with y swings through the
// clouds and the city) from 0 to the level's end ($1A04 = $1F00), where THE END stays. BG1 (the near clouds and towers)
// and BG2 (the sky, its stars, the moon, the cloud bank and the far city; mode 0: half the camera's speed) are streamed
// from the level's data as in a level; BG3 holds the credits' text, fixed on the screen.
constexpr uint8_t kRollLevel = 0x1C, kRollStage = 8, kRollAct = 8;
// Its frame work's draw list and OAM build ($81:8379 JSL $81:9198, JSL $81:9258; done at $81:8381). Drawn wide, the
// birds in the sides cost them enough to drop frames the console doesn't (36 at 106, 122 at 140, nearly all while the
// flock is out): the CPU runs those two at a quarter of their time there (Remove Slowdown's way), so the roll keeps
// the console's pace (at 4:3 it drops 3 frames in all, which this takes away too: it reaches THE END 3 frames sooner).
constexpr uint32_t kRollDraw = 0x818379, kRollDrawDone = 0x818381;
constexpr int kRollTurbo = 4;
// The roll's end: the roll's player (its object at $08DB: Aladdin and Jasmine, x / y at $08E7 / $08EA; their carpet,
// type $72, is drawn at the player's place, $83:F435) is flown right, faster and faster, from $81:E3B3 until it is
// past camera + $140 ($81:E3C9 ADC #$0140 / CMP $0C); then the script signals THE END's sequence ($81:E3D2 INC $1B14:
// the object waiting on it, $84:AC6F, goes on) and moves the player out of the level (x high byte $22, y high byte
// $FE), 64 px past the screen's edge at 4:3. Drawn wide, the riders and the carpet vanished in the right side. The
// flight now goes on to the picture's edge + 64 px (kRollExitTest), while THE END keeps the console's timing: the hook
// gives the signal at the original's moment (nonzero, the script's own INC is skipped, kRollExitSignal), and the
// camera, which follows the player's y ($81:C6EF LDA $08EA), is given the moved-out y meanwhile (kRollCamY), as at 4:3.
constexpr uint32_t kRollExitTest = 0x81E3C9, kRollExitSignal = 0x81E3D2, kRollExitOn = 0x81E3D5;
constexpr uint32_t kRollCamY = 0x81C6EF;
constexpr uint32_t kRollCarpetGone = 0x1B14, kRollPlayerX = 0x08E7, kRollPlayerY = 0x08EA;
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
// The delete every object's end goes through ($83:96E2, DP the object, +$34 its list index: killed, collected, out of
// the keep range, swept, past the screen's edge): it clears the spawned bits unless the object is once only.
constexpr uint32_t kObjDelete = 0x8396E2;
// The spawner's test of one list object in a column ($82:A8C0: X its index, spawned bits & $81; the next object at
// $82:A8EF) and the allocator's answer for it (after JSL $82:AA2B: carry set = refused; $0012 the index). The frame
// spawner's start ($82:A7A3: REP #$30, LDX #0, STZ $2A, then the camera at $82:A7AA).
constexpr uint32_t kSpawnTest = 0x82A8C0, kSpawnNext = 0x82A8EF, kSpawnAllocated = 0x82A8E8, kSpawnFrameTop = 0x82A7A3;
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
// The Lava Cavern's 4:3 limits (level $06: its camera scrolls right by itself, $083D = 2, $81:C5C2, 0.625 px a frame).
// The scroll holds Aladdin at camera + $F0 (ADC #$00F0: an invisible wall 16 px inside the screen's right edge); its
// edge rule ($145B = 3, $81:EA11) kills him 48 px past either edge of the screen (SBC #$0030 / ADC #$0130: left behind);
// its objects delete themselves past camera - $20 / + $120 ($83:9738 SBC #$001F with carry clear, $83:9749 ADC #$0120).
constexpr uint32_t kLavaWall = 0x81C5E3, kLavaBehind = 0x81EA17, kLavaAhead = 0x81EA31;
constexpr uint32_t kObjGoneLeft = 0x83973D, kObjGoneRight = 0x83974E;
constexpr int kLavaLevel = 0x06;
// A Whole New World (level $12, a carpet ride scrolling by itself): its gems ($35) fly past and delete themselves at
// the same tests, 32 px into the left margin (tools/qa round 2, N5); there too the objects' lifetime follows the
// picture's edges.
constexpr int kRideLevel = 0x12;
constexpr int kTowerLevel = 0x0A;   // Inside the Lamp 3: its one-screen-wide tower between gold pillars (span: a shaft)
// Its corridor's first page (page column 0 of row 0, block map $7F:0E00, above the tower): once the camera reaches x
// 256 a task ($81:A363: waits for $1774 = 1, then STA $7FF043 with $FE) writes $FE over the entry's high byte, $0E00
// to $FE00, a page the camera may not enter: a door shut behind Aladdin, the camera's x held at 256 and over from then
// on (a restart from its checkpoint, $0360 set, shuts it as the level loads, $81:8B15). Its block map stays in RAM.
constexpr uint16_t kTowerShut = 0xFE00, kTowerTop = 0x0E00;
// Objects on a timer from their spawn, by type, and the return address of that first wait (saved PC and bank, object
// +$03 / +$05): the Lava Cavern's lava pillar ($5B, $83:DF14: spawned as its cap lying in the lava, animation 2, it
// waits a time from its list entry - COP with the table $83:D3F8 by its list parameter, object +$07; with the
// parameter's bit 7 none -, bubbles for 32 frames, rises, holds and sinks, and deletes itself) and the Carpet Escape's
// rock hanging from the ceiling ($83, $84:9538: it hangs, waits by the table $84:D83A and falls; with the parameter's
// bit 4 it falls once Aladdin has passed it instead, no wait).
// Inside the Lamp's rising platform ($79, $83:F541: spawned at the camera's centre y + $20, it rises from its spawn on,
// a frame at a time - BRK, return $83:F58B, JSL $81:A4F7 - until Aladdin takes it or it leaves the screen) is one too:
// its first frame's wait is held the same way (it stays where it was spawned, its range test running).
struct TimedType { uint8_t type, bank; uint16_t wait; };
constexpr TimedType kTimedTypes[] = {{0x5B, 0x83, 0xDF3A}, {0x83, 0x84, 0x9579}, {0x79, 0x83, 0xF58B}};
// Objects that delete themselves by a range of their own from the camera's centre ($1A44), 4:3-based: the rising
// platform $79 (Inside the Lamp 2 and 3, +-$D0: $83:F63C LDA $1A44 / SBC $0C / ADC #$00D0 / CMP #$01A0, on at
// $83:F648 SEP / BCC kept) and Sultan's Dungeon 3's moving platforms $7D (+-$E0: $84:9106 LDA $0C / SBC $1A44 / ADC
// #$00E0 / CMP #$01C0, on at $84:9112 SEP / BCS deleted). In widescreen the test takes the picture's sides as the keep
// range's does.
struct SelfRange { uint32_t pc, on; bool centre_minus_x; uint16_t off, range; };
constexpr SelfRange kSelfRanges[] = {{0x83F63C, 0x83F648, true, 0xD0, 0x1A0}, {0x849106, 0x849112, false, 0xE0, 0x1C0}};
// Iago ($38; Sultan's Dungeon 1 and 2, Jafar's Palace 2 and 3) is created by a generator ($97, $84:9F9F: a timer, then
// the camera's centre within a range, two free slots) at the camera's centre + $90 ($84:A05E LDA $1A44 / STA $0C, then
// $81:AA81 with the offset $80:DBFF; by the generator's $15 bit 0, - $90): 16 px past the screen's right edge, with what
// he carries ($53, $83:D58C, kept at his side each frame). He flies left at 0.5 px a frame ($83:BE38 JSL $81:A5A7,
// $80:BAC3; back at $83:BE3C) until he is within 96 px of Aladdin, hovers, drops it and flies off.
constexpr uint32_t kIagoSpawnX = 0x84A05E, kIagoFlown = 0x83BE3C;
constexpr int kIagoType = 0x38, kIagoOff = 0x90, kIagoMarginSpeed = 4;
// The object runner's count of a waiting object's frames ($83:94BB DEC $01 / BEQ resume, state 1; then $83:94BF runs
// its type's idle routine while it waits).
constexpr uint32_t kObjWaitDec = 0x8394BB, kObjWaitIdle = 0x8394BF;
// The lamp's beam at the Underground River's end (level $05; a screen task, $82:84CB, a frame at a time from $82:8514):
// a column of light 17 px wide from world x $0E78, colour math (add, fixed colour) inside window 2 on the lines of an
// HDMA table on WH2 / WH3 (channel 7, indirect: $7E:F532 / $F533 the beam's edges). Its left edge on the screen is
// $0E78 - camera - shake, its right edge 16 px on, clipped at 255 ($82:853F); with the left edge past the screen
// (CMP #$0100, $82:8534) the beam is switched off (fixed colour 0, $82:85A1).
constexpr int kLampLevel = 0x05, kLampBeamX = 0x0E78, kLampBeamW = 16;
constexpr uint32_t kLampBeamTest = 0x828534, kLampBeamOn = 0x828556;
constexpr uint8_t kLampOffL = 0xFF, kLampOffR = 0xFE;   // (WH2 / WH3 for a beam in the right margin: empty on the screen)

inline uint8_t rd8(const Snes& s, uint32_t a) { return s.wram[a & 0x1FFFF]; }
// An attract demo playing (the game's own flag: input from its recording at $84:F700). Demos replay recorded input:
// any change to the code they run (the wider spawn / keep ranges, the cheats, Remove Slowdown's timing) sends them off
// their path, so while one plays the hooks change nothing, the turbo stays off and the demo is shown at 4:3.
inline bool demo_playing(const Snes& s) { return s.wram[0x0331] == 2; }
// A level index ($035E below $14; the screens - story scenes, which run the level-start object spawn too, STAGE CLEAR,
// the title - are above) and no demo. (The title zeroes $035E while it waits for GAME START: AladdinWide::armed_.)
inline bool playing_level(const Snes& s) { return s.wram[0x035E] < 0x14 && !demo_playing(s); }
// The staff roll (kRollLevel): the ending hands over to it with the screen black, and it stays to THE END.
inline bool staff_roll(const Snes& s) {
    return s.wram[0x035E] == kRollLevel && s.wram[0x035C] == kRollStage && s.wram[0x035D] == kRollAct && demo_playing(s);
}
inline uint16_t rd16(const Snes& s, uint32_t a) { return (uint16_t)(s.wram[a & 0x1FFFF] | (s.wram[(a + 1) & 0x1FFFF] << 8)); }
inline void wr8(Snes& s, uint32_t a, uint8_t v) { s.wram[a & 0x1FFFF] = v; }
inline uint16_t rom16(const Snes& s, uint32_t a) { return s.peek16(a); }
// A 16-bit result in A with its N and Z flags (an immediate ADC / SBC answered by a hook; the code after it tests N or
// sets the carry again itself).
inline void set_a(CpuRegs& r, uint16_t v) {
    r.a = v;
    r.n = (v & 0x8000) != 0;
    r.z = v == 0;
}
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
    // Jafar's cobra (level $11): the fire floor the game paints into the map's 32 columns on its screen only (BG1's map
    // rows 21-31 at $4000) under the spiked arch, a picture on BG2's map at $4800 (rows 22-28) that it switches to in
    // the same frame.
    static constexpr int kJafarLevel = 0x11;
    static constexpr int kFireRoom = 0x4000, kFireArch = 0x4800, kFireRow = 21;
    // The arch waves: its object (type $88, script $84:8E6A, DB $80) moves a 24-bit y ($0E-$10) by a velocity ($1D-$1F)
    // that an acceleration ($20) turns round at the centre ($40): up (resumed at $84:8E8E, the velocity less the
    // acceleration each frame) until y < centre, then down ($84:8EAA, plus) until y >= centre, each half starting with a
    // velocity and acceleration from tables ($80:C200 / C173 / C0E6, $80:C28D by index $80:D7BE / $80:D7C1 + the wave's
    // strength $1B1B, read at each up-swing's start); with $1B1A clear it holds y at $100 ($84:8E7B) and inserts
    // nothing. Each step it shifts its table of 32 column scrolls ($7E:DFC0, newest first) one column on and inserts
    // ((y - $100) & $1FF) | $4000 at the front; the NMI copies it to BG3's map, mode 2's offset-per-tile for BG2: the
    // arch's screen column c (1-32) is scrolled by entry c - 1, column 0 by the register (y - $100). The wave travels
    // right a column per step.
    static constexpr int kArchType = 0x88, kArchHist = 40;
    static constexpr uint32_t kArchTable = 0x7EDFC0, kArchOn = 0x1B1A, kArchStrength = 0x1B1B;
    static constexpr uint16_t kArchHold = 0x8E7B, kArchUp = 0x8E8E, kArchDown = 0x8EAA;
    static constexpr uint32_t kArchStartUp = 0x80D7BE, kArchStartDown = 0x80D7C1;
    static constexpr uint32_t kArchVelLo = 0x80C200, kArchVelMid = 0x80C173, kArchVelHi = 0x80C0E6, kArchAcc = 0x80C28D;
    // Jafar's Palace 3's end (level $10; its event task $80:A5AC): the camera stops at $10A0 until Aladdin reaches x
    // $1190 on the floor; then every object is deleted, the zone's right limit set to $11A0 ($1A42), the camera panned
    // there ($083D = 4) and held in x ($1A10 = 1; Jafar, type $98 from $84:A112, holds x and y); Jafar fights in the
    // throne room on the game's screen, Aladdin kept on it. Jafar beaten ($00BD, $84:A36F), 32 frames on the event
    // lets the camera go ($80:A622: $1A40 = $11A0, $1A42 = $7FA0, $1A10 = 0) and Aladdin walks on right to the exit.
    // The margins (the corridor behind, the hall on to the exit) are dimmed while the fight holds him, by a fade.
    static constexpr int kPalace3Level = 0x10, kPalace3Boss = 0x11A0;
    // Sultan's Dungeon 3's end (level $0D; its event task $80:A503): the zone's right limit at $900 until Aladdin
    // reaches x $9F0 on the floor; then the right limit is set to $A00 and the camera panned there ($083D = 4); there
    // the left limit closes too ($1A40 = $1A42 = $A00, no hold) and the boss (type $69, $83:EA10) fights on the game's
    // screen. Beaten ($00BD, $83:EB1E), 432 frames on the event opens the right limit ($80:A59E: $1A42 = $7FFF) and
    // Aladdin walks on right to the exit.
    static constexpr int kDungeon3Level = 0x0D, kDungeon3Boss = 0x0A00;
    // Jafar's fight (level $11): the room grows to 768 px as the camera is held for it ($1A04 from $100 to $200,
    // $1A10 = 1) and the event pans it to the fight's screen; the sorcerer, then the cobra, fight there, and the level
    // ends with the cobra (the ending, level $39). A death restarts the level: the room at 512 px, the camera free.
    static constexpr int kJafarRoom = 0x200;
    // The boss fights that keep Aladdin on the game's screen until won: the word that marks the fight on (a zone's
    // right limit from the event's pan to the win's release; Jafar's room grown) and whether the camera is held in x
    // then too. The margins are dimmed through them, by a fade.
    struct BossFight { int level; uint32_t addr; int value; bool hold; };
    static constexpr BossFight kBossFights[] = {{kPalace3Level, kZoneMaxX, kPalace3Boss, true},
                                                {kDungeon3Level, kZoneMaxX, kDungeon3Boss, false},
                                                {kJafarLevel, kCamMaxX, kJafarRoom, true}};
    static constexpr int kFadeFrames = 16;        // a fight's dim fades in / out over this many frames (1/32 each)

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
        // (kObjDelete only reads: the objects deleted are counted at every width, so a width switched on later, or a
        // 4:3 state loaded wide, knows them)
        for (uint32_t pc :
             {kOamPiece, kOamPieceFlip, kOamBuild, kOamDma, kLevelLoop, kLevelWait, kTaskYield, kSpawnInit, kObjDelete,
              kLavaWall, kLavaBehind, kLavaAhead, kObjWaitDec, kRollDraw, kRollDrawDone})
            s.markExec(pc, true);
        if (ext_ > 0)
            for (uint32_t pc : {kDrawTest, kKeepTest, kSpawnInitCam, kSpawnInitRight, kSpawnInitLeft, kSpawnInitCount,
                                kSpawnFrameCam, kSpawnRowX, kSpawnEdgeLeft, kSpawnEdgeRight, kStubRetryR + 4,
                                kStubRetryL + 4, kSweepDelete, kHdmaOn, kLampBeamTest, kObjGoneLeft, kObjGoneRight,
                                kSpawnTest, kSpawnAllocated, kIagoSpawnX, kIagoFlown, kRollExitTest, kRollExitSignal, kRollCamY})
                s.markExec(pc, true);
        if (ext_ > 0)
            for (const SelfRange& sr : kSelfRanges) s.markExec(sr.pc, true);
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
            take_arch(s);
            return false;
        case kHdmaOn:   // (only watched: where this frame's band tables start on the screen, band_adj)
            hdma_line_ = s.vPos();
            return false;
        case kSpawnInit:
            respawn_ = false;   // (a level's start spawn: for the width set now)
            armed_ = playing_level(s);
            // (its set-up clears every spawned bit: nothing is deleted or refused in the level yet)
            std::memset(gone_, 0, sizeof(gone_));
            std::memset(again_, 0, sizeof(again_));
            std::memset(refused_, 0, sizeof(refused_));
            std::memset(waiting_, 0, sizeof(waiting_));
            std::memset(starved_, 0, sizeof(starved_));
            spawn_mode_ = kSpawnGame;
            wall_valid_ = behind_valid_ = false;   // (the Lava Cavern's limits: from the picture's edges)
            return false;
        case kObjWaitDec: {
            // (kObjWaitDec only acts on a timed object the margins spawned early, at any width: one still waiting when
            // widescreen is switched off is let go at the original's moment too)
            // A timed object the margins spawned ahead of the original (waiting_: the Lava Cavern's pillar lies in the
            // lava, its cap showing; the Carpet Escape's rock hangs from the ceiling) doesn't count its first wait
            // down until the original's spawner would create it: in the frame after the one its column reaches the
            // original's spawn edges (the camera here, before this frame's scroll, is the one the spawner last saw),
            // as a fresh one runs its start then (its wait set, not counted). From then on it counts as the game's
            // own: the pillar bubbles and rises, the rock falls, at the original's camera x.
            const uint8_t idx = rd8(s, r.dp + 0x34);
            if (idx == 0 || !bit(waiting_, idx)) return false;
            const TimedType* tt = timed_type(rd8(s, r.dp + 0x06));
            if (!tt || rd16(s, r.dp + 0x03) != tt->wait || rd8(s, r.dp + 0x05) != tt->bank) {
                set_bit(waiting_, idx, false);
                return false;
            }
            if (!beyond(s, idx)) set_bit(waiting_, idx, false);   // (this frame not counted either)
            r.pc = (uint16_t)kObjWaitIdle;
            return true;
        }
        case kObjDelete: {
            // A level object deleted (killed, collected, swept, out of range, its own end): the module's own spawns
            // (a width change's, the retries) don't create it again; the game's spawner does as usual when its edge
            // crosses the object's column again. Unless it deleted itself past the 4:3 screen's edge, or the sweep
            // took it beyond the original's spawn edges (pending_again_): the original would only have it later,
            // and it gets that chance (the edge-column retry).
            // Also any delete beyond the original's spawn edges (x >= camera + $120 or < camera - $40), where the 4:3
            // game has no object yet or any more: some types delete themselves by a range of their own from the
            // camera's centre ($1A44: Inside the Lamp 2's $79 at +-$D0, $83:F639; Sultan's Dungeon 3's $7D at
            // +-$E0, $84:9103), so the margin's spawn deleted them at once, and as "gone" the edge-column retry
            // never brought them at the original's moment: none of them appeared at 106 or 140. One whose list column
            // is still beyond those edges is also tried in the frame they reach it (refused_pending): the edge-column
            // retry runs at the spawner's 32-px steps, which don't come while the view is stopped at a level's start.
            const uint8_t idx = rd8(s, r.dp + 0x34);
            if (idx != 0) {
                const int x = (int16_t)rd16(s, r.dp + 0x0C), cam = (int16_t)rd16(s, kCamX);
                const bool right = x >= cam + 0x120, left = x < cam - 0x40;
                set_bit(waiting_, idx, false);
                set_bit(gone_, idx, true);
                set_bit(again_, idx, r.dp == pending_again_ || right || left);
                if (ext_ > 0 && (right || left) && !(rd8(s, kSpawnedBits + idx) & 0x80)) {
                    const int c = (rom16(s, list_entry(s, idx) + 2) & 0x3FFF) >> 5;
                    if (right ? c > floor_shr(cam + 0x120, 5) : c < floor_shr(cam - 0x40, 5))
                        set_bit(refused_, idx, true);
                }
            }
            pending_again_ = 0;
            return false;
        }
        case kTaskYield:
            // Remove Slowdown ends wherever the level's main task gives the CPU back: its frame wait, the pause loop's,
            // and the restart that leaves the level (an exit, a life lost: the next screen keeps the console's timing).
            s.turboActive = false;
            s.cpuTurbo = std::max(1, cfg_.cpu_turbo);   // (the staff roll's own speed-up ends there too)
            return false;
        case kRollDraw:
            // The staff roll drawn wide: its draw list and OAM build at the console's timing (kRollTurbo).
            if (ext_ > 0 && staff_roll(s)) {
                s.cpuTurbo = std::max(kRollTurbo, cfg_.cpu_turbo);
                s.turboActive = true;
            }
            return false;
        case kRollDrawDone:
            if (staff_roll(s)) {   // (in a level this is the level's own Remove Slowdown: left as it is)
                s.turboActive = false;
                s.cpuTurbo = std::max(1, cfg_.cpu_turbo);
            }
            return false;
        case kLevelLoop:
            // Remove Slowdown: the frame's work runs faster up to the task's yield (not in a demo: its input is a
            // recording of frames, and its lag frames are part of it)
            s.turboActive = s.cpuTurbo > 1 && active(s);
            loop_frame_ = s.frameCount();
            logic_done_ = band_taken_ = false;
            spawn_mode_ = kSpawnGame;   // (also the way back from the width change's spawn)
            if (respawn_ && active(s)) {
                // A width change mid-level: the spawner's edge columns move to the new margins, and the game's own
                // level-start spawn runs again past its set-up, which would clear the spawned bits: objects already
                // out are skipped by them, so the new margins get only theirs. Objects the game has deleted since
                // the level's start (gone_) are skipped too: the Market at Dusk's arena sweep had just cleared two
                // guards, which came back and took the boss's sprite banks (tools/qa round 2, N1); an enemy
                // defeated in view would have come back the same way.
                respawn_ = false;
                if (ext_ > 0) {
                    spawn_mode_ = kSpawnWidth;
                    r.pb = 0x60;
                    r.pc = (uint16_t)kStubRespawn;
                    return true;
                }
            }
            return false;
        default:
            break;
        }
        if (ext_ == 0 && active(s) && (pc == kLavaWall || pc == kLavaBehind || pc == kLavaAhead)) {
            // Widescreen just switched off in the Lava Cavern: its limits ease to the 4:3 screen's edges as above,
            // then the original code runs again (nothing is held once they are there, so 4:3 play is the original).
            if (!(pc == kLavaBehind ? behind_valid_ : wall_valid_)) return false;
            const int target = (int16_t)r.a + (pc == kLavaWall ? 0xF0 : pc == kLavaBehind ? -0x30 : 0x130);
            lava_limit(s, pc, 0, 0);
            if (pc == kLavaWall && wall_x_ == target) wall_valid_ = false;
            if (pc == kLavaBehind && behind_x_ == target) behind_valid_ = false;
            return true;
        }
        if (ext_ > 0 && (pc == kDrawTest || pc == kKeepTest) && staff_roll(s)) {
            // The staff roll, centred: its objects (the birds, the carpet, the Genie, THE END's twinkles) are drawn
            // and kept within the picture's sides as well, so they fly out whole at their true x. Its spawner keeps
            // the original's edges: the roll's choreography (the Genie's entrance, the birds against the credits)
            // stays the console's.
            const uint16_t d = (uint16_t)(rd16(s, kCamCentre) - rd16(s, r.dp + 0x0C));
            const uint32_t tab = pc == kDrawTest ? kDrawRanges : kKeepRanges;
            const int off = rom16(s, tab + (r.x & 0xFFFF)), range = rom16(s, tab + 2 + (r.x & 0xFFFF));
            const bool in = (uint16_t)(d + off + ext_) < range + 2 * ext_;
            if (pc == kDrawTest) r.pc = (uint16_t)(in ? kDrawIn : kDrawOut);
            else {
                r.pc = (uint16_t)(in ? kKeepIn : kKeepOut);
                r.c = !in;
            }
            return true;
        }
        if (ext_ > 0 && (pc == kRollExitTest || pc == kRollExitSignal || pc == kRollCamY) && staff_roll(s)) {
            // The roll's end (kRollExitTest above): the flight on to the picture's edge, the signal and the camera's
            // y as the original has them
            const int cam = (int16_t)rd16(s, kCamX), px = (int16_t)rd16(s, kRollPlayerX);
            if (pc == kRollExitTest) {   // ADC #$0140 (A the camera, carry clear)
                if (px - cam > 0x140 && rd8(s, kRollCarpetGone) == 0) wr8(s, kRollCarpetGone, 1);
                set_a(r, (uint16_t)(r.a + 0x140 + ext_));
                r.pc = (uint16_t)(kRollExitTest + 3);
                return true;
            }
            if (pc == kRollExitSignal) {   // INC $1B14: given already
                if (rd8(s, kRollCarpetGone) == 0) return false;
                r.pc = (uint16_t)kRollExitOn;
                return true;
            }
            // LDA $08EA: past the original's end of the flight, the y the original has moved the player to
            if (px - cam <= 0x140 || px - cam > 0x140 + ext_ + 0x10) return false;
            set_a(r, (uint16_t)((rd16(s, kRollPlayerY) & 0xFF) | 0xFE00));
            r.pc = (uint16_t)(kRollCamY + 3);
            return true;
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
            bool in = (uint16_t)(d + off + er) < range + el + er;
            if (pc == kKeepTest && in && (uint16_t)(d + off) >= range && let_go(s, r.dp, el, er)) in = false;
            if (pc == kDrawTest) r.pc = (uint16_t)(in ? kDrawIn : kDrawOut);
            else {
                r.pc = (uint16_t)(in ? kKeepIn : kKeepOut);
                r.c = !in;
            }
            return true;
        }
        case kSpawnFrameCam: {
            // A once-only object (a boss, Jasmine, an item) the allocator refused is otherwise never tried again
            // while the camera stays (an arena): once a frame, while its column is within the original's spawn
            // edges, its column is run again for it alone (the stub comes back to the frame spawner's start). The
            // same takes a timed object held back from the margins (held) as its column reaches those edges.
            spawn_mode_ = kSpawnGame;
            if (retry_frame_ != s.frameCount()) {
                retry_frame_ = s.frameCount();
                int col = 0;
                const int idx = refused_pending(s, col);
                if (idx != 0) {
                    spawn_mode_ = kSpawnRefused;
                    retry_idx_ = idx;
                    r.x = (uint16_t)col;
                    r.pb = (uint8_t)(kStubRetryRefused >> 16);
                    r.pc = (uint16_t)kStubRetryRefused;
                    return true;
                }
            }
            r.a = (uint16_t)(rd16(s, kCamX) - el + ext_);
            r.pc = (uint16_t)(pc + 3);
            return true;
        }
        case kSpawnInitCam:
            // The spawner's camera: the picture's middle (the camera where the view is centred on it).
            r.a = (uint16_t)(rd16(s, kCamX) - el + ext_);
            r.pc = (uint16_t)(pc + 3);
            return true;
        case kSpawnTest: {      // one list object (X): skipped by the module's own spawns as they require (skip)
            const int idx = r.x & 0xFF;
            // An object whose script runs on a timer from its spawn (kTimedTypes) mustn't start it in the margin:
            // spawned early the Lava Cavern's pillar rose and sank before Aladdin got there, the Carpet Escape's rock
            // fell ahead of him. It is spawned there, in its waiting state (the pillar's cap lying in the lava, the
            // rock hanging), with its wait held until the original's moment (waiting_, kObjWaitDec); if the allocator
            // refuses it, the retry of refused_pending creates it when its column reaches the original's spawn edges.
            // (One without the wait - a rock that waits for Aladdin - leaves waiting_ at its first other wait.)
            if ((spawn_mode_ == kSpawnGame || spawn_mode_ == kSpawnWidth) && held(s, idx)) {
                set_bit(refused_, idx, true);
                set_bit(waiting_, idx, true);   // (held: never deleted, so the width change's spawn takes it too)
            }
            if (spawn_mode_ == kSpawnGame || !skip(s, idx)) return false;
            r.pc = (uint16_t)kSpawnNext;
            return true;
        }
        case kSpawnAllocated: { // the allocator's answer (carry set: refused): a once-only object to try again
            const uint8_t idx = rd8(s, 0x0012);
            if (r.c && idx != 0 && (rom16(s, list_entry(s, idx)) & 0x8000)) set_bit(refused_, idx, true);
            if (r.c && idx != 0) set_bit(waiting_, idx, false);   // (a refused timed object: created fresh by the retry)
            // (refused at the margins' own columns: tried again while it is still beyond the picture's edge, starved_)
            if (r.c && idx != 0 && (spawn_mode_ == kSpawnGame || spawn_mode_ == kSpawnWidth)) set_bit(starved_, idx, true);
            return false;
        }
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
            spawn_mode_ = kSpawnEdge;
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
                    const int k = next_column(s, cols, n, i);
                    spawn_mode_ = k == n - 1 ? kSpawnGame : kSpawnEdge;   // (the margin's own column: the game's)
                    r.x = cols[k];
                    r.pc = (uint16_t)(right ? kStubRetryR : kStubRetryL);
                    return true;
                }
            spawn_mode_ = kSpawnGame;
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
                const int col = (rom16(s, list_entry(s, idx) + 2) & 0x3FFF) >> 5, cam = (int16_t)rd16(s, kCamX);
                if (col >= (cam + 0x120) >> 5 || col <= floor_shr(cam - 0x40, 5)) {
                    wr8(s, kSpawnedBits + idx, 0);
                    pending_again_ = obj;   // (its delete follows: not gone for the edge-column retry)
                }
            }
            return false;
        }
        // The Lava Cavern's limits follow the picture's edges instead of the game's screen's (the view stops at the
        // level's ends, so the wall stays inside the level: at its end camera + $F0 is the exit's x either way).
        case kLavaWall:
        case kLavaBehind:
        case kLavaAhead:
            return lava_limit(s, pc, el, er);
        case kObjGoneLeft:  // REP #$21 / SBC #$001F (BMI next): an object 32 px past the picture's left edge is deleted
        case kObjGoneRight: { // REP #$21 / ADC #$0120: or past its right edge
            const bool wide = rd8(s, kLevel) == kLavaLevel || rd8(s, kLevel) == kRideLevel;   // (follow the picture)
            const uint16_t lim = (uint16_t)(pc == kObjGoneLeft ? r.a - 0x20 - (wide ? el : 0)
                                                               : r.a + 0x120 + (wide ? er : 0));
            // (the test's outcome, CMP $0C: deleted when x <= the left limit (not negative) / x > the right limit; a
            // delete past the 4:3 screen's edge of an object the margins brought out early isn't "gone" for good)
            const uint16_t x = rd16(s, r.dp + 0x0C);
            if (pc == kObjGoneLeft ? !(lim & 0x8000) && lim >= x : lim < x) pending_again_ = r.dp;
            if (!wide) return false;
            set_a(r, lim);
            r.pc = (uint16_t)(pc + 3);
            return true;
        }
        case kIagoSpawnX:   // LDA $1A44 (A 16 bits): Iago's place from the picture's edge on his side, not the screen's
            set_a(r, (uint16_t)(rd16(s, kCamCentre) + ((rd8(s, r.dp + 0x15) & 1) ? -el : er)));
            r.pc = (uint16_t)(kIagoSpawnX + 3);
            return true;
        case kIagoFlown: {
            // Iago comes in from the picture's edge (kIagoSpawnX) and crosses the margin at kIagoMarginSpeed times his
            // own speed, up to the place the original creates him at (the camera's centre + $90 on his side, for the
            // camera now): from there on he flies, hovers and drops what he carries as the original, a few frames later
            // (at his own 0.5 px a frame the margin took up to 2 * ext frames: 212 at 106).
            if (rd8(s, r.dp + 0x06) != kIagoType) return false;
            const bool leftward = rd8(s, r.dp + 0x14) != 0;
            const int32_t at = ((int16_t)rd16(s, kCamCentre) + (leftward ? kIagoOff : -kIagoOff)) * 256;
            int32_t x = (int16_t)rd16(s, r.dp + 0x0C) * 256 + rd8(s, r.dp + 0x0B);
            const int32_t v = (kIagoMarginSpeed - 1) * (rd16(s, r.dp + 0x1A) * 256 + rd8(s, r.dp + 0x19));
            if (leftward ? x <= at : x >= at) return false;
            x = leftward ? std::max(at, x - v) : std::min(at, x + v);
            wr8(s, r.dp + 0x0B, (uint8_t)x);
            wr8(s, r.dp + 0x0C, (uint8_t)(x >> 8));
            wr8(s, r.dp + 0x0D, (uint8_t)(x >> 16));
            return false;
        }
        case kLampBeamTest: {
            // The lamp's beam with its left edge in the right margin (A: its screen x, 16 bits): kept on (fixed colour,
            // colour math) with an empty window on the game's screen, WH2 > WH3; the margin draws it at its place
            // (beginLine). (Up to the widest right side, 2 * ext: past the picture it isn't drawn anyway.)
            const int x = (int16_t)r.a;
            if (x < 0x100 || x >= 0x100 + 2 * ext_) return false;
            wr8(s, 0x002C, (uint8_t)x);
            wr8(s, 0x002D, (uint8_t)(x >> 8));
            wr8(s, 0x7EF532, kLampOffL);
            wr8(s, 0x03A8, kLampOffL);
            wr8(s, 0x7EF533, kLampOffR);
            wr8(s, 0x03A9, kLampOffR);
            r.mf = true;   // (SEP #$20, as at $82:853C)
            r.pc = (uint16_t)kLampBeamOn;
            return true;
        }
        default:
            // An object's own range from the camera's centre (kSelfRanges), widened by the picture's sides as the keep
            // range is: spawned in the margins, it lives there instead of deleting itself in the next frame (and coming
            // at the original's moment, inside the picture). (The carry as the CMP leaves it: set = out.)
            for (const SelfRange& sr : kSelfRanges)
                if (pc == sr.pc) {
                    const uint16_t c = rd16(s, kCamCentre), x = rd16(s, r.dp + 0x0C);
                    const uint16_t d = sr.centre_minus_x ? (uint16_t)(c - x + sr.off + er) : (uint16_t)(x - c + sr.off + el);
                    r.c = d >= sr.range + el + er;
                    r.pc = (uint16_t)sr.on;
                    return true;
                }
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
            bool sh = false;
            span(s, cam, lo, hi, &sh);
            const bool stop = can_stop(s);
            const bool jump = !lim_valid_ || std::abs(cam - last_cam_) >= 64;
            if (!jump && (lo != lim_lo_ || hi != lim_hi_ || stop != stop_)) {
                const int before = target_v(cam, lim_lo_, lim_hi_, stop_);
                // (into or out of a shaft, where the game's camera moves only in y: the offset closes as it does)
                if (sh || shaft_) slide_y_ = true;
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
            shaft_ = sh;
            int moved = jump ? 0 : std::abs(cam - last_cam_);
            if (slide_y_ && !jump) moved = std::max(moved, std::abs(camy - last_camy_));
            // Jafar's room held: the camera never moves again in the fight, so the view eases to the arena by itself
            // (a sixteenth of the way a frame, 1-3 px; the room's own pan as the fight starts closes most of it)
            if (!jump && d_off_ != 0 && arena(s) && rd8(s, kLevel) == kJafarLevel)
                moved = std::max(moved, std::min(3, (std::abs(d_off_) + 15) / 16));
            lim_valid_ = true;
            last_cam_ = cam;
            last_camy_ = camy;
            d_off_ = std::clamp(d_off_, -2 * ext_, 2 * ext_);
            const int step = std::min(kSlide, moved);
            d_off_ = d_off_ > 0 ? std::max(0, d_off_ - step) : std::min(0, d_off_ + step);
            if (d_off_ == 0 || jump) slide_y_ = false;
            // A boss fight holding Aladdin on the game's screen: its sides dimmed, fading in and out a step a frame.
            const int want = boss_held(s) ? kFadeFrames : 0;
            fight_dim_ += fight_dim_ < want ? 1 : fight_dim_ > want ? -1 : 0;
            // A background the game scrolls itself: the inputs of its scroll for the frame its logic just finished.
            if (logic_done_ && !band_taken_) {
                band_taken_ = true;
                take_band(s, cam, jump);
            }
        } else {
            lim_valid_ = false;
            d_off_ = 0;
            fight_dim_ = 0;
            band_next_.valid = band_shown_.valid = false;
        }
        info.gameplay = level_ || stage_clear_ || roll_;   // (the frame just drawn: frame_start)
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
        if (stage_clear_) {   // (the backdrop and the sprites at their true x in the margins)
            for (bool& h : p.hideWideBg) h = true;
            p.hideWideObj = genie_loading_;
            return;
        }
        if (roll_) {
            roll_line(p, row);
            return;
        }
        if (!level_ || ext_ == 0 || !snes_) return;
        const Snes& s = *snes_;
        // The layer structs' positions as the frame starts: the game's logic moves them during the frame, a byte at a
        // time and past the level's edge before it clamps them (Inside the Lamp 3: x $00FD for part of a line, then
        // $FFFD, then 0), while the registers hold the frame's scroll throughout.
        if (row == 0) {
            for (int k = 0; k < 2; k++) {
                near_x_[k] = (int16_t)rd16(s, kLayer[k] + 0x3B);
                near_y_[k] = (int16_t)rd16(s, kLayer[k] + 0x3F);
            }
        }
        // The layers drawn from the level's data: world position of this line.
        for (int l = 0; l < 2; l++) {
            LineLayer& ll = line_[l];
            ll.decode = false;
            // The struct whose map the layer shows. In an arena also the other layer's, and one the game has stopped
            // streaming (flags 0): Jafar's room, once held, keeps its data (identical to VRAM where the map holds it)
            // while BG1 is pointed at its map; from the level's data the margins show the room at any width (the
            // map's 64 columns hold 240 px beside the screen). Not BG2 holding an object's picture (mode $12: the
            // cobra's arch).
            const uint16_t map = p.bgMapBase(l);
            int k = l;
            if (arena_ && map != rd16(s, kLayer[l] + 0x2E) && map == rd16(s, kLayer[1 - l] + 0x2E)) k = 1 - l;
            const uint32_t b = kLayer[k];
            if ((!rd8(s, b + 0x4F) && !arena_) || map != rd16(s, b + 0x2E) || p.bgMapSize(l) != 1) continue;
            if (l == 1 && rd16(s, kBgMode) == 0x12) continue;
            const int sx = near_x_[k], sy = near_y_[k];
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
            ll.lo = jafar_ ? jafar_lo_ : -0x8000;   // (Jafar's room: mirrored past its ends, below)
            ll.hi = jafar_ ? jafar_hi_ : 0x7FFF;
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
            int band = 0;
            const int adj = band_adj(p, row, line_[1], &band);
            if (adj != 0) {
                p.bgHofsAdj[1] = (int16_t)adj;
                if (line_[1].decode) {
                    p.wideFetchAll[1] = true;
                    line_[1].wx += adj;
                }
            }
            // (high frame rate: the in-between pictures move each band with its own last place)
            p.lineBand[1] = (uint8_t)band;
        }
        // A picture an object moves on BG2 (the Carpet Escape's wave: the map's first 256x256 screen, the rest empty,
        // placed by the scroll at [-x, 256 - x); parked at x = 256 while it isn't there): in the margins only its
        // own copy, not the map's 512-px repeat.
        obj_bg2_ = !line_[1].decode && rd16(s, kBgMode) == 0x12;
        if (obj_bg2_) {
            const int h = unwrap10(p.bgHofs(1), (int16_t)rd16(s, kBg2Shown));
            obj_lo_ = h == 256 ? 0 : -h;
            obj_hi_ = h == 256 ? 0 : 256 - h;
            // (Jafar's arch, scrolled with the camera at 512: on the game's screen only, not its repeat at 21:9)
            if (paint_row_ < 0x10000) {
                obj_lo_ = 0;
                obj_hi_ = 256;
            }
        }
        // The level's extent on this line (BG1's page row). Past it the margins show scenery beside the level: the
        // background (BG2) going on, at full brightness, without the level's own layer (BG1: the walls, platforms
        // and props) or the sprites - nothing there is the level, so nothing there looks like a place to go (black
        // with Past Level Ends: Black). Everywhere else the margins show the level as the game stores it at full
        // brightness, also where the game's camera can't go now (a zone's limits, an arena, pages it can't enter at
        // its height): it is the level.
        int lo = -kMaxExt, hi = 256 + kMaxExt;
        if (line_[0].decode) {
            const int wy = line_[0].wy;
            int slo, shi;
            row_run(s, line_[0].wx, wy, slo, shi);
            lo = slo - line_[0].wx;
            hi = shi - line_[0].wx;
            // BG2 there: mirrored about the level's edges on this line as before (only what the game shows of it
            // comes into view; the pages beside Inside the Lamp 3's tower are drawn as its bottom page). Where a strip
            // of the level at the edge hides BG2 on every line of its page (edge_cover), about the strip's inner edge,
            // so a blank part of BG2 behind the strip stays out of the reflection. Where the background is drawn
            // shifted (bgHofsAdj: the view gliding or stopped while the game's camera isn't), its blank strip comes out
            // from behind the edge strip: the mirror leaves that part out too.
            const int r = floor_shr(wy, 8);
            const int nl = lo > -el_ ? edge_cover(p, s, slo >> 8, r, false) : 0;
            const int nr = hi < 256 + er_ ? edge_cover(p, s, (shi - 1) >> 8, r, true) : 0;
            const int adj = p.bgHofsAdj[1];   // (< 0: the background drawn further right)
            const int ml = lo + (nl ? 8 * nl + std::max(0, -adj) : 0);
            const int mh = hi - (nr ? 8 * nr + std::max(0, adj) : 0);
            if (mh - ml >= 64) {
                p.wideMirrorLo = ml;
                p.wideMirrorHi = mh;
            }
            // Inside the Lamp 3's tower (and the corridor's left end above it, the same left pillar): a shaft inside
            // solid gold wall. Past its gold pillars (page column 0's edges, BG1 strips edge_cover finds: 3 tile
            // columns, the fourth the pillar's dark inner edge) the sides show the pillar's outer tile column on BG1
            // reflected back and forth (flat gold, the pillar's bands running on at their height, no seam), the line's
            // own pixels, so it scrolls with the pillar; nothing else there but the bar. The corridor's lines keep
            // their level on the right: the rule is per line, so the corridor comes down onto the wall as the view
            // glides. (Black with Past Level Ends: Black: the wall is part of the mirror.)
            if (rd8(s, kLevel) == kTowerLevel) {
                if (nl >= 3 && (slo >> 8) == 0) {
                    p.wideWall = 0x01;
                    p.wideWallL = lo;
                }
                if (nr >= 3 && ((shi - 1) >> 8) == 0) {
                    p.wideWall = 0x01;
                    p.wideWallR = hi;
                }
            }
        }
        // Jafar's room held (jafar_): the view is centred on the arena, so its right side lies past the room's end
        // (x 768). The fight's sides are the room going on, every layer and the sprites (the cobra's coils, the
        // fireballs, Jafar's magic) drawn there: past its ends the room's layers mirrored tile by tile from its data
        // (LineLayer lo / hi), its painted rows and the arch repeated from the screen (fetchWideTile). (Black there
        // with Past Level Ends: Black.)
        if (jafar_ && p.edgeMirror) {
            lo = -kMaxExt;
            hi = 256 + kMaxExt;
        }
        if (fire_ && p.bgMode() == 2) {
            if (!opt_built_) build_arch_opt(p);
            p.wideOptOn[1] = true;
        }
        p.wideShowLo = lo;
        p.wideShowHi = hi;
        p.wideMirror = 0x02;
        if (row < kHudLines) {
            // the status bar's lines: its two parts at the picture's edges
            p.wideShowKeep = 0x04;   // (the bar: BG3; sprites past the level's ends hidden on these lines too)
            if (cfg_.hud_anchor) {
                PpuLayerRemap& rm = p.layerRemap[2];
                rm.count = 2;
                rm.seg[0] = {(int16_t)(-el_), (int16_t)(kHudLeftEnd - el_), 0, 0};
                rm.seg[1] = {(int16_t)(kHudLeftEnd + er_), (int16_t)(256 + er_), (int16_t)kHudLeftEnd, 0};
            } else {
                p.hideWideBg[2] = true;
            }
        } else {
            // BG3 below the bar holds the game's centred texts (ACTION, PAUSE): at the picture's middle
            p.hideWideBg[2] = true;
            if (cfg_.hud_anchor && mid_ != 0) {
                PpuLayerRemap& rm = p.layerRemap[2];
                rm.count = 1;
                rm.seg[0] = {(int16_t)mid_, (int16_t)(256 + mid_), 0, 0};
            }
        }
        if (fight_dim_ > 0) {
            // A boss fight holding Aladdin on the game's screen (boss_held): nothing beside it is his to reach until
            // it is won and the way on opens; dimmed (the bar spared), fading in and out (fight_dim_, in 32nds).
            p.wideDimLo = 0;
            p.wideDimHi = 256;
            p.wideDimAmt = fight_dim_;
        }
        p.wideDimSpare = row < kHudLines ? 0x04 : 0;
        lamp_window(s, p);
        clipped_windows(p);
    }

    // The game's other windows (the screen tasks' boxes and irises, $82:8000: the "ABU NO!" box at the Underground
    // River's end, ...) are clipped at its screen's edges: a window reaching one is cut there, and the PPU would
    // continue it into the margin with the edge pixel's value - the inside of a box running off the screen's edge
    // to the picture's edge (tools/qa round 2, N3). In the margins such a window ends at the screen's edge, as at
    // 4:3 (its true edges only where known: the lamp's beam). A window over the whole line (0-255) still covers the
    // margins: a full-screen effect.
    static void clipped_windows(Ppu& p) {
        for (int w = 0; w < 2; w++) {
            Ppu::WideWin& ww = p.wideWin[w];
            const int l = p.windowLeft(w), r = p.windowRight(w);
            if (ww.on || (l == 0 && r == 255)) continue;
            ww.on = true;
            ww.lo = l;
            ww.hi = r;
        }
    }

    // The staff roll (roll_), centred on the game's screen. Its two streamed layers - BG1 the near clouds and towers,
    // BG2 the night sky with its stars, the moon, the cloud bank and the far city - are drawn in the sides from the
    // level's data at the line's own scroll, as a level's are: the sky goes on as the game's camera would show it, the
    // moon once (the data holds it once; the VRAM map's 512-px repeat doubled it), and nothing of the game's own
    // scrolling changes. Past the level's ends (its start, at the camera's first ext px; THE END, held at the level's
    // last camera x) each layer is mirrored tile by tile about the end: the sky, its clouds and stars going on. BG3,
    // the credits' text (fixed on the screen), stays on the game's 256 px: centred, never repeated into the sides.
    void roll_line(Ppu& p, int row) {
        const Snes& s = *snes_;
        if (row == 0) {
            for (int k = 0; k < 2; k++) {
                near_x_[k] = (int16_t)rd16(s, kLayer[k] + 0x3B);
                near_y_[k] = (int16_t)rd16(s, kLayer[k] + 0x3F);
            }
        }
        const int maxx = (int16_t)rd16(s, kCamMaxX);
        for (int l = 0; l < 2; l++) {
            LineLayer& ll = line_[l];
            ll.decode = false;
            const uint32_t b = kLayer[l];
            if (!rd8(s, b + 0x4F) || p.bgMapBase(l) != rd16(s, b + 0x2E) || p.bgMapSize(l) != 1) continue;
            const int sx = near_x_[l], sy = near_y_[l];
            const int wx = unwrap10(p.bgHofs(l), sx), wy = unwrap10(p.bgVofs(l), sy);
            if (std::abs(wx - sx) > 64 || std::abs(wy - sy) > 64) continue;   // (not following its struct: VRAM)
            ll.decode = true;
            ll.wx = wx;
            ll.wy = wy + row + 1;
            ll.off = rd16(s, b + 0x42);
            ll.bank = rd8(s, b + 0x4C);
            ll.rep = false;
            ll.lo = 0;
            ll.hi = (l == 1 && bg2_ratio_ != 0 ? (maxx * bg2_ratio_) >> 8 : maxx) + 256;
        }
        p.hideWideBg[2] = true;
        // With Past Level Ends: Black, black there instead (the sprites kept: the carpet, THE END's twinkles).
        if (!p.edgeMirror && line_[0].decode) {
            p.wideShowLo = -line_[0].wx;
            p.wideShowHi = maxx + 256 - line_[0].wx;
            p.wideShowKeep = 0x10;
        }
    }

    // The Lava Cavern's limits (exec hooks at the immediates; A the camera) for the picture's edges el / er.
    bool lava_limit(Snes& s, uint32_t pc, int el, int er) {
        CpuRegs& r = s.cpu.r;
        // A width change moves the picture's edges at once; the limits then ease to their new places so that the
        // change itself neither pushes Aladdin nor kills him (tools/qa round 2, N4: 60 px in one frame, off a rock
        // into the lava). The wall (world x, CMP $08E7 / STA: it holds him at most there) never comes in past him:
        // it holds its place in the world while he stands beyond its new place, and the scroll brings it there
        // (0.625 px a frame), or follows him as he steps back. The left-behind limit comes in at most 2 px a frame
        // more than the camera; the one ahead stays 64 px past the wall.
        if (pc == kLavaWall) {   // LDA $1773 / ADC #$00F0 (carry clear): the wall 16 px inside the picture's right edge
            const int target = (int16_t)r.a + 0xF0 + er, x = (int16_t)rd16(s, 0x08E7);
            wall_x_ = wall_valid_ ? std::max(target, std::min(wall_x_, x)) : target;
            wall_valid_ = true;
            set_a(r, (uint16_t)wall_x_);
            r.pc = (uint16_t)(pc + 3);
            return true;
        }
        if (pc == kLavaBehind) { // LDA $1773 / SEC / SBC #$0030 (BMI next): left behind 48 px past the picture's left edge
            const int target = (int16_t)r.a - 0x30 - el;
            behind_x_ = behind_valid_ && target > behind_x_ ? std::min(target, behind_x_ + 2) : target;
            behind_valid_ = true;
            set_a(r, (uint16_t)behind_x_);
            r.pc = (uint16_t)(pc + 3);
            return true;
        }
        {  // LDA $1773 / CLC / ADC #$0130: 48 px past its right edge
            int lim = (int16_t)r.a + 0x130 + er;
            if (wall_valid_) lim = std::max(lim, wall_x_ + 0x40);
            set_a(r, (uint16_t)lim);
            r.pc = (uint16_t)(pc + 3);
            return true;
        }
    }

    // The lamp's beam (level $05's end): window 2 clipped at the screen's right edge, which the margin would continue
    // to the picture's edge (a wall of light right of the pedestal), or kept empty while the beam is in the margin
    // (exec hook): in the margins the window is the beam's own 17 px, from its place in the world.
    void lamp_window(const Snes& s, Ppu& p) const {
        if (rd8(s, kLevel) != kLampLevel || (p.windowSelect(5) & 0x0F) != 0x0C || (p.colorWindowSel() & 0x30) != 0x20 ||
            p.fixedColor() == 0)
            return;
        const int lo = kLampBeamX - unwrap10(p.bgHofs(0), near_x_[0]);   // (the camera with the shake, as the task's)
        const int wl = p.windowLeft(1), wr = p.windowRight(1);
        Ppu::WideWin& w = p.wideWin[1];
        w.on = true;
        if (wl == kLampOffL && wr == kLampOffR) {           // (in the right margin)
            w.lo = lo;
            w.hi = lo + kLampBeamW;
        } else if (wl <= wr && wl == lo && wr == std::min(lo + kLampBeamW, 255)) {
            w.lo = lo;
            w.hi = lo + kLampBeamW;
        } else {                                            // (the lines outside it; the beam closing as the lamp is taken)
            w.lo = wl;
            w.hi = wr;
        }
    }

    int fetchWideTile(Ppu& p, int layer, int mapX, int mapY, int screenX, uint16_t* entry) override {
        (void)mapX;
        // Jafar's fire: the arch (BG2, an object's picture) and the room's painted rows (BG1, map rows 21-31, by the
        // map row the PPU reads, so the shake's wrap shows what the game's screen shows) repeated from the screen
        if (fire_ && ((layer == 1 && obj_bg2_) || (layer == 0 && (mapY & 31) >= kFireRow))) {
            *entry = painted_entry(p, layer, mapY, screenX);
            return kWideEntry;
        }
        if (layer == 1 && obj_bg2_ && level_) return screenX >= obj_lo_ && screenX < obj_hi_ ? kWideVram : kWideBlank;
        if (layer > 1 || !(level_ || roll_) || !snes_ || !line_[layer].decode) return kWideVram;
        const LineLayer& ll = line_[layer];
        int x = ll.wx + screenX;
        if (ll.rep) x &= 0x1FF;
        uint16_t flip = 0;
        if (x < ll.lo) { x = 2 * ll.lo - 1 - x; flip = 0x4000; }          // (mirrored about the layer's ends)
        else if (x >= ll.hi) { x = 2 * ll.hi - 1 - x; flip = 0x4000; }
        *entry = (uint16_t)(tile_at(*snes_, ll, x, ll.wy + p.subDy(layer)) ^ flip);
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
        v.push_back(10);   // version
        v.push_back((uint8_t)((level_ ? 1 : 0) | (lim_valid_ ? 2 : 0) | (stop_ ? 4 : 0) | (respawn_ ? 16 : 0) |
                              (armed_ ? 32 : 0)));
        const uint32_t since = snes_ ? snes_->frameCount() - loop_frame_ : 255;
        v.push_back((uint8_t)std::min<uint32_t>(since, 255));
        // (the last two words held a dimmed range once, written as 0: now the camera's y and the shaft's flags)
        for (int x : {ext_, lim_lo_, lim_hi_, d_off_, last_cam_, last_camy_, (shaft_ ? 1 : 0) | (slide_y_ ? 2 : 0)}) {
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
        // (version 5) the level objects deleted / refused, and whose spawn is running
        v.push_back((uint8_t)spawn_mode_);
        v.push_back((uint8_t)retry_idx_);
        v.insert(v.end(), gone_, gone_ + 32);
        v.insert(v.end(), again_, again_ + 32);
        v.insert(v.end(), refused_, refused_ + 32);
        v.push_back((uint8_t)((wall_valid_ ? 1 : 0) | (behind_valid_ ? 2 : 0)));
        for (int x : {wall_x_, behind_x_}) {
            v.push_back((uint8_t)x);
            v.push_back((uint8_t)(x >> 8));
        }
        v.insert(v.end(), waiting_, waiting_ + 32);   // (version 6) timed objects waiting for the original's moment
        v.push_back((uint8_t)fight_dim_);             // (version 8) a boss fight's dim, fading
        // (version 9) Jafar's arch: its wave's table, the entries moved past it and its motion (arch_)
        const Arch& a = arch_;
        v.push_back((uint8_t)((a.valid ? 1 : 0)));
        v.push_back((uint8_t)a.known);
        for (uint16_t e : a.t) {
            v.push_back((uint8_t)e);
            v.push_back((uint8_t)(e >> 8));
        }
        for (uint32_t x : {a.y, a.vel, (uint32_t)a.pc, (uint32_t)a.centre}) {
            v.push_back((uint8_t)x);
            v.push_back((uint8_t)(x >> 8));
            v.push_back((uint8_t)(x >> 16));
        }
        for (uint8_t x : {a.acc, a.idx, a.on, a.strength}) v.push_back(x);
        v.insert(v.end(), starved_, starved_ + 32);   // (version 10) objects refused at the margins' columns
        return v;
    }
    void load_ext(const uint8_t* d, size_t n) {
        lim_valid_ = false;
        loop_frame_ = 0x80000000u;
        d_off_ = 0;
        shaft_ = slide_y_ = false;
        for (bool& b : oam_set_) b = false;
        for (bool& b : build_set_) b = false;
        respawn_ = true;   // (a state without the module's data: the margins' objects)
        logic_done_ = band_taken_ = false;
        band_next_.valid = band_shown_.valid = false;
        // (a state without them: nothing known deleted or refused)
        std::memset(gone_, 0, sizeof(gone_));
        std::memset(again_, 0, sizeof(again_));
        std::memset(refused_, 0, sizeof(refused_));
        std::memset(waiting_, 0, sizeof(waiting_));
        std::memset(starved_, 0, sizeof(starved_));
        spawn_mode_ = kSpawnGame;
        retry_idx_ = 0;
        retry_frame_ = 0xFFFFFFFFu;
        pending_again_ = 0;
        wall_valid_ = behind_valid_ = false;
        arch_.valid = false;   // (a state without it: the arch's wave known from the next logic frame on)
        // (a state without it: a boss fight's dim as it stands, no fade)
        fight_dim_ = snes_ && boss_held(*snes_) ? kFadeFrames : 0;
        // (a state without it: a level being played if one is on screen)
        armed_ = snes_ && playing_level(*snes_) && in_level(*snes_);
        const size_t need1 = 2 + 5 * 2 + 128 * 3, need2 = 3 + 7 * 2 + 2 * 128 * 3, need3 = need2 + 1 + 7 * 2;
        const size_t need5 = need3 + 2 + 3 * 32 + 5, need6 = need5 + 32, need8 = need6 + 1;
        const size_t need9 = need8 + 2 + 2 * (32 + kArchHist) + 4 * 3 + 4;
        const size_t need10 = need9 + 32;
        if (n < 2 || d[0] < 1 || d[0] > 10 || (d[0] == 1 && n < need1) || (d[0] == 2 && n < need2) ||
            (d[0] >= 3 && n < need3) || (d[0] >= 5 && n < need5) || (d[0] >= 6 && n < need6) ||
            (d[0] >= 8 && n < need8) || (d[0] >= 9 && n < need9) || (d[0] >= 10 && n < need10))
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
        if (d[0] >= 7) {   // (version 7: the camera's y and the shaft's flags; before, 0 or a dimmed range once held)
            last_camy_ = v[5];
            shaft_ = (v[6] & 1) != 0;
            slide_y_ = (v[6] & 2) != 0;
        } else {
            lim_valid_ = false;   // (an older state's view knew no shafts: its first frame where the view stops now)
        }
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
        if (d[0] >= 5) {
            spawn_mode_ = d[o] <= kSpawnRefused ? d[o] : kSpawnGame;
            retry_idx_ = d[o + 1];
            o += 2;
            std::memcpy(gone_, d + o, 32);
            std::memcpy(again_, d + o + 32, 32);
            std::memcpy(refused_, d + o + 64, 32);
            o += 96;
            wall_valid_ = (d[o] & 1) != 0;
            behind_valid_ = (d[o] & 2) != 0;
            wall_x_ = (int16_t)(d[o + 1] | (d[o + 2] << 8));
            behind_x_ = (int16_t)(d[o + 3] | (d[o + 4] << 8));
            o += 5;
        }
        if (d[0] >= 6) std::memcpy(waiting_, d + o, 32);
        if (d[0] >= 8) fight_dim_ = std::min<int>(d[o + 32], kFadeFrames);
        if (d[0] >= 9) {
            size_t q = o + 33;
            Arch& a = arch_;
            a.valid = (d[q] & 1) != 0;
            a.known = std::clamp<int>(d[q + 1], 0, 32 + kArchHist);
            q += 2;
            for (uint16_t& e : a.t) {
                e = (uint16_t)(d[q] | (d[q + 1] << 8));
                q += 2;
            }
            uint32_t w[4];
            for (uint32_t& x : w) {
                x = d[q] | (d[q + 1] << 8) | (d[q + 2] << 16);
                q += 3;
            }
            a.y = w[0];
            a.vel = w[1];
            a.pc = (uint16_t)w[2];
            a.centre = (uint16_t)w[3];
            a.acc = d[q];
            a.idx = d[q + 1];
            a.on = d[q + 2];
            a.strength = d[q + 3];
        }
        if (d[0] >= 10) std::memcpy(starved_, d + o + 33 + (need9 - need8), 32);   // (after the arch's data)
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
    // Patch-ROM stub for a refused once-only object's retry: JSL kStubColumn (X), JML $82:A7A3 (the frame spawner's
    // start, which sets its registers again).
    static constexpr uint32_t kStubRetryRefused = 0x608040;
    // Whose spawn is running (spawn_mode_): the game's own, the width change's (kStubRespawn), the original's edge
    // columns (kStubRetryR / L), a refused object's retry (kStubRetryRefused).
    enum { kSpawnGame = 0, kSpawnWidth = 1, kSpawnEdge = 2, kSpawnRefused = 3 };

    const Snes* snes_ = nullptr;
    // The level objects (by list index) deleted since the level's start (gone_), of them the ones whose last delete was
    // past the 4:3 screen's edge or the sweep's beyond the original's edges (again_), and the ones waiting for the
    // original's moment (refused_: once-only objects the allocator refused, timed ones held back from the margins).
    // Bits by index. waiting_: timed objects the margins spawned ahead of the original, their first wait held
    // (kObjWaitDec).
    // starved_: objects the allocator refused at the margins' own columns, tried again while beyond the picture's edge.
    uint8_t gone_[32] = {}, again_[32] = {}, refused_[32] = {}, waiting_[32] = {}, starved_[32] = {};
    int spawn_mode_ = kSpawnGame, retry_idx_ = 0;
    uint32_t retry_frame_ = 0xFFFFFFFFu;
    uint16_t pending_again_ = 0;   // the object whose delete follows: past the 4:3 screen's edge, or swept beyond
    // The Lava Cavern's wall and left-behind limit as last given (world x), easing to a width change's new edges.
    int wall_x_ = 0, behind_x_ = 0;
    bool wall_valid_ = false, behind_valid_ = false;
    GameConfig cfg_;
    int ext_ = 0, last_ext_ = -1;
    bool level_ = false, respawn_ = false;
    bool armed_ = false;   // a level being played since its start spawn (active)
    uint32_t loop_frame_ = 0x80000000u;   // the frame the level loop's top last ran in
    bool lim_valid_ = false;
    int lim_lo_ = 0, lim_hi_ = 0, d_off_ = 0, last_cam_ = 0;   // where the view stops; its offset still to close
    int last_camy_ = 0;
    bool shaft_ = false;     // the span is a shaft's column (span)
    bool slide_y_ = false;   // the offset came from a shaft's span: it closes as the camera moves in x or y
    bool stage_clear_ = false;   // this frame: the STAGE CLEAR screen (its plain backdrop fills the margins)
    bool genie_loading_ = false; // ...with its Genie's first frame drawn (his tiles not yet uploaded)
    bool roll_ = false;          // this frame: the staff roll (centred; its sky drawn from the level's data)
    bool stop_ = true;                    // the view may stop at the level's ends (can_stop)
    int el_ = 0, er_ = 0, mid_ = 0;
    int bg2_ratio_ = 0;      // this frame: BG2's x speed against the camera (8.8), 0 = scrolled by the game itself
    bool obj_bg2_ = false;   // this line: BG2 holds an object's picture (obj_lo_ / obj_hi_: its own copy on screen)
    int obj_lo_ = 0, obj_hi_ = 0;
    bool arena_ = false;            // this frame: an arena holds the game's camera (arena)
    int paint_row_ = 0x10000;       // this frame: the first line the game has painted on its screen only
    bool jafar_ = false;            // this frame: Jafar's room held (level $11's arena): the fight's sides
    bool fire_ = false;             // ... with the cobra's fire floor and arch painted on the game's screen
    int jafar_lo_ = 0, jafar_hi_ = 0;   // this frame: the room's extent (its layers mirrored past it)
    int8_t period_[2][32] = {};     // this frame: a painted map row's repeat in tile columns (0: not worked out yet)
    bool opt_built_ = false;        // this frame: the arch's margin columns' scrolls (Ppu::wideOptV) are set
    // The arch's wave as the game's logic left it (taken at its frame wait, kLevelWait, where its step is done): the
    // column table ($7E:DFC0) with the entries that have moved on past it, and the object's motion (arch_steps).
    struct Arch {
        bool valid = false;
        int known = 0;                     // entries of t known: the game's 32, then the ones moved past them
        uint16_t t[32 + kArchHist] = {};   // newest first
        uint32_t y = 0, vel = 0;           // 24 bits each
        uint16_t pc = 0, centre = 0;
        uint8_t acc = 0, idx = 0, on = 0, strength = 0;
    };
    Arch arch_;
    int fight_dim_ = 0;             // the sides' dim while a boss fight holds Aladdin (boss_held), in 32nds: 0-16
    LineLayer line_[2];
    struct Cover { int px, py; bool right; int n; };
    Cover cover_[16];                  // this frame: edge_cover's answers
    int cover_n_ = 0;
    int near_x_[2] = {}, near_y_[2] = {};   // this frame: the layer structs' x / y at its first line
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
        PatchAsm f(s, kStubRetryRefused);
        for (uint8_t b : jsl(kStubColumn)) f.b8(b);                     // JSL column (X)
        for (uint8_t b : jml(kSpawnFrameTop)) f.b8(b);                  // JML $82:A7A3
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
    // (Not one the game deleted where it had it, killed or swept in view: skip.)
    int next_column(const Snes& s, const uint8_t* cols, int n, int i) const {
        for (i++; i < n - 1; i++) {
            const int count = rd8(s, kColCount + cols[i]), first = rd8(s, kColFirst + cols[i]);
            for (int k = 0; k < count; k++) {
                const int idx = (first + 1 + k) & 0xFF;
                if (idx == 0) continue;
                const uint16_t dy = (uint16_t)(rom16(s, list_entry(s, idx) + 4) + 0x40 - rd16(s, kSpawnCamY));
                if (!(rd8(s, kSpawnedBits + idx) & 0x81) && dy < 0x180 && !skip_edge(s, idx)) return i;
            }
        }
        return n - 1;
    }
    // A level object's list entry (type, flags - bit 7 once only -, x, y).
    static uint32_t list_entry(const Snes& s, int idx) {
        return kObjList + (uint32_t)((idx - 1) * 6 + rd16(s, kObjListOff));
    }
    static bool bit(const uint8_t* b, int i) { return (b[(i >> 3) & 31] >> (i & 7)) & 1; }
    static void set_bit(uint8_t* b, int i, bool v) {
        if (v) b[(i >> 3) & 31] |= (uint8_t)(1 << (i & 7));
        else b[(i >> 3) & 31] &= (uint8_t)~(1 << (i & 7));
    }
    // What the module's own spawns leave out (spawn_mode_): a width change's, everything the game has deleted since the
    // level's start; the original's edge columns, what it deleted for good (not past the 4:3 screen's edge, nor swept
    // beyond the original's edges) and an ordinary object whose place is well inside the picture (in_view); the
    // refused object's retry, everything but it.
    bool skip_edge(const Snes& s, int idx) const { return (bit(gone_, idx) && !bit(again_, idx)) || in_view(s, idx); }
    bool skip(const Snes& s, int idx) const {
        switch (spawn_mode_) {
        case kSpawnWidth: return bit(gone_, idx);
        case kSpawnEdge: return skip_edge(s, idx);
        case kSpawnRefused: return idx != retry_idx_;
        default: return false;
        }
    }
    // The original's moment for an object the margins had early (the edge-column retry, the once-only retry of one
    // deleted beyond the original's edges) is its spawn at camera + $120 / - $40: 32 / 64 px past the 4:3 screen's
    // edges, inside the picture from ext 32 / 64 on. An ordinary object (not once only, not timed) whose place is
    // more than 16 px inside the picture is left to the margins' own columns instead, as the original leaves it to its
    // edges: an Agrabah Market guard that walked left out of the wide keep range came back at camera - $40 when the
    // view went back left, 64 px inside the picture's left edge at 106 (the original has him come back off its
    // screen); a guard refused for lack of a sprite bank at the margin's column came at camera + $120.
    bool in_view(const Snes& s, int idx) const {
        if (ext_ == 0) return false;
        const uint32_t e = list_entry(s, idx);
        if ((rom16(s, e) & 0x8000) || timed_type(rom16(s, e) & 0xFF)) return false;
        int el, er;
        object_split(s, el, er);
        const int cam = (int16_t)rd16(s, kCamX), x = rom16(s, e + 2) & 0x3FFF;
        return x > cam - el + 16 && x < cam + 256 + er - 16;
    }
    // A level object whose timer mustn't start in the margins, only when the original's spawn edges reach its column:
    // a type whose script runs on a timer from its spawn (kTimedTypes: the Lava Cavern's pillar waits, bubbles, rises,
    // holds and sinks, the Carpet Escape's rock waits and falls - once, as the camera never comes back), not out yet,
    // never deleted in this level, its column beyond the original's edges (kSpawnTest: spawned with its wait held).
    bool held(const Snes& s, int idx) const {
        if (idx == 0 || bit(gone_, idx) || (rd8(s, kSpawnedBits + idx) & 0x81)) return false;
        return timed_type(rom16(s, list_entry(s, idx)) & 0xFF) && beyond(s, idx);
    }
    static const TimedType* timed_type(int type) {
        for (const TimedType& t : kTimedTypes)
            if (t.type == type) return &t;
        return nullptr;
    }
    // A level object's list column beyond the original's spawn edge columns (camera + $120 / - $40).
    static bool beyond(const Snes& s, int idx) {
        const int cam = (int16_t)rd16(s, kCamX), c = (rom16(s, list_entry(s, idx) + 2) & 0x3FFF) >> 5;
        return c > floor_shr(cam + 0x120, 5) || c < floor_shr(cam - 0x40, 5);
    }
    // A level object to spawn at the original's moment, not out yet (spawned bits & $81 clear), its column within the
    // original's spawn edges (camera - $40 .. camera + $120) and its row within the spawner's: its index and column.
    // (A once-only object the allocator refused: tried once a frame until it is out. One held back from the margins,
    // or one deleted beyond the original's spawn edges with its column beyond them: tried once, as the original's
    // spawner tries it once when its edge reaches the column.)
    int refused_pending(Snes& s, int& col) {
        const int cam = (int16_t)rd16(s, kCamX);
        const int lo = floor_shr(cam - 0x40, 5), hi = floor_shr(cam + 0x120, 5);
        for (int idx = 1; idx < 256; idx++) {
            if (!bit(refused_, idx)) continue;
            if (rd8(s, kSpawnedBits + idx) & 0x81) {
                set_bit(refused_, idx, false);
                continue;
            }
            const uint32_t e = list_entry(s, idx);
            const int c = (rom16(s, e + 2) & 0x3FFF) >> 5;
            const uint16_t dy = (uint16_t)(rom16(s, e + 4) + 0x40 - rd16(s, kSpawnCamY));
            if (c >= lo && c <= hi && dy < 0x180) {
                if (!(rom16(s, e) & 0x8000)) set_bit(refused_, idx, false);
                if (in_view(s, idx)) continue;   // (its one try: in the picture, left to the margins' columns)
                col = c;
                return idx;
            }
        }
        // Then one the allocator refused at the margins' own columns (starved_: no free sprite VRAM bank, the wider
        // view keeping more of the level's enemies alive; an Agrabah Market guard, refused while a pot thrower 170 px
        // left of the screen held the fourth bank): the game's spawner tries it again only when an edge crosses its
        // column again, and the original's edge-column retry brought it at camera + $120, 74 px (106) / 108 px (140)
        // inside the picture. Tried once a frame while its place is still at the picture's edge or beyond it (within
        // the margins' spawn columns), it comes in from the edge as soon as a bank frees; once its place is in the
        // picture it is left to the original's moment, as before.
        int el, er;
        object_split(s, el, er);
        for (int idx = 1; idx < 256; idx++) {
            if (!bit(starved_, idx)) continue;
            const int side = starved_place(s, idx, el, er);
            if (side == 0) {
                set_bit(starved_, idx, false);
                continue;
            }
            if (side != 2) {
                col = (rom16(s, list_entry(s, idx) + 2) & 0x3FFF) >> 5;
                return idx;
            }
        }
        return 0;
    }
    // A starved_ object's place: 0 no longer waiting (out, deleted, or its place in the picture or beyond the margins'
    // spawn columns), 2 waiting out of the spawner's rows, else the side it waits on (1 right, -1 left).
    int starved_place(const Snes& s, int idx, int el, int er) const {
        if ((rd8(s, kSpawnedBits + idx) & 0x81) || bit(gone_, idx)) return 0;
        const int cam = (int16_t)rd16(s, kCamX);
        const uint32_t e = list_entry(s, idx);
        const int x = rom16(s, e + 2) & 0x3FFF, c = x >> 5;
        // (its place at the picture's edge or beyond it, up to 8 px in)
        const int side = x >= cam + 256 + er - 8 && c <= floor_shr(cam + er + 0x120, 5) ? 1
                         : x < cam - el + 8 && c >= floor_shr(cam - el - 0x40, 5)        ? -1
                                                                                          : 0;
        if (side == 0) return 0;
        return (uint16_t)(rom16(s, e + 4) + 0x40 - rd16(s, kSpawnCamY)) < 0x180 ? side : 2;
    }
    // While a starved_ object waits on one side, an object beyond the original's keep range and off the picture on the
    // other side (behind the view: the 4:3 game has deleted it) is let go as the original lets it go, freeing its slot
    // and sprite VRAM banks. In the Agrabah Market a pot thrower ($51) 64 px past the picture's left edge held the bank
    // a guard coming in on the right needed (the wider keep range kept it). (Not a once-only object: it would never
    // come back.)
    bool let_go(const Snes& s, uint16_t dp, int el, int er) const {
        int side = 0;
        for (int i = 0; i < 32 && side == 0; i++)
            for (int b = 0; b < 8 && starved_[i] && side == 0; b++)
                if ((starved_[i] >> b) & 1) {
                    const int p = starved_place(s, i * 8 + b, el, er);
                    if (p == 1 || p == -1) side = p;
                }
        if (side == 0) return false;
        const uint8_t idx = rd8(s, dp + 0x34);
        if (idx != 0 && (rd8(s, kSpawnedBits + idx) & 0x80)) return false;
        const int x = (int16_t)rd16(s, dp + 0x0C), cam = (int16_t)rd16(s, kCamX);
        return side > 0 ? x < cam - el - 32 : x >= cam + 256 + er + 32;
    }

    // A level screen: the main task waits in the level's main loop or its pause loop (task 0's saved return address).
    static bool in_level(const Snes& s) {
        const uint16_t sp = rd16(s, 0x0071);
        if (sp < 0x0100 || sp > 0x0170) return false;
        const uint32_t ret = rd16(s, sp + 7) | (rd8(s, sp + 9) << 16);
        return ret == kLevelWaitRet || ret == kPauseWaitRet;
    }

    // A page entry of the table as the margins take it (idx: its offset in the table). Inside the Lamp 3's corridor's
    // first page, shut behind the camera by the game (kTowerShut), is still the level: its gold block and the left
    // pillar going up stay in the margin as the camera goes on, as they were before the door shut (it had them go to
    // background only - the block, its ledge and the pillar vanishing from the left side as the camera passed x 256).
    static uint16_t page_entry(const Snes& s, int idx) {
        const uint16_t pg = rd16(s, kPageTable + (idx & 0xFFF));
        if (pg == kTowerShut && (idx & 0xFFF) == 0x42 && rd16(s, kLayer[0] + 0x42) == 0 && rd8(s, kLevel) == kTowerLevel)
            return kTowerTop;
        return pg;
    }
    // BG1's page entry (page x, page y in pages; -1 .. border).
    static uint16_t page(const Snes& s, uint32_t base, int px, int py) {
        const int idx = rd16(s, base + 0x42) + (py + 1) * 64 + (((px + 1) * 2) & 0xFE);
        return page_entry(s, idx);
    }
    static bool barrier(const Snes& s, int px, int py) { return (page(s, kLayer[0], px, py) & 0x8000) != 0; }

    // A tile entry of a streaming layer at world (x, y), as the column builder ($81:C2E0) finds it.
    static uint16_t tile_at(const Snes& s, const LineLayer& ll, int x, int y) {
        const int idx = ll.off + (((y + 256) & 0xFF00) >> 2) + ((((x >> 8) + 1) * 2) & 0xFE);
        uint16_t pg = page_entry(s, idx);
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
    // end: the Cave of Wonders' last event), else centred; a shaft, its column centred (shaft: the screen's rows hold
    // nothing else). (Not the zones' limits nor the pages the camera can't enter at its height elsewhere: they change
    // as the camera goes, and the view would have to pan by itself; the margins show the level there.)
    void span(const Snes& s, int cam, int& lo, int& hi, bool* in_shaft = nullptr) const {
        level_extent(s, lo, hi);
        const bool sh = !arena(s) && shaft(s, cam, lo, hi);
        if (in_shaft) *in_shaft = sh;
        if (sh) {
            // a shaft: its one page column, narrower than the picture, centred
            lo = cam;
            hi = cam + 256;
            return;
        }
        if (arena(s) && rd8(s, kLevel) == kJafarLevel) {
            // Jafar's room: the fight is the game's screen (Aladdin held on it, Jafar and then the cobra at its
            // right, the cobra's head and coils swaying past its edge), at the room's right end: centred, its right
            // side past the room's end (its sides: beginLine). An open span, so that the view's target is the camera's
            // and the span doesn't change as the room's pan moves it (the offset then closes with the pan).
            lo = -0x4000;
            hi = 0x4000;
            return;
        }
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
    // A shaft: every page row of the game's screen (camera y to + 223) holds only the camera's own page column - the
    // pages beside it may not be entered (bit 15) or lie outside the level - so the game's camera is pinned in x
    // there (Inside the Lamp 3's tower, the Cave of Wonders' drop at x 2816). The pages beside it hold nothing of the
    // level: the game draws them as page 0, its picture is the column's own 256 px.
    static bool shaft(const Snes& s, int cam, int lo, int hi) {
        if (cam & 0xFF) return false;
        const int c = cam >> 8, cy = (int16_t)rd16(s, kCamY);
        if (cy < 0) return false;
        for (int r = cy >> 8; r <= (cy + 223) >> 8; r++) {
            if (barrier(s, c, r)) return false;
            if ((c - 1) * 256 >= lo && !barrier(s, c - 1, r)) return false;
            if ((c + 1) * 256 < hi && !barrier(s, c + 1, r)) return false;
        }
        return true;
    }
    // The camera held in x while it follows in y: a fixed arena (Jafar's). (Both held: a death's fall.)
    static bool arena(const Snes& s) { return rd16(s, kCamHoldX) && !rd16(s, kCamHoldY); }
    // A boss fight that holds Aladdin on the game's screen until it is won (kBossFights: the word that marks it on -
    // its zone's right limit, which opens once the fight is won; Jafar's room grown - and the camera held in x if the
    // fight holds it).
    static bool boss_held(const Snes& s) {
        const int level = rd8(s, kLevel);
        for (const BossFight& b : kBossFights)
            if (level == b.level && rd16(s, b.addr) == b.value && (!b.hold || rd16(s, kCamHoldX))) return true;
        return false;
    }
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
    int band_adj(const Ppu& p, int row, LineLayer& ll, int* band_id = nullptr) const {
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
            if (band_id) *band_id = k + 1;
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
            if (band_id) *band_id = band + 1;
            dh = band == 9 ? v - c : 0;
        } else if (mode == 0x10) {
            const int sub = rd16(s, kBgSub);
            dh = phase_x(sub, v) - phase_x(sub, c);
            // (past what the game shows of it at the level's end its last pages hold other data: mirrored there)
            if (sub != 0) ll.hi = phase_x(sub, (int16_t)rd16(s, kCamMaxX)) + 256;
        }
        return dh - (v - c);
    }

    // The first line on which the game has painted on its screen what the level beside it doesn't show (0x10000:
    // none). Jafar's cobra: from the frame its room's map ($4000, on BG1) and the arch's ($4800, on BG2) are switched
    // to, the fire floor (BG1's map from row kFireRow) and the arch (BG2's map from its first row that isn't empty).
    static int painted_row(const Snes& s, const Ppu& p) {
        if (rd8(s, kLevel) != kJafarLevel || p.bgMapBase(0) != kFireRoom || p.bgMapBase(1) != kFireArch) return 0x10000;
        // (a line shows map y = scroll + row + 1; the room's scroll is signed: the shake as the arch rises takes it
        // a few lines above 0, 1016-1022 in the register, which as 248-254 put the row at -87 and dimmed the whole
        // side, wall included; tools/qa round 2, N2)
        const int vofs = ((p.bgVofs(0) + 0x80) & 0xFF) - 0x80;
        int row = kFireRow * 8 - 1 - vofs;
        for (int r = 0; r < 32; r++) {
            bool any = false;
            for (int c = 0; c < 32 && !any; c++) any = p.vram[(kFireArch + r * 32 + c) & 0x7FFF] != 0;
            if (any) {
                row = std::min(row, (r * 8 - 1 - p.bgVofs(1)) & 0xFF);
                break;
            }
        }
        return std::max(row, 0);
    }

    // The arch's object and column table at the end of a logic frame (its step done). The table shifts one column on
    // with each step: the entry that moves past its end is kept (the margin right of the game's screen shows it).
    void take_arch(const Snes& s) {
        Arch& a = arch_;
        if (ext_ == 0 || rd8(s, kLevel) != kJafarLevel || !active(s)) {
            a.valid = false;
            return;
        }
        uint32_t b = 0;
        for (int i = 0; i < 35 && !b; i++) {
            const uint32_t o = 0x092B + i * 0x50;
            const uint16_t pc = rd16(s, o + 3);
            if (rd8(s, o) && rd8(s, o + 6) == kArchType && rd8(s, o + 5) == 0x84 &&
                (pc == kArchHold || pc == kArchUp || pc == kArchDown))
                b = o;
        }
        if (!b) {
            a.valid = false;
            return;
        }
        uint16_t t[32];
        for (int i = 0; i < 32; i++) t[i] = rd16(s, kArchTable + 2 * i);
        if (a.valid && std::equal(t + 1, t + 32, a.t) && !std::equal(t, t + 32, a.t)) {
            std::memmove(a.t + 1, a.t, sizeof(a.t) - sizeof(a.t[0]));   // (one step: the last entry moves on)
            a.known = std::min(a.known + 1, 32 + kArchHist);
        } else if (!a.valid || !std::equal(t, t + 32, a.t)) {
            a.known = 32;
        }
        std::memcpy(a.t, t, sizeof(t));
        a.valid = true;
        a.y = rd8(s, b + 0x0E) | (rd8(s, b + 0x0F) << 8) | (rd8(s, b + 0x10) << 16);
        a.vel = rd8(s, b + 0x1D) | (rd8(s, b + 0x1E) << 8) | (rd8(s, b + 0x1F) << 16);
        a.acc = rd8(s, b + 0x20);
        a.pc = rd16(s, b + 3);
        a.centre = rd16(s, b + 0x40);
        a.idx = rd8(s, b + 0x42);
        a.on = rd8(s, kArchOn);
        a.strength = rd8(s, kArchStrength);
    }
    // The entries the arch's next steps will insert, in order (fewer once it holds): its script run ahead from
    // arch_ - the velocity less (up) or plus (down) the acceleration, y plus the velocity, the entry, the turn at the
    // centre with the next half's velocity and acceleration from the tables.
    int arch_steps(uint16_t* out, int n) const {
        const Snes& s = *snes_;
        const Arch& a = arch_;
        uint32_t y = a.y, v = a.vel, acc = a.acc;
        int idx = a.idx;
        auto start = [&](uint32_t table) {
            const int k = (uint8_t)rom16(s, table + idx);
            v = (uint8_t)rom16(s, kArchVelLo + k) | ((uint8_t)rom16(s, kArchVelMid + k) << 8) |
                ((uint8_t)rom16(s, kArchVelHi + k) << 16);
            acc = (uint8_t)rom16(s, kArchAcc + k);
        };
        int phase = a.pc == kArchUp ? 1 : a.pc == kArchDown ? 2 : 0;
        if (phase == 0) {   // (holding: an up-swing starts once the wave is on, a step with no entry)
            if (!a.on) return 0;
            idx = a.strength;
            start(kArchStartUp);
            phase = 1;
        }
        int k = 0;
        while (k < n) {
            v = (phase == 1 ? v - acc : v + acc) & 0xFFFFFF;
            y = (y + v) & 0xFFFFFF;
            const int y16 = (int)((y >> 8) & 0xFFFF);
            out[k++] = (uint16_t)(((y16 - 0x100) & 0x1FF) | 0x4000);
            if (phase == 1 && y16 < a.centre) {
                phase = 2;
                start(kArchStartDown);
            } else if (phase == 2 && y16 >= a.centre) {
                if (!a.on) break;
                idx = a.strength;
                phase = 1;
                start(kArchStartUp);
            }
        }
        return k;
    }
    // The arch's scrolls for the margin columns (Ppu::wideOptV for BG2), on the frame's first line in mode 2: the
    // game's screen has column 0 at the register and columns 1-32 at its table (BG3's map, as the PPU reads it). Left
    // of it come the entries the next steps insert (column -k the k-th: the wave travels in from there), right of it
    // the ones moved past the table's end. The table on the screen is the one the NMI copied, arch_ the one the last
    // logic frame left: one or two steps ahead in a frame the logic ran ahead of the picture (d).
    void build_arch_opt(Ppu& p) {
        opt_built_ = true;
        const int reg = p.bgVofs(1) & 0x3FF, b3h = p.bgHofs(2), b3v = p.bgVofs(2);
        uint16_t sc[32];
        int16_t* o = p.wideOptV[1] + Ppu::kWideOptOff;
        o[0] = (int16_t)reg;
        for (int c = 1; c <= 32; c++) {
            sc[c - 1] = p.tileEntry(2, ((c - 1) * 8 + (b3h & ~7)) >> 3, (b3v >> 3) + 1);
            o[c] = (int16_t)((sc[c - 1] & 0x4000) ? (sc[c - 1] & 0x3FF) : reg);
        }
        int d = -1;
        for (int k = 0; k <= 4 && d < 0 && arch_.valid; k++)
            if (std::equal(sc, sc + 32 - k, arch_.t + k)) d = k;
        // right: entries 32 on (column 33 on)
        int16_t last = o[32];
        for (int c = 33; c + Ppu::kWideOptOff < Ppu::kWideOptN; c++) {
            const int j = c - 1 + (d < 0 ? 0 : d);
            if (d >= 0 && j < arch_.known) last = (int16_t)(arch_.t[j] & 0x3FF);
            o[c] = last;
        }
        // left: the entries to come
        constexpr int kLeft = Ppu::kWideOptOff;
        uint16_t f[kLeft];
        int n = 0;
        if (d >= 0) {
            for (int k = d - 1; k >= 0 && n < kLeft; k--) f[n++] = arch_.t[k];
            n += arch_steps(f + n, kLeft - n);
        }
        last = o[0];
        for (int k = 1; k <= kLeft; k++) {
            if (k <= n) last = (int16_t)(f[k - 1] & 0x3FF);
            o[-k] = last;
        }
    }
    // A painted map row's entry for a margin column (screenX): the row repeated from the game's screen, by the row's
    // own repeat (the smallest period its columns 8-31 have; the whole screen if none), so the fire floor and the
    // arch's spikes go on as the game draws them, at their height, animated with them (the fire's tiles are animated
    // in VRAM, every 8 frames).
    uint16_t painted_entry(const Ppu& p, int layer, int mapY, int screenX) {
        const int hofs = p.bgHofs(layer) & 0x3FF, base = hofs >> 3, row = mapY & 31;
        int8_t& per = period_[layer][row];
        if (per == 0) {
            uint16_t e[32];
            for (int i = 0; i < 32; i++) e[i] = p.tileEntry(layer, base + i, row);
            per = 32;
            for (int q = 1; q <= 16 && per == 32; q++) {
                bool ok = true;
                for (int i = 8; i + q < 32 && ok; i++) ok = e[i] == e[i + q];
                if (ok) per = (int8_t)q;
            }
        }
        const int v = screenX + (hofs & 7), c = floor_shr(v, 3);
        const int b0 = per == 32 ? 0 : 8;
        return p.tileEntry(layer, base + b0 + (((c - b0) % per) + per) % per, row);
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

    // How many 8-px tile columns at a page's left (right) edge BG1 covers on every line of the page: from the edge in,
    // columns whose tiles draw every pixel in all 32 tile rows (at most 8). Inside the Lamp 3's tower: its gold
    // pillars, 4. (Cached for the frame.)
    int edge_cover(const Ppu& p, const Snes& s, int px, int py, bool right) {
        for (int i = 0; i < cover_n_; i++)
            if (cover_[i].px == px && cover_[i].py == py && cover_[i].right == right) return cover_[i].n;
        const int bpp = p.bgBpp(0);
        int n = 0;
        if ((bpp == 2 || bpp == 4 || bpp == 8) && !p.bgBigTiles(0)) {
            for (; n < 8; n++) {
                const int x = right ? px * 256 + 255 - n * 8 : px * 256 + n * 8;
                bool full = true;
                for (int ty = 0; ty < 32 && full; ty++) {
                    const uint16_t e = tile_at(s, line_[0], x, py * 256 + ty * 8);
                    const int adr = p.bgCharBase(0) + (e & 0x3FF) * bpp * 4;
                    for (int y = 0; y < 8 && full; y++) {
                        unsigned acc = 0;
                        for (int k = 0; k < bpp / 2; k++) {
                            const uint16_t w = p.vram[(adr + y + 8 * k) & 0x7FFF];
                            acc |= w | (w >> 8);
                        }
                        full = (acc & 0xFF) == 0xFF;
                    }
                }
                if (!full) break;
            }
        }
        if (cover_n_ < (int)(sizeof cover_ / sizeof cover_[0])) cover_[cover_n_++] = {px, py, right, n};
        return n;
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

    // STAGE CLEAR (screen $24, its task from the picture's set-up to the fade-out after B, $81:F7B6-$81:F90C): a
    // panel on a plain backdrop (colour 0, the dark purple) whose BG maps hold nothing past its 256 px. The margins
    // show the backdrop and the sprites, so the purple fills the width around the panel (the app's plain bars don't
    // take it: the panel's gold frame runs 4 px inside its edges) and the Genie flies out into it. Read as the frame
    // starts, so a loaded state draws the same.
    static bool stage_clear(const Snes& s) {
        if (rd8(s, kLevel) != kStageClearScreen) return false;
        const uint16_t sp = rd16(s, 0x0071);
        if (sp < 0x0100 || sp > 0x0170) return false;
        const uint32_t ret = rd16(s, sp + 7) | (rd8(s, sp + 9) << 16);
        return ret >= kStageClearFrom && ret <= kStageClearTo;
    }

    // STAGE CLEAR's Genie on his first frame drawn, as the frame starts (his loop has run once more by then: $40 is
    // $54): the tiles shown aren't his yet (in the game they are past the screen's left edge), so the margins hide it.
    static bool genie_loading(const Snes& s) {
        for (int k = 0; k < 35; k++) {
            const uint32_t o = 0x092B + k * 0x50;
            if (rd8(s, o) == 0 || rd8(s, o + 6) != kGenieType) continue;
            const uint32_t pc = rd16(s, o + 3) | (rd8(s, o + 5) << 16);
            return pc == kGenieFly && rd8(s, o + 0x14) == 0 && rd8(s, o + 0x40) >= 0x54;
        }
        return false;
    }

    void frame_start(Ppu& p) {
        el_ = er_ = ext_;
        stage_clear_ = ext_ > 0 && snes_ && !level_ && stage_clear(*snes_);
        genie_loading_ = stage_clear_ && genie_loading(*snes_);
        roll_ = ext_ > 0 && snes_ && !level_ && staff_roll(*snes_);
        mid_ = 0;
        arena_ = false;
        paint_row_ = 0x10000;
        jafar_ = fire_ = opt_built_ = false;
        std::memset(period_, 0, sizeof(period_));
        cover_n_ = 0;
        band_on_ = false;
        hdma_line_ = -1;   // (the IRQ comes after the first lines)
        if (level_ && ext_ > 0 && snes_) {
            const Snes& s = *snes_;
            // the camera on screen: BG1's scroll register, completed from the NMI's copy
            const int cam = unwrap10(p.bgHofs(0), (int16_t)rd16(s, kBg1Shown));
            if (lim_valid_) {
                el_ = split_left(cam);
            } else {
                // (a state made at another width just loaded, the width set from 4:3: where the view stops already)
                int lo, hi;
                span(s, cam, lo, hi);
                el_ = std::clamp(cam - target_v(cam, lo, hi, can_stop(s)), 0, 2 * ext_);
            }
            er_ = 2 * ext_ - el_;
            mid_ = (er_ - el_) / 2;
            arena_ = arena(s);
            paint_row_ = painted_row(s, p);
            jafar_ = arena_ && rd8(s, kLevel) == kJafarLevel;
            fire_ = jafar_ && paint_row_ < 0x10000;
            if (jafar_) level_extent(s, jafar_lo_, jafar_hi_);
            // On fire, the left side is the fight's screen going on, as the right: mirrored about its left edge (the
            // room's last camera x). The room's pillars left of it would stand cut off over the fire repeated there.
            if (fire_) jafar_lo_ = std::max(jafar_lo_, (int)(int16_t)rd16(s, kCamMaxX));
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
            p.objTrueXSet[i] = (level_ || stage_clear_ || roll_) && ext_ > 0 && oam_set_[i];
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
