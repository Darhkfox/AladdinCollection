# Aladdin (Super Nintendo): widescreen

How `src/game/snes_aladdin.cpp` (`AladdinWide`) widens the picture. Game logic and the RAM map: `re_snes_game.md`.
Extension per side (`wide_ext`, `src/app/settings.cpp`): CRT pixels 30 / 46 / 68 / 106, square pixels 52 / 72 / 96 /
140 for 16:10 / 16:9 / 2:1 / 21:9. `max_ext()` is 140.

## 1. When

A level is on screen while the main task waits in the level's main loop or its pause loop (task 0's saved return
address `$81:82E9` / `$81:83FA`), or the loop's top (`$81:82CF`, an exec hook) ran within the last 4 frames (a frame
the game's logic doesn't finish in leaves the task running and its saved stack stale). Everything else - logos,
title, OPTIONS, password, story scenes, STAGE CLEAR, the bonus wheel, continue, the ending and staff roll - is
reported as no gameplay and pillarboxed, and so is a level the game runs as an attract demo.

**Attract demos** (`$0331` = 2: the pad read from the recording at `$84:F700`; their level index is the level's own,
`$00` for Agrabah Market) replay recorded input against the original game: anything that changes the code they run
sends them off their path - the wider draw / keep ranges and the margins' spawn, the cheats (the Debug Mode's start
with 10 hearts and 99 apples: `$81:80ED` / `$84:AEFD` read `$0009`), and Remove Slowdown (the lag frames are part of
the recording); even a hook that only answers a test in fewer cycles moves the scheduler's idle count (`$0329`). The
demo's flag is set (`$81:8066`, from the title's timeout) before its level's start. While one plays the module
changes nothing: every behaviour hook falls through to the original code, the turbo stays off, the cheats' hooks and
the Infinite Gems write rest, the debug flag reads as 0 at those two starts, and the demo is reported as no gameplay
(shown at 4:3 with the side bars). The hooks only act in a level being played (`AladdinWide::active`): armed by the
level's own start spawn (`$82:A72C` with a level index below `$14` and no demo) and disarmed once the index is a
screen's or a demo plays - the story scenes run that spawn too (index `$30`, `$35`), and the title zeroes the index
while it waits for GAME START (`$80:9F70`). `tools/re/snes_attract.py`: 60,000 frames from power-on with no input -
the logos, the opening story, the title and four demos (Agrabah Market and the Cave of Wonders twice each, 5,508 demo
frames) - at 46, 106 and 140, with Remove Slowdown at 4:3, 46 and 140, and with the gameplay cheats (Invincibility,
Infinite Health / Lives / Apples / Gems) at 4:3, 106 and 140 with Remove Slowdown: the work RAM equals the 4:3 run
without them in every frame, and no demo frame is reported as gameplay. (The Debug Mode changes the title itself;
`snes_attract_debug.py`: its demos play the same, frame by frame.) The launcher's preview (4:3, from the opening story)
and the app's own attract mode show the demos 4:3 with side bars.

## 2. Background: drawn from the level's data

The engine streams two BG layers: BG1 the level (struct `$1738`), BG2 the background (`$188E`), each a 64x32 map
(`$4800` / `$4000`) filled a column (8 px, `$81:C2E0`) or a row (`$81:C383`) at a time around the camera from four
edge cursors (`+$00/$0A/$14/$1E`, set by `$81:C128`, moved by `$81:C41F` / `$81:C45C`), queued for the NMI (`+$50`,
`$80:8366`). Struct fields: `+$2E` VRAM map base, `+$3B` x, `+$3F` y, `+$42` the layer's offset in the page table,
`+$4C` the metatile bank, `+$4F` streaming flags (0: a fixed picture).

The level data is all in RAM:
- `$7F:F000` page table: per layer a grid of 256x256-px pages, 32 entries (2 bytes) a row, a border row and column at
  -1; index = `+$42 + (y/256 + 1) * 64 + ((x/256 + 1) * 2 & $FE)`. An entry is the page's block map in bank `$7F`;
  bit 15 marks a page the camera may not enter (drawn as page 0).
