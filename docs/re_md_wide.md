# Aladdin (Mega Drive / Genesis, USA) — widescreen

E = extra pixels on each side (16:10 / 16:9 / 2:1 / 21:9: 32 / 54 / 80 / 128 with CRT pixels, 20 / 40 / 64 / 108
square). The module supports E up to 128. Screen x runs from -E to 319+E; VDP sprite X = screen x + 128.

Scripts: `tools/re/md_wide_util.py` (map access, the plane checker), `md_wide_demo.py` (review strips of every level
through the module, `md_out/wide_<E>.png`; `caves` disassembles the patched code), `md_shots.py` (one level, a
script of inputs), `md_wide_edges.py` (every level's start and right end, slid, with a per-frame check that the view
stays inside the level), `md_wide_spawn.py` (where objects appear, against the slid view), `md_wide_popin.py` (map
objects first seen well inside the slid view, and objects dropped for want of sprite tiles), `md_attract.py` (the
attract mode at every width, with and without cheats, against widescreen off), `md_apptest.py` (the real app's
autotest on level states: HUD, pillarboxing as the player sees them). `md_util.start_play` starts a level through the
module's level select and skips its story card (START) until the first frame of play.

**Recipe**
1. The planes are not patched: the map plane's columns outside what the game keeps valid are drawn per line from the
   level map in RAM (`line_hook`, §2). The Rug Ride (a 512-px loop, no map) keeps the VDP's own wrap.
2. Near a level's ends the picture slides (`view_shift`) so the widened view shows only what the 4:3 game shows; the
   game's camera is untouched. Plane B slides by what the level's own scroll routine gives for the moved camera; the
   slide follows the scroll line by line (§5).
3. Objects are part of the map and spawn when their block column / row is streamed: caves spawn the columns the slid
   view reaches too, the despawn window follows the slid view, an object short of an object slot or sprite tiles gets
   room from objects out of sight (the tile pool compacted), and the sprite clip covers ±2E (§3).
4. HUD sprites are moved to the screen edges and kept still while the view slides (`sprite_xoff`, §4).
5. Gameplay = a level fading in, or the gameplay or pause loop ran in the last 4 frames (PC hooks); Abu's bonus rounds
   and the Rug Ride are shown 4:3. The attract mode (title, demos) runs on the original ROM, cheats off (§6).

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
* **Spawn caves**: `$7300` works out the reach on each side from the camera (the slide s of §5, the same sum as the
  module's `view_slide`): kl = ⌈(E − s)/16⌉, kr = ⌈(E + s)/16⌉ block columns (0..⌈2E/16⌉). After the left writer's
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
    a shown frame): 0 everywhere but the Escape, whose lava rocks are spawned again over a rock already risen (an
    exact overlap, nothing changes on screen; the 4:3 game does the same at its edge). The QA's `t_popin.py` also
    counts objects born while a respawn is rebuilt behind black, ones thrown or dropped by others (Rooftops; as at
    4:3), and a first frame's stale position (the frame can end inside the spawn code). No object is dropped for want
    of tiles any more but one at Jafar's Palace at 21:9 (CRT): a knife thrown from the left extension that needs a
    larger run as it hits, when every object holding tiles is in view (and the same object drop as the 4:3 game's own
    in Jafar's Quarters).
* **Sprite clip** (`$1ABA0A`): map objects' pieces need VDP X in [0, `$1BF`) — now [−o, 448 + 2E) with
  o = max(0, 2E − 96) (`$1ABAD0` → `jsr $7180.w`: the compare runs on a biased copy on the stack, so the X written is
  the piece's own, negative left of X 0); screen-fixed objects (+6 bit 3) [0, 448 + 2E) (`$1ABA8C` → `jsr $71C0.w`).
  From E = 33 the module switches the VDP's 10-bit sprite X on during play; X 768-1023 are read as −256..−1
  (`vdp.cpp`), so pieces entering the view's left edge show even fully slid.
* The clip caves mark the entry: map objects clear bit 15 of the size / link word the builder writes (d1), screen-
  fixed ones set it (bit 7 of the SAT entry's size byte, which the VDP ignores) — `sprite_xoff` keeps those (messages
  such as FIND THE SCARAB, HIT STATUES, PAUSED) where the game puts them on the screen while the view slides.
* Caves: the `$FF` fill at `$69E0-$FFFF` is unreferenced and reachable with `jsr abs.w`; `$7000-$73FF` is used
  (left / right / row spawn `$7000` / `$7100` / `$7200`, clips `$7280` / `$72C0`, reach `$7300`, despawn `$7380`).

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

## 6. Gameplay and other screens

* Wide: frames in which the gameplay loop `$1A8C16` or the pause loop `$1A9230` ran within the last 4 frames;
  not after `$1A8E5C` (level over), `$1A8F82` (death screen), `$1A902E` (fell off the map), and not in the attract
  mode (below).
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
