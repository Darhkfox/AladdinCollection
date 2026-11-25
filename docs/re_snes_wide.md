# Aladdin (Super Nintendo): widescreen

How `src/game/snes_aladdin.cpp` (`AladdinWide`) widens the picture. Game logic and the RAM map: `re_snes_game.md`.
Extension per side (`wide_ext`, `src/app/settings.cpp`): CRT pixels 30 / 46 / 68 / 106, square pixels 52 / 72 / 96 /
140 for 16:10 / 16:9 / 2:1 / 21:9. `max_ext()` is 140.

## 1. When

A level is on screen while the main task waits in the level's main loop or its pause loop (task 0's saved return
address `$81:82E9` / `$81:83FA`), or the loop's top (`$81:82CF`, an exec hook) ran within the last 4 frames (a frame
the game's logic doesn't finish in leaves the task running and its saved stack stale). Everything else - logos,
title, OPTIONS, password, story scenes, the bonus wheel, continue, the ending and staff roll - is reported as no
gameplay and pillarboxed, and so is a level the game runs as an attract demo. STAGE CLEAR is drawn wide (below).

**STAGE CLEAR** (screen `$24`) is a panel - a gold frame 4 px inside the screen's edges, the title, the password's
portraits (sprites), the CONGRATULATIONS bar - on the backdrop, colour 0 (`$1806`, a dark purple). BG1 / BG2 (64-column
maps `$4800` / `$4000`) hold the level's leftovers past the panel's 32 columns and BG3 (32 columns, scrolled 252) its
own repeat: nothing of a wider picture. The app's plain bars didn't take it: the panel's 8-px edge strip holds the
frame's line, so it isn't plain (`plain_bar`). While the screen's task waits between the picture's set-up and the
fade-out after B (`$81:F7B6` .. `$81:F90C`, read at the frame's start, so a loaded state draws the same) the module
reports the frame as gameplay and hides every BG layer in the margins: they show the backdrop, so the purple fills
the width around the 4:3 panel and fades with it. The sprites are drawn there at their true x (the OAM builder's,
as in a level), which only the Genie reaches: he (object type `$8F`, `$83:FA45`) waits out of sight at x -32 (y 200),
flies right 4 px a frame for `$56` frames (`$40` counts them; the panel's task reads his x, `$A5`, and shows each
portrait as he passes it), is moved to y 128 at x 312 and flies back left as many (`$14` set), and is deleted at
x -32. His sprite is 48 px wide (x - 31 .. x + 17 facing right, x - 17 .. x + 31 facing left) and the draw range
test hides him from x 292 on: every end of his path lies just past the game's screen by his own width. In the sides
he so flies out whole instead of being cut at the 4:3 edge, but appears and goes beside the panel: he comes in from
beside its left edge, flies out to the right and goes there, comes back 72 px higher 12 frames later and goes as he
leaves its left edge. More of his path can't be shown: he waits without his draw flag and turns past the draw
range (drawing him there would change the game's OAM in RAM). His first frame drawn shows tiles his graphics' upload hasn't replaced yet (past the
4:3 screen's left edge in the game): the sides hide the sprites on that frame (`genie_loading`: his flight's loop,
`$83:FA8C`, first pass, `$40` >= `$54` as the frame starts). The other sprites (the gem row, the portraits'
sparkles, his trail) stay on the 256 px. The bonus wheel (`$2F`), the story scenes between stages (`$30`) and the
continue screen are on black: their black bars look the same as a fill. While visible none of them nor the password
screen (`$2C`) has a sprite past the 4:3 screen's edges (checked after every stage's last level, with and without a
Genie token, and on the password screen with input).

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
never goes - a first version stopped at 0 there and showed that column dimmed and mirrored), in a shaft (below), and
on an arena's screen when the camera is held in x while free in y (`$1A10` set, `$1A12` clear: Jafar, the Cave of Wonders' last event). The
view is centred on the camera and stops there; an arena's picture is centred on its screen but kept inside the level
where the level is as wide as the picture (the Cave's end: centred, it went a pixel past the level's end once the
offset closed); a span narrower than the picture is centred. That is where every layer can follow a view that stays
still while the game's camera moves (`can_stop`): BG1, and BG2 in every level's mode - at a known speed (`$083F` modes
0-8, the parallax shift) or as the game scrolls it itself (`$0A-$14`: raster bands, phases, an object; section 2). (A
first version kept the view centred in those seven levels, with the level mirrored and dimmed past their ends; before
that they stopped with the bands sliding against the still picture.) A mode outside those would keep the view centred.

**Shafts.** Where every page row of the game's screen (camera y to + 223) holds only the camera's own page column - the
pages beside it may not be entered (bit 15) or lie outside the level - the game's camera is pinned in x, and the span
is that column, 256 px, centred (`shaft`). Two in the game: Inside the Lamp 3 (level `$0A`, page column 0 from row 1
to its start in row 7; the corridor in row 0 runs right to x 3328) and the Cave of Wonders' drop (column 11, row 2,
between its upper rows and the treasure room's row 3; there the view is centred anyway). The pages beside a shaft hold
nothing of the level: the game draws a bit-15 page as page 0 (the tower's bottom), and VRAM beside the screen holds
that. So the sides show nothing of the tower's rooms: the tower stands as a shaft inside solid gold wall, its gold
pillars going on as that wall (below). Before, the tower took the level's extent (the corridor's), so the view
stopped at x 0: the tower stood at the picture's left edge with 212 px (106) / 280 px (140) of it mirrored and dimmed
on the right, its right wall doubled - the user's report from the tower at 21:9, "not centred and mirrored badly".
The span becomes a shaft's as the screen leaves the corridor's row (camera y past 256) and the corridor's as it
reaches it again.

The span changes only at an arena, when the level's width changes (Jafar's room) or into and out of a shaft. The view's
offset to its new place then closes only as the game's camera moves (at most 4 px a frame, and no more than the camera
moved; into or out of a shaft, where the camera moves only in y, its move in y counts too): the picture never pans by
itself while the game's screen holds still. Climbing out of the tower the picture glides from centred to the
corridor's start over the first ~30 frames of the screen's climb, and back as Aladdin drops in. (A first version
also stopped at the zones' camera limits and at pages the camera can't enter at its height and slid there by itself:
in play testing layers moved where the console's don't. `tools/re/snes_motion.py` measures each layer's motion from
the pixels and checks it.)

