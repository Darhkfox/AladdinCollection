# Aladdin (Master System) — widescreen research (E extra px each side)

Module: `src/game/sms_aladdin.cpp`. Game facts (RAM, engines, maps): `docs/re_sms_game.md`.
Scripts (`tools/re/`): `sms_wide_shots.py` (contact sheets per stage and width; `--drive N` moves the free-scrolling
stages' camera on its own with a test patch at `$2BFF`; `Player` plays a stage through for the other scripts),
`sms_wide_check.py` (the extension's background against the hardware's own picture, whole stages),
`sms_slide_check.py` (the view's slide at the level ends), `sms_run_sheet.py` (whole-stage contact sheets),
`sms_rewind.py` (save states / rewind), `sms_popin.py` (where objects first appear, objects the sprite table had no
room for), `sms_map.py` (map model vs VRAM), `sms_app_test.py` (the full app, autotest; `--rewind N`, `--states
sms_end` for states near the auto-run stages' ends). Widths: 6/22/40/74 (CRT pixels) and 26/44/64/102 (square);
`max_ext()` = 102.

**Summary**
1. Background: the 32-column name table holds exactly the 256-px view; every other cell of the camera's band comes
   from the level map (`bg_entry` → `map_entry`), exact where both exist (checked against VRAM in every stage).
   Parallax bands repeat every 256 px and use the table. The old blanked column (x 0-7) is drawn from the map too
   (`hw_x0` = 8). Stage 7's reloaded section graphics, and its city's colours once the night palette is in, come
   from ROM copies at the sides (section 4).
2. The camera is the original's (Aladdin's position is an 8-bit screen X). Near a level's ends the picture slides
   instead (`view_slide`): the view shows the level from its first / to its last pixel and holds still while the
   camera covers the E px next to the end. Only a level narrower than the view is masked (Jafar's room: dimmed).
3. Objects live in screen coordinates: their wake / keep windows are widened (ROM patches) as far as the sliding
   view reaches; sprite pieces beyond the 8-bit table X get their true X from PC hooks on the three sprite writers
   (`sprite_x`); objects the 64-entry table has no room for are drawn as extra sprites by the VDP.
4. Status bar (rows 0-1): the stone counter moves to the right edge (`line_hook`), the gems stay centred; neither
   moves with the slide.
5. Not gameplay (every mode but the ten stage modes 2..$B): pillarboxed, the blanked left column drawn as the
   hardware does. In the stages, lines 176-191 (blank border / a story moment's text box) get colour 0 at the sides.
6. Save states and rewind carry what the hooks keep from one frame to the next (`save_ext`, section 6).
7. The attract demos run on the original ROM and are shown at 4:3: they play exactly as at 4:3 (section 3).

---------------------------------------------------------------------------------------------------

## 1. Background

* Camera: `$C482`. The VBlank writes reg 8 = -`$C4A7` (the first band) and the line IRQs the other bands;
  `on_vblank` keeps `$C482` (and the frame counter `$C20E`) as they are when the registers are written
  (module_state[4]), `on_frame_start` makes it the frame's camera (module_state[5]): the RAM value changes in the
  middle of the frame (the game's logic runs during the display).
* Bands: the line interrupts (`$C499`-`$C49C`; the counter reloads, so the scroll a handler writes shows from line
  2·`$C499` + 3, then + `$C49A` + 1, + `$C49B` + 1) split stage 0 (the camera's band above, the floor `$C491` /
  `$C492` from lines 161 / 169) and stage 1 (sky and city `$C495`, `$C494`, `$C493` from the top, the camera's band
  from line 129). A cell of the camera's band at unwrapped column `vcol` is world x = c + hs + 8·vcol, c = the
  camera whose low byte the line's scroll has, next to the frame's (`band_camera`): the IRQ handlers read RAM as
  they run, and the logic for the next frame may have moved it already. Before, such a line counted as a parallax
  band and its sides showed the wrapped table (garbage at the sides of stage 1's lower half). Other bands repeat
  every 256 px in the map, so the hardware table (wrapped) is right there.
* `map_entry` = the streamer's lookup (`$2E19` paged / `$2DEC` flat, metatiles at `$C49D`, slot 2 = bank
  `$3306[stage]`). `sms_map.check` compares it with the name table every frame: 0 mismatches in all stages (whole
  stages 1 and 5 too). Entries are masked to 13 bits (the hardware ignores bits 13-15; the VDP uses 14 and 15 for
  the module's cells, section 4).
* The paged maps end where their page table does (it ends where the first page's data begins: stage 0 15 pages,
  1 17, 5 31, 7 32, 8 20); stages 0, 1 and 8 end before `$C480` allows (stage 0's camera stops at 3584 while `$C480`
  = `$1300`). `level_bounds` takes the map's end, `map_entry` has nothing past it. Before, the right side of stage
  0's last screen showed the data after the table as cells (blocks of sky and awning), and the view did not slide
  there.
* With a fine scroll the screen's last, partial cell (x 248 + (scroll & 7) .. 255) shows the same table column as
  the hidden first one - the column the streamer rewrites as the camera moves, sometimes a frame late (at 4:3 a
  few pixels at the screen's edge; in a wide picture a stale sliver in the middle: a cut tree crown in A Whole New
  World, a gold stripe in the Treasure Room). It comes from the map too (`SmsVdp::hw_right_partial_out`).
* Rows 0-1 (status bar, reg 0 bit 6): blank beyond the 256 px (`name_entry(0, row)`); rows 22+ colour 0 (section 5).
* Check (`sms_wide_check.py 74` / `102`, whole stages played through, background only, every third frame: each
  extension pixel against the colours the hardware region showed at that world position with the same palette):
  see section 7. What differs is animated tiles whose patterns follow the camera (stage 0's far building and brick
  wall, `$30F6`; stage 1's railings and roof faces, tiles 257-272): the side shows them as they are now, right for
  the current frame.

## 2. Camera and level edges: the sliding view

The camera is never moved: Aladdin's X (`$C402`) is an 8-bit screen position kept in `$10`..`$E8`, so a camera kept
E px inside the level would make its first / last E px unreachable. Instead the picture slides (`view_slide`, from
the camera and the level's bounds alone): the view's left edge (world x) is camera - E, clamped to [lo, hi + 256 -
(256 + 2E)] (lo = `$C4B9`; hi = `$C480`, or less where the map ends first: section 1), and the picture is drawn
s = left - (camera - E) px further right: `SmsVdp::sprite_shift` for the sprites, per line `bg_shift` through the
`line_shift` callback (asked as each line is drawn, when the line interrupts have set its scroll). At a stage's
start the view holds still on the level's first 256 + 2E px while the camera covers its first E px (s = E .. 0); at
the end on the last 256 + 2E px (s = 0 .. -E). A level narrower than the view is centred and the rest masked
(`post_frame`); Jafar's room keeps the original camera (below). Unshifted: the status bar (lines 0-15) and lines
176-191.

Parallax bands hold still with the picture: a band line's shift is what that band would have moved while the
camera covered the slide, less what the logic has moved it already (the IRQs read live RAM). Stage 0 (camera 1 px a
frame): the floor bands move 2 and 3 px per camera px. Stage 1 (2 px a frame): `$C493` 1 px every frame, `$C494` on
even frames, `$C495` every fourth (`$2CC7`), counted from the frame counter at the camera's last move
(`band_steps`). The camera's own band: s less the band camera's lead over the frame's. In stages 0 and 1 the
logic's lead is taken out without a slide too: otherwise the bands would jump by it where a hold ends (and they
keep step with the camera's band and the sprites, which the original's floor and rooftops do not quite).

Boss arena — Jafar (stage 9): the original camera moves 0..136 over a 384-px room, all of it within Aladdin's
reach; past the room's ends there is no level. There the extension carries the room's pillars on (its middle part,
x 128..255, repeats every 128 px: pillars at 148, 276, …; the hourglass at the right end is not repeated) and
`post_frame` reports those columns as `dim_left` / `dim_right` instead of masks: shown dimmed, as the Castle of
Illusion port shows its arenas.

## 3. Objects and sprites

ROM patches (`wide_patches(E)`, code caves in bank 1's unused zero fill, `$7A00`-`$7B63`):

| at | original | patched | purpose |
|---|---|---|---|
| `$3E50` | `ld hl,($C482)` | `call $7A00` | a waiting object (stages 0 1 5 7 8) that moves with the camera wakes K px earlier |
| `$4151` | `call $41B3` | `call $7A40` | ... and is loaded K px further right (`$410D`): same place in the level |
| `$3EC5` | `ld de,$0020` | `call $7A90` | it leaves (next record takes the slot) at x < -32 - E, up to E px further at the end |
| `$3E25` / `$3E29` | `ld de,$0040` / `$0170` | `call $7B10` / `$7B30` | stages 2 3 4 6 9: awake while x in [-64 - 2E, 304 + 2E) |
| `$3D86` / `$3D6B` | `ld (ix+$1D),0; ret` | `jp $7AD0` / `$7AF0` | sprite table full: skip the draw (see below) |
| `$4AAEB` (bank `$12` `$AAEB`) | `ld a,h; or a; ret z` | `call $7B50` | rolling boulders come home off the view (below) |

The caves of the first five rows first test `$C104` (a demo playing: the original's behaviour), a second line of
defence: while a demo plays the ROM holds none of these patches (below).

* K = E, except that at a stage's start the view reaches the level's first 256 + 2E px: an object whose X is
  within lo + 2E is awake from the start, placed X - lo px further right. K depends only on the object, so its
  place and its wake agree whenever it loads or wakes; objects keep their world position and run their code up to
  2E px earlier (as in the Castle of Illusion port). Objects defined in screen space (`+$20` = 0: stage 7's flyers
  enter at x = -16 / -31, stage 1/8 screen objects) are left alone: they appear at their screen position, which can
  be inside the left side (a known limitation).
* The leave distance grows by the end slide: 32 + E + clamp(camera - (hi - E), 0, E), hi as `level_bounds` has it
  (a PC hook at `$7AA8` puts it in de, so stages 0, 1 and 8 use their map's end).
* A width change in the middle of a stage moves the waiting objects of stages 0 1 5 7 8 by the difference of K
  (`configure`, module_state[7] = the width they were loaded for).
* The original's allocators (`$3D74`, `$3D5E`) put an object that does not fit in the 64 entries to sleep, but the
  draw call after them still writes its pieces beyond `$C3BF` — over the palette buffer `$C3C0` (seen as a magenta
  palette in stage 8) — and a sleeping object of the queue stages stops following the camera that frame. With more
  objects awake this is common (stage 5 at E=102: ~150 object-frames in 1500). The patched path keeps the object
  awake and skips the draw; a PC hook on the cave marks it (module_state[6]) and at the VBlank the module computes
  its pieces (frame data in bank `$12`) into `SmsVdp::extra` — drawn after the table's 64, behind them.
* True X: PC hooks before the "off screen" test of each writer — Aladdin `$23C2`, the second figure `$3C63`
  (hl = the piece's 16-bit X, de = its table byte), objects `$0C99` (the table pointer on the stack) — record the
  piece's 256-px page (2 bits per entry) and, inside (-8 - 2E, 256 + 2E) (as far as the sliding view reaches),
  clear H so the game stores the low byte. Pieces the original parks at X = 0 under the blanked column are marked
  hidden, and so are the pieces of an object parked at x = -64 (the keep window's left end) that does not move with
  the camera: Jafar's room keeps two there (types $0B, $44), never seen in the original. The VBlank upload hook
  `$02D9` turns the pages into the displayed order (reversed on odd frames: `$C20E` bit 0) in module_state[0..3];
  `sprite_x` adds ±256 or hides; the VDP moves every sprite by the slide. Off the stages the hooks change nothing.
* The Caverns' and the Treasure Room's rolling boulders (type `$0A`, bank `$12` `$AADE`) roll left from their home,
  fall, and come back home once it is off the screen (hl = home - camera not within x 0..255): just past the
  original's left edge, under its blanked column, which put them out of nothing 70-100 px inside a wide view's left
  side. They come back once it is off the view: not within [-2E - 32, 256 + 2E) (the keep window reaches 64 px
  further, so they still do).
* The attract mode's demos replay recorded input against the original game. Any change to the code they run
  changes their course: the windows above (objects act earlier), the cheats, and also the number of instructions,
  as the game's random numbers take the Z80's R register. At 2:1 square and wider the third demo (The Rooftops)
  lost Aladdin in a pit and waited on TRY AGAIN with the demo's input for ever. While a demo plays (`$C104` = 2..$B)
  the module puts the original ROM back (but for the developer menu's title change; `pre_frame` switches at the
  frame the demo starts or ends - the title's fade lies between the flag and the stage), its hooks change nothing
  (`in_stage` is false) and the demo is shown at 4:3. Should a demo still reach a game over, a PC hook at `$7270`
  sends it to the title as a demo that loses Aladdin goes (`$352C`). `sms_attract.py`: 60,000 frames from power-on
  (12 demos) at E = 0, 6, 22, 40, 74, 26, 44, 64, 102, also with all cheats: the work RAM is identical to the 4:3
  run in every frame.
* Pop-in (`sms_popin.py`, objects whose first awake frame is inside the view, 1500 frames from the stage start):
  stage 5: 5 at E = 0, 40 and 102 — the designed ones (fire bursts out of the lava), at their place; stage 0: none;
  stage 7: 0 / 5 / 4, the screen-space flyers entering at x = -16. Objects drawn as extra sprites in those runs:
  stage 5 0 / 22 / 207 object-frames, stage 7 0 / 0 / 306.

## 4. Stage 7: section graphics and palettes

A Whole New World reloads tiles 259-374 (`$31EE`) when the camera passes `$400`, `$B00`, `$1400`, `$1B00` (table
`$32A0`; lists in bank `$1A`: source, VRAM address, tile count). Each section's cells use only its own list's
tiles, and no reloaded tile is used within 320 px after a threshold (the view while a list loads). So a side can
show a section whose tiles are not, or no longer, in VRAM — seen as garbage left of the city after the camera
passed `$400`. The module copies each section's tiles from ROM (`SectionTiles`) and returns those cells with
bit 15 set: `SmsVdp::alt_patterns` points at the section's copy (section of world x = the last threshold ≤ x - 256).

Colours: sections 0-3 use the stage's palette (`$B3E4` table: `$B4D8`). In section 4 (`$C6A6` = 4, the night sky)
the stars twinkle in background entries 10-13 (`$3950`, every 4 frames from `$3979`) and the list's end loads the
background palette `$33A7` (`$323D`), while the city's last buildings are still in a wide view's left side: they
turned yellow and magenta for about a second. Cells of the other palette's part of the flight now carry bit 14:
the VDP takes their colours from `SmsVdp::alt_cram` (the module's copy of that palette, from ROM; the sprites' half
as it is).

## 5. Status bar and other screens

* Rows 0-1, fixed (reg 0 bit 6): gems at columns 15-17, the stone counter (stages 2 3 4 6) at 26-29, PAUSE in row
  0. With `hud_anchor` the line hook moves x 192..255 of lines 0-15 to the right edge; the gems and PAUSE stay in
  the middle. There are no status sprites. The slide leaves lines 0-15 alone.
* `FrameInfo.gameplay` = modes 2..$B but the attract demos (shown at 4:3); title, password, game over, the story
  scenes (mode $13), the menus and the ending are pillarboxed. With the core ignoring reg 0 bit 5 in widescreen,
  the line hook draws the blanked column (backdrop colour, no sprites) on those screens.
* In the stages lines 176-191 are blank rows in colour 0 (black; red in Jafar's room), where the lamp's chamber and
  other story moments put a text box: the line hook gives the sides colour 0, so the box stays 256 px wide in the
  middle and the border runs the whole width (sprites crossing it are kept).

## 6. Save states and rewind

module_state (in the core's state) holds the displayed sprite pages, the cameras (with the frame counter), the
undrawn objects and the width the waiting objects were placed for. The rest the hooks keep between frames is in
`save_ext` (version 1): the pages the writers left for the next table upload (the game's logic runs across the
frame's end, so a state taken there has part of the next table written), the extra sprites made at the last VBlank
for the frame being drawn, and the frame counter at stage 1's last camera move. The camera of the frame before
comes from module_state. A reconfigure at the same width inside a stage keeps them; a state without them (older
states, or from another width) starts them empty.

`sms_rewind.py` steps back as the app does (load a state, one frame, compare its picture with the forward run's):
stages 0 1 5 7 8 at E = 74 and 102, 2000 frames each, 1000 states: every picture identical; loading the core's state
alone (`--no-ext`) gives wrong extra sprites for a frame (stage 5 at 102: 17 of 300).

## 7. Verification

| check | how | result |
|---|---|---|
| map model | `sms_map.check` every frame, all stages; whole stages 1 and 5 | 0 mismatching cells |
| extension background | `sms_wide_check.py 74` / `102`, whole stages (`Player`), every 3rd frame | 0 in stages 2 6 8 9; ≤ 0.04 % in 3 4 5 7 (fade-in, lava, torches); 0.2-0.4 % in 0 1 (tiles animated with the camera) |
| slide at the ends | `sms_slide_check.py 22 74 102`, 600 frames (free stages to the far end and back) | no jumps, view never outside the level, bands still while it holds (0.0 %) |
| level ends | `sms_run_sheet.py`, app `--states sms_end` (21:9 CRT and square) | the view holds at the last pixel; no cells past the map |
| rewind | `sms_rewind.py` (tool), app `--rewind 120` (hold R, achievements off) | identical pictures; app: no glitches while rewinding or after |
| attract mode | `sms_attract.py`, 60,000 frames at every width, with and without cheats | RAM identical to 4:3 every frame; 12 demos, never a game over |
| widescreen off | the module at E = 0 against the plain core: attract 20,000 frames, every stage 3,000 | RAM and picture identical |
| object pop-in | `sms_popin.py 0 40 102` | section 3 |
| width changes | E 0 → 102 → 40 in stage 5 | waiting objects wake for the current E |
| screens | `sms_wide_shots.py`, `sms_run_sheet.py` (whole stages 0 1 5 7 at 74 / 102), `sms_app_test.py` (16:9 and 21:9 CRT, 21:9 square) | reviewed: stages, ends, HUD, scenes |

## 8. Known limitations

* Screen-space objects that enter and leave on the left (stage 7's companions, x = -16 / -31) appear and vanish
  at that position, inside the left side of a view wider than 16:10.
* Objects run their code up to 2E px earlier than in the original (as in the Castle of Illusion port). (The attract
  demos don't: they run on the original ROM.)
* Tiles the game animates with the camera (stage 0's far building and brick wall, stage 1's railings) show at the
  sides as they are now; at the start / end holds stage 0's far building keeps changing while the view holds still.
* With the sprite limit on ("Remove Sprite Flicker" off), sprites in the sides count towards the 8 per line.
