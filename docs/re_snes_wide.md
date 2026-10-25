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
The struct's x / y it is completed from are taken as the frame starts (`near_x_` / `near_y_`): the game's logic moves
the camera during the frame, a byte at a time and past the level's edge before it clamps it (Inside the Lamp 3 at its
left end: x `$00FD` for part of a line, then `$FFFD`, then 0), and a line drawn meanwhile was taken for one the game
scrolls on its own - drawn from VRAM and not limited to the level: one bright line of other data for a frame, which
the dimming used to hide.
`tools/re/snes_mapdecode.py` checks the decode against VRAM over the game's own screen in every level: identical.

A layer is drawn from VRAM instead where it doesn't follow its struct: a band of the lava cavern's background (scrolled
in raster bands by HDMA at its own speeds, `$083F` mode `$0C`), or a fixed / fully loaded repeating picture. Their VRAM
map holds the whole 512-px repeat, so the wrap is right. Agrabah's raster bands (modes `$0A`, `$0E`) are drawn from the
level's data at every band's scroll, from their picture's first 512 px (x mod 512: section 2). The game's VRAM map
doesn't hold the whole repeat: the column before x = 0 keeps what the level's start loaded there (the border column's
page) until the camera passes x 480. The 4:3 screen never shows that column, but the margin did: 8 px of that page in
the Rooftops' hills and town, left of the game's screen.

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
- `$0E` Agrabah Rooftops (`$81:C952`): seven bands (bounds `$80:C60A`: 0, `$CA`, `$D8`, `$148`, `$150`, `$160`,
  `$178`). The routine fills them from the bottom up, each half the one below it, camera / 2^(7-k): the palace band
  camera / 32, then the town's / 16, / 8, / 4 and the bottom / 2 (BG2's struct). It then sets the first two to
  (`$1A30` + camera) / 2 and / 4 (the sky drifting).
- `$0A` / `$0E` build their HDMA table by world y from camera y + 8 (`$81:C84B`; double-buffered, `$80:C628`, BG2HOFS
  on channel 4): the first entry is one line, then come the bands on screen. HDMA starts from the IRQ
  (`re_snes_game.md`, core fix) at the line its NMI arms: 8, or the level's own `$1C7D` if set (12 in Agrabah
  Rooftops: every split there is 4 lines lower), or 4 lines after an NMI that ran past line 4 (`$80:8291`). The
  module watches the IRQ's `STA $420C` (`$80:8345`, an exec hook that only reads the line). The table's second entry
  reaches the screen 3 rows later (`hdma_row0`: row 11 from line 8, row 15 in the Rooftops); until then every line
  has the NMI's scroll, which is the first entry's. Each line's band is found from BG2's vertical scroll (camera y)
  and checked against the register: a band's neighbour at its edge, in a frame the game's logic didn't finish in.
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
worked-out value takes the sum from the register (its low bits unknown: within 1 px).

These three levels' BG2 page rows alternate two pages over the level, so their background repeats every 512 px, the
VRAM map's width (a band at another speed than BG2's struct shows the map's repeat). Every band line is drawn from the
level's data at x mod 512 (`world_bands`, `LineLayer::rep`), not from VRAM and not mirrored. Near the level's start,
where a band shows a little before BG2's first column (a half-speed band up to ext / 2 px), it shows the repeat. That
part is not drawn from the page table's border column (page -1), which isn't the repeat in every row. Agrabah Market's
town row is (`$1800` before `$1600`, `$1800`, ...) and so is the Market at Dusk's (`$0200`), but their sky rows hold
`$0000` there. So do the Rooftops' palace and town row (before `$0000`, `$4400`, ...) and their bottom row (before
`$4800`, `$4800`, ...): page `$0000` is the one with the palace, so a second palace showed in the margin. Past the
level's right end the page rows run out a page or two later (the Market at Dusk's town row at x 1280); mod 512 holds
there too. `tools/re/snes_motion.py` checks every band against the game's own move of it, from the registers
(`Ppu::lineRegs`, `snt.py line_regs`), and `snes_bands.py` checks every line against the routines (section 6).

(A first version took the Rooftops' five lower bands at camera / 2 and every band table from row 11. Where the view
stops at the level's ends (camera x below ext at its start, above `$1A04` - ext at its end), the palace and the town
slid against the still picture: the palace by ~0.47 px per px of the camera, 48 px from camera x 4 to 100 at 21:9
CRT. Each split was 4 lines off, and a second palace showed in the margin from the border column. Bands drawn from
VRAM also showed the stale column described above.)

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

**Dimmed** only where the margins can't show what the console would:
- Past the level on each line - its own page row's pages the camera may enter around the game's screen (a page it may
  not, bit 15, the game draws as page 0), the level's width: BG1 and BG2 are mirrored about the level's edge and
  dimmed (`Ppu::wideMirror`, `wideDimLo/Hi`; black instead with *Past Level Ends: Black*, `Ppu::edgeMirror`).
