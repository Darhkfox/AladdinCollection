# Aladdin (Mega Drive / Genesis, USA) — widescreen

E = extra pixels on each side (16:10 / 16:9 / 2:1 / 21:9: 32 / 54 / 80 / 128 with CRT pixels, 20 / 40 / 64 / 108
square). The module supports E up to 128. Screen x runs from -E to 319+E; VDP sprite X = screen x + 128.

Scripts: `tools/re/md_wide_util.py` (map access, the plane checker), `md_wide_demo.py` (review strips of every level
through the module, `md_out/wide_<E>.png`; `caves` disassembles the patched code), `md_shots.py` (one level, a
script of inputs), `md_wide_edges.py` (every level's start and right end, slid, with a per-frame check that the view
stays inside the level), `md_wide_spawn.py` (where objects appear, against the slid view), `md_wide_popin.py` (map
objects first seen well inside the slid view, and objects dropped for want of sprite tiles), `md_wide_stones.py` (the
Cave's stepping stones: copies per block and stones missing from the view), `md_wide_boss.py` (the idol fight's
framing: views at the arena's ends, lurches), `md_wide_carpet.py` (the carpet after the idol: when it spawns, shows and
picks Aladdin up, beaten beside either pedestal), `md_attract.py` (the
attract mode at every width, with and without cheats, against widescreen off), `md_apptest.py` (the real app's
autotest on level states: HUD, pillarboxing as the player sees them). `md_util.start_play` starts a level through the
module's level select and skips its story card (START) until the first frame of play.

**Recipe**
1. The planes are not patched: the map plane's columns outside what the game keeps valid are drawn per line from the
   level map in RAM (`line_hook`, §2). The Rug Ride (a 512-px loop, no map) keeps the VDP's own wrap.
2. Near a level's ends the picture slides (`view_shift`) so the widened view shows only what the 4:3 game shows; the
   game's camera is untouched. Plane B slides by what the level's own scroll routine gives for the moved camera; the
   slide follows the scroll line by line (§5). In a boss fight the slide frames the arena's centre (§5, Boss fights).
3. Objects are part of the map and spawn when their block column / row is streamed: caves spawn the columns the slid
   view reaches too, the despawn window follows the slid view, an object short of an object slot or sprite tiles gets
   room from objects out of sight (the tile pool compacted), and the sprite clip covers ±2E (§3).
4. HUD sprites are moved to the screen edges and kept still while the view slides (`sprite_xoff`, §4).
5. Gameplay = a level fading in, the gameplay or pause loop ran in the last 4 frames, or an iteration of the gameplay
   loop is still under way (PC hooks, §6); Abu's bonus rounds and the Rug Ride are shown 4:3. The attract mode (title, demos) runs on the original ROM, cheats off (§6).

## 1. Camera and streaming

* Camera `$7DF6` / `$7DF8`; the map pixel at screen (x, line) is (x + camX + 16, line + camY + 16).
  Plane A: VRAM `$C000`, 64×32 cells (reg 16 = `$01`), plane B `$E000`, H scroll table `$F000` (reg 13 = `$3C`,
  reg 11 = `$02`: one value per 8-line row in most levels, full-screen in some), SAT `$F400`. No window plane.
* The per-level scroll routine (header +`$34`, e.g. `$1AAA88`) writes plane A's H scroll -(camX + 16) for every row,
  plane B's from its parallax, V scroll camY + 16 (mod 256).
* Map in RAM at `$FF0000`: one word per 16×16 block (a byte offset into the block definitions at (`$7DBE`), 4 name
  table words TL TR BL BR); row pointers at `$9884` + 64 × row. `md_wide_util.check_plane` confirms the plane equals
  the map in every level but the Rug Ride (0 differing cells over 40 × 6 frames of walking / jumping per level).
* Streaming (after VBlank, `$1AAA2A`): the camera code (`$1AA8FA`) accumulates its X / Y moves in `$F0B2` / `$F0B4`
  and flags a column / row; the writers `$1AB34E` (left column), `$1AB44C` (right), `$1AB55A` (row below),
  `$1AB66C` (row above) move L = `$7E06` / Y = `$7E08` by 16 and write one 16-px block column (16 blocks: the whole
  plane height) or row (23 blocks). So the plane holds map x [L, L+368) and map y [Y, Y+256); camX = L + d with
  |d| < 16: the 4:3 view [L+16+d, L+336+d) always lies inside with at least 1 px to spare on the left, 17 on the
  right. The level start "scrolls" 23 columns right and back (`$1AA724`) to fill the plane.
* Clamps (`$1AA8FA`): left scrolling needs L ≥ 17 (camX ≥ 0), right scrolling camX + speed < W·16 − 353. The 4:3
  view therefore shows map x [16, W·16 − 18) at most (camX in [0, W·16 − 354]). There is no other camera lock: the
  camera follows Aladdin's screen position toward x `$70` / `$F0` by facing; bosses fight in normal map areas.
  Aladdin's walking limits are screen x 20..304 (`$1A9F2C` / `$1A9E4E`), so the camera itself must stay the original.

## 2. Drawing the extension from the map (`draw_map_ext`)