- a page's block map: 16x16 blocks of 16 px, 2 bytes each at `((x >> 3) & $1E) | ((y << 1) & $1E0)`.
- a block: `entry | $8000` in the layer's metatile bank holds 4 tile entries (TL, TR, BL, BR at `((x >> 2) & 2) |
  ((y >> 1) & 4)`).

So the PPU's `fetchWideTile` hook gives every margin tile straight from that data, at the world position of the
screen pixel (the layer's scroll register completed from its struct, per line). Nothing of the game's streaming
changes, any width up to the PPU's limit works, and a width change in the middle of a level needs no redraw.
`tools/re/snes_mapdecode.py` checks the decode against VRAM over the game's own screen in every level: identical.

A layer is drawn from VRAM instead where it doesn't follow its struct: a band of a background scrolled in raster
bands by HDMA at another speed than its struct's (`$083F` modes `$0A-$0E`: Agrabah's skyline, the lava cavern's), or
a fixed / fully loaded repeating picture - their VRAM map holds the whole 512-px repeat, so the wrap is right.

**Parallax.** BG2's speed by `$083F` (`$81:C781`): modes 0 / 6 / 8 half the camera's x, 2 / 4 the camera's. Where the
view stops at a level edge while the game's camera moves on (below), a half-speed background would slide against the
still picture; it is drawn as if the camera were at the picture's middle: shifted by `(ratio - 1) * (ext - extL)` on
the whole line (`bgHofsAdj`, the line decoded throughout: `wideFetchAll`), which is 0 wherever the view is centred.
Before its first column and past what the game shows of it at the level's end (`$1A04 * ratio + 256`) it is mirrored.

**The Carpet Escape's wave** is a picture on BG2 (a 64x64 map, its first 256x256 screen; `$083F` = `$12`, an object
sets the scroll): in the margins only its own copy, at `[-x, 256 - x)`, is shown (none while parked at x = 256), not the
map's 512-px repeat.

**Backgrounds the game scrolls itself** (`$083F` modes `$0A-$14`, the routines of `$81:C781`, run at the end of the
camera's move `$81:C5A4`). Where the view stops, BG2 is drawn on each line where the game's own code would scroll it
with its camera at the picture's middle, v = camera - extL + ext (`band_adj`): the line is shifted by
`H(v) - H(camera) - (v - camera)` on top of the view's shift (`bgHofsAdj`; drawn from the level's data the whole line,
`wideFetchAll`, where the line follows BG2's struct). So it holds still with the stopped picture and moves exactly as
the game moves it; where the view is centred, v = camera and nothing changes. H by mode:
- `$0A` Agrabah Market, Market at Dusk (`$81:C98C`): six bands by world y (bounds `$80:C61A`: 0, `$C8`, `$FC`, `$10C`,
  `$11C`, `$1AC`): S, S/2, S/4, S/8 with S = `$1A30` + camera, then camera / 2 (the town, BG2's struct) and camera *
  5/4. `$1A2F` (24 bits) drifts by `$C0` a frame and drops by the camera's move right (`$19E5` the camera before it):
  the top four bands hold still while the camera moves right and move with it (1, 1/2, 1/4, 1/8) while it moves left.
  With the camera at v the game would have dropped `$1A30` by v's moves instead: `band_d_` keeps that difference (mod
  4096, a multiple of the 512-px map), updated once per logic frame.
- `$0E` Agrabah Rooftops (`$81:C952`): seven bands (bounds `$80:C60A`): (`$1A30` + camera) / 2 and / 4 (the sky
  drifting), then camera / 2.
- `$0A` / `$0E` build their HDMA table by world y from camera y + 8 (`$81:C84B`; double-buffered, `$80:C628`, BG2HOFS
  on channel 4): the first entry one line, then the bands on screen. HDMA starts from the IRQ at line 8
  (`re_snes_game.md`, core fix), so the second entry reaches row 11 (`kBandRow0`). Each line's band is found from BG2's
  vertical scroll (camera y) and checked against the register (a band's neighbour at its edge).
- `$0C` the Lava Cavern (`$81:C7E7`): eleven bands in a fixed screen table (`$7E:F400`, counts from `$80:B992`, rewritten
  in place by the logic, so mid-frame), each a 24.8 accumulator with its own speed (`$7E:F800` + `$7E:F900`): they drift
  whatever the camera does, but band 9, which is the camera: every line holds still (H independent of the camera),
  band 9's moves with BG1.
- `$10` Jafar's Palace 3 (`$81:C9F2`, phase `$0841`): the camera; from its last part (phase 2, 4) at half speed past
  `$1000` (`$81:CA55`). Past what the game shows of it at the level's end (`phase($1A04) + 256`) its page row holds
  other data (a block of zigzag tiles at 21:9): mirrored there.
- `$12` the Carpet Escape's wave: placed by its object at its world position (`$83:F244`: camera + `$80` - x), `$14`
  Sultan's Dungeon 3 (`$81:CA6A`): the camera (its y moves by itself): both move with BG1 - no shift.

The sums S (and `$1A30` + camera for `$0E`) whose low bits the shifts need are taken when the frame's logic is done (the
level loop reached its frame wait `$81:82E6` since its top: `logic_done_`) and used for the frame that shows that
camera (`band_next_` / `band_shown_`); in a frame the game's logic didn't finish in, a line whose register isn't the
worked-out value takes the sum from the register (its low bits unknown: within 1 px). A half-speed band drawn from
the level's data shows up to ext / 2 px before its first column at the level's start: these levels fill the page
table's border column (page -1) with their background's repeat (Agrabah Market's town: `$1800`, `$1600`, `$1800`, ...;
the Market at Dusk's `$0200`, `$0000`, `$0200`, ...), so it is drawn from there, not mirrored as the parallax levels'
(whose border column is empty in places: Desert Ruins); at the right end their page rows go on past what the game
shows. `tools/re/snes_motion.py` checks every band against the game's own move of it, from the registers
(`Ppu::lineRegs`, `snt.py line_regs`).

## 3. The view

The picture is the game's 256 px plus `extL` / `extR` (`Ppu::setExt` at the first line; the PPU's `kMaxExt` is now 288
so one side can take both extensions, 2 x 140).

**Where the view stops** (`span`): at the level's ends, `[0, $1A04 + 256)` less the columns of pages no row the
camera reaches may enter (bit 15 top to bottom: `level_extent`; the Underground River's first 256 px, where its camera
never goes - a first version stopped at 0 there and showed that column dimmed and mirrored), and on an arena's screen
when the camera is held in x while free in y (`$1A10` set, `$1A12` clear: Jafar, the Cave of Wonders' last event). The
view is centred on the camera and stops there; an arena's picture is centred on its screen but kept inside the level
where the level is as wide as the picture (the Cave's end: centred, it went a pixel past the level's end once the
offset closed); a span narrower than the picture is centred. That is where every layer can follow a view that stays
still while the game's camera moves (`can_stop`): BG1, and BG2 in every level's mode - at a known speed (`$083F` modes
0-8, the parallax shift) or as the game scrolls it itself (`$0A-$14`: raster bands, phases, an object; section 2). (A
first version kept the view centred in those seven levels, with the level mirrored and dimmed past their ends; before
that they stopped with the bands sliding against the still picture.) A mode outside those would keep the view centred.

The span changes only at an arena or when the level's width changes (Jafar's room). The view's offset to its new place
then closes only as the game's camera moves (at most 4 px a frame, and no more than the camera moved): the picture
never pans by itself while the game's screen holds still. (A first version also stopped at the zones' camera limits and
at pages the camera can't enter at its height and slid there by itself: in play testing layers moved where the
console's don't. `tools/re/snes_motion.py` measures each layer's motion from the pixels and checks it.)

**Dimmed** (`reach`): past where the game's camera can show now - the zone's camera limits `[min(cam, $1A40),
max(cam, $1A42) + 256)` (Market at Dusk's boss), an arena, the pages it may enter in the page rows its screen covers
(barrier pages, bit 15). On each line, past the level (the line's own page row's pages, the level's width) BG1 and BG2
are mirrored about the level's edge (`Ppu::wideMirror`, `wideDimLo/Hi`; black instead with *Past Level Ends: Black*).

A state made at another width (or the width switched on from 4:3) shows its first frame where the view stops already
(the level's extent at the camera), not centred.

**Save states and rewind** (the app saves one every 2 frames while rewinding is possible): `save_ext` (version 4)
carries the view (span, offset, reach, whether it may stop), the sprites' recorded x - both the latched set and the one
being built for the next OAM DMA - how many frames ago the level loop's top ran, so a state saved in a frame the game's
logic didn't finish in still counts as a level, the band routines' inputs (both snapshots, whether this logic frame's
is taken, the ratchet's difference) and whether a level is being played (section 1; an older state: a level index, no
demo and the main task in the level loop). `tools/re/snes_rewind.py` rewinds as the app does and compares every
rewound picture with the straight run's: identical (also from near a level's end: its `state` argument).

## 4. Objects

- **Drawn** while within a range of the camera's centre (`$81:91D4`: `centre - x + off < 2 * off`, table `$80:BA02` by
  the object's class): the hook runs the test with the view's margins, `-off - extR <= centre - x < off + extL`.
- **Kept alive** within another range (`$83:962E`, table `$80:CE73`), deleted beyond: the same widening.
- **Spawned** from the level's object list (`$82:8A7A`, sorted into 32-px columns at `$7E:C100` / `$C200`, rows at
  `$C300-$C500`, spawned bits `$7E:C600`): at a level start all columns from camera - `$40` to camera + `$120` (12,
  `$82:A72C`); as the camera crosses 32 px (`$1BEF`) the next column on that side (`$1BEB` right, `$1BED` left,
  `$82:A79C`); rows within `[camera - $40, camera + $140)`. The hooks give the spawner the camera at the picture's
  middle (`cam - extL + ext`, so its columns follow the view also where it is split) and move both edge columns out by
  `ext` (`$82:A767`, `$82:A772`; the start's column count `$82:A77E`, the rows' range `$82:A876`).
- A width change mid-level (or a state made at another width, or without the module's data) re-runs the level
  start's spawn at the next main-loop top, past its set-up: a stub in patch ROM (`$60:8000`: `JSL` an entry that sets
  DP 0 / DB `$82` and pushes D and B as `$82:A72C` does, `JML $82:A742`; back at `JML $81:82CF`). The set-up
  (`$82:A93A` -> `$82:A978`) clears `$7E:C100-$C7FF`, the spawned bits at `$7E:C600` among them; run with it, as a
  first version did, every object near the camera was spawned again on each widening (gems counted twice, Jafar's
  room grew twice as far). Past it, objects already out are skipped by their bits (`$82:A8C0`: `AND #$81`) and the new
  margins get only theirs (`tools/qa` round 1, F1: no duplicates in any level after any widening; the margins hold
  what a level start there spawns). The request waits through screens that aren't the level loop yet (the
  Underground River's set-up) and is dropped when a level's own start spawn (`$82:A72C`) runs for the new width.
