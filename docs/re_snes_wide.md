# Aladdin (Super Nintendo): widescreen

How `src/game/snes_aladdin.cpp` (`AladdinWide`) widens the picture. Game logic and the RAM map: `re_snes_game.md`.
Extension per side (`wide_ext`, `src/app/settings.cpp`): CRT pixels 30 / 46 / 68 / 106, square pixels 52 / 72 / 96 /
140 for 16:10 / 16:9 / 2:1 / 21:9. `max_ext()` is 140.

## 1. When

A level is on screen while the main task waits in the level's main loop or its pause loop (task 0's saved return
address `$81:82E9` / `$81:83FA`), or the loop's top (`$81:82CF`, an exec hook) ran within the last 4 frames (a frame
the game's logic doesn't finish in leaves the task running and its saved stack stale). Everything else - logos,
title, OPTIONS, password, story scenes, STAGE CLEAR, the bonus wheel, continue, the ending and staff roll - is
reported as no gameplay and pillarboxed. The attract demos are levels and widen too.

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

A layer is drawn from VRAM instead where it doesn't follow its struct: a background scrolled in raster bands by HDMA
(`$083F` modes `$0A-$10`: Agrabah's skyline, the lava cavern's), or a fixed / fully loaded repeating picture - their
VRAM map holds the whole 512-px repeat, so the wrap is right.

**Parallax.** BG2's speed by `$083F` (`$81:C781`): modes 0 / 6 / 8 half the camera's x, 2 / 4 the camera's. Where the
view stops at a level edge while the game's camera moves on (below), a half-speed background would slide against the
still picture; it is drawn as if the camera were at the picture's middle: shifted by `(ratio - 1) * (ext - extL)` on
the whole line (`bgHofsAdj`, the line decoded throughout: `wideFetchAll`), which is 0 wherever the view is centred.
Before its first column and past what the game shows of it at the level's end (`$1A04 * ratio + 256`) it is mirrored.

**The Carpet Escape's wave** is a picture on BG2 (a 64x64 map, its first 256x256 screen; `$083F` = `$12`, an object
sets the scroll): in the margins only its own copy, at `[-x, 256 - x)`, is shown (none while parked at x = 256), not the
map's 512-px repeat.

## 3. The view

The picture is the game's 256 px plus `extL` / `extR` (`Ppu::setExt` at the first line; the PPU's `kMaxExt` is now 288
so one side can take both extensions, 2 x 140).

**Where the view stops** (`span`): at the level's ends, `[0, $1A04 + 256)`, and on an arena's screen when the camera is
held in x while free in y (`$1A10` set, `$1A12` clear: Jafar). The view is centred on the camera and stops there; a
span narrower than the picture is centred. Only where every layer can follow a view that stays still while the game's
camera moves (`can_stop`): BG1, and a BG2 drawn from the level's data at a known speed (`$083F` modes 0-8, with the
parallax shift of section 2). In the levels whose background the game scrolls in raster bands (`$0A-$10`: Agrabah
Market, Agrabah Rooftops, Market at Dusk, Lava Cavern, Jafar's Palace 3), by an object (`$12`, the Carpet Escape) or
itself (`$14`, Sultan's Dungeon 3), those bands would slide against the still picture: there the view stays centred on
the game's camera everywhere (every layer moves exactly as on the console) and the margins past the level's ends are
mirrored and dimmed.

The span changes only at an arena or when the level's width changes (Jafar's room). The view's offset to its new place
then closes only as the game's camera moves (at most 4 px a frame, and no more than the camera moved): the picture
never pans by itself while the game's screen holds still. (A first version also stopped at the zones' camera limits and
at pages the camera can't enter at its height and slid there by itself: in play testing layers moved where the
console's don't. `tools/re/snes_motion.py` measures each layer's motion from the pixels and checks it.)

**Dimmed** (`reach`): past where the game's camera can show now - the zone's camera limits `[min(cam, $1A40),
max(cam, $1A42) + 256)` (Market at Dusk's boss), an arena, the pages it may enter in the page rows its screen covers
(barrier pages, bit 15). On each line, past the level (the line's own page row's pages, the level's width) BG1 and BG2
are mirrored about the level's edge (`Ppu::wideMirror`, `wideDimLo/Hi`; black instead with *Past Level Ends: Black*).

**Save states and rewind** (the app saves one every 2 frames while rewinding is possible): `save_ext` carries the view
(span, offset, reach, whether it may stop), the sprites' recorded x - both the latched set and the one being built for
the next OAM DMA - and how many frames ago the level loop's top ran, so a state saved in a frame the game's logic didn't
finish in still counts as a level. `tools/re/snes_rewind.py` rewinds as the app does and compares every rewound picture
with the straight run's: identical.

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
- A width change mid-level (or a state made at another width) re-runs the level start's spawn at the next main-loop
  top (a stub in patch ROM, `$60:8000`: `JSL $82:A72C`, `JML $81:82CF`); objects already out are skipped by their
  spawned bits, so the new margins get theirs at once.
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
  `settings.ini` for 140 (`tools/qa/snes/app/script1.txt`).

## Known limitations

- Objects that only exist where the game's camera is (scripted events, bosses) appear as in the original; enemies
  can be seen waiting at the margins' edge before they wake (they act as on the console once in range).
- In the levels with a background scrolled in raster bands, by an object or by itself (Agrabah Market, Agrabah
  Rooftops, Market at Dusk, Lava Cavern, Carpet Escape, Sultan's Dungeon 3, Jafar's Palace 3) the view doesn't stop at
  the level's ends: there the margins show the level mirrored and dimmed (black with *Past Level Ends: Black*).
- Where Jafar's room grows and the camera is then held, the view keeps its place (the offset only closes while the
  camera moves), so the arena isn't centred in the picture; its unreachable sides are dimmed.
- The dimmed sides of an arena show the level's data, not what the game draws there during the fight (Jafar's cobra
  phase sets the floor on fire only on the game's screen).