The widened, slid view needs up to 37 block columns; the 512-px plane holds 32 and the game keeps only 23 valid
([L, L+368)). Rather than widening the four writers (and the level-start fill), every scanline's pixels of the map
plane (plane A, or B when `$F165` swaps them in Sultan's Dungeon) that lie outside [L, L+368) are drawn in the VDP
line hook straight from the RAM map, with this line's own scroll and the frame's slide:
* map x at screen x 0 = the value ≡ −hscroll (mod 512) in [L − 128, L + 384), plus the slide; map y = Y + ((line +
  vscroll − Y) mod 256) — the same positions the VDP shows in the centre (`line_view`).
* block → name-table word → `VDP::pattern_row` (VRAM tiles, so animated tiles animate), with the priority bit.
The planes in VRAM are untouched, so nothing depends on how the view got there: width changes mid-level, loaded
states and the level-start fill need nothing extra. The picture on plane B is a 512×256 image the game scrolls
endlessly (header +`$1C`), so the VDP's wrap-around shows it right in the extension. The Rug Ride (`$F173`) never
streams: plane A is one 512-px loop of the lava cave and camX runs on; its "map" (15000 × 16) is nominal, so no
drawing there.

## 3. Objects and sprites

* Objects are map blocks: the block's word / 2 indexes an object-type table (`$AE87`, from header +8); a nonzero type
  spawns through the handler table `$4154` when the block's column (`$1AE3FC` left, `$1AE406` right) or row
  (`$1AE47E` / `$1AE488`) is streamed (`$1B526C`): x = `$7DB0` + `$F150` (L and −16 for the left column and rows,
  L and +`$150` for the right column, whose map x is L + 352), y likewise from `$7DB2` + `$F152`; the table entry is
  cleared (no duplicate) and restored when the object despawns. An object stored in column c
  appears at screen x 16c − (camX + 16): the 4:3 game spawns them at screen x −31..−1 (left) and 321..351 (right).
* **Spawn caves**: `$7300` is an `rts` on which the module's PC hook sets the reach on each side from the camera and
  the slide in force (§5, a boss fight's framing included; `reach_hook`): kl = ⌈(E − s)/16⌉, kr = ⌈(E + s)/16⌉ block
  columns (0..⌈2E/16⌉) in d0 / d1. After the left writer's
  own spawn (`$1AB446` `bsr $1AE3FC` → `jsr $7000.w`) the cave spawns columns L/16 − kl … L/16 − 1 as well (those
  ≥ 1), after the right writer's (`$1AB554` → `jsr $7100.w`) columns L/16 + 23 … L/16 + 22 + kr (inside the map):
  the spawner is entered past its set-up (`$1AE42C`) with its column x (`$7DB0`, scratch) and map pointer (a0) moved
  — L itself is never touched, since a frame can end inside the call and the view is worked out from L. The whole
  band each time, not only its far column: a block whose object couldn't get sprite tiles when its column came (see
  below) is tried again at the next column step, while still outside the view. A row spawns blocks [L/16 − kl,
  L/16 + 22 + kr] within the map instead of 23 from L (`$1AE4A8`: the loop's a0 / d6 / d4 set by `$7200`). Objects then appear the same 1-31 px outside the slid view as outside the 4:3 one
  (`md_wide_spawn.py`, checked against the slid view; what remains inside is what the original also spawns inside —
  thrown apples, projectiles, things placed left of their block, rows below / above). While the view slides, its far
  edge stays put in the map and so does the spawn column.
* **Objects that keep their map block.** Most spawn handlers spawn through `$1B526C`, which clears the block's type
  (`$FFAE87` + word / 2: no duplicate) until the object despawns. Three kinds go through `$1B52A0`, which leaves it
  (its only two callers: `$1B6614` for `$EF` / `$F4`, `$1B7338` for `$61`): the stepping stones `$EF` (template
  `$1B7FE4` in the Cave of Wonders' river, blocks 132/144/156/165/175 at row 59; `$1B7FD0`: the Escape's lava rocks)
  and `$F4` (block 61,58, only once the monkey statue `$35` has set `$F10E`), and `$61`, four apples from one block
  while Aladdin has none (`$EFE0` = "00"). A stone (object `$62`) sinks while stood on (`$63`, about 1 px every 3
  frames; Aladdin goes under with it at y 1200) and is cleared at the bottom (some 130 frames after it was stepped on),
  to come back the next time
  its column is streamed. The 4:3 game streams a column as it enters its plane window at an edge, just outside its
  view (and so puts a second stone over one still there when the camera crosses that edge back and forth: in the
  river, `$EF` at 2288 twice after a 16-px back-and-forth). The band of the spawn caves is spawned whole at every column
  step (for the retries), so such a block was spawned again at every step its column spent in the band: in a map-view
  pass back and forth over the river at 21:9, up to 10 stones on block 132 (x 2096), up to 17 objects alive against
  the 4:3 game's 7-10, and the stone at block 156 (x 2480) never spawned (no free slot: the duplicates were in view,
  not evictable) - the QA's screenshot of the river at 21:9, camera ≈ 2150: Aladdin on the stone of block 144, the
  ring on his left the leaping fish's (`$3E`, block 139), and nothing beyond the rock outcrop where the stone of block
  156 belongs (in the right extension). Stacked copies sink together (1 px apart), so they showed only as missing
  objects. Now (`spawn_call`, `spawn_keep`; with the widescreen ROM only):
  - a block whose object is alive (+`$32` = the block word / 2, +`$34` the type) is not spawned again: the column and
    row spawners' handler call (`$1AE46C` / `$1AE4E8`, `jsr (a4)`) is skipped (pc to the register restore after it);
  - at `$1B52A0`, a column spawn (the column spawner's return `$1AE46E` among the four longs on the stack; `$61` calls
    through a helper) outside a fill goes ahead only when the block's column (map x (`$7DB0` + `$F150` + 16) & ~15)
    lies entirely outside the slid view - the band's far column, as the 4:3 game's edge columns lie outside its view;
    otherwise it returns NE (as with no free slot). Rows (their spawns are the row's entry) and the fill
    (`$FFF175`: the camera runs 23 columns right and back, the whole band each time) are only deduplicated;
  - they are never evicted for room (they would return only at their column's next entry).
  The same pass over the river: one stone per block at 16:9 and 21:9, block 156 present, 15 objects at most. The
  Escape's lava rocks are no longer spawned again over a risen rock (`md_wide_popin.py`: 6 → 0 at every width).
* **Despawn** (`$1ADE84`): x outside [camX − `$50`, camX + `$190`] → [camX − `$50` − 16kl, camX + `$190` + 16kr]
  (`$1ADEB6` → `jsr $7380.w`, carry = out): the original's 80-px margin outside the view as slid. A wider window (±2E
  on both sides, as before) kept objects far outside the view alive, holding sprite tiles a new one needed.
* **Room for objects.** The widened view keeps more objects alive than the game was sized for, and two of its
  resources can run out where the 4:3 game's never do; an object that finds no room then shows up late, well inside
  the view (the QA's F1: Desert / Dungeon guards `$11`, Jafar's Palace statue `$3C`, the Market's apples at 21:9).
  * *Object slots*: 32 at `$FF7E40` (`$42` bytes); each kind looks for a free one in its own range (`$1AE262` slots
    3-22, `$1AE27A` 1-24, `$1AE292` 24-1, `$1AE2AA` 20-1, `$1AE2C2` 1-31, `$1AE2DA` 25-30: Z set and a5 the slot at
    their `rts`). None free: the spawn is skipped and its map block stays (it comes at a later column step).
  * *Sprite tiles*: every object takes a run of consecutive slots from one pool (`$1AD3E8`: `$74` one-byte slots at
    `$FFF008`, `$FF` = taken; slot k = 4 tiles at VRAM `$8600` + `$80`k, table `$11F500`; +`$29` the run's length −
    1, +`$2A` its first byte, +`$2E` its VRAM address). First fit, so the free slots get scattered (a guard needing 16
    can find 24 free in runs of 13 and 11); when no run is long enough the object is dropped and its map block put back
    (`$1AD406` `bsr $1AE6DE`).
  * The module's PC hooks (with the widescreen ROM only): at a failed free-slot search's `rts`, and at `$1AD406`. Room
    comes from objects the view doesn't show — alive, ones the game brings back from their map block (+6 bit 5,
    +`$34`), not screen-fixed (+6 bit 3), not never-despawned (+`$3C` bit 1), not in a0-a6, farther outside the slid
    view than the one asking (a slot search: past the spawn band) and either 32 px out (the spawn band reaches 31) or
    with no piece in the sprite table being shown — the farthest first, despawned the game's way (`$1AE0B0`,
    mirrored in C++: cleared, tiles freed, map block back, the same for its +`$3E` object). For tiles, once enough
    slots are free in all, allocations are moved to make a run (the fewest slots into free ones, else all packed to
    the start) and the asker gets it as `$1AD3E8` would (+`$2A`, +`$2E`, `pc = $1AD62C`). The allocation pass runs
    before the VBlank wait, while the frame is drawn, and the frame's uploads (a DMA queue at `$FF769A`, 14 bytes an
    entry, count `$FFEFEE`) and sprite table follow it, so a move copies the tiles in VRAM at once and points the
    sprite table being shown and the queued uploads at them (else the moved objects showed garbage for a frame).
  * `md_wide_popin.py` (the QA's walk through every wide level at every width, objects counted well inside the view on
    a shown frame): 0 everywhere (the Escape's lava rocks, spawned again over a rock already risen - 6 at every width
    before - no longer are: they keep their map block, see Objects that keep their map block). The QA's `t_popin.py` also
    counts objects born while a respawn is rebuilt behind black, ones thrown or dropped by others (Rooftops; as at
    4:3), and a first frame's stale position (the frame can end inside the spawn code). No object is dropped for want
    of tiles any more but one at Jafar's Palace at 21:9 (CRT): a knife thrown from the left extension that needs a
    larger run as it hits, when every object holding tiles is in view (and the same object drop as the 4:3 game's own
    in Jafar's Quarters).
* **Boss tune**: four object types start BOSS TUNE (50) from their spawn handler (`$4154`), right after spawning:
  `$18` → `$1B6C5A` and `$28` → `$1B6D1E` (Agrabah Rooftops' end; `$28` only while Aladdin's Y is in [`$2AD`,
  `$2F4`)), `$4D` → `$1B6CCE` (Jafar's Palace), `$F5` → `$1B6D90` (the Cave of Wonders' golden idol, block column 129,
  row 29). The 4:3 game spawns them as their block enters its plane window (columns L/16 … L/16 + 22, rows Y/16 …
  Y/16 + 15), so the tune starts as the arena is about to come into view. The widened spawning brought that up to
  128 px earlier at 21:9 and, through the widened rows, from places the 4:3 game never starts it: a player up on the
  ledges right of and above the idol's arena heard the boss tune as the camera came down a little and the widened row
  reached the pedestal in the view's bottom-left corner. Now a boss object spawned outside the 4:3 window keeps the
  rest of its handler (the object, its palette and animation, as the wider view shows it) but the handler's stop-all
  and start are skipped (hooks at the stop-all: `$1B6C64` / `$1B6CEC` / `$1B6D52` / `$1B6DA4`, resuming at the code
  after the start) and held in `module_state` [3..6] (slot, block column / row: save states and rewind keep it) until
  the block lies in the 4:3 window; then, right after the gameplay loop's streaming (`$1A8CE8`), the module queues the
  same commands (stop all; BOSS TUNE if the music option `$FFF57F` is on) - in the iteration the 4:3 game would have.
  Boss objects are never evicted for room (a respawn runs the handler again). `md_boss_tune.py cave`: the camera
  coming down right of the arena starts no tune at 4:3 or now at 21:9 (it did at camera 2169, 229 before); in from the
  left at the arena's height the tune is queued at camera 1716 at 4:3 and 1716 / 1724 at 16:9 / 21:9 (one map-view
  step; before: 1652 / 1588); the Rooftops' and Jafar's Palace's likewise within a step of 4:3.