- Where the game paints on its screen what the level beside it doesn't hold (`painted_row`): Jafar's cobra (level
  `$11`) sets the floor on fire by writing the fire into its room's map (`$4000`, on BG1 since the room was held:
  rows 21-31) in the 32 columns on its screen only, and in the same frame points BG2 at a spiked arch (`$4800`, rows
  22-28, mode `$12`: placed by an object). From that frame the margins are dimmed on the lines from the arch's first
  row down (line 163; the fire's starts at 167), where they show the room's stored floor; the wall above is the
  room's and stays bright. The dim comes in the frame the fire appears on the game's screen (frame by frame: neither
  before nor after).
- Everywhere else the margins show the level as stored, at full brightness, also where the game's camera can't go now:
  a zone's camera limits (`$1A40` / `$1A42`: the Market at Dusk's wait at `$400` and its boss at `$500`, Sultan's
  Dungeon 3's boss at `$A00`, the end of Jafar's Palace 3 at `$11A0`), an arena (the Cave of Wonders' end, Jafar's
  room before the fire), the pages the camera can't enter in another page row its screen covers (Agrabah Rooftops,
  Sultan's Dungeon 2, Jafar's Palace 2). Nothing changes the level there, and its objects are drawn as the game has
  them: the sweeps leave those areas as empty as the console's would be. **The Cave of Wonders' end** (level `$04`):
  at its last camera x (`$1A04` = 3840) the camera is held in x (`$1A10` = 1, `$1A12` = 0); the magic carpet (`$6B`)
  on the floor rises, Abu comes in from the left (seen leaping in on the left side before the 4:3 screen shows him)
  and plays with it, then it flies off right with Aladdin after it and the level fades out (to the Underground River).
  The treasure room doesn't change and its layers keep streaming (the structs' flags stay `$0F`): the margins are its
  data, as anywhere. (A first version dimmed all of these (`reach`): past `[min(cam, $1A40), max(cam, $1A42) + 256)`,
  beside an arena's screen, and past pages the camera can't enter in page rows `camy >> 8` to `(camy + 255) >> 8` - a
  row below the screen's 224 lines included: the Cave's treasure room was dimmed on one side through its event, and
  the sides went from bright to dimmed in one frame as the camera was held and back as it was let go.)

In an arena a layer is drawn from the level's data also once the game stops streaming it, and from the struct whose
map it shows: Jafar's room (on BG2, struct `$188E`, map `$4000`) has both structs' flags drop to 0 once the camera is
held; partway through the fight the game points BG1 at the room's map, and at the cobra BG2 at the arch's. The room's
data is still the room's: identical to VRAM over the 240 px the map holds beside the game's screen (64 columns less
the screen's 33 and the streaming's column each side) and over the screen itself until the fire. Drawn from VRAM, as a
first version did, the left 40 px at 21:9 with square pixels (280 px on one side) showed the screen's own right edge
and two columns of zeros. BG2 holding an object's picture (mode `$12`) isn't drawn from data: the cobra's arch,
scrolled with the camera at 512, is shown on the game's screen only (its map's repeat had shown at the far left at
21:9).

