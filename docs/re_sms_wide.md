# Aladdin (Master System) — widescreen research (E extra px each side)

Module: `src/game/sms_aladdin.cpp`. Game facts (RAM, engines, maps): `docs/re_sms_game.md`.
Scripts (`tools/re/`): `sms_wide_shots.py` (contact sheets per stage and width; `--drive N` moves the free-scrolling
stages' camera on its own with a test patch at `$2BFF`; `Player` plays a stage through for the other scripts),
`sms_wide_check.py` (the extension's background against the hardware's own picture, whole stages),
`sms_slide_check.py` (the view's slide at the level ends), `sms_run_sheet.py` (whole-stage contact sheets),
`sms_rewind.py` (save states / rewind), `sms_popin.py` (where objects first appear, objects the sprite table had no
room for), `sms_map.py` (map model vs VRAM), `sms_walls.py` (stages 0 and 1's camera-animated walls, frame to frame),
`sms_pursuer.py` (the Market's pursuer at every width), `sms_app_test.py` (the full app, autotest; `--rewind N`, `--states
sms_end` for states near the auto-run stages' ends). Widths: 6/22/40/74 (CRT pixels) and 26/44/64/102 (square);
`max_ext()` = 102.

**Summary**
1. Background: the 32-column name table holds exactly the 256-px view; every other cell of the camera's band comes
   from the level map (`bg_entry` → `map_entry`), exact where both exist (checked against VRAM in every stage).
   Parallax bands repeat every 256 px and use the table. The old blanked column (x 0-7) is drawn from the map too
   (`hw_x0` = 8). Stage 7's reloaded section graphics, and its city's colours once the night palette is in, come
   from ROM copies at the sides (section 4). The tiles stages 0 and 1 animate with the camera (the Market's gap
   walls and brick wall, the Rooftops' roof ends) are given for the walls as the view shows them (section 4a).
2. The camera is the original's (Aladdin's position is an 8-bit screen X). Near a level's ends the picture slides
   instead (`view_slide`): the view shows the level from its first / to its last pixel and holds still while the
   camera covers the E px next to the end. Only a level narrower than the view is masked (Jafar's room: dimmed).
3. Objects live in screen coordinates: they wake, stay awake and leave with the original's margins around the
   view as shown (the slide included, per side, per frame); nothing appears or vanishes inside the view. Sprite
   pieces beyond the 8-bit table X get their true X from PC hooks on the three sprite writers (`sprite_x`);
   objects the 64-entry table has no room for are drawn as extra sprites by the VDP.
4. Status bar (rows 0-1): the stone counter moves to the right edge (`line_hook`), the gems stay centred; neither
   moves with the slide. READY (sprites) stays in the picture's middle too (`ready_sprite`, section 5).
5. The story screens are wide where their picture allows (section 5a): 31 of the 36 story scenes carry their own
   picture on from ROM (characters walking off go on into the sides; those the game brings in from off its screen
   walk in from the wide picture's edge, and those it removes beyond the 4:3 edge walk on out, drawn with the game's
   own walk cycle and timing: "Walks"), the intro and title fill the width with their
   plain background, the ending carries its sprite starfield and sea on. Five scenes, menus, password, game over and
   the attract demos are pillarboxed, the blanked left column drawn as the hardware does. A screen fading out after
   the next mode is set is still drawn as itself. In the stages, lines 176-191 (blank border / a story moment's text
   box) get colour 0 at the sides.
6. Save states and rewind carry what the hooks keep from one frame to the next (`save_ext`, section 6).
7. The attract demos run on the original ROM and are shown at 4:3: they play exactly as at 4:3 (section 3).
8. The objects' extra work in a wide view takes no CPU time (the clock stops for it): no slowdown the original
   doesn't have (section 3, "CPU time").
9. Left behind by the scroll, Aladdin is lost at the view's left edge, not the 4:3 screen's (section 2); a stage's
   fade-out after its end or before TRY AGAIN stays wide (section 5).
10. A visual fix to the original, at every width (4:3 and widescreen off too): the parallax bands' raster splits in
   1-1 and 1-2 land a line late, so each band's first tile row showed its top line with the band above's scroll (the
   Rooftops' sky-orange roof edge floating in the brown city on line 128). Those lines are drawn with their own
   band's scroll, as if the split were on time (section 1, "Late splits"). Only pixels change.

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
* Late splits (a visual fix to the original; every width, 4:3 and widescreen off included). Each band's line
  interrupt is raised at the end of the line above its first tile row; its handler (`$01E9` → `jp ($C91D)`, the k-th
  scroll write = -`$C4A7+k`) cannot write reg 8 before that row's first line has started, so the scroll changes one
  line into the row: stage 1's sky and city at 65 / 81 / 129 for rows from 64 / 80 / 128, stage 0's floor at 161 / 169
  for rows from 160 / 168 (`$C499`-`$C49B`: 2·`$C499` + 3, ...). That first line showed the row's top pixels shifted by
  the gap between the two scrolls: in 1-2 the roofs' sky-orange top row floating in the brown city on line 128 over
  a gap or a lower roof (at 4:3 in 1095 of 2400 frames of a `Player` run), small dashes of the far city and clouds on 64 and 80,
  pot rims and stone fragments on the Market floor's 160 and 168. Stage 9's interrupt at line 79 writes no scroll
  (`$C49A` = `$FF`); no other stage splits. `line_scroll` (asked first as each line is drawn: `SmsVdp::line_scroll`)
  draws such a line with the scroll the coming write brings, read from RAM as the handler will read it: an interrupt
  raised at the end of a line above (`SmsVdp::line_irq_line`), still pending or its handler under way (the Z80's PC in
  `$0038`-`$0044` / `$01E9`-`$0257`), whose handler (`$C91D` as the line after the raise finds it, before any handler
  can have moved the chain on) is one of the three with a scroll write, and no scroll write since. A split the game
  takes a few more lines late (its logic holding interrupts off: 1-2 lines now and then) is drawn on time the same way,
  up to `SPLIT_LATE` = 6 lines; an interrupt lost to a status read outside the handler (`$09A5`, at a mode change) is
  left alone. reg 8 stands in for the corrected scroll only while the VDP draws the line (and its high frame rate
  in-betweens): the registers, the interrupts and all the game reads stay the original's. In a wide picture the line
  counts as its band's (`line_shift`, `bg_entry`: the camera's band from the map at 128). Measured (`Player` bot, 2400
  frames, the module against the build before, 4:3 / 74): RAM, VRAM and CRAM identical every frame; pictures differ
  only on corrected lines (1-1: 2399 frames, lines 160 / 168; 1-2: 2103 frames at 4:3, 64 / 80 / 128, 1095 of them on
  128); every corrected line's scroll is the one the split brought where it landed; afterwards the scroll changes
  exactly on 64 / 80 / 128 and 160 / 168. The attract demos (shown at 4:3) get it too (the Rooftops' demo).
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
* The vertical scroll: only Jafar's turn into the snake uses it, shaking the screen (reg 9 = 222 / 224 for about 8
  s). The VDP gives `bg_entry` the unwrapped row (`(line + vscroll) / 8`, 28+ past the table's end); it takes it
  modulo 28 as the hardware does. Before, every row arrived as 28+ and fell back to the hardware table, which
  repeats every 256 px: a second hourglass, then a second snake, steps and bits of the status bar at the sides.
* Check (`sms_wide_check.py 74` / `102`, whole stages played through, background only, every third frame: each
  extension pixel against the colours the hardware region showed at that world position with the same palette):
  see section 7. What differs is the tiles stages 0 and 1 animate with the camera (section 4a): a wall in a side
  shows a face the original only ever has for it off its screen (a left wall's full face), which the check counts
  as unseen at that world position.

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
(`band_steps`). The camera's own band: s less the band camera's lead over the frame's. A line's band is counted
from the scroll register's writes before it (the VDP counts them per line): the game sometimes takes a line
interrupt a few lines late (its logic holding interrupts off), and the lines between then still show the band above. In stages 0 and 1 the
logic's lead is taken out without a slide too: otherwise the bands would jump by it where a hold ends (and they
keep step with the camera's band and the sprites, which the original's floor and rooftops do not quite).

Left behind (stages 1 and 8, widescreen only, a deliberate change of the game's rules): while the scroll pushes
Aladdin (`$C41C`: standing still against it, or held by a wall) `$34A5` moves his X 2 px a frame left and, once it
would go below 0, counts him lost (`$34BC`: pit flag `$C461`, health 0), then carries him to x -32 (`$34C7`) where
he waits about 220 frames until TRY AGAIN - under the original's blanked column, but 70 px inside the picture at
21:9. His X stays an 8-bit screen position for the game (its tests compare low bytes), so in a wide view it is held
at 0 while the module counts how much further the scroll takes him (`behind`, module_state[6] bits 16-23; the
`$23C2` hook draws him that much further left). He is lost once 8 px past the view's left edge (the picture's edge
for the logic's camera and slide), as the original loses him 8 px past its blanked column's edge: at that moment he
is as far out of the picture at every width as he is out of the 4:3 one, and the carry to -32 and the wait happen
out of sight. Walking right takes the distance back first (`$34A5`'s entry). When nothing pushes him any more the
game puts a player below x 24 at 24 (`$36B8`), a rescue that never reaches past the blanked column: one left behind
further catches up 4 px a frame instead, unless he walks. A stage start or TRY AGAIN clears it (a PC hook on the
auto-scrolling stages' handler `$33B7`; `$C912` is no test for it: the game also sets it around the VDP writes of
most frames, `$0B20`, and a frame can start there - seen on 2 of 24 frames in the Market). (`behind.py`-style
check: stage 1 standing still at 21:9 CRT, lost at x -86 = the picture's edge - 12, as the original at -4 = its
blanked column's edge - 12; TRY AGAIN restarts with nothing left over; walking right from 32 px behind comes back.)

Boss arena — Jafar (stage 9): the original camera moves 0..136 over a 384-px room, all of it within Aladdin's
reach; past the room's ends there is no level. There the extension carries the room's pillars on (its middle part,
x 128..255, repeats every 128 px: pillars at 148, 276, …; the hourglass at the right end is not repeated) and
`post_frame` reports those columns as `dim_left` / `dim_right` instead of masks: shown dimmed, as the Castle of
Illusion port shows its arenas.

## 3. Objects and sprites

Objects wake, stay awake and leave with the original's margins around the view as it is shown - the slide
included, per side, per frame: the caves take their values from the module's PC hooks, computed from the logic's
camera (the one the next frame shows). Objects entering from the right therefore act from as far before their
place in the level comes into view as in the original, which is E + slide px of camera travel earlier than there
(the minimum for them to be there when the view reaches them); nothing else acts earlier.

ROM patches (`wide_patches(E)`, code caves in bank 1's unused zero fill, `$7A00`-`$7B5F`):

| at | original | patched | purpose |
|---|---|---|---|
| `$3E50` | `ld hl,($C482)` | `call $7A00` | the wake test of a waiting object (stages 0 1 5 7 8): `wake_decide` (hook at `$7A0B`) |
| `$3EC5` | `ld de,$0020` | `call $7A90` | it leaves (the next record takes the slot) once x + de < 0: hook at `$7AA3` |
| bank `$12` `$B3D6` | `ld de,$0020` | `call $7A90` | the bird type `$3B` leaving by itself, the same test |
| `$3E25` / `$3E29` | `ld de,$0040` / `$0170` | `call $7B10` / `$7B30` | stages 2 3 4 6 9: awake while x in [-64 - E + s, 304 + E + s) (hook at `$7B23`) |
| `$3D86` / `$3D6B` | `ld (ix+$1D),0; ret` | `jp $7AD0` / `$7AF0` | sprite table full: skip the draw (see below) |
| bank `$12` `$AAEB` | `ld a,h; or a; ret z` | `call $7BA0` | rolling boulders come home: home not within [-64 - E + s, 256) (hooks at `$7BA4`, `$7BA8`) |
| bank `$12` `$AC23` | `ld a,h; or a; ret z` | `call $7B50` | the Treasure Room's bats come home off the view and the keep window (hook at `$7B54`) |
| bank `$12` `$A77A`, `$A862`, `$AA53`, `$AA7D`, `$AB12` | `ld a,(ix+$1D)` | `call $7B70` | the fixed hazards' awake test on the original's keep window (hook at `$7B73`, below) |
| `$3D13`, `$425B`, bank `$12` `$B25A` | `ld b,$07` | `ld b,$0C` | the queue stages keep 12 objects (below) |
| bank `$0C` `$AE9A`, `$B5C4` | `ld b,$07` | `ld b,$0C` | the other loops over them: stage 8's flying figure drops what waits for it; the Market's pursuer runs into what is on the screen |

The caves first test `$C104` (a demo playing: the original's behaviour), a second line of defence: while a demo
plays the ROM holds none of these patches (below).

* Waking (queue stages, `wake_decide`): the record's X (+`$28`) is the camera at which the original wakes it, at
  its preset screen X (+`$02`, mostly 256). One that moves with the camera and waits at or beyond the right edge
  (screen X ≥ 232) wakes once the view's right edge (256 + E + s) reaches it: when camera + E + s ≥ X, placed X -
  camera px further right (its place in the level; one that comes late, as the original's sometimes do, at its own
  X). At a stage's start (s = E) that is everything within lo + 2E, awake from the first frame where the view
  shows it. One inside the original screen (stage 5's bursts of fire, stage 0's X 192) wakes when the original
  wakes it, where it does. Objects are placed as they wake, not when loaded, so width changes need nothing.
* A Whole New World's birds that fly in from the left (screen-space, `+$20` = 0: type `$46` at x = -16, 1 px every
  4 frames; type `$3B` at -32, 1 px a frame) start as far beyond the view's left edge as the original's beyond its
  own: E - s further out, and as much earlier as they take to fly that far (the carpet's camera moves 2 px a frame:
  8 or 2 camera px per px). From then on they are where the original has them. One that the list loads too late
  for that starts at the view's edge all the same and flies 1 px a frame more (PC hooks on their handlers, bank
  `$12` `$B1FC` / `$B374`) until it is where the original's is by then (started at -16 / -32 when the camera reached
  its X, flying 1 px every 4 frames / every frame), on its path from then on. (Before: 3 px a frame more, towards
  the original's start: it dashed across the side and stayed a few px behind.) The `$3B` leaves by itself once 32 px past the
  screen's left edge (it drops off the bottom): past the view's, as everything else.
* The Treasure Room's bats (type `$0D`, bank `$12` `$AC16`) come home (x -2, y 0: the original screen's top left
  corner) by the boulders' old test (`$7B50`): off the view and past the keep window's left edge.
  Before, one that had flown off swooped out of the wide view's left side, 66 px inside it at 21:9.
* Leaving: 32 px past the view's left edge (de = 32 + E - s) - or, for an object that shows nowhere (all its
  pieces above or below the screen: flown off the top, fallen off the bottom), where the original lets it go (de =
  32), so its slot frees as in the original.
* The queue's slots: the original keeps 7 objects of the list, loading the next record into the slot of one that
  leaves. A wide view shows more at once, and with 7 the list could not keep up: at 21:9 in A Whole New World
  objects woke late, up to 100 px inside the view. With 12 (`$CA00`-`$CCFF`, the free-scrolling stages' object RAM,
  unused in these stages) it keeps up. Two more loops of the game go over these objects with `ld b,7` (bank `$0C`): stage 8's flying figure
  drops the pots that wait for it (type `$51`, `$AE96`: `+$30`, placed under it at x 192), and the Market's pursuer
  runs into what is on the screen (`$B5C0`). They take 12 too: past the seventh slot a pot was never dropped and
  waited at x 256, awake, for good (in the right side, holding a slot: stage 8 lost 5 of its 57 records). A width
  change in the middle of a stage to or from 4:3 moves the objects
  between 12 and 7 slots (`resize_queue`): more slots take the next records at once; fewer keep those awake, then
  those waiting by their X, and send the rest back to the list (the record index `$C680` steps back: the lists are
  in order of X, near enough). States of an earlier version (module_state[7] a width: their waiting objects were
  placed when loaded) are converted on load (`unshift_old_state`).
* CPU time: a wide view keeps more objects awake, and the queue stages run 12 slots; the objects' loop no longer
  fit the frame and the auto-scrolling stages lagged (the logic missed frames and the scroll stalled every 4-9
  frames: the Rooftops took 18 % longer at 21:9). A pass of the loop (a slot: queue stages `$3D15`..`$3D39`, the
  others `$3CBC`..`$3CE4`) that the original would not have made takes no time: an awake object outside the
  original's window (queue stages: x beyond 256, where it would still wait, or before -32, where it would have left;
  the others outside [-64, 304), `$3E1F`) and, in the queue stages, a waiting or empty slot past the seventh. The
  pass runs with the clock stopped: at `$3D15` / `$3CBC` the module gives the Z80 a credit of 32768 cycles
  (`wait_cycles` negative: the core's `run` goes on without returning, so no line is drawn and no interrupt comes),
  and at `$3D39` / `$3CE4` takes back what the pass left of it. (Giving the time back after the pass instead left
  the lines drawn meanwhile behind: the logic still met the VBlank in the middle of a pass.) The caves' extra
  instructions are taken off too (the wake test's 59 cycles, the leave and keep tests' 87). What the original does
  keeps its cost, and its own slowdowns. The slot being timed is in module_state[7] (bits 0-22, beside the
  placement flag). Measured (`r2_lag.py`, Player bot, 3000 frames, frames without a logic step at 4:3 / 22 / 74 /
  102): Market 13 / 14 / 10 / 18 (before 175 / 246 / 279 at 22 / 74 / 102), Rooftops 57 / 84 / 84 / 69 (before 337
  / 433 / 445; the stage takes 2146 frames at 21:9 against 2119, before 2495), Escape 7 / 11 / 5 / 3 (before 26 /
  36 / 38), A Whole New World 5 / 27 / 15 / 19 (before 250 / 375 / 445), Jafar's Palace 3 / 10 / 7 / 6 (before 193
  / 217 / 229), the Sultan's Palace 48 / 40 / 38 / 40 (before 81 / 108 / 108). What lags more than at 4:3 is the
  objects inside the original's window that the original had not loaded yet (its seven slots full).
* The original's allocators (`$3D74`, `$3D5E`) put an object that does not fit in the 64 entries to sleep, but the
  draw call after them still writes its pieces beyond `$C3BF` — over the palette buffer `$C3C0` (seen as a magenta
  palette in stage 8) — and a sleeping object of the queue stages stops following the camera that frame. With more
  objects awake this is common. The patched path keeps the object awake and skips the draw; a PC hook on the cave
  marks it (module_state[6]) and at the VBlank the module computes its pieces (frame data in bank `$12`) into
  `SmsVdp::extra` — drawn after the table's 64, behind them.
* True X: PC hooks before the "off screen" test of each writer — Aladdin `$23C2`, the second figure `$3C63`
  (hl = the piece's 16-bit X, de = its table byte), objects `$0C99` (the table pointer on the stack) — record the
  piece's 256-px page (2 bits per entry) and, inside (-8 - 2E, 256 + 2E) (as far as the sliding view reaches),
  clear H so the game stores the low byte. Pieces the original parks at X = 0 under the blanked column are marked
  hidden, and so are, in Jafar's room, the pieces of an object parked at x = -64 (the keep window's left end) that
  does not move with the camera: it keeps two there (types $0B, $44), never seen in the original. (Elsewhere an
  object at -64 is one passing by: hidden for that frame, it blinked.) The VBlank upload hook
  `$02D9` turns the pages into the displayed order (reversed on odd frames: `$C20E` bit 0) in module_state[0..3];
  `sprite_x` adds ±256 or hides; the VDP moves every sprite by the slide. Off the stages the hooks change nothing.
* **Fixed hazards on the original's window.** The free-scrolling stages' hazards that stay where they are put tie
  their logic to the awake flag (+`$1D`, set by the keep test `$3E1F`): the Caverns' ceiling boulders and what
  drops from the Sultan's Palace's ceiling (types `$09` / `$1E`, handler `$A9D9`: fall once their x is in [64, 160); landed, they lie there while
  awake and go back to the ceiling - re-armed - once asleep, `$AA53`), the rolling boulders (`$0A`, `$AA7D`: roll
  left from home while awake; asleep, or after their fall, they come home), the geysers (`$0C`, `$A77A`: erupt every
  few seconds while awake), the spikes (`$12`, `$A862`: rise and retract as Aladdin nears; asleep, back to their
  first phase) and the crumbling ledges (`$0E`, `$AB12`: asleep, back home). With the wide keep window [-64 - E + s,
  304 + E + s) they acted E + s px of camera travel early (a geyser's cycle, a boulder's roll, out of step with the
  4:3 game's by the time Aladdin got there) and re-armed only as much further away: a ceiling boulder that had
  fallen stayed on the floor when Aladdin came back, where the 4:3 game had put it back at the ceiling to fall
  again (the user's state in the Caverns, sub-area 2: walk left to the wall, back right - at 4:3 the boulder at
  world x 1216 is asleep at x 304 and falls again at x 150; at 21:9 it stayed awake, on the floor, and never fell).
  Their `ld a,(ix+$1D)` (each followed by `or a`) now calls `$7B70` (`ld a,(ix+$1D); ret`), and a hook at its `ret`
  (`awake43`) clears a outside [-64, 304), the original's window: they act, re-arm and come home at the moment the
  4:3 game has them do it, relative to Aladdin, at every width. They stay awake in the wide window for the drawing
  and the touch test, so they show in the sides as they are (a geyser waiting there shows its splash, a boulder
  hanging at the ceiling); one the 4:3 game re-arms or brings home just past its own screen does so in a side (a
  landed boulder back at the ceiling at x 304, at the right side's far end at 21:9). The queue stages' objects wake
  and leave by their own rules (stage 8's ceiling drops, type `$1E`): unchanged there. The extra `call` / `ret`
  cycles are taken off (`slot_begin`'s clock).
* The Caverns' and the Treasure Room's rolling boulders (type `$0A`, bank `$12` `$AADE`) come back home once it is off
  the screen (hl = home - camera not within x 0..255) and roll again from there. On the right this is the 4:3 game's
  test (home x 256 or more: a boulder that rolls again and again from a home just past the 4:3 edge does so at every
  width, coming home in the right side); on the left a home under the original's blanked column put the boulder out
  of nothing 70-100 px inside a wide view's left side, rolling away left out of play: there it comes home only past
  the wide keep window's edge, home not within [-64 - E + s, 256). (Before, home had to be off the view on the right
  too: a boulder whose home was in the right side never came back.)
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
* Measured (`sms_wakes.py 74 102`, every stage played for 4200 frames): objects that wake inside the view (more
  than 8 px in) are only those present from a stage's first frame and those the original also wakes inside its
  screen (stage 1's one 16 px inside the right edge, stage 3/4's three, stage 5's bursts of fire, stage 8's two):
  none appears or vanishes inside the view otherwise. A Whole New World's wakes lie at the same distances from the
  view's edges at 21:9 as at 4:3. Camera travel by which a record wakes earlier than at 4:3: E (± the camera's step)
  for those entering from the right, 0 for those inside the original screen; the birds from the left by their
  lead (up to 8 (E - s)).
* The Market's pursuer (stage 0's second figure, bank `$0C` `$A978`; his X `$C689` is a screen X): unless stunned he
  runs 1 px a frame toward screen x 40, or 88 once the camera has passed `$B6C6`[stuns] (960, 1456, 2176, 3040)
  (`$B66F`); 40-48 px from an object on the screen he jumps it, but type 8 (the rolling barrels) stuns him for 120
  frames (`$B5AF`: `$C69D`, count `$C69E`), during which he slides back 2 px a frame (camera + 1) to about x -196,
  and then runs back in: about 360 frames in all. Nothing of the wide view's own touches him (the left-behind rule is
  stages 1 and 8's; his loop over the objects, `$B5C0`, takes 12 slots but only objects on the original screen; the
  stopped clock leaves his frames alone). Measured (`sms_pursuer.py`, invincible, whole stage, 4:3 / 16:9 / 21:9 CRT /
  21:9 square): Player bot 3612 / 3614 / 3609 / 3617 frames, 4 stuns and 22 jumps at every width, distance to
  Aladdin while he chases 247 / 249 / 250 / 251 px, back at his place about 357 frames after each stun (4:3: the same;
  its first stun falls as he sets off for x 88); at the same camera his X is the same but for the stuns: the barrels
  wake E px of camera travel earlier (above) and meet him earlier - stuns at camera 964 / 1460 / 2179 / 2976 at 4:3,
  920 / 1416 / 2136 / 2932 at 21:9 CRT. Running back in he shows in the left side, 145 frames a stun at 21:9 CRT (186
  at square), where the 4:3 screen has lost him (33 frames under its blanked column). Without cheats, standing still,
  he catches Aladdin at frame 457 / 444 / 412 / 397 (a barrel knocking Aladdin back into him comes earlier); walking
  left, at frame 28 at every width.

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

## 4a. Stages 0 and 1: tiles animated with the camera

The Market's sky gaps (world x 64..191 of every 512 px but one) have side walls drawn in perspective: the left wall
(B, 16 px at x 64 mod 512, tiles 265-272 h-flipped) shows its face while it is left of the screen's middle (x below
~100), the right wall (A, x 176 mod 512, tiles 257-264) while it is right of it; between them a brick wall (tile 273)
moves slower than the camera. The Rooftops' roofs end in faces the same way at its five gaps (B 265-272 at x 96 mod
512 - the first gap, 1088, at 64 -, A 257-264 32 px further). The tiles are rewritten for the camera's phase:

| | phase | tables (entry: source w, VRAM address w, tile count b) | data |
|---|---|---|---|
| stage 0 (`$30F6`) | (camera - 40) & `$1FC`, 128 steps of 4 px | A: bank `$0A` `$8000`; B: bank 2 `$A324`; bricks: `$0A` `$8400` (8 steps, a 32-px cycle) | bank `$3311`[0] = `$0A` |
| stage 1 (`$3159`) | (camera - 40) & `$1F8`, 64 steps of 8 px | A: bank `$0A` `$9850`; B: `$0A` `$9A50` | bank `$3311`[1] = `$0A` |

Most entries write a part of the 8 tiles (stage 0's walls 5, 2 or 1 tiles, the middle ones cycling with the bricks);
the VBlank job (`$32DE` / `$32EA`, every fourth frame, `$C20E` & 3) copies them, up to 3 frames behind the camera. So
the tiles show the one wall of each kind near the original screen as it looks at its place there, and where the
original never shows one they hold whatever suited the tables. In a wide view:

* a right wall coming into the right side showed no face (the tables' look for it far left of the screen) until 8 px
  from the 4:3 picture, then all of it at once - a face popping up 66 px inside the 21:9 picture, at every gap;
* a left wall just past the left edge (x -20..-8, under the original's blanked column) showed scraps of other tiles
  for ~8 frames, a magenta and yellow strip (the round-2 "scrambled pillars");
* a left wall in the right side had bricks out of step with the brick wall beside it (a fixed image there);
* a stage 1 roof end far in a side took another gap's look (beyond the tables' reach: the far right at 21:9 square);
* while the view holds still at the stage's start and end (the slide) the walls kept changing and the bricks kept
  moving with the camera.

The module keeps the tiles' state for every phase (`CamTiles`, from ROM: two cycles of the job's uploads in order;
the second leaves each phase's state) and the walls (the map's columns using each kind's tiles). Each frame
(`anim_tiles`, at the frame's start) it gives the VDP the 17 tiles (`SmsVdp::bg_alt`: cells of those patterns, the
hardware table's too, take their rows from the module) as for the wall of each kind nearest the view (they lie 512
px or more apart, so at most one is in view):

* camera: the view's (camera + slide). Without a slide it is the one whose tiles are in VRAM - the bricks' tile tells
  their step, and the camera is taken back to it - and a wall where the tables are right keeps VRAM's tiles: the 4:3
  part is the original's;
* a wall wholly off the original screen (x ≥ 256, or x + width ≤ 8) whose place the tables don't have right
  (`kAnimGroups`: stage 0 right walls beyond 263, left walls outside -148..-20; stage 1 roof ends outside -160..343 /
  -128..375) looks as at the window's end (stage 0: right walls 232..263, all face; left walls -52..-21, all face, or
  in the right side 197..228, no face and bricks cycling) - in stage 0 a whole number of 32-px brick steps away, so
  the bricks in it keep step with the brick wall's.

So a right wall shows its face as it comes into the right side, a left wall its full face all through the left
side, the bricks next to a wall line up, and at a stage's start and end walls and bricks hold still with the view
(their look then is the view's: a wall left of the wide picture's middle shows its face). Measured (`sms_walls.py`:
each wall's 16 columns, lines 16-151, at its place in the level, frame to frame; a change of more than 600 px is a
sudden one; the tables' own steps reach 989): whole Market, Player bot, before / after: E = 22: 61 / 39 sudden
changes, 18 / 0 with the wall in a side; 74: 67 / 35, 30 / 0 (largest 1706 px: the face popping up at x 263);
102: 67 / 35, 30 / 0. The 35 left are the original's own steps inside the 4:3 picture (43 at 4:3, the stage's start
included). The Rooftops: no sudden changes before or after at 22 / 74 / 102 (its faces' changes are small; the
module only steps in beyond the tables' reach).

## 5. Status bar and other screens

* Rows 0-1, fixed (reg 0 bit 6): gems at columns 15-17, the stone counter (stages 2 3 4 6) at 26-29, PAUSE in row
  0. With `hud_anchor` the line hook moves x 192..255 of lines 0-15 to the right edge; the gems and PAUSE stay in
  the middle. There are no status sprites. The slide leaves lines 0-15 alone.
* Screen-fixed texts in the stages: PAUSE (row 0, above), READY, and the story moments' text box (lines 176-191,
  below); TRY AGAIN? / GAME OVER is a screen of its own (mode $11, pillarboxed, centred). READY is sprites: a
  stage's setup (`$3486`, called by every engine's setup - not the Lamp's, not in the demos - at the stage's start
  and at TRY AGAIN) fills table slots 57-61 with Y $40 and the X / tile pairs at `$3834` (x 108-147, centred on the
  256 px) and the slots keep them for about 70 frames, until the objects' writers take them. It is shown at a
  stage's start, where the view has slid by E, and moved with the slide like every sprite: at 21:9 it sat at x
  108-147 of the wide picture, E px left of its middle (74 / 102 px at 21:9 CRT / square, with or without
  `hud_anchor`; away from a level's ends, where there is no slide, it was centred). `sprite_x` now tells it by what
  the VDP's table holds (`ready_sprite`: slot, Y, X and tile as `$3834` has them) and gives it the slide back,
  ignoring the page a piece left in those slots: centred at every width. Measured (tool, stages 0 2 5 restarted, and
  the app at 21:9 CRT and square, `hud_anchor` on and off): READY's middle at the picture's (202.5 / 202 at 74, 230.5 /
  230 at 102; 128.5 / 128 at 4:3); PAUSE and the stone counter unchanged; TRY AGAIN? centred in its pillarbox.
* `FrameInfo.gameplay` = modes 2..$B but the attract demos (shown at 4:3), and a stage fading out; also the widened
  story screens (section 5a). Password, game over, the menus, the scenes kept 4:3 and the demos are pillarboxed. With
  the core ignoring reg 0 bit 5 in widescreen, the line hook draws the blanked column (backdrop colour, no sprites)
  on those screens.
* A stage's fade-out: the game sets the next mode (and at an exit the next stage number, `$35B8`, `$1504`) before
  it fades the stage out (`ld ($C200),a; call $0913`), so the picture dropped to 4:3 bars for about 0.8 s at full
  brightness and then faded. A hook at `$0913` tells the caller by its return address (the exits `$1523`, `$167B`,
  `$35D7`; TRY AGAIN `$1503`, `$1652`, `$1C44`, `$20DC`, `$34F7`, `$36A9`, `$37BF`, `$394B`; Jafar's end `$2794`)
  and marks the stage as still shown (module_state[6] bit 28, the stage shown in bits 24-27: at an exit the number
  less one) until `$0913` returns, with the screen black. Meanwhile the module draws that stage (map, slide, bands,
  masks) and reports gameplay. Measured (Player bot, every auto-scrolling stage, the Treasure Room into the Lamp, a
  death before TRY AGAIN, at 21:9 CRT): no frame after a stage's end shows its picture without being reported
  gameplay; black 46 frames after the exit.
* In the stages lines 176-191 are blank rows in colour 0 (black; red in Jafar's room), where the lamp's chamber and
  other story moments put a text box: the line hook gives the sides colour 0, so the box stays 256 px wide in the
  middle and the border runs the whole width (sprites crossing it are kept).

## 5a. Story screens: the scenes, the intro and title, the ending

Every mode but the ten stage modes used to be pillarboxed (the app fills the bars with a plain edge's colour). The
story screens are now drawn wide where their picture allows it (`story_screen`, `story_wide`; the module's section
"story screens").

* **Story scenes** (mode $13, scene `$C11F` = 0..$23, scripts `$5F42`; scenes 2-8, $A-$B, $C-$F, $12-$18, $19-$1C,
  $1D-$1F and $20-$23 run on into each other, `$6697`). Each is a 256x160 picture of its own: `$4E92` builds 16x10
  metatiles into `$D000` and copies it to table rows 0-19 (bank `$1C` `$8000`: a word per scene, metatile bank << 8 |
  map bank; the map pointers at (`$C48F`) = `$8048` + 2 * scene in the map bank, rows 16 bytes apart; the metatiles at
  (`$C49D`), from the scene's table `$5A91` via `$11E4`). Only a few match a stage's map in part (scene $A is stage
  4's first screen, $6 is stage 6's at x 144), none with room to go on at both ends. The scroll is 0 but for a shake
  of 2 px (`$5205`: scenes 8 $A $C $1C horizontally, $B $D vertically) and scene $20's band (Jafar's snake, rows
  14-16, a line interrupt at reg 10 = `$47`). Characters are sprites: Aladdin's writer (`$2379`, entries 0-16), the
  second figure's (`$3C13`, 20-36) and the scene's list (`$5099`, entries from `$BE94`, X at `$CF37` + 4k + 2); some
  are drawn into the table (the framed pictures, the Genie in $E/$F/$16/$17/$1C/$22/$23, Jafar's snake in $20).
  Rows 20-23 are the text box (portrait at column 2, the blank entry `$11BF` everywhere else: `$05C7` fills the
  table with it first). Both buttons skip a scene (`$66B7`).
  The sides repeat part of the scene's own map, from ROM (`kSceneRules`: per band of cell rows, a range of source
  columns for each side, `scene_cell`), so what the game draws into the table is never repeated; the text box's rows
  are `$11BF` at the sides, the hidden column 0 shows the table's own (or, where a rule says so, the left side's).
  Rules: the whole picture (its edges meet: the
  stalls, rooftops, hills, pillars and towers of 0-7, 9, $E, $F, $19, $1B, $1D, $20-$23; in $1B the lamp's
  table, rows 14-19 at columns 10-17, isn't repeated: the sides show columns 22-29 there, the same frame on the wall
  and the frieze in step - the right side reached the table at 21:9 square, `sub0`/`sub1`/`sub`); $A without the claw
  at its left edge: columns 8-15 on the left and 4-31 on the right above row 15; below, the rock floor (rows 16-17 the
  ledge, 18-19 the slab, columns 2-25, the slab's rounded end at 25; small rocks on row 15) is cut at the claw's foot
  (column 1 dark, column 0 a black block `$1100` in the table) and ends at the right in the lava (columns 26-29) with a
  rock at the edge. The claw is drawn in outline (colours 10-12) over the cave's dark, colour 0: its cells at columns
  1-3, rows 10-17 (`$14E`-`$164`), the foot's outline in rows 16-17 (`$15F`, `$160`/`$161`, `$163`/`$164`, the last two
  holding the ledge's tip). Under the gem, column 1's rows 17-19 are tile `$100`, colour 0 throughout: the dark there is
  the background, not a part of the claw. The same claw stands in stage 4's first screen (the Lamp, the level's left
  end: the same tiles in the same columns, `sms_map.entry`), and there the slab begins under it with its rounded end
  (`132b` / `132c` at column 2); no other scene, stage or the title draws these tiles ($B keeps them in VRAM, unused). Widened, the floor runs on
  unbroken out of the left edge, behind the claw (`kSceneFills`, widescreen only): column 1's slab (rows 18-19) from
  column 17 (1105 / 1109, before the slab's 1106 / 110a at column 2 in its two- and four-column steps), and rows 16-17
  of columns 1-3 from columns 7-9 (143 143 144 / 128 128 125, before the 140 / 125 at column 4) - behind the claw: a
  cell made each frame of the claw cell's pixels where they aren't colour 0 and the floor cell's elsewhere (`StoryFills`
  in `alt_patterns`, bit 15; the claw cell's priority), so every pixel of the claw stays. The VDP asks `bg_entry` only
  for the columns left of `hw_x0`: for a scene with fills the frame's start moves it to x 34 (column 3 at the shake's
  2 px to the right), and `story_bg_entry` returns the fill there or -1 (the table). Abu stays as he is. The hidden
  column shows column 16 and the side 15 down to 5, repeating every 12 (`side0`; the slab's steps in phase; row 15's
  small rocks from 4-15, whole); the right side is the lava (26-29). (Before: a version filled row 17 of columns 2-3
  with plain ledge cells - the claw's foot outline was lost there, the user's "part of the statue is missing"; before
  that the side's slab ended in a rounded end in the hidden column, with the dark column 1 between it and the slab -
  the user wanted it filled; before that the left side was the cave's dark below the stalactites - the floor stopped
  at the claw, the user's "add a floor to the left side of the statue"; before that, rows 16-19
  took the water on the left and the floor again on the right: the user's "left/right borders issues", QA state
  `ready/r.state`.) $B (the escape: the lava's drips) has its sides laid out from its own pieces (`kSceneSides`): its
  drips - arcs of two rows, six cells (116d 116e 116f 136f 136e 136d over 1171 1172 1173 1173 1372 1371) or four
  (1165-1168 over 1169 1124 1124 1124) - hang in a cluster at the picture's left (columns 0-11, rows 0-11), cut by its
  edge, and sparsely at the top of its right part; no stretch of its columns carries either edge on (the whole picture
  repeated put the cluster again in the right side next to the empty right part, and the empty part in the left side:
  the user's "the save state two cut scene doesnt look right", a state in $B). The left side completes the arcs the edge
  cuts (a four-arc ending at column 0 in rows 3-4, a six-arc ending at column 2 in rows 5-6, a six-arc ending at
  column 0 in rows 9-10, a four-arc's top ending at column 1 in row 11) and thins the cluster out with whole arcs
  further left; the right side has two arcs near the top, as the picture's right part. Each arc whole, with the
  picture's priority bit; rows 12-19 blank; the hidden column the table's. No other scene uses $B's map ($A shares its
  metatiles); $C / $D (the fall) stay 4:3. $10 / $18 / $1A pillars on the left (0-8,
  not the throne), the room on the right (0-27); $11 / $13 and $15 / $17 stars on the left (0-12), the balcony / cliff
  carried on on the right (24-31 or 28-31); 4's hidden column, rows 18-19, the left side's (its cells there are bare
  rock without the priority bit, never shown on the hardware: a dark block under the ridge that runs into it from the
  left side; now the ridge goes on to the hill at column 1, and so do the copies of column 0 in the sides); $12 and $1E sky, railing or floor (0-15); $1C plain sky and the balcony
  (28-31); $1F the floor around the hourglass (0-7 / 24-31). Kept 4:3, with black bars: 8 (a rock cut by the right
  edge), $C and $D (rock walls cut by both edges), $14 (a roof's slope cut by the left edge), $16 (the Genie's cave,
  lit from the middle, black rock at the right edge). Chosen with an offline model of the rules for every scene, then
  checked in the module at 74 and 102 and in the app.
  The shake (`$5205`: 8, $A, $C, $1C) moves the whole picture 2 px with reg 8 = 2 or 254, one frame in four. The VDP
  gives `bg_entry` the column from the scroll as 0..255 (x - 254 for a move to the left), so on the frames shaken left
  the sides came out 32 columns off: the right side was the table's own columns 0-9 (in $A the claw, Abu's ledge and
  the floor's start again, with a dark gap; in $1C sky where the balcony goes on) and the left side the rule's columns
  counted 32 further - the right side flashing a copy of the picture's left part every 8 frames (the user's "the right
  side is flashing duplicate on the left", a state in $A; there since the scenes were widened, before the walks too).
  In the shaking scenes (`scene_shakes`) `story_bg_entry` now counts a scroll of 248-255 as 8-1 px to the left: the
  sides move with the picture. Not by the scroll alone: $20's upper part (lines 0-143, the split at reg 10 = `$47`)
  slides 4..88 px left over its still floor in steps of 4 while Jafar's snake comes in from the right edge, the table's
  column 0 (the snake's head) belonging at the right there - counted as a move left, the head showed again in the
  hidden column at 252 / 248 (tried, dropped); its sides stay as they were. Measured (headless, every scene from the title at 74 and 102, the sides against themselves shifted
  by the shake) and in the app at 21:9 CRT and square (the user's state, frames 10-41: each side against the first
  frame at the best shift of -3..3 px): before 35 % of the right side's lower part wrong on each left shake frame,
  after 0 on every frame.
  A scene can draw another picture over its own: Jafar's spell in the throne room ($10, ticks ~730-830) blanks the
  table but for the pillars' outlines in the spell's colours, and the sides, still from ROM, showed the room's pillars
  as dotted outlines beside a black 4:3 picture. Each frame (`scene_replaced_rows`, at the frame's start) a row whose
  32 cells mostly (more than 24) differ from the picture's own is taken as drawn over, and its sides repeat the
  table's cells of the same source columns instead. Checked over every scene every 10 frames (the table against the
  ROM model): the spell is the only such stretch inside a scene; the tiles the model's cells use never change in VRAM
  during a scene (no reloads), and the cells the scenes draw into (the Genie, the framed pictures) are at most 10 a
  row.
* **Characters at the sides**: the writers park a piece whose 16-bit X is off the 256 px at X = 0 (under the blanked
  column). PC hooks before their "off screen" test (`$23C2`, `$3C63`, `$50D4`: hl = the piece's X, bc = its
  character's) record each piece by slot of the sprite table buffer; at the VBlank's upload (`$02D9`) the slot's Y /
  X / pattern are taken with it, and while they stay as they were the record holds (the scenes run their writers on
  some frames only: not while text is written - 289 to 1315 of ~1400 uploads in a scene have no writer pass before
  them). Parked entries are hidden (module_state[0..3], `sprite_x`) and the pieces in reach drawn as extra sprites at
  their true X: the game's sprite table, its RAM, stays as it is. Scenes keep characters waiting off the screen for
  their cue (scene 0's second guard at x -48 for 557 frames, Aladdin at -16 for 1009 frames in $18, a figure at -36 in 4):
  a character is drawn beyond the 256 px only once it has been on them (X within 0..255, or one of its pieces shown
  there - past the blanked column, x 8 on: the writer has put the piece's Y in the buffer before its X) since it was
  put where it is (a jump of more than 16 px, or another scene). So one leaving walks on into the side (and stays
  there if it stops there), one waiting stays unseen unless the scene's walk (below) draws it. While a character
  isn't shown, its pieces under the blanked column (x 0..7, which a widened scene shows) are hidden too, as the
  hardware hides them. (Scene 4's guard waits at x -36 for the whole scene with two pieces at x 4, under the blanked
  column: counted as shown, he was drawn in the left side, mostly behind the rocks' priority cells, his turban and
  a foot poking out at the 4:3 picture's bottom left corner, in the hidden column - the user's "garbage graphics at
  the bottom left", a state in scene 4. Before the walks, characters coming in from the left showed as a sliver in
  that column for a few frames - scenes 0, 3, 9, $B, $10, $1D, $1E: now nothing there until they are drawn whole.)
* **Walks** (`kSceneWalks`, generated by `tools/re/sms_scene_walks.py`): the scenes move their characters by a clock of
  their own, -(`$CF79`), counted by `$6100` on each pass of the scene's logic and not while text is written (a button
  hurries the text, not the clock: the characters' X per tick is the same with and without button-1 taps, every
  scene). Each walk has a tick t0 and the steps of the character's walk cycle as the game plays it (per tick the pose -
  the piece list its writer is called with, bank and address - and the X / Y move), recorded from the scenes entered
  from the title and played with no input. Aladdin's and the second figure's tiles are streamed to VRAM (patterns 1-17
  / 18-34, `$2362` / `$3BFC` from the pose's record, 8 bytes before its piece list: tile bank, duration, tile address)
  only for the pose shown, so their walk poses are drawn from ROM (`SmsVdp::ExtraSprite::data`); the list's
  characters use tiles the scene keeps in VRAM. The module draws the pieces as their writer places them (`$2390`: Y =
  offset + the character's Y, $C0 = none, clamped as the writer does; X = signed offset + the 16-bit X).
  Nothing of a scene's characters (walks, and the parked pieces of the ones shown) is drawn until the scene's picture
  is built ((`$C48F`) = `$8048`, as for its sides; the screen is black until its fade-in): the title's 20-frame wait
  after GAME START already has the scene's mode, and a state saved there by a version before the title's fade kind
  showed scene 0's Aladdin walking over the title (QA, fixed). Checked against master frame by frame from power-on
  through GAME START into scene 0, through PASSWORD into a stage, and every scene from its playthrough state into
  the next scene or stage, at 74 and 102: no extra sprite differs on any frame that isn't the scene's.
  - IN: before t0 the character is drawn on the cycle played backwards from where the game has it at t0 - so it
    walks in from beyond the wide picture's edge with the game's frames and speed and is where the game takes over,
    at t0, on the same step; from t0 until it is well in, it counts as shown. Its own (hidden or parked) pieces are
    not drawn meanwhile. A cue earlier than the walk across the side takes (scene 0's Aladdin: t0 58 at 1 px a tick)
    leaves the character partway across the side when the scene fades in (the clock is 0 through the fade-in: it
    stands there, in the pose of tick 0, and walks from the first tick): at 21:9 CRT Aladdin starts at the very edge,
    at 21:9 square 27 px in. The walk is drawn while the character is within 80 px of the view.
  - OUT: one the game removes just beyond the 4:3 edge after walking off goes on with the cycle of the ticks before,
    until it is out of the widest view (t1).
  - SHOWN: drawn as the game has it, t0 to t1.
  | scene | character | walk | t0 | cycle |
  |---|---|---|---|---|
  | 0 the market | Aladdin | in from the left (waits at -17 walking on the spot) | 58 | 48 ticks, 1 px |
  | 0 | the guard | in from the left after him (waits at -48 running on the spot) | 494 | 24, 2 px |
  | 1 the market | Aladdin | in from the left (put at -8) | 365 | 48, 1 px |
  | 1 | Abu | out to the right (removed at 354) | 348 | 20, 1 px |
  | 6 the palace | Jasmine | in from the left (waits at -32) | 66 | 48, 1 px |
  | $10 the throne room | Jasmine | in from the right (put at 264) | 346 | her walk in $11, 48, 1 px |
  | $10 | Jasmine | out to the right (removed at 327) | 592 | 48, 1 px |
  | $11 night | Jasmine | in from the right (put at 256) | 808 | 48, 1 px |
  | $11 | Rajah | shown lying on the balcony at 256-287 | 0-392 | |
  | $11 | Rajah | out to the right (removed at 287) | 798 | 2, 1 px in 2 |
  | $18 the throne room | Jafar | out to the right (removed at 259) | 384 | 8, 1 px |
  | $1A the throne room | Aladdin | in from the right (waits at 268) | 98 | 48, 1 px |
  | $20 Jafar's room | Abu | in from the right (waits at 256 running on the spot) | 114 | his run off at 402, 24, 2 px |
  | $23 the balcony | Iago | shown on the tower's corner at 256 | 0-850 | |
  Not walked: the pole vault in 3 (Aladdin and Jasmine wait at -30 / -26 in the sky beyond the roof the picture
  repeats, and leap in), the carpet in $B (it swoops in from -52 at 5 px a tick from tick 1, while the scene still
  fades in: drawn from there it popped up in the side), the bird of $1D (it comes in from the top, on the 256 px), Jafar's snake in $20 (drawn into
  the table), scene 0's guard at the scene's end (he reaches 270 as the scene ends; the fade freezes everything), and
  the characters walking off as a scene ends (2, $1B, $22: the scene's last ticks).
  Checked: every walk's hand-over, every frame from 6 before t0 to 8 after at 74 and 102 (no doubled or missing
  character, the step and the pose go on), strips of each walk at 74 and 102, and the app at 21:9 CRT and square.
* **Intro, title and menu** (mode 0, script `$CF01`): the Genie and his framed pictures, the logo and the menu on
  a plain background, table entry 0 at every edge column in every state: the sides are entry 0, the blanked column
  the plain background without sprites. The attract mode passes through it (its RAM untouched).
* **Ending** (mode $12, 8532 frames, then the title): the stars are sprites (buffer slots 20-42, pattern 131, moved
  1 px left every 4 frames by `$7686`: they wrap at 256), the sea (rows 16-21, a 32-px pattern) scrolls per line, the
  credits are written on rows 6-13 and "The End" is drawn there. The sides show the table's wrap but `$0100` (blank)
  on rows 2-15, every star again 256 px left and right, and the carpet (Aladdin's writer) at its true X when it flies
  off.
* **Fades**: a screen's end sets the next mode and then fades out (`$0913`), so for about a second the old picture is
  shown with the new mode - a scene's (`$66D7`, return `$66F7`) with its stage's mode, the title's (`$4BD2`, `$4BF4`,
  a pad code `$1101`) with mode $13, $F or a demo's, the ending's (`$75EC`), the password's and game over's with a
  stage's mode. Before, a scene fading into its stage was taken for the stage: masked sides, and the sprites parked
  under the blanked column showed in the picture's left column. The `$0913` hook now keeps the kind of screen fading
  (module_state[6] bits 29-31, cleared at `$092D`): it is drawn as itself, and a stage's mode doesn't count as a
  stage meanwhile (`shows_stage`). The lamp's chamber into scene $A (`$351E`, return `$3536` with mode $13) is a
  stage's end like the others: the stage stays wide through its fade (it dropped to 4:3 at once).
* **A wait before the fade**: two screens set the next mode and then wait before they call `$0913` (the ROM's every
  write of `$C200`, checked): the title's GAME START / PASSWORD (`$4BC9`: mode $13 or $F, scene and `$C120` 0, then
  `ld b,$14; call $0B4D`, 20 frames of the title, the choice blinking) and a password taken (`$7494`: the stage and its
  mode, then 30 frames of the password screen). The title was taken for scene 0 meanwhile, and `scene_cell` read the
  title's map and metatile pointers (`$C48F` / `$C49D`, both 0): a block of junk tiles in the right side (at 21:9 CRT
  x 330-404, rows 0-7, and a piece near the bottom; QA round 3 n1, "garbage sprites": they were background cells, no
  extra sprite was drawn). The password screen was taken for the stage: reported gameplay, its sides drawn from the
  stage's map for 30 frames. PC hooks right after the mode's write (`$4BCA`, `$7497`) now set the fading screen's kind
  as `$0913` does (title; password: pillarboxed), so both stay themselves until their fade is black. And a scene's
  sides come from ROM only while (`$C48F`) is the scenes' map table (`$8048`, set by `$4E92`): before a scene's picture
  is built they are entry 0 (in the black between a stage or the title and the scene).
  Measured from the title (the user's state, 21:9 CRT, app): 19 frames of junk before, none after; headless the same,
  and the password screen 30 frames of the stage's map at its right edge before, pillarboxed throughout after. The other
  changes - scene to scene, scene to stage, stage to scene, the lamp's chamber, TRY AGAIN, game over, the ending to the
  title - set the mode right before `$0913` (QA's round-3 replays at 21:9 CRT, power-on to scene $11 and scene $23
  through the ending to the title, before and after: the same but for the title's wait; the sides change under a
  still 4:3 picture only where a character walks or flies in a side).
  Parked sprites: the title, password, game over and menus draw no extra sprites (`StorySprites::upload` draws them
  on a widened scene or the ending only, and a character's only once it has been on the 256 px since it was put
  there on the screen shown), so a non-gameplay screen's parked pieces stay hidden as in the original.
* What doesn't change: Jafar's snake (scene $20) grows out of the 4:3 picture's right edge, as the game only writes
  the part on the original screen into the table. Characters leaping or swooping in (the pole vault in 3) appear at
  the 4:3 edge.

## 6. Save states and rewind

module_state (in the core's state) holds the displayed sprite pages, the cameras (with the frame counter), the
undrawn objects ([6] bits 0-15) with how far Aladdin is left behind (bits 16-23), the stage fading out (bits
24-28) and another screen fading out (bits 29-31, section 5a), and the placement flag ([7] bit 30) with the queue loop's slot being timed (bits 0-22). The rest the hooks keep between frames is in
`save_ext` (version 1): the pages the writers left for the next table upload (the game's logic runs across the
frame's end, so a state taken there has part of the next table written), the extra sprites made at the last VBlank
for the frame being drawn, and the frame counter at stage 1's last camera move. The camera of the frame before
comes from module_state. A reconfigure at the same width inside a stage keeps them; a state without them (older
states, or from another width) starts them empty. After them comes the story screens' record (tag 'S', version 2:
each slot's piece, character and contents, each character's X and whether it has been on the screen, section 5a,
and the scene's clock at the writers' last pass, which places the walks; version 1, without it, still loads), then
'D' and a ROM offset per extra sprite (the walks' poses drawn from ROM; none: VRAM's).

`sms_rewind.py` steps back as the app does (load a state, one frame, compare its picture with the forward run's):
stages 0 1 5 7 8 at E = 74 and 102, 2000 frames each, 1000 states: every picture identical; loading the core's state
alone (`--no-ext`) gives wrong extra sprites for a frame (stage 5 at 102: 17 of 300). `--scenes` / `--ending` do the
same in story scenes (through their fade-out into the next mode) and the ending: identical; without the module's part
scene 3 at 102 differs in 76 of 954 (the characters in the sides).

## 7. Verification

| check | how | result |
|---|---|---|
| map model | `sms_map.check` every frame, all stages; whole stages 1 and 5 | 0 mismatching cells |
| extension background | `sms_wide_check.py 74` / `102`, whole stages (`Player`), every 3rd frame | 0 in stages 2 6 8 9; ≤ 0.04 % in 3 4 5 7 (fade-in, lava, torches, stars); 0.2-0.35 % in 0 1 (the walls' off-screen faces, section 4a; stage 0 at 74: 0.37 % before) |
| camera-animated walls | each wall in view frame to frame, whole stages 0 and 1 at 22 / 74 / 102 (section 4a) | no sudden change with a wall in a side (before: 18-30 a run in the Market); the 4:3 picture's own steps as at 4:3 |
| the Market's pursuer | `sms_pursuer.py`: E = 0 / 22 / 74 / 102, invincible and not, running, pausing, standing, walking back | as at 4:3 but for the barrels' earlier stuns (section 3) |
| slide at the ends | `sms_slide_check.py 22 74 102`, 600 frames (free stages to the far end and back) | no jumps, view never outside the level, bands still while it holds (0.0 %) |
| level ends | `sms_run_sheet.py`, app `--states sms_end` (21:9 CRT and square) | the view holds at the last pixel; no cells past the map |
| rewind | `sms_rewind.py` (tool), app `--rewind 120` (hold R, achievements off) | identical pictures; app: no glitches while rewinding or after |
| attract mode | `sms_attract.py`, 60,000 frames at every width, with and without cheats | RAM identical to 4:3 every frame; 12 demos, never a game over |
| widescreen off | the module at E = 0 against the plain core: attract 20,000 frames, every stage 3,000 | RAM identical; the picture identical but for the late splits' lines drawn on time (section 1), a visual fix to the original |
| object wakes / pop-in | `sms_wakes.py 74 102`, every stage 4200 frames; `sms_popin.py` | section 3: none inside the view but the original's own |
| fixed hazards | stages 2 3 4 6 driven right then left, every logic step at the same camera, each hazard (types `$09 $0A $0C $0E $12 $1E`) against 4:3 at 74 / 102; the user's Caverns state walked left to the wall and back at 0 / 74 / 102 | state, Y and X as at 4:3 (before: the ceiling boulders, geysers and rolling boulders out of step for hundreds of steps); the boulder falls again on the same frame at every width (before: never, at 74 and 102); left: the rolling boulders' homes under the blanked column (section 3) |
| width changes | E 0 → 102 → 40 in stage 5 | waiting objects wake for the current E |
| slowdown | `r2_lag.py` (QA, round 2), every stage at 0 / 22 / 74 / 102, 3000 frames | section 3, "CPU time": near 4:3 everywhere |
| left behind, fade-outs | stage 1 standing still and walking back, stage 8's wall; every auto-scrolling stage's exit, a death to TRY AGAIN | sections 2 and 5 |
| story screens | every scene, the intro, title and ending at 74 and 102 (tool), the app at 21:9 CRT (before / after); `sms_rewind.py 74 102 --scenes .. --ending` | section 5a; rewind identical |
| scene walks | every walk: each frame from 6 before t0 to 8 after at 74 / 102, strips at 74 / 102, the app at 21:9 CRT and square (before / after); scenes as played from the QA playthrough states: RAM, VRAM, CRAM at 74 and 102 against 4:3 (and the plain core) every frame through every story screen into the next stage, without input and with button-1 taps | no doubled or missing character at the hand-over; identical |
| scene characters under the blanked column | every scene from the title at 74 / 102 against the build before the walks, frame by frame (pictures), and the sprite pixels in the hidden column | scene 4's waiting guard unseen again (turban and foot gone), its hidden column's bottom cells the ridge; characters walking in from the left are drawn whole from their first piece past x 8 (before the walks a sliver in the hidden column for a few frames: 0, 3, 9, $B, $10, $1D, $1E) |
| scene sides | every scene's table against the ROM model every 10 frames; the tiles the model uses, every 5 frames; the 4:3 edges of every widened scene at 74 | the spell in $10 the only picture drawn over (now repeated from the table); no tile reloads; $A's rule remade |
| scene shakes, floors | every scene from the title at 74 / 102, frame by frame against the build before (left side, 4:3 part, right side; RAM, VRAM, CRAM); the lines' scrolls of every scene; the user's $A state in the app at 21:9 CRT and square, frames 10-41 | the shaken scenes' sides (8 $A $C $1C; 8 and $C are 4:3) move with the picture: no copy of the table's left part on left shakes; $A's floor carried on to the left at 6..102, unbroken behind the claw with every claw pixel kept (still, both shakes, fade in and out); $B's sides laid out from its drips (through its fade-in, shake and fade-out); $1B's table not repeated; every other scene's frames identical to before; RAM / VRAM / CRAM identical |
| screen changes | GAME START and PASSWORD from the title, a password taken, game over (continue) at 21:9 CRT (tool, app); QA's round-3 replays (`r3_play_e74`, `r3_play_e74b`: to scene $11, and scene $23 to the title), frames where the sides change under a still 4:3 picture | section 5a "A wait before the fade": none but characters walking in a side |
| screen-fixed texts | READY at stages 0 2 5's (re)start, PAUSE at a stage's start (slid) and mid-stage, TRY AGAIN?, at 0 / 74 / 102 with `hud_anchor` on and off (tool and app) | section 5: centred in the wide picture (READY was E px left at a start before) |
| screens | `sms_wide_shots.py`, `sms_run_sheet.py` (whole stages 0 1 5 7 at 74 / 102), `sms_app_test.py` (16:9 and 21:9 CRT, 21:9 square) | reviewed: stages, ends, HUD, scenes |
| late splits drawn on time | the module against the build before: stages 0 and 1 (`Player`, 2400 frames, E = 0 / 74 / 102), attract 15,000 frames (E = 0 / 74); QA `r2_identity.py` with the picture compared outside the lines whose drawn scroll differs from the plain core's | RAM, VRAM, CRAM identical every frame; pictures differ only on the corrected lines (64 / 80 / 128, 160 / 168), each drawn with the scroll its split brought a line later; `sms_rewind`, `hfr_test.py sms`, `sms_walls`, `r2_lag`, `r2_wakes` print the same as before |
| high frame rate | `hfr_test.py sms` (attract mode, stages 0 1 5 7) | RAM and picture identical with and without; in-betweens between the frames, the sides with the 256 px (section 7a) |

## 7a. High frame rate

With *High Frame Rate* on a 100 Hz+ display the VDP draws up to three in-between pictures with each line
(`SmsVdp::sub_frames`, `sub_fb`), as the Mega Drive's and SNES cores do. The background's line is drawn again with its
scroll part of the way back to the last frame's on that line as the screen shows it (the scroll less the line's
`bg_shift`, so the slide at a stage's ends and the parallax bands held still against it stay still), sampled at the
in-between's place: `render_bg`'s hardware-column test and `bg_entry` take that place, so the columns beyond the 256-px
name table come from the level map where the background is drawn and can't drift from it. The line hook runs for the
in-between lines too (the stone counter, the rows below the playfield). Sprites, the table's and the module's extra
ones, go part of the way back to where they were, matched by their pattern (`hfr_match`; the true X from `sprite_x`,
the view's shift taken off). Cuts are shown as they are: a scroll jump over 32 px, a line drawn with the display off,
another width, name or sprite table, a loaded state. The status bits stay the frame's.

## 8. Known limitations

* Objects entering from the right act from E + slide px of camera travel earlier than in the original (as soon
  as the view's edge is as near them as the original's is when it wakes them); A Whole New World's birds from the
  left start correspondingly earlier, and one the list loads late catches up quickly. (The attract demos run on the
  original ROM.)
* The walls stages 0 and 1 animate with the camera (section 4a) have no look of their own for places off the
  original screen: there they show the look the tables give the nearest place they have right (a right wall all
  face, a left wall all face or none), not a face wider than the original ever draws.
* The free-scrolling stages' fixed hazards (ceiling boulders, rolling boulders, geysers, spikes, crumbling ledges)
  run on the original's window (section 3): one the 4:3 game re-arms or brings home just past its own screen does
  so where a wide view shows it (a landed boulder back at the ceiling at the far end of a side). A rolling boulder
  whose home is under the original's blanked column comes home only past the wide keep window's edge (it would
  come out of nothing in the left side, rolling away from Aladdin).
* Left behind by the scroll Aladdin is lost at the view's edge (section 2), and can walk back in until then: a
  wider view gives him more time than the original.
* With the sprite limit on ("Remove Sprite Flicker" off), sprites in the sides count towards the 8 per line.
* Story scenes 8, $C, $D, $14 and $16 stay 4:3 (black bars): their picture is cut by its edges (a rock, rock walls, a
  roof's slope, a cave lit from the middle) and can't be carried on. In the widened scenes the walks in (section 5a)
  are drawn from where the game has the character at its cue; one whose cue comes too soon after the scene starts
  for the whole walk across the side (scene 0's Aladdin, 1 px a tick, 58 ticks) stands partway across the side as
  the scene fades in, then walks. Characters leaping in (the pole vault) still appear at the 4:3 picture's edge, and
  Jafar's snake grows out of it; one that walks off and stops in a side stays there. The walks are a table made from
  the scenes as the game plays them (`tools/re/sms_scene_walks.py`): the clock they follow can't be changed by input,
  but a scene changed by a ROM hack would need the table made again.