* **Palette line 2**: several kinds of map object draw their sprites in CRAM line 2 and load their colours into it from
  their spawn handler (`$1B2650`, which keeps the source at `$FF726A` for the fades and the hit flash's restore,
  `$1ACDA2`; skipped while a level start or a restart fills the plane, `$FFF175`): `$23`-`$25`, `$2D`-`$2F`, `$4E`,
  `$4F` `$1292B2` (the Cave's bats and the like), `$F5`-`$F8` `$1292F2` (the golden idol and its pedestals), `$3E`
  `$129532` and `$3C` `$1294F2` (the same colours), `$4D` `$1295D2` (Jafar), `$8F` `$129612`, `$28` `$129152`, `$1F` /
  `$81` `$1295F2`. Whichever spawned last colours them all; the 4:3 game's levels are laid out so that it is the one on
  screen. With the wider spawning a kind could load its colours while another is on screen: in the idol's arena at
  21:9 (CRT), Aladdin moving left brings the bat at block (109, 36) into the spawn band (camera 1872; the 4:3 window
  never reaches it from the arena, whose left end is camera 1821) and the idol and both pedestals took the bat's dark
  red, yellow and magenta for the rest of the fight (the QA's report: bright yellow / magenta outlines). Now:
  - a map object spawned outside the 4:3 window whose colours differ from the line's, while an object of the kind that
    loaded the line's colours is on screen (a boss's colours, `$1292F2` / `$1295D2` / `$129152`: alive), has the
    handler's call skipped (a hook at `$1B2650` that checks the return address against the 13 spawn-handler calls,
    then returns): its load is held, and it is drawn in the line's colours meanwhile;
  - otherwise its load goes through but is noted as early, since the 4:3 game would load those colours later, possibly
    after another kind's: the Cave's leaping fish (`$3E`, the river under the arena) loaded early, a bat's held load was
    done at its 4:3 moment in the same frame, and the fish was shown in the bat's colours, partly in the 4:3 area, for
    20 frames (the QA's re-run of the arena, 21:9 from the ledges above);
  - right after the gameplay loop's streaming (`$1A8CE8`, with the boss tune), an entry whose object's block (found by
    its block word, +`$32` × 2, unique in every map) comes into the 4:3 window - when the 4:3 game would spawn it and
    load its colours - loads them if the line has others, and is done; then a held one whose line is free (no object of
    the line's kind alive; for a boss's colours, none on screen, so the boss doesn't come in in a bat's colours while
    that bat lingers in view) loads and becomes early. A held one whose colours the line has by then counts as early.
  - `module_state[7]`: three entries of 10 bits (slot, palette index << 5, bit 8 early); save states and rewind keep
    them. With no free entry, a load goes through as the game does it.
  - While a level start or a restart fills the plane (`$FFF175`) the handlers load no colours at all (they test it
    before `$1B2650`): the 4:3 game fills only its own window and loads a later spawn's colours when it comes. The
    widened fill spawns beyond that window, so such an object never loaded its colours: after a death in the river at
    21:9 the leaping fish (block 139,59) came with the restart and leapt in a bat's colours. Now an object of a line-2
    kind spawned during a fill outside the 4:3 window (`line2_filled`, right after the spawners' handler call,
    `$1AE46E` / `$1AE4EA`, their registers still on the stack) gets a held entry: its colours go in at its 4:3 moment or
    once the line is free.
  Checked (the entries read from save states): the arena at 21:9, in from the left and a 1,710-frame fight walking both
  ways - idol / pedestals in other colours on 1,671 frames before, 0 now; held: that bat (drawn in the idol's gold in the
  left extension, the only place it shows, 526 frames) and, coming in, the idol itself for 4 frames in the right
  extension while a bat was in view; from the ledges above, 26 frames (16:9: 1) before, 0 now, and the fish in other
  colours in the 4:3 area 20 frames at 21:9 before, 0 now. What that run still counts for the fish (E=0 / 54 / 128: 15 /
  35 / 12 frames) is Aladdin falling into the river: `$1A905E` → `$1B2784` points every line's source at an all-black
  palette (`$128ED2`, line 2 `$128F12`) and fades CRAM to it (frames shown 4:3 from the fall on, CRAM the fish's own
  colours a step darker each frame); the widened band spawns the fish earlier, so it is on screen in more of those
  fades. A map-view sweep of every camera row of the Dungeon, the Cave and Jafar's Palace at 16:9 / 21:9: line 2
  repainted under an object on screen by an object outside the 4:3 window 1 / 1 (that bat) before, 0 now. What remains
  is the 4:3 game's own: a load at its window's edge repainting a statue that only the extension shows (Jafar's Palace
  at 21:9), and the 4:3 game itself repaints the idol with the bat's colours when its camera reaches (1740, 440) with
  the idol alive.
* **Sprite clip** (`$1ABA0A`): map objects' pieces need VDP X in [0, `$1BF`) — now [−o, 448 + 2E) with
  o = max(0, 2E − 96) (`$1ABAD0` → `jsr $7180.w`: the compare runs on a biased copy on the stack, so the X written is
  the piece's own, negative left of X 0); screen-fixed objects (+6 bit 3) [0, 448 + 2E) (`$1ABA8C` → `jsr $71C0.w`).
  From E = 33 the module switches the VDP's 10-bit sprite X on during play; X 768-1023 are read as −256..−1
  (`vdp.cpp`), so pieces entering the view's left edge show even fully slid.
* The clip caves mark the entry: map objects clear bit 15 of the size / link word the builder writes (d1), screen-
  fixed ones set it (bit 7 of the SAT entry's size byte, which the VDP ignores) — `sprite_xoff` keeps those (messages
  such as FIND THE SCARAB, HIT STATUES, PAUSED) where the game puts them on the screen while the view slides.
* Caves: the `$FF` fill at `$69E0-$FFFF` is unreferenced and reachable with `jsr abs.w`; `$7000-$74FF` is used
  (left / right / row spawn `$7000` / `$7100` / `$7200`, clips `$7280` / `$72C0`, reach `$7300` (an `rts` the module
  hooks), despawn `$7380`, band refresh `$7400`, §5).

## 4. HUD

The status bar is sprites, written first into the RAM SAT (`$1AB7C4`): the lamp (`$E680` at VDP X `$92`), its smoke
(`$E68C` …), Aladdin's face and lives (`$E6EA`, `$E6E0`+digit, X `$90`/`$AA`, bottom), apples (`$E6F3` X `$18E`) and
gems (`$E6F7` X `$16A`) bottom right, the score (`$E7C0`+, X `$134`…, top right). Their tiles ($680-$6FF, $7C0-$7E7)
are used by nothing else (checked: every SAT entry of every level over 600 frames of play). `sprite_xoff` moves the
left group (lamp, smoke, face, lives digit) by −E and the right group (apple / gem icons and digits, score) by +E
(with `hud_anchor`; not in the 4:3 levels), and adds the view's slide so the HUD doesn't move with it.
`sprite_is_hud` keeps them over the edge masks. The groups go by tile, not by X: the smoke trail runs from VDP X
`$C2` to `$C2` + 8 × health, so with 8 health its last piece is at X `$102` — splitting at X 256 sent that piece to
the right side (a stray wisp of smoke near the top right at 21:9). Only the digits ($6E0-$6E9: lives at X `$AA`,
apples / gems from X `$17C`) go by X.

## 5. Level edges: the picture slides

The game's camera stays the original (Aladdin's walking limits are in screen coordinates). Instead, near a level's
ends the picture slides: before each line is drawn (`VDP::pre_line_hook`; line 0 at `MegaDrive::on_frame_start`) the
module takes the camera the line is scrolled with — from the map plane's H scroll in VRAM, as the camera in RAM may
already be the next frame's — and, when it differs from the one the slide in force was worked out for, sets
`VDP::view_shift` to s (and plane B's slide, below) from that line on:
* camX < E: s = E − camX (the view starts at the level's first pixel shown, map x 16, and Aladdin stands further
  left); camX > span − E (span = W·16 − 354, the largest camera X): s = span − E − camX; else 0. A level narrower than
  the widened view would be centred (s = span/2 − camX) with the rest black — none of the wide levels is (Jafar's
  Quarters, 800 px, is the narrowest). |s| ≤ E.
* While the camera moves inside those E px, s changes by the same amount the other way: the picture holds still
  (Aladdin walks across it) and nothing jumps when s reaches 0. Stateless: save states and width changes mid-level
  need nothing.
* Per line, not per frame: on a lag frame the game's VBlank work (scroll table, sprite table) runs while the next frame
  is being drawn, so the scroll changes partway down; a slide worked out once at the frame's start held the old camera
  against the new scroll and the held picture jumped 3 px for one frame (QA F3, Agrabah Rooftops / Jafar's Quarters).
  Sprites take view_shift as it is on their line too, so planes and sprites stay together.
* The Escape's earthquake: its scroll routine (`$1AB066`) adds a 0-3 px wobble to both planes' H scroll and writes it
  to `$FFF082`, which the sprite builder subtracts from every sprite X. The camera for the slide is the H scroll's less
  that wobble, so the slide stays put and the picture shakes as at 4:3 (taking it from the H scroll alone, the slide
  absorbed the shake and the sprites shook against still scenery: QA F4).
* Planes and sprites (game objects) move with the view; the HUD and screen-fixed messages are compensated in
  `sprite_xoff`.
* **Plane B** moves at its own rate (per-row parallax in most levels: 1/2, 1/4, 1/8, 3/8 ... of the camera, plus
  drifting cloud rows): shifted by s it would drift against its normal motion. The module runs the level's scroll
  routine (header +`$34`, `$FF7DA4`) on a sandbox 68000 over a copy of RAM, once with the frame's camera and once with
  camera + s; its VDP writes only fill a copy of the H scroll table. The difference, line by line, is plane B's slide
  (`view_shift_b`): the background then shows exactly what the game would show with the camera there, and holds still
  while the view slides (measured: plane A and plane B pictures identical between frames 20-25 px of camera apart in
  every level with a slide; only the drifting cloud rows change). In Sultan's Dungeon the planes are swapped: plane B
  (the map) takes s, and plane A (the picture) its own slide, re-rendered in the line hook.
* What is left outside the level after the slide (only in a narrower level) is blacked out per line with the VDP's
  `edge_mask_l/r` — sprites too, but not HUD sprites. There is no vertical extension, so nothing to do vertically.

**Boss fights** (`slide_target`, `boss_arena`, `boss_frame`). The 4:3 game never locks its camera (`$7DFE` is only
ever `$70` / `$F0`, §1): in a fight it follows Aladdin, who is held in the arena by its walls, and the arena is wider
than the 4:3 view. The Cave of Wonders' golden idol: Aladdin walks 2047-2305, between the pedestal the boss spawns
from (`$F5`, block 129,29; object `$8B` at 2048,704) and the other one (`$F6`, 143,32; at 2272,752), between which the
idol flies; the camera runs 1821-2178, so the 4:3 game shows map x 1838-2515 over the fight. Jafar's Palace: Jafar
(`$4D`, block 51,47) stands at x 800 in a hall Aladdin walks 477-1138 (camera 252-947). With the slide 0 the widened
view is the 4:3 view plus E on each side, so at either end of an arena it showed E px beyond what the 4:3 game ever
shows in the fight on the outer side (at 21:9 from camera 1821: map x 1709-2285, the bat country left of the arena, and
the idol on the far pedestal cut at the right edge; in Jafar's hall at the left wall: x 140-716, Jafar out of view).
* A fight is on while a boss object is alive - one of the four kinds whose spawn starts BOSS TUNE (§3: `$18`, `$28`,
  `$4D`, `$F5`), the game's own mark of a boss fight - and its tune isn't held back (it has come into the 4:3 window,
  when the 4:3 game starts the fight), with the camera at the arena's height (the 4:3 view within 112 px of its rows:
  the idol's pedestal lives on while Aladdin falls into the river 300 px below).
* Its arena: the boss's block and, within a plane window of it, the blocks of the kinds that share its colours (§3,
  Palette line 2: the idol's other pedestal); for spawned ones their objects' blocks. The centre, map x 8 × (first +
  last block column), is what a 4:3 camera of 8 × (first + last) − 176 centres: the idol 2000 (the middle of 1821-2178),
  Jafar 640 (his hall 252-947), the Rooftops' boss 2240.
* The slide then aims there: s = C − camX within [−E, E] and the level's limits (the view inside [16, W·16 − 18)) - the
  view holds still while the camera is within E of C and follows it beyond; past E + 320 from C (Aladdin leaving the
  arena) it goes back to the level's own slide over the next 2E px of camera (the view at 1.5 times the camera's pace).
  So the extra width is split evenly around the 4:3 framing of the arena where it can be. Idol fight: 21:9 shows
  1888-2464 for camera 1872-2128 and 1837-2413 / 1938-2514 at the ends (never beyond 1837-2514, what the 4:3 game shows
  in the fight); 16:9: 1962-2390, ends 1837-2265 / 2086-2514. Jafar at 21:9: 528-1104, ends 268-844 / 673-1249 (Jafar
  in view at both walls). At the end of Agrabah Rooftops the boss's arena is the level's last screen: the level's limit
  keeps the view where it was (21:9 unchanged; 16:9 holds on the last 428 px instead of following Aladdin 27 px short of
  the map's end).
* No lurch: a change of the fight state or arena changes the target at once; the difference is kept as a remainder
  `e` added to the slide and eased out (|e|/16 px a frame, 1-3; about a second). `md_wide_boss.py`: the framing
  starts at camera 1724 / 1732 at 16:9 / 21:9 (the tune's moment), the view never moves more than 3 px a frame further
  than the camera in, through and out of the fight (the boss removed: slide back to 0), at 1.5 times the camera's pace
  where it catches up after Aladdin leaves.
* The spawn band follows the slide (`reach_hook`), so objects still come in just outside the view. While a framing is
  in force (or easing out) the view can run ahead of the camera between column steps, so right after the gameplay
  loop's streaming (`$1A8CE8`, `jsr $1B315C`) the module pushes the return past it and two column ranges and enters the
  cave `$7400` (`band_refresh`), which spawns the two outermost columns of each band through the column spawner
  (`$1AE42C`, as the left writer sets it up) and then jumps to `$1B315C`; blocks already spawned are cleared or alive,
  so only what is missing comes.
* `module_state[1]` bits 2-23 (bits 0-1 are the developer flags): the fight bit, the arena's column sum and `e`; reset
  at a level's load and its fade-ins (start, restart). Older save states have 0 there: no framing, as before.
  Abu's bonus rounds and the Rug Ride have no boss; Jafar's Quarters' boss music is the level's own and it has no boss
  object (Jafar's magic is re-armed by the level routine): the 800-px level is its own arena and the level's limits
  already keep the view inside it.
* **After the fight.** The Rooftops' boss (object `$10`, or `$13` from `$28`) and Jafar (`$12`, then the snake `$11` put
  in his slot) end their level from the kill itself: `$FFF0E9` = `$20` / `$14` (`$1AC4A2` / `$1AED04`, `$1AC59A`), with
  nothing streamed in (killed at 4:3 / 16:9 / 21:9 from one state: Rooftops 30 / 31 / 31 frames from the kill to the
  level's end, Jafar 19 / 20 / 20). The Cave of Wonders' idol is different. Its tenth hit (`$FFF125` counts them,
  `$1AEAA8`) turns it into its death effect, sets `$FFF112` (`$1AEB36`) and starts the level's music again; the carpet
  that then takes Aladdin away (over the treasure and down to the river's bank, where the level goes on) is a map
  object, `$F9` at block (144, 29) right of the far pedestal, whose handler (`$1B6B5C`) spawns it (two linked objects,
  `$1B78A0` / `$1B78C8`) only once `$FFF112` is set; it then hovers there until Aladdin steps on it. Until then its
  column spawns nothing and the block stays, so the 4:3 game brings the carpet when that column next enters its plane
  window (`md_wide_carpet.py`: the idol beaten beside either pedestal, then walks of 900 frames):
  - beaten from the arena's left half (camera below about 1952, the column outside the window): the carpet streams in
    at the right edge as Aladdin walks right (spawned at camera 1952; standing still or walking left, never);
  - beaten from the right half (camera 2075 / 2109, the column inside the window and the carpet's place in view): no
    carpet standing still, walking right or walking left only; it comes once Aladdin has gone left far enough for the
    column to leave the window and turns back (90 frames left, then right: spawned at camera 1953, frame 116, Aladdin on
    it at frame 165). That is the original's own, and the widened view keeps it (21:9: frames 101 / 160; 16:9: 65 / 151).
  - The widened view shows the carpet's place from the left half as well (at 21:9 the whole arena), and its column lies
    in the spawn band, which is spawned at a column step only (in a boss framing also its two outermost columns each
    frame): the place stayed empty after the kill until Aladdin stepped right, and the carpet then popped in well inside
    the view (21:9: spawned at camera 1874-1886, about 150 px inside its right edge; standing still or walking left,
    never). Now the idol's tenth hit (a PC hook on `$1AEB36`, `module_state[1]` bit 24) has the next band refresh
    (`$1A8CE8`, `band_refresh`, in the same iteration of the gameplay loop) spawn both bands whole, as a column step
    does: a carpet whose column lies in the band comes with the kill, one inside the 4:3 window waits for its column as
    at 4:3. From the left pedestal (camera 1852-1854) the carpet now comes with the kill at 16:9 and 21:9 (16:9 did so
    before, through the band's outermost columns; 21:9 spawned it at frame 11 of a walk right and never standing still
    or walking left); walking right, it shows in the 4:3 part of the view at frame 20 / 19 and Aladdin is on it at frame
    59 (4:3: 21 / 68; 21:9 before: 16 / 63). From the far pedestal nothing changes. Nothing else comes with that spawn
    (the objects alive after the kill, from one state per width and spot, differ only by the carpet).
  - At 16:9 the band refresh's outermost columns reach column 144 near the arena's left end (camera about 1854): walking
    left from the far pedestal, the carpet appears at the view's right edge there and is despawned out of view some 25
    frames later, to come back at the next step right (the 4:3 game spawns it only on a step right).
  - The Escape's carpet at the level's end (`$FB`, block 618,21, handler `$1B6BB2`: only with Aladdin's x ≥ `$2646`)
    follows no boss; from one state at 4:3 and 21:9 it spawns on the same frame and the level ends on the same frame.

## 6. Gameplay and other screens

* Wide: frames in which the gameplay loop `$1A8C16` or the pause loop `$1A9230` ran within the last 4 frames;
  not after `$1A8E5C` (level over), `$1A8F82` (death screen), `$1A902E` (fell off the map), and not in the attract
  mode (below).
* Also wide: the frames of one iteration of the gameplay loop that takes longer than that. The game draws nothing new
  meanwhile and the picture holds as at 4:3. Hitting the Cave of Wonders' monkey statue `$36` (block 81, 25) or `$37`
  (87, 6) with the sword runs a scan of the whole level map (`$1AC386` / `$1AC3BC`: 14,400 words, clearing the blocks
  whose attribute is `$4F` / `$FE` resp. `$4E` / `$FD` - a wall elsewhere, for `$36` 101 blocks at columns 106-117, rows
  0-13) inside the iteration: 9 frames without the loop's top, from frame 5 on shown 4:3 (black bars for 5-6 frames at
  every width; the QA's report at 21:9). `$35` (38, 56) only sets a flag (`$F10E`; `$36` / `$37` set `$F10F` / `$F110`).
  The loop's top sets bit 31 of `module_state[0]` and the frame wait `$1B249E` (the loop's at `$1A8CD2`) clears it;
  while it is set, up to a second, the frames count as play. Seeded random play in every wide level (2 × 6,000 frames
  each, invincible) finds no other iteration over 4 frames and no other short 4:3 stretch in play.
* Also wide: a level's fade-in, from its call to the palette fade (`$1A8BFC` `bsr $1AE1A0` at a level's start,
  `$1A91B8` after a life lost) to the gameplay loop. The picture is already the level's, so it is shown wide and slid
  from its first frame; before, it faded in 4:3 with the game's own camera and jumped by the slide on the first frame
  of play (QA F2). Whether a frame is drawn wide is decided at its start and holds for the whole frame (the slide
  itself follows the scroll per line); `FrameInfo::gameplay` reports it as drawn.
* 4:3: logos, title, OPTIONS, the debug screen, story cut-scenes and title cards, the instructions, "Level
  Complete", the Genie's wheel, the death screen, continue / game over, the ending and credits; Abu's bonus rounds
  (a 336-px room: 8 px a side more) and the **Rug Ride**: its obstacles are sent in at the 4:3 view's right edge
  (screen x 320) on a timer — wider, they would pop up in the middle of the right extension, and sending them in
  earlier would change the ride.
* **Attract mode**: the demos replay recorded input (`$F57C` = 1, the recording at (`$F576`), table `$4B04`, five
  demos), so anything that changes the code they run - the widescreen patches' spawning, a cheat's patch or RAM write
  - changes their course. From power-on through the title routine and while a demo plays, the ROM is the original
  and no cheat writes RAM; the demos are shown 4:3. Told without state: the main entry (`$1A8A4A`) sets SP to
  `$FFEFD8` and calls the set-up and the title from that top level, so while they run their return address at
  `$FFEFD4` is `$1A8A58` … `$1A8B32` (the title's); the first call of a game overwrites it (before the main entry the
  word is still 0, power-on). The ROM switches at the first frame boundary where it is safe (`patches_safe`): between a
  game's start and its level lie the instructions and the story card, between the demo flag and the demo's level the
  title's fade. The cheats (Developer Cheat Mode's `$7E20` included) apply in a game only. `md_attract.py`: 40,000
  frames from power-on (the five entries of the demo table) at every width, with and without all cheats, 68000 and Z80
  RAM identical to widescreen off on every frame.
* Width changes mid-level: the patches change at a frame where no changed word is at the PC or on the stack
  (`patches_safe`); the extension is drawn from the map at once. Objects in the newly shown columns appear when the
  view next streams their column or row.

## 7. Checks

| check | how | result |
|---|---|---|
| plane = map | `check_plane(md, 0)` every 6 frames, 40 steps of play per level | 0 differing cells (all but the Rug Ride; death screens clear the planes and are 4:3) |
| ext 0 = original | module at ext 0 vs. the bare core, same state and inputs, 900 frames | RAM, VRAM and picture identical |
| spawns | `md_wide_spawn.py 20/54/80/108/128` | no edge pop-in left from the spawn rule (§3), checked against the slid view |
| level ends | `md_wide_edges.py 54/108/128`: every wide level's start, walking away, the map's right end (map view) and back | the view never leaves [16, W·16 − 18); no black borders, no jumps, no garbage |
| parallax while sliding | plane A / plane B rendered alone 20-25 px of camera apart inside the slide | both identical (cloud rows aside) |
| HUD | sprite extent of the top band at E = 20…128, health 8 / 7 / 5 / 2, anchoring on / off, and while sliding | the smoke trail stays one piece at the left (or centred); still while the view slides |
| every level, 16:9 and 21:9 | `md_apptest.py … 2,4` (the app) and `md_wide_demo.py 54 128` | reviewed: extension, HUD, slide at the level starts, sprites at both edges |
| 16:10, 2:1, 4:3 levels | `md_apptest.py … 1,3,4 1,8,3,5` | HUD in place in the 4:3 levels, wide levels right |
| caves | `md_wide_demo.py caves` | disassemble as intended |
| pop-in (QA F1) | `md_wide_popin.py 0,20,32,40,54,64,80,108,128` (the QA's walk through every wide level) | see §3 (Room for objects) |
| attract demos | `md_attract.py --cheats …` (40,000 frames from power-on at every width) | RAM identical to widescreen off every frame, with and without cheats |
| lag frames, shake (QA F3 / F4) | QA `t_tear_all.py`, `t_jitter.py`, `t_shake.py` | held picture still on every frame of the slide zones (Rooftops 10 → 0, Jafar's Quarters 4 → 0 frames moved at 21:9); the Escape shakes as at 4:3 (cliff edge 43/42/41/40 at a constant slide, sprites with it) |
| level start / respawn (QA F2) | QA `t_app_levelstart.py`, `t_app_respawn.py`; per-frame trace of a respawn | fade-in frames wide and slid, no jump into play; the masks open while the picture is still dark |
| statue hits (black bars) | the Cave's statues `$35` / `$36` / `$37` hit from both sides, 4:3 / 16:9 / 21:9 | 9 frames inside one loop iteration (`$36`, `$37`) as at 4:3; frames shown 4:3: 6 / 6 / 5 before at 21:9, 0 now |
| palette line 2 | the idol's arena in from the left and from above with a fight; map-view sweep of the Dungeon, Cave, Jafar's Palace | see §3 (Palette line 2) |
| stepping stones | `md_wide_stones.py 0 54 128` (map-view pass back and forth over the river) | at most 3 / 8 / 10 stones on one block and 0 / 9 / 17 views missing one (block 156, 165) before; 3 / 1 / 1 and 0 now (the 3 at 4:3 are the game's own) |
| river foreground | plane A alone at 21:9 at the QA's spot (camera ≈ 2150) against 4:3 renders of the same map pixels (same CRAM) | 126,720 pixels identical, 0 different: the flat patches under the water line are the map's own art |
| boss framing | `md_wide_boss.py 0 54 128`; Jafar's hall and the Rooftops' end walked wall to wall | views as in §5 (Boss fights); no frame moves the view more than 3 px further than the camera |
| after the bosses | `md_wide_carpet.py 0 54 128`; the Rooftops' boss and Jafar (both forms) beaten from one state at 0 / 54 / 128 | see §5 (After the fight): the carpet with the idol's kill where the band holds its column, else as at 4:3; the other two end their level from the kill, nothing streamed |
| idol arena colours | the arena runs of §3 (Palette line 2) at 0 / 54 / 128, split into frames shown as play and the death-fade / fill frames (line 2 on the all-black `$128F12`) | in other colours on shown frames: 21:9 from the left 526 → 0 (the bat), from above 31 → 0 at 16:9 and 91 → 0 at 21:9; what remains are fade and fill frames (the run now falls into the river on its second life, as at 4:3) |
| arena pop-in | `md_wide_popin.py`'s count on the idol arena in from the left / from above with a fight, and Jafar's hall | from above: 19 / 30 objects first seen inside the view and 4 / 20 dropped for want of tiles at 16:9 / 21:9 before, 9 / 7 and 0 now (rows coming in at the top on the flight, as at 4:3: 8); the rest as at 4:3 |
| save states | a state saved with a load held / in the middle of a statue's map scan / while a boss framing eases in, holds, eases out / on each of the 13 frames around the idol's kill (16:9, 21:9), run on in a fresh instance | identical to the run it came from (RAM, VRAM, CRAM, `module_state`, picture; the first frame after a load now has the 10-bit sprite X set as `post_frame` sets it - before, a sprite past VDP X 511 showed wrong for that frame) |