**Windows: the lamp's beam** (the Underground River's end, level `$05`). The window effect seen in play is the
column of light over the lamp (a screen task, `$82:84CB`, every frame from `$82:8514`): colour math (add, fixed
colour 8, 8, 16; CGWSEL `$20`, CGADSUB `$33`) inside window 2 (WOBJSEL `$C0`: the colour window is window 2
inverted, and math is prevented inside it), on the lines of an HDMA table on WH2 / WH3 (channel 7, indirect, from
`$7E:F500`; the beam's edges at `$F532` / `$F533`, the lines above and below it `$FF` / `$00`). Its left edge on the
screen is `$0E78` - camera - shake, its right edge 16 px on, **clipped at 255** (`$82:853F`); with the left edge past
255 (`CMP #$0100`, `$82:8534`) the beam is switched off (fixed colour 0, `$82:85A1`). The PPU continues a window past
the screen's edges with the edge pixel's value, so while the beam was cut by the screen's right edge the whole right
margin was inside it: a wall of light from the beam to the picture's edge, down to the pedestal (the user's report:
"extra blue to the right of the lamp" at 21:9). And with the beam in the margin, not yet on the game's screen, the game
had it switched off: it appeared only as it reached the screen. Now the PPU takes a window's true edges for the
margins from the hook (`Ppu::wideWin`, per line, in screen x; used outside 0-255 only): on the beam's lines the beam's
own 17 px (`[L, L + 16]`, L from BG1's scroll register on the line, which matches the task's edge every frame), on
the others the registers' (empty). With the left edge in the right margin (up to 256 + 2 * ext) an exec hook at
`$82:8534` keeps the beam on and writes an empty window for the game's screen (WH2 `$FF`, WH3 `$FE`, both to the
shadows and the HDMA table), and the margin draws it at its place: it slides in from the picture's edge with the
pedestal. The game's 256 px are unchanged (the 4:3 run against the wide one with the same input, line 40 down, from
the user's state: identical but one frame of a sprite, as before the change); at 4:3 nothing is hooked. When the lamp
is taken the beam closes (`$82:85CE`, its edges move in a pixel a frame) with the camera at its last x, the beam
on the screen. The screen tasks of `$82:8000` hold other window effects (an iris `$82:860B`, a sprite mask
`$82:86CF`, a shape by line `$82:819A` that an object spawns at `$83:AB36`, the Market at Dusk's end at `$83:EA10`),
and `$81:9F5F-$81:A178` set colour windows; the play bot (3,000 frames a level at 106, the shadow registers each
gameplay frame) met none of them in play. Jafar's room enables the colour window (WOBJSEL `$80`) with colour math
prevented everywhere (CGWSEL `$30`): nothing to see. The levels' ends and events the bot doesn't reach were not
scanned.

A state made at another width (or the width switched on from 4:3) shows its first frame where the view stops already
(the level's extent at the camera), not centred.

**Save states and rewind** (the app saves one every 2 frames while rewinding is possible): `save_ext` (version 4)
carries the view (span, offset, whether it may stop; two words that held the dimmed range are written as 0 and not
read), the sprites' recorded x - both the latched set and the one being built for the next OAM DMA - how many frames
ago the level loop's top ran, so a state saved in a frame the game's logic didn't finish in still counts as a level,
the band routines' inputs (both snapshots, whether this logic frame's is taken, the ratchet's difference) and whether
a level is being played (section 1; an older state: a level index, no demo and the main task in the level loop).
`tools/re/snes_rewind.py` rewinds as the app does and compares every rewound picture with the straight run's:
identical (also from near a level's end: its `state` argument).

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
- **Refused spawns.** The allocator (`$82:AA2B`) refuses an object when no slot is free or, for a type with graphics
  of its own (`$82:AA61`, by `$82:B000`), no sprite VRAM bank is: four banks (`$15E9-$15EC` users, `$15ED-$15F0` the
  type; a type takes 1-3 adjacent ones by `$7F:E900`, `$82:ABCE` / `AC10` / `AC34`, shared while loaded). Its spawned
  bit stays clear, but the spawner comes back to a column only when an edge crosses it again. The margins meet objects
  earlier and keep more alive: in the Market at Dusk Jasmine (`$56`, three banks), who ends the level, met the
  margin's edge at 106 (her column 42 at camera ~970) while a `$20` and two `$4D` held banks 0 and 2 and was never
  created - after the boss the game walked Aladdin to x 1616 and waited for her (every run of the check below at 106,
  as reported). Some objects also delete themselves past the 4:3 screen (`$83:9738` / `$83:9749`: x < camera - `$20`
  / > camera + `$120`) and so were gone for good once the margin had spawned them. So at each new edge column
  (`$82:A7E3` / `$82:A7EB`, the JSR `$A89D`) the spawner first takes the original's edge columns, camera + `$120`
  (left: - `$40`) and the one before it on that side (the original's crossings are up to 31 px off the 32-px grid),
  where an object it would take now isn't out (`$82:A8C0`: spawned bits & `$81` clear, y in range): it gets its chance
  at the original's moment; then the margin's own column. None pending (the usual case), the game's JSR runs as it is.
  (Stubs at `$60:8020` / `$60:8028`; `$82:A780` with Y = 1 runs one column; an exec hook after it takes the next from
  X, so a state saved mid-way needs nothing of the module's.)
- **The sweep.** An event can delete every object (`$81:9D82`: the Market at Dusk's arena at camera `$400`, `$80:A176`;
  other levels' events). The delete (`$83:96E2`) clears an object's spawned bits unless it is once only (bit 7, from
  its list entry: kept at `$81`). Objects out in the margin ahead of the original went too, and a once-only one never
  came back: the Market at Dusk's boss (`$57`, column 44) whenever its column reached the margin before the sweep
  (at 140 in every run, at 106 depending on where the spawner's 32-px steps fall), and Jasmine when she had a bank. An object
  whose list entry (`$82:8A7A` + `$006D` + (index - 1) * 6: type, flags, x, y) lies at or beyond the original's
  spawn edge columns when the sweep reaches it (`$81:9D8E`, Y the slot) is left unspawned instead, for the spawner to
  take when the original's edge gets there (above).
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
- `snes_bands.py <ext> [levels] [turbo]`: Agrabah's three band levels on a tour that carries Aladdin to both ends and
  the middle at three heights and across both ends. On every line it checks the module's BG2 shift against the ROM
  routines' with the camera at the picture's middle (the line's band from where this frame's HDMA started), that the
  camera's bands hold still while the view is stopped, and the centred picture against the bare core's (each line's
  margin past the level's end left out: black when the frame is run again with the mirror off). At 46, 72,
  106 and 140, with and without Remove Slowdown: no line off, no stopped-view frame moving, no picture differing in
  any of the three. Before this fix the Rooftops had 63,122 / 69,247 / 77,505 / 96,429 lines off (of ~943,000),
  77 / 126 / 191 / 251 stopped-view frames moving, and 76 / 75 / 82 / 95 pictures differing at the four widths.
  The Market and the Market at Dusk had none before either. Rewind from states high at both of the Rooftops' ends
  (106, 140, 46): identical.
- The dimming (section 3): the old module against the new, frame by frame with the same input, at 106 - the play bot in
  all 19 levels (2,400 frames), the end states, the Cave of Wonders' end into the Underground River, Jafar's fight:
  the pictures differ only where the margins were dimmed beside an arena or a zone's limits or past another page row's
  pages (the Cave's end, Jafar's room, the Market at Dusk at `$400` and `$500`, Sultan's Dungeon 3's boss, Jafar's
  Palace 3's end, Sultan's Dungeon 2, Jafar's Palace 2, Agrabah Rooftops) and in single lines of single frames drawn
  while the struct was being written (Inside the Lamp 3, the Underground River: now right). At 4:3 identical. The
  fire's dim starts in the frame the fire does, at 106 and 140; *Past Level Ends: Black* still blacks out the same
  areas.
- The lamp's beam (section 3), from a state at the Underground River's end (camera 3331): the beam's edges in the
  HDMA table match `$0E78` - BG1's scroll every frame (on the screen, and in the margin with the hook); the game's
  256 px against the 4:3 run with the same input: identical (but one frame of a sprite, as before); `snes_motion.py`,
  `snes_rewind.py` at 106 and 140 and `snes_edges.py` / `snes_motion.py` in levels 4-5: as before.
- `snes_dusk_end.py`: the Market at Dusk from its start through the boss to STAGE CLEAR, 24 runs a width (Remove
  Slowdown off and on, three ways there, four ways to fight). Before the edge-column retry and the sweep's rule, at
  106 none of the 12 runs that beat the boss could finish (Aladdin walked to x 1616, no Jasmine) and at 140 the boss
  never fought (swept); now every width's runs end as the 4:3 run with the same input does (the boss beaten in 12 at
  0, 46, 106 and 140 - the apples alone never beat him - and the level ended in all 12). The objects with the play
  bot in all 19 levels (1,800 frames, the camera, Aladdin and every object's type and place compared frame by frame
  with the module before): identical at 46; at 106 (Remove Slowdown off and on) and 140, 14 levels identical, and in
  Agrabah Market and the Desert Ruins (a `$20` refused at the margin's edge), the Market at Dusk (Jasmine), Inside the
  Lamp 2 (a `$79`) and Sultan's Dungeon 3 (its `$7D`s, which deleted themselves past camera + `$120` when the margin
  spawned them) the first difference is an object the 4:3 game has there, now out.

## Known limitations

- Objects that only exist where the game's camera is (scripted events, bosses) appear as in the original; enemies
  can be seen waiting at the margins' edge before they wake (they act as on the console once in range).
- In the levels whose background moves at half speed (modes 0, 6, 8), where the view stops at a level's start the
  background shows a mirror image of itself before its first column (up to ext / 2 px) wherever it is seen there;
  Agrabah's three band levels draw it from their background's 512-px repeat instead (section 2).
- Agrabah Rooftops' palace band (camera / 32) and the one below it (camera / 16) never show their picture's 512-px
  wrap in the game (x 0-383 and 0-511). The repeat joins with a seam in the far hills, which shows in the left margin
  (at x 0 of the band, a little left of the game's screen). Mirrored there instead, the palace showed a second time
  at the picture's left edge at 21:9.
- The bands' lines follow the core's HDMA timing (the table's second entry 3 rows after the line the IRQ switches HDMA
  on at): if the console's timing differs, they are a line or so off (the band's value is checked against the register
  at its edges).
- Where Jafar's room grows and the camera is then held, the view keeps its place (the offset only closes while the
  camera moves), so the arena isn't centred in the picture; beside it the room is shown as stored.
- Jafar's cobra sets the floor on fire only on the game's screen (section 3): beside it the floor lines show the
  room's stored floor, dimmed, from the frame the fire appears to the end of the fight.
- Where the game's camera can't go (a zone's limits, an arena) the margins show the level and the objects the game
  has out there; ones it would only bring out with its camera there don't appear (the events' sweeps empty those
  areas on the console too).