**Past the level's ends** on each line - its own page row's pages the camera may enter around the game's screen (a page
it may not, bit 15, the game draws as page 0), within the level's width - the margins show scenery beside the level,
not the level: BG2 mirrored about the level's edge on the line, at full brightness; BG1 (walls, platforms, props) and
the sprites are not drawn there, on the status bar's lines too (`Ppu::wideMirror` = BG2 only, `wideShowKeep` = BG3;
black instead with *Past Level Ends: Black*, `Ppu::edgeMirror`). Nothing there is a place to go, so nothing there
looks like one, and nothing needs dimming. Where a strip of the level at the edge hides BG2 on every line of its page
(`edge_cover`: BG1's tile columns from the page's edge in whose tiles draw every pixel in all 32 tile rows, cached per
frame), BG2 is mirrored about the strip's inner edge instead (`Ppu::wideMirrorLo/Hi`: the part of the span the mirror
takes its pixels from).

**Inside the Lamp 3's tower: a shaft in solid gold wall.** The tower stands between two gold pillars 32 px wide on BG1
(page column 0, rows 0-7; `edge_cover` finds 3 tile columns that draw every pixel, the fourth holds the pillar's dark
inner edge) with their ornaments (zigzag and scallop bands, a medallion with an orange diamond) across the pillar's
width. In the tower only (level `$0A`, on a line whose span ends at page column 0's edge with such a strip: the
tower's rows both sides, the corridor's row on its left, where the same left pillar runs up), the sides are that
wall going on (`Ppu::wideWall` = BG1, `wideWallL` / `wideWallR`: the pillar's outer edge on the line): the pillar's
outer tile column (8 px, plain gold at the pillar's outer edge) reflected back and forth outward from the edge over the
whole side, and nothing else there - no BG2, no sprites; the status bar (BG3) stays on top as on the game's screen
(where its right part sits over the right pillar too). The pillars keep their own shading and medallions at the
shaft's edges, and the wall beside them is flat gold with their bands running on as mouldings at their height (the
medallion's outer part as a row of plain hexagons, without the orange diamond): seamless, since a reflection joins
pixel to the same pixel. The wall is the line's own pixels, so it scrolls with the pillar exactly, and at the line's
own scroll (no level data the game doesn't show). Repeating the whole pillar was tried in mock-ups: every 32 px (a row
of shaded columns, the medallions side by side), or mirrored (64-px double pillars); both read as a busy colonnade
rather than a wall. With *Past Level Ends: Black* the sides are black (the wall is part of the mirror).
**The corridor** at the tower's top is real level data (its gold blocks carved with genie faces go on right to x
3328). The rule is per line: a line of the corridor's row shows the corridor on the right (inside the level) and the
wall on the left (past its left end); a line of the tower shows the wall both sides. As the view glides between the
tower and the corridor's start (above), each line's wall stays attached to its pillar, so the corridor's
floor comes down onto the wall's top on the right with no flash, pop or tear; at the corridor's start (the view
stopped at x 0) the left side is gone and the right side below the corridor is wall. In an in-between picture (high
frame rate) the wall's edge moves with the layer's own shift (`subHofs_`), so it stays joined to the pillar there too.
The corridor's right end (x 3328) keeps the background-only rule, as every other level's ends.
**The door shut behind Aladdin.** Once the camera reaches x 256 the game shuts the corridor's first page behind him:
a task (`$81:A363`, waiting for `$1774` = 1) writes `$FE` over the high byte of page (0, 0)'s entry (`$7F:F043`),
`$0E00` to `$FE00`, a page the camera may not enter, and from then on the camera stays at x 256 or more (the tower's
camera limit `$1A06` is already 0 there); a restart from the level's checkpoint (`$0360` set) shuts it as the level
loads (`$81:8B15`). The page's block map stays in RAM. Taken as the game's table has it, the corridor's first page
became past the level's end the moment the camera passed x 256: on the next line its gold block, its ledges and the
left pillar going up vanished from the left side (background only, mirrored about x 256), and the span's change
(its left end from 0 to 256) took the view's offset with it - the user's report from the tower's top at 21:9:
"moving right ... there's a graphical glitch on the left (platforms/area disappear)". The margins take that entry as
the page it was (`page_entry`: level `$0A`, BG1's page (0, 0) reading `$FE00` is `$0E00`; for the extent, the line's
run, the decode and `edge_cover`), so nothing changes when the door shuts: the corridor's start stays in the left
side as the camera goes on, and with the camera at x 256 the view stays centred with it on the left (the level, at full
brightness, as anywhere the game's camera can't go now). The game's own camera and streaming read the table as it is.
(`r3_tower.py`: the walk right along the corridor and back no longer moves the view with the camera still at x 256,
and every past-the-end pixel at the tower is the wall; the user's state, holding right, frame by frame: no change in
the left side as the door shuts.)
(Round 3 showed the tower's background there instead: BG2 mirrored about the pillars' inner edges, the pastel sky,
columns and clouds going on past them. The user, at 21:9: "for the vertical bit can we just extend the walls to the
sides in widescreen". Before that, the level itself was mirrored and dimmed: both pillars doubled, the genie statues,
pink platforms and vines mirrored into the sides.)
The other places with sides past the level (the mirror run against the black run, every level's start and end walks
and the play bot, 1,800 frames, at 140): Agrabah Rooftops' start (past the rock face that ends its row: before its
mirror image dimmed, now the pink sky band beyond the cliff), the Underground River's end (the top-left corner over
the rocks: the dark cave background), Jafar's room at 21:9 with square pixels (12 px a side, the room on BG2: the
room's picture mirrored at full brightness instead of two dim bands) and single sprites' pixels on the bar's lines in
Sultan's Dungeon 2 and Jafar's Palace 2. None has a strip (BG2 mirrored about the edge, as before but undimmed).