- **Sprites:** the OAM builder (`$81:9309`) stores each piece's 16-bit screen x (`$81:938B`, flipped `$81:93F9`, X =
  OAM index * 4); recorded at those stores, latched at the NMI's OAM DMA (`$80:81B2`), the PPU places them by their
  true x (`objTrueX`). `tools/re/snes_oam_writers.py`: no other code writes sprite x in any level.

## 5. Status bar and texts

BG3 holds the bar (rows 1-4, lines 0-39) and nothing else but the centred texts (ACTION, PAUSE on row 12). The bar's
lines are remapped: lives and hearts (x < 152) to the picture's left edge, apples and gems to its right edge
(`layerRemap`); with *HUD anchoring* off the bar stays over the game's screen. Below the bar BG3 is hidden in the
margins and, where the view is split, moved to the picture's middle (`(extR - extL) / 2`) with the anchoring on.

## 6. Verification

- `tools/re/snes_wide_shots.py <ext>`: every level's start and a run right; `snes_wide_play.py <ext> <levels>`: a bot
  plays each level (invincible) with pictures every 150 frames (`tools/qa/snes/play_<ext>_<n>.png`). Checked at 46,
  106 and 140 in all 19 levels.
- The app itself: `build/bin/AladdinCollection.exe --game snes --autotest <dir> <script>` with `aspect=1` in
  `settings.ini` for 140; `tools/re/snes_apptest.py <dir> 4 <levels>` makes the states (each level's start and, from
  `snes_end_states.py`, near its right end), the script and contact sheets.
- `snes_motion.py <ext> all`: every level from its start; `snes_motion.py <ext> <level> 900
  tools/states/snes_end_<level>.state` from near its right end (`hover` for the Lava Cavern). At 46, 106 and 140 the
  seven levels whose background the game scrolls itself have no BG2 error (every band, from the registers) and no
  "still" frame, from their starts and their ends; BG1 is off in 0-3 single frames a run, all at a fade (a level's
  fade-in, a life lost, the exit), as with the centred view before. The twelve other levels' counts are the previous
  module's exactly (theirs are frames of animated or flat pictures: Underground River, Inside the Lamp 3, Jafar's
  Palace 2 most). The lava's drifting bands measure wrong from the pixels (listed only).
- `snes_edges.py 140 <levels>`: the picture's world range against the level's in every frame from each level's start
  and end state: nothing past the level's ends in the seven levels at 46 and 140, nor in the twelve others but
  Jafar's room, narrower than the picture at 21:9 (pictures `tools/qa/snes/edges_<ext>_<level>.png`).
- `snes_rewind.py 140 all` (and 106 in the seven levels, 140 from their end states): every rewound picture identical.
  The app's own rewind (`R` held in an autotest script, achievements off) shows the same.

## Known limitations

- Objects that only exist where the game's camera is (scripted events, bosses) appear as in the original; enemies
  can be seen waiting at the margins' edge before they wake (they act as on the console once in range).
- In the levels whose background moves at half speed (modes 0, 6, 8), where the view stops at a level's start the
  background shows a mirror image of itself before its first column (up to ext / 2 px) wherever it is seen there; the
  levels the game scrolls itself draw that from the page table's border column (section 2).
- The bands' lines follow the core's HDMA timing (the table's second entry at row 11): a few lines off the console's
  if that timing is (the band's value is checked against the register at its edges).
- Where Jafar's room grows and the camera is then held, the view keeps its place (the offset only closes while the
  camera moves), so the arena isn't centred in the picture; its unreachable sides are dimmed.
- The dimmed sides of an arena show the level's data, not what the game draws there during the fight (Jafar's cobra
  phase sets the floor on fire only on the game's screen).