**Dimmed** only where the margins can't show what the console would:
- Where the game paints on its screen what the level beside it doesn't hold (`painted_row`): Jafar's cobra (level
  `$11`) sets the floor on fire by writing the fire into its room's map (`$4000`, on BG1 since the room was held:
  rows 21-31) in the 32 columns on its screen only, and in the same frame points BG2 at a spiked arch (`$4800`, rows
  22-28, mode `$12`: placed by an object). From that frame the margins are dimmed on the lines from the arch's first
  row down (line 163; the fire's starts at 167), where they show the room's stored floor; the wall above is the
  room's and stays bright. The dim comes in the frame the fire appears on the game's screen (frame by frame: neither
  before nor after). The fire's row is found from BG1's vertical scroll taken as signed: as the arch rises the room
  shakes a few lines above 0 (register 1016-1022). Read as 248-254, the row came out at -87 and the whole side,
  wall included, was dimmed in every shaking frame, blinking at about 7.5 Hz (`tools/qa` round 2, N2: 672 of 5998
  fire frames at 106; now none, the only full dims being the fight's fade-out).
- **Jafar's Palace 3's fight** (level `$10`, the user's request at 21:9: "maybe we should have the sides dimmed on
  this battle, and undimmed after so Aladdin can get to the exit"). Its event task (`$80:A5AC`) holds the camera at
  `$10A0` until Aladdin reaches x `$1190` on the floor; then it deletes every object, sets the zone's right limit to
  `$11A0` (`$1A42`), pans the camera there (`$083D` = 4) and holds it in x (`$1A10` = 1); Jafar (type `$98`, from
  `$84:A112`; 6 hit points) holds x and y too and fights in the throne room, Aladdin kept on the game's screen. Beaten
  (`$00BD`, `$84:A36F`), the event waits 32 frames and lets the camera go (`$80:A622`: `$1A40` = `$11A0`, `$1A42` =
  `$7FA0`, `$1A10` = 0): Aladdin walks on right to the exit at the level's end (the view stopped there: 116 px left /
  96 px right at 21:9 with CRT pixels). While the fight holds him (`boss_held`: level `$10`, `$1A10` set, `$1A42` =
  `$11A0`) both sides are dimmed, the bar spared, on every line (the corridor behind him and the hall on to the exit:
  nothing there is his to reach until he wins); the dim fades in over 16 frames as the event pans the camera and out
  over 16 frames from the frame the camera is let go (`fight_dim_`, 1/32 of the brightness a frame, `Ppu::wideDimAmt`:
  the PPU darkens by that many 32nds, rounded; 16 is the old half brightness exactly, the fire floor's). No flash: the
  sides' mean brightness steps 2-3 levels (of 255) a frame. Saved with the view (version 8); an older state from the
  fight opens with the sides dimmed.
- **Sultan's Dungeon 3's fight** (level `$0D`, the user's request: the same as Palace 3's). Its event task (`$80:A503`)
  sets the zone's right limit to `$900` and waits for Aladdin at x `$9F0` or more on the floor (y `$78` or more, a heart
  left); then it takes his control (`$08DE` = `$CB3B`, as Palace 3's), fades the music, sets the right limit to `$A00`
  and pans the camera there (`$083D` = 4); at `$A00` it closes the left limit too (`$1A40` = `$1A42` = `$A00`, no
  hold) and the boss (type `$69`, `$83:EA10`, waiting for `$00BE`) comes out and fights on the game's screen. Beaten
  (`$83:EADC`, then `$00BD`, `$83:EB1E`), the event waits 240 + 192 frames and opens the right limit (`$80:A59E`:
  `$1A42` = `$7FFF`, 432 frames after the win): the camera follows Aladdin again and he walks on right to the exit. The
  same treatment: the margins dimmed from the frame the right limit becomes `$A00` (the pan) until it opens, fading in
  and out over 16 frames, by the same `fight_dim_` and strength. Both fights are one table (`kBossFights`: the level,
  the zone's right limit while the fight is on, and whether the camera is held in x then too - Palace 3's is, `$1A10`;
  the dungeon's isn't), read by `boss_held`. Measured at 106 (a bot beats the boss by real play - walking at it,
  slashing, throwing, jumping; one hit from above - in some 450 frames): the sides' mean brightness in the fight 30.4
  / 31.5 (64.5 / 66.5 before; the game's screen 43.5 both ways), back to 64.5 / 66.5 over the 16 frames from the frame
  the limit opens, 2-3 levels a frame; the run's events on the same frames as before. Jafar's Palace 3's fight is
  unchanged, pixel for pixel (the fade-in from the event and the fade-out after Jafar, at 106 and 140).
- **The other held fights**, by the same measure: the Market at Dusk's boss (zone limits `$1A40` = `$1A42` = `$500`,
  no hold; once the boss is beaten the camera is let go only as Aladdin reaches the arena's right wall, and the game
  then walks him on to Jasmine with no input: no way on for the player to find), Jafar (level `$11`: an arena, `$1A10`
  set and `$1A12` clear, then the cobra and its fire floor; the level ends with him, no exit to walk to). They keep
  their sides bright (Jafar's fire floor dimmed as before).
- Everywhere else the margins show the level as stored, at full brightness, also where the game's camera can't go now:
  a zone's camera limits (`$1A40` / `$1A42`: the Market at Dusk's wait at `$400` and its boss at `$500`, the waits
  before Sultan's Dungeon 3's boss at `$900` and at the end of Jafar's Palace 3 at `$10A0`), an arena (the Cave of Wonders' end, Jafar's
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

**Windows: the others** (`clipped_windows`). The screen tasks' boxes clip their windows at the screen's edges too: the
"ABU NO!" box after the lamp is taken (a box around Aladdin, then the treasure, as the camera pans to it) had the
margin on the side it ran off continue its inside to the picture's edge for about 13 frames - the cave wall 88 px
(106) / 140 px (140) past the box's edge, then the rocks and the waterfall (`tools/qa` round 2, N3). In a level, a
window that isn't the beam's and doesn't cover the whole line (0-255) now ends at the screen's edge in the margins
(`Ppu::wideWin` with the registers' edges): the box keeps its 4:3 width with the margins outside it, as on the
console's screen. A window over the whole line (a full-screen effect, the box's first frame) still covers them.
`r2_iris2.py` frame by frame at 0 / 106 / 140: the box never reaches the margins; at 4:3 nothing is hooked.

A state made at another width (or the width switched on from 4:3) shows its first frame where the view stops already
(the level's extent at the camera), not centred.

**Save states and rewind** (the app saves one every 2 frames while rewinding is possible): `save_ext` (version 5)
carries the view (span, offset, whether it may stop; two words that held the dimmed range are written as 0 and not
read), the sprites' recorded x - both the latched set and the one being built for the next OAM DMA - how many frames
ago the level loop's top ran, so a state saved in a frame the game's logic didn't finish in still counts as a level,
the band routines' inputs (both snapshots, whether this logic frame's is taken, the ratchet's difference) and whether
a level is being played (section 1; an older state: a level index, no demo and the main task in the level loop),
and (version 5) the level objects deleted, swept or refused since the level's start, whose spawn is running, and the
Lava Cavern's eased limits (section 4).
Version 8 adds a boss fight's dim as it fades (`fight_dim_`, one byte at the end; an older state: dimmed if the fight
holds Aladdin, no fade). Sultan's Dungeon 3's fight uses the same byte (no new version).
Version 7 adds the camera's y and whether the span is a shaft's and its offset closes with the camera's y, in the two
words the dimmed range held (version 6 and older: not read, and the view starts where it stops now, as for a state
made at another width: the user's state from the tower opens centred).
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
  **Nothing the game has deleted comes back.** Every object's end goes through one delete (`$83:96E2`, DP the
  object): killed, collected, out of the keep range, swept, past the screen's edge. It clears the spawned bits (but
  a once-only object's), so the game's own spawner brings an enemy back when its edge crosses the column again -
  but the width change's spawn took every cleared object in its columns at once. In the Market at Dusk, widening in
  the half second after the arena's sweep (or F9 of a narrower state saved there) brought back the two `$4D`
  guards the sweep had just deleted. They took the sprite VRAM banks, the boss (`$57`) was refused at the spawn and
  at both edge-column retries and never tried again: the level could not end (`tools/qa` round 2, N1: 0->106 and
  46->106 at k = 0-24 frames after the sweep, 106->140 at 0). The module now counts the deletes (an exec hook that
  only reads, at every width - 4:3 too, so a width switched on later or a 4:3 state loaded wide knows them - reset
  by the level's start spawn, kept in states), and the width change's spawn skips every object deleted since the
  level's start (exec hook at the spawner's test `$82:A8C0`, while that spawn runs). With the play bot in all 19
  levels (900 and 1,800 frames, invincible, apples, then a switch with the camera still, 46->140, 0->106,
  106->140), objects deleted earlier in the level that the switch brought back: 33 / 28 before (guards `$20`,
  `$4D`, `$2F`, `$6D`, `$87`, ...), now 0.
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
  These original's columns leave out what the game deleted for good (killed, collected, swept in view): an enemy
  defeated just inside the screen's edge sat in the column before the original's edge and came back. They still
  take one that deleted itself past the 4:3 screen's edge or that the sweep took beyond the original's edges (the
  margins had it early: the exec hooks at `$83:973D` / `$83:974E` and `$81:9D8E` mark its delete), and any object
  deleted while beyond the original's spawn edges (x >= camera + `$120` or < camera - `$40`, read at the delete).
  Some types delete themselves by a range of their own from the camera's centre (`$1A44`): Inside the Lamp 2's `$79`
  (+-`$D0`, `$83:F639`) and Sultan's Dungeon 3's `$7D` (+-`$E0`, `$84:9103`). The margin's spawn deleted them in
  the next frame, and the first version of the "gone" rule counted that as for good: at 106 and 140 none of them
  came where the 4:3 game has them, only a frame or two at the margin's edge (Sultan's Dungeon 3: 38 `$7D`s on a
  carry route over 2,400 frames, Inside the Lamp 2: 9 `$79`s). They
  then came at the original's moment, as before that rule - inside the picture (their ranges are now widened: "Late
  in the picture" below). One whose list column is still beyond those edges at its
  delete is also marked for the retry below (`refused_pending`), so it is tried in the very frame the original's
  edge reaches its column: the edge-column retry runs only at the spawner's 32-px steps, and while the view is
  stopped at a level's start (the spawner's camera is the picture's middle, which doesn't move) there are none -
  Sultan's Dungeon 3's first two `$7D`s at 140 were lost that way.
  A once-only object (a boss, Jasmine, an item; list flags bit 7) refused by the allocator (`$82:A8E8`, carry set)
  is also tried again once a frame while its column is within the original's spawn edges (camera - `$40` ..
  camera + `$120`) and its row within the spawner's, its column run for it alone (a stub at `$60:8040` from the
  frame spawner's camera, `$82:A7AA`, back to its start `$82:A7A3`): whatever held the banks, he comes once they
  free (a 4:3 state of the arena loaded at 106 with the deletes unknown: the guards come back, leave the keep range,
  and the boss comes 90 frames in). The same retry takes an object held back from the margins (the pillars, below)
  or deleted beyond the original's edges with its column beyond them (above), but only once, in the frame its column
  reaches the original's edge, as the original's spawner tries it once there.
  (Stubs at `$60:8020` / `$60:8028`; `$82:A780` with Y = 1 runs one column; an exec hook after it takes the next from
  X, so a state saved mid-way needs nothing of the module's.)
- **Timed objects: the Lava Cavern's pillars** (type `$5B`, `$83:DF14`; seven in the level, list x 472, 3424, 3616,
  3760, 4112, 4264, 4411). A pillar is spawned hidden in the lava and runs on a timer from its spawn: it waits `$D3F8`
  + its list parameter frames (none with the parameter's bit 7), bubbles for 32 frames, rises (a second object, its
  top), holds for 128 frames, sinks and deletes itself (`JML $83:96CA`). At 4:3 it does this once: its column is
  spawned 32 px past the screen's right edge, so it rises as Aladdin gets there (the first at camera 280, x - camera
  192), and the camera, which only scrolls right, never brings its column back to the spawner's edge. The margin
  spawned it ext px (0.625 px a frame: ~170 frames at 106) earlier - the first one already at the level's start - so
  every pillar rose and sank in the right margin, ahead of the original by camera 85-160 px, and was gone when
  Aladdin reached it (the user's report at 21:9: "the pillar that comes out the lava doesn't return after it's
  appeared once"; it doesn't at 4:3 either, but there it rises in front of him). The margins' spawns (the game's
  own columns, the level start's, a width change's) now leave a `$5B` beyond the original's spawn edges unspawned
  and mark it for the retry above (the refused once-only objects' bits, kept in states): it is spawned in the frame
  its column reaches the original's edge (camera + `$120`), so it rises at the original's camera x. At 106 and 140,
  with Remove Slowdown off and on, all seven spawn and rise at the 4:3 run's camera x to the pixel (only the last
  ones' deletes come later, by the wide lifetime rule: they sink fully in the left margin).
  That first fix had one flaw: a pillar is not invisible while it waits. Its script (`$83:DF14`) sets animation 2 -
  its cap, a flat rock lying in the lava - and waits there (`COP` with A = `$83:D3F8` + its list parameter, flags &
  `$7F`: state 1, frames at object `+$01`, return address `$83:DF3A` at `+$03`; the runner counts it down at
  `$83:94BB` and runs the type's idle routine `$83:DFCD` meanwhile), then bubbles (animation 1, 32 frames) and
  rises. At 4:3 it is spawned 32 px past the screen's right edge, so its cap slides in at the edge and lies there
  a while before it rises. Held back to the original's spawn column (camera + `$120`, 74 px inside the
  picture's right edge at 106, 108 at 140), the cap popped into view in the middle of the margin, and the rise
  followed soon after: the user's report at 21:9 with Remove Slowdown on, "the boulders in the lava don't appear
  till the last moment". Now the margins spawn a pillar as the game would (at their own edge column, out of view),
  and the module holds its first wait instead (`waiting_`, kept in states): an exec hook at the runner's
  `DEC $01` skips the count (on to the idle routine at `$83:94BF`) while the object is a `$5B` at `$83:DF3A` and its
  column is beyond the original's spawn edges, and for one frame more - the frame a fresh pillar spawned at the
  original's moment runs its start (the wait set, not counted). The camera the hook reads, before the frame's
  scroll, is the one the spawner saw in the frame before, so the count starts exactly where the 4:3 spawn would
  have. A delete, or the allocator refusing the spawn, clears the bit (a refused one is created fresh at the
  original's edge by the retry above). The Lava Cavern at 106, the play route of `snes_lava.py` (Aladdin carried
  at the screen's middle): the seven caps come in at x - camera 374-401 (past the picture's edge, 362), the tops
  rise at camera 280, 3256, 3408, 3536, 3928, 4136, 4148 - the 4:3 run's to the frame with Remove Slowdown on,
  to the camera pixel with it off.
  **The Carpet Escape's rocks** (type `$83`, `$84:9538`, nine in its list) have the same flaw in their own way: one
  hangs from the cave's ceiling, waits (`COP` with A = `$84:D83A` by its parameter, return `$84:9579`) and falls; at
  4:3 it falls at x - camera 179-269, and the margins' earlier spawn made each fall 70-90 px ahead of the original,
  in the right margin (x - camera 261-330 at 106), before the carpet got there. The same hold covers it (a table of
  timed types, `kTimedTypes`): spawned at the margin's edge, hanging, it falls at the 4:3 run's camera x (66, 99,
  324, 918, 1317 on a run with no input). (Its two rocks with parameter bit 4 fall once Aladdin has passed them, no
  timer: untouched.) No other level's list has either type. At 4:3 the hook only reads (nothing is ever
  held there: the game's spawner has no columns beyond the original's), and a pillar or rock still held when
  widescreen is switched off is let go at the original's moment as well.
  The deleted-objects record wasn't involved: it only limits the module's own spawns (a width change's, the
  retries), never the game's spawner, and the original never spawns a pillar twice.
- **Late in the picture.** The user's report at 21:9 (CRT pixels, Remove Slowdown on, Jafar's Palace 3): "some
  enemies appear late, especially Iago the parrot". A bot survey (below) lists every object first drawn more than 16
  px inside the picture's side edges; at 4:3 the same test gives the original's own (an enemy that drops in, parts
  and shots: Sultan's Dungeon 1's `$28` / `$60`, ...). In the margins there were four causes:
  - **Iago** (`$38`; Sultan's Dungeon 1 and 2, Jafar's Palace 2 and 3) isn't in the level's list: a generator (`$97`,
    `$84:9F9F`; a timer, then the camera's centre within a range, two free slots) creates him at the camera's centre
    + `$90` (`$84:A05E` `LDA $1A44` / `STA $0C`, then `$81:AA81` with the offset at `$80:DBFF`), 16 px past the 4:3
    screen's right edge - 92 px inside the picture at 106 - with what he carries (`$53`, `$83:D58C`, held at his
    side). He flies left at 0.5 px a frame (`$83:BE38`, `JSL $81:A5A7`, speed `$80:BAC3`) until he is within 96 px of
    Aladdin (`$81:AD68`), hovers, drops it and flies off. The hook at `$84:A05E` answers the camera's centre + the
    picture's right side (left: - its left side), so he comes from the picture's edge; at 0.5 px a frame he would then
    take 2 * ext frames (212 at 106) to reach the original's place, so while he is beyond it (centre + `$90` for the
    camera now) he crosses the margin at 4 times his speed (an exec hook after his move, `$83:BE3C`), and from there
    on flies, hovers and drops as the original - from the user's state, holding RIGHT, he reaches the original's place
    27 frames after the original's spawn (at his own speed: about 42).
  - **Self-ranged objects** (`kSelfRanges`): the Genie balloons of Inside the Lamp 2 (`$79`, +-`$D0`, `$83:F63C`) and
    Sultan's Dungeon 3's moving platforms (`$7D`, +-`$E0`, `$84:9106`) deleted themselves in the margins, and only the
    original's moment brought them (camera + `$120`: the balloon popped in 77 px inside the picture's right edge at 106,
    the platforms 50). Their tests now take the picture's sides as the keep range's does, so the margins' spawn lives.
    A platform's place is a function of the level's time (`$1A2C`, from its list y), so it is where the 4:3 game has it
    from any spawn; a balloon rises from its spawn (`$83:F541`: at the camera's centre y + `$20`, a frame at a time,
    `BRK` returning to `$83:F58B`) and is held there as the pillars are (`kTimedTypes`: its wait at `$83:F58B` skipped
    until the original's moment) - it hangs in the margin and rises at the 4:3 run's frame and height.
  - **Sprite-bank starvation.** More of a level's enemies are alive in the wider view, so a margin's column can find
    the four sprite VRAM banks taken (Agrabah Market at 106: a guard `$20` refused while a pot thrower `$51`, 64 px past
    the picture's left edge, held the fourth); the edge-column retry then brought him at camera + `$120`, 74 px inside
    the picture. Now an object the allocator refuses at the margins' own columns (`starved_`, kept in states) is tried
    once a frame while its place is at the picture's edge or beyond it (up to 8 px in, within the margins' spawn
    columns), and while one waits, an object on the other side that the original no longer keeps (beyond its keep
    range, `$80:CE73`) and 32 px or more past the picture's edge is let go as the original lets it go (the keep test
    answers out; not a once-only object) - the guard came in the next frame, from beyond the picture's edge.
  - **The original's moment inside the picture.** The edge-column retry and the once-only retry of an object deleted
    beyond the original's edges spawn at camera + `$120` / - `$40`, inside the picture from ext 32 / 64 on: a guard
    that walked out of the wide keep range on the left came back 64 px inside the picture's left edge when the view
    went back. An ordinary object (not once only, not timed) whose place is more than 16 px inside the picture is now
    left to the margins' own columns there (`in_view`), as the original leaves its respawns to its edges; once-only
    objects (bosses, Jasmine, items) and the timed ones keep the original's moment.
  Survey (`late_survey.py`, the play bot of `r2_popin.py`, 2,400 frames a level, Remove Slowdown on, objects created
  within 48 px of another left out, the first 8 frames after a level's start or a life left out), objects first seen
  more than 16 px inside the picture and outside the game's screen: 46: 4 -> 0, 106: 9 -> 0, 140: 12 -> 1 (the Market
  at Dusk's boss at 140: Jasmine, out early, holds three banks when the arena's sweep has deleted him, and he comes when
  they free). Inside the game's screen, at every width as at 4:3: 7-11 (the original's own). The guard drawn below the
  floor in the user's shot is a beaten guard (`$2D`) falling off the screen, as at 4:3.
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
- **The Lava Cavern's limits** (level `$06`; widescreen only, a deliberate change of the game's rules). Its camera
  scrolls right by itself: camera mode `$083D` = 2 (`$81:C5C2`, table `$81:C5AA`) adds `$00A0` to the 24-bit camera
  `$1772` a frame (0.625 px) up to the level's last camera x (`$1A04` = 4352, the camera never moves in y). Three
  rules hold it to the game's 4:3 screen:
  - the scroll holds Aladdin at camera + `$F0` (`$81:C5DF`: `ADC #$00F0` / `STA $08E7`; skipped while `$1427` & 15):
    an invisible wall 16 px inside the screen's right edge;
  - the level's edge rule (`$145B` = 3, set at its start `$81:89E1`; the rules by `$145B`, `$81:E97D` -> table
    `$81:E986`: 0 every walking level, x held within camera + 16 .. + 240 (`$80:C9B9`); 1 the two carpet rides, the
    same plus y within camera y + 24 .. + 216; 3 the Lava Cavern; 4 a life being lost) kills Aladdin
    (`$83:8052`, the instant death the cheats don't cover) once he is 48 px past either edge of the screen
    (`$81:EA11`: `SBC #$0030` with a `BMI` for the level's start, `ADC #$0130`). Nothing pushes him: he stands still
    and is *left behind*. In the wide picture that happened while he was still in view (58 px inside the picture's
    left edge at 106, 92 at 140);
  - its objects (the falling lava and fire, types `$55`, `$54`, `$3C`, ...) delete themselves 32 px past either edge
    of the screen (`$83:9738` / `$83:9749`, shared by all levels): they vanished in the left margin.
  The hooks answer those immediates with the picture's edges for the logic's camera (`object_split`: left edge camera
  - extL, right edge camera + 256 + extR): the wall at the picture's right edge - 16, left behind 48 px past its left
  edge (or right edge), the objects deleted 32 px past it (only in this level). The view stops at the level's ends
  (section 3), so the wall stays inside the level: at the end (camera 4352, extR 0) it is camera + `$F0` = 4592, the
  exit's x (`$80:EC1E`: 4592, 104, within 8 px), as in the original. At 4:3 the hooks act only while easing (below).
  A width change moves the picture's edges at once, and the wall followed: Aladdin pressed against it went from
  x - camera 346 to 286 in one frame (106->46), and 46 more to 4:3, which could drop him off a rock into the lava
  (`tools/qa` round 2, N4). The limits now ease (kept in states): the wall (a clamp, `CMP $08E7` / `STA`) never
  comes in past Aladdin - it holds its place in the world while he stands beyond its new place, so the scroll
  (0.625 px a frame) brings it there, or follows him as he steps back; the left-behind limit comes in at most 2 px a
  frame faster than the camera, so a narrower picture never kills him on the spot; the one ahead stays at least 64
  px past the wall. Switched to 4:3, the hooks act until the limits reach the screen's, then run the original code.
  `r2_lava.py` switch, RIGHT held at the wall, 106->46->0->140->106->0: x - camera moves at most 2 px a frame
  (before: jumps of 60 and 46).
  The two carpet rides (the Carpet Escape: camera mode 6, 3 px a frame; A Whole New World: mode `$0A`) scroll by
  themselves too, but under rule 1: Aladdin is only held inside the 4:3 screen (16 px from its sides), never killed
  at its edges, and the rides' obstacles come in at the 4:3 screen's edge. Left as they are, but A Whole New World's
  objects take the Lava Cavern's lifetime rule: its gems (`$35`) flying past deleted themselves 32 px into the left
  margin, 74 px (106) / 108 px (140) inside the picture (`tools/qa` round 2, N5: 14 in 2,400 frames); now they leave
  by the picture's edges. (The Carpet Escape's objects don't use those tests.) `r2_vanish.py` at 46, 106 and 140,
  all 19 levels, 2,400 frames: no self-delete inside the picture anywhere.

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
- `snes_lava.py [widths] [turbo]`: the Lava Cavern at 0, 46, 106 and 140, Remove Slowdown off and on (section 4):
  RIGHT held, Aladdin stops at the picture's right edge - 16 (camera + 240 / 286 / 346 / 380); left standing,
  he is left behind 48 px past its left edge (to the pixel), the level starts again; carried to its end and walked
  right, he reaches the exit at 4586 and the level ends 236 frames after the camera stops (the Carpet Escape next),
  the same at every width; no object deletes itself inside the picture (24-37 a run do, outside it). Before: the wall
  and the death 106 / 140 px inside the picture, and every self-delete (34 / 32 a run) inside it. `snes_edges.py` and
  `snes_rewind.py` (106, 140, from the end state too) as before; `snes_motion.py` level 6: no BG2 or "still" frame,
  BG1 off in 2 frames of a life lost's fade instead of 1 (the wide run now plays differently from the 4:3 one with the
  same input), from the end state as before (the drifting lava bands' listed pixel counts are higher: more fire alive
  in the margins).
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
- Timed objects and the "gone" rule (section 4): every level object's lives (spawn and end camera x) at 4:3 and at
  106 / 140 with the same input (all 19 levels, 2,400 frames, Aladdin carried right; the auto-scrollers to their
  end). The Lava Cavern's seven pillars spawn and rise at the 4:3 camera x at 46, 106 and 140 (Remove Slowdown off
  and on); before, all seven rose 85-160 px of camera early. Inside the Lamp 2's `$79`s and Sultan's Dungeon 3's
  `$7D`s now live where the 4:3 game has them (spawned within 9 px of camera of its spawn there, the same end), at
  140 the Dungeon's first two too; before, none did. The rest of the list's differences are the carry route's (Aladdin
  stopped by a wall the wide run meets at another camera y, an idle `$2F` woken at the level's start). A width
  switch (46->140, 0->106, 106->140 after 900 and 1,800 frames of the play bot, all 19 levels) brings back no
  object deleted since the level's start. `r2_parity.py` (19 levels and the attract loop: 0 frames differing),
  `snes_attract.py 0 106 140t4` (0 RAM differences), `snes_dusk_end.py 0,106` (12 / 12 ended),
  `r2_dusk_window.py` (all ok), `snes_lava.py` (all ok), `snes_bands.py 106` (0 bad), `snes_rewind.py 140 all` and
  106 in levels 6 and 13 (0 differing): as before.

- Shafts and STAGE CLEAR (section 3; `tools/re/snes_edges.py` counts a shaft's frames apart and checks the view is
  centred there): from the user's state in Inside the Lamp 3's tower at 106 and 140 the view is centred
  (`view_left` = ext); carried up into the corridor, right, left and back down (1,200 frames) the view never moves by
  more than 4 px a frame nor while the camera holds still, and every rewound frame of that run and of STAGE CLEAR
  (through B and its fade) equals the straight run's at 106 and 140. `snes_edges.py` at 106 / 140: the tower's frames
  centred, the rest as before (Jafar's room 12 px a side at 140). `r2_parity.py` (0 frames differing), `snes_attract.py
  0 106 140t4` (0 RAM differences), `snes_bands.py 106` (0 bad), `snes_dusk_end.py 0,106` (12 / 12), `snes_lava.py`
  (0 bad), `snes_rewind.py 140 all` and 106 in level 10 (0 differing), `snes_motion.py 106 all`: as before.

- Past the level's ends (section 3), against the module before with the same input: from the user's state in Inside
  the Lamp 3's tower at 106 and 140, carried up the tower's open left side into the corridor's row and back down
  (1,200 frames, both glides), every pixel inside the level's extent is the same (the extent from the run with *Past
  Level Ends: Black*), and with that option on the pictures are identical in every frame; in the sides no statue,
  platform or sprite, no dimming, and no frame where they jump while the view glides (the share of side pixels that
  change from frame to frame: 0.24 / 0.25 on average at 106 / 140 against 0.38 / 0.37 before, at most 0.48 / 0.51,
  in the tower's vine section, against 0.62 / 0.64). The other places (above) at 140: as described.
  `r2_parity.py` (0 frames differing), `snes_attract.py 0 106 140t4` (0 RAM differences), `snes_bands.py 106` (0 bad),
  `snes_dusk_end.py 0,106` (12 / 12), `snes_lava.py` (0 bad), `snes_rewind.py 140 all`, 106 in levels 2, 5, 10 and
  18, from the user's tower state at 106 and 140 and over the climb above (0 differing), `snes_edges.py` 106 / 140
  (identical output), `hfr_test.py snes` (all passed). `snes_motion.py 106 all`: as before but in Inside the Lamp 3,
  BG1 1 / BG2 6 frames (140: 1 / 13; from the user's state: 1 / 4) against 0, every one a frame of a fade whose master
  brightness steps from the frame before (the level's fade-in, a life lost and the restart's fade-in): the shift
  measure isn't brightness-blind, and the sides at full brightness change more with the step than dimmed ones did
  (it hits its limit, 10 px). Measured on pictures scaled to their brightness the shift is 0 in all of them.

- The tower's gold wall (section 3): `tools/qa` round 3's `r3_tower.py` / `r3_pastend.py`, with a past-the-end pixel
  now passing either as background (no BG1 or sprites, = BG2 + BG3) or, in level `$0A` only, as wall (no BG2, no
  sprites, = BG1 + BG3), and every past-the-end pixel of a tower frame required to be wall: at 46 / 106 / 140 every
  tower frame's sides are wall, nothing else fails, and every other level's counts are as in round 3 (108 runs, the
  same pixels past the ends, 0 wall pixels outside the tower); the view's moves as before. With *Past Level Ends:
  Black* the sides are black. High frame rate, from a state below the corridor carried up and down through both
  glides (260 frames, 4 pictures a frame) at 106 / 140: on every tower line of every frame and in-between each side is
  an exact reflection of the 8 px at the pillar's outer edge (0 of 93,056 lines otherwise); `hfr_test.py snes` all
  passed. `snes_motion.py 106 10` from the start and from that state: 0 / 0 / 0 (before: BG1 1 / BG2 6, BG2 2 - the
  fade frames above, now on gold wall). `r2_parity.py` (0 frames differing), `snes_attract.py 0 106 140t4` (0 RAM
  differences), `snes_bands.py 106` (0 bad), `snes_dusk_end.py 0,106` (12 / 12), `snes_lava.py` (0 bad),
  `snes_rewind.py` 106 all, 106 from the user's tower state and 140 from the glide state (0 differing),
  `snes_edges.py` 106 / 140 (identical output).

- STAGE CLEAR's Genie (section 1): `tools/re/snes_clear_genie.py`, all seven STAGE CLEARs at 30 / 46 / 68 / 106 and
  52 / 72 / 96 / 140, each from its first frame against 4:3 with the same input through B to the next screen: the work
  RAM identical in every frame, the game's 256 px identical to the 4:3 picture, the sides plain backdrop with the
  sprites off, and with them every side pixel the Genie's (his box this frame or the last); he is in the sides on 56
  frames, up to 55 px into the left and 59 px into the right one (all of them at 30 / 46 / 52). The STAGE CLEARs all
  run his flight alike. His first frame (wrong tiles) is hidden with Remove Slowdown on and off; the app's shots at
  21:9 from that state show him whole in the sides from his second frame. `r3_stageclear.py` at 106 / 140: as
  before but for the sides holding more than one colour on 55 frames of each STAGE CLEAR (his flight). The bonus
  wheel (after the Market at Dusk, the Agrabah Market and the Underground River with a token), every story scene
  after a STAGE CLEAR and the password screen: no visible frame with a sprite past the 4:3 edges. `r2_parity.py`
  (0 frames differing), `snes_attract.py 0 106 140t4` (0 RAM differences), `snes_rewind.py 106 all`, 106 from the
  first frame of Jafar's Palace 3's STAGE CLEAR and 140 from the Carpet Escape's (0 differing), `hfr_test.py snes`
  (all passed).

With *High Frame Rate* on a 100 Hz+ display the PPU draws up to three in-between pictures with each line
(`Ppu::subFrames`, `subFrame`; ported from the Super Mario All-Stars port's PPU). Each BG layer's line is drawn again
with its scroll part of the way back to the last frame's on that line, as the picture shows it: the register plus the
hook's `bgHofsAdj`, less the left extension, so where the view stops at a level's end (the extension changing while
the game's camera moves) the picture holds still in the in-betweens too; layers the hook remaps (the status bar, the
centred texts) were placed already. Unlike All-Stars, which moved the drawn pixels and repeated the edge column in
widescreen, a layer is drawn again sampled at the in-between's place: `renderBg` takes the margin test and
`fetchWideTile`'s screen x there (and the module adds `Ppu::subDy` to the line's world y), so the margins are fetched
from the level's data where the layer is drawn and go along with the game's 256 px to the picture's edges. The raster
bands (Agrabah's `$0A` / `$0E`, the Lava Cavern's `$0C`) carry their band per line (`Ppu::lineBand`, set by
`beginLine` from `band_adj`): a band line moves with the last frame's line of the same band (where its content was, or
the nearest), and BG2's bands aren't moved vertically, so a band's edge stays with its content instead of taking a
neighbour's scroll for a line or two. Sprites go part of the way back, matched by their look (`hfr_match`); a zone's
sprites (texts placed on the picture) don't take the view's slide. Cuts are shown as they are: a jump over 32 px, a
line in forced blank or at brightness 0, another BG mode, map or width, a view jumping more than 32 px, a loaded state,
and the first picture after a module's redraw held the screen (`SnesSession::thawed_`). The game's state is untouched
(the range / time over bits stay the frame's). `tools/re/hfr_test.py snes`: RAM and picture identical with and without,
every frame; layer by layer the in-betweens at (1 - k/4) of the frame's move and the margins moved as the game's
256 px (Agrabah Market, the Market at Dusk, the Lava Cavern, the attract mode).

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
- STAGE CLEAR is widened with its plain backdrop only: the panel (its gold frame, the title, the CONGRATULATIONS
  bar) keeps its 4:3 width in the middle of the purple. Its maps hold nothing wider. Its Genie flies out into the
  sides at his real place, but only one sprite's width (up to 55 px left, 59 px right): the game starts, turns and
  deletes him just past its screen, so in the sides he appears and goes beside the panel (section 1).
- Inside the Lamp 3's tower is centred; the level holds nothing beside it, so the sides are its gold pillars going on
  as a flat gold wall (their outer tile column reflected outward, their bands running on). The game has no such wall:
  it is made from the pillar's own pixels. Past other levels' ends the sides are their background mirrored.
- Where Jafar's room grows and the camera is then held, the view keeps its place (the offset only closes while the
  camera moves), so the arena isn't centred in the picture; beside it the room is shown as stored.
- Jafar's cobra sets the floor on fire only on the game's screen (section 3): beside it the floor lines show the
  room's stored floor, dimmed, from the frame the fire appears to the end of the fight.
- Where the game's camera can't go (a zone's limits, an arena) the margins show the level and the objects the game
  has out there; ones it would only bring out with its camera there don't appear (the events' sweeps empty those
  areas on the console too).
