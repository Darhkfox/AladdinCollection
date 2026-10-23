# Aladdin (Master System) — widescreen research (E extra px each side)

Module: `src/game/sms_aladdin.cpp`. Game facts (RAM, engines, maps): `docs/re_sms_game.md`.
Scripts: `tools/re/sms_wide_shots.py` (contact sheets per stage and width; `--drive N` moves the free-scrolling
stages' camera on its own with a test patch at `$2BFF`), `tools/re/sms_wide_check.py` (the extension's background
against the hardware's own picture), `tools/re/sms_popin.py` (where objects first appear, objects the sprite
table had no room for), `tools/re/sms_map.py` (map model vs VRAM), `tools/re/sms_app_test.py` (the full app,
autotest). Widths: 6/22/40/74 (CRT pixels) and 26/44/64/102 (square); `max_ext()` = 102.

**Summary**
1. Background: the 32-column name table holds exactly the 256-px view; every other cell of the camera's band comes
   from the level map (`bg_entry` → `map_entry`), exact where both exist (checked against VRAM in every stage).
   Parallax bands repeat every 256 px and use the table. The old blanked column (x 0-7) is drawn from the map too
   (`hw_x0` = 8). Stage 7's reloaded section graphics are drawn from ROM copies at the sides (section 4).
2. The camera is the original's (Aladdin's position is an 8-bit screen X, so the 4:3 view can't move inside the
   level); world x outside what the original can show is masked (`post_frame`).
3. Objects live in screen coordinates: their wake / keep windows are widened by E (ROM patches); sprite pieces
   beyond the 8-bit table X get their true X from PC hooks on the three sprite writers (`sprite_x`); objects the
   64-entry table has no room for are drawn as extra sprites by the VDP.
4. Status bar (rows 0-1): the stone counter moves to the right edge (`line_hook`), the gems stay centred.
5. Not gameplay (every mode but the ten stage modes 2..$B): pillarboxed, the blanked left column drawn as the
   hardware does. In the stages, lines 176-191 (blank border / a story moment's text box) get colour 0 at the sides.

---------------------------------------------------------------------------------------------------

## 1. Background

* Camera: `$C482`. The VBlank writes reg 8 = -`$C4A7` (the first band) and the line IRQs the other bands;
  `on_vblank` keeps `$C482` as it is when the registers are written (module_state[4]), `on_frame_start` makes it the
  frame's camera (module_state[5]): the RAM value changes in the middle of the frame (the game's logic runs during
  the display).
* A line belongs to the camera's band when its H scroll (reg 8 at the line, the band IRQs have run) is -camera
  mod 256. Then a cell at unwrapped column `vcol` is world x = camera + hs + 8·vcol (camera + hs is a multiple of
  256) and playfield row `vrow - 2`. Other bands (stage 0's floor, stage 1's sky and city, stage 9's lower band)
  repeat every 256 px in the map, so the hardware table (wrapped) is right there.
* `map_entry` = the streamer's lookup (`$2E19` paged / `$2DEC` flat, metatiles at `$C49D`, slot 2 = bank
  `$3306[stage]`). `sms_map.check` compares it with the name table every frame: 0 mismatches in all stages.
* Rows 0-1 (status bar, reg 0 bit 6): blank beyond the 256 px (`name_entry(0, row)`); rows 22+ colour 0 (§5).
* Check (`sms_wide_check.py 102`, background only, every extension pixel at a world position the hardware region
  shows at another time): 0.0 % different in stages 1, 8, 9; 0.3-1 % in 2, 4, 6, 7; 2.7-5 % in 0, 3, 5. All
  differences are animated tiles (water, lava, treasure glitter) and stage 0's brick wall, whose patterns the game
  shifts with the camera (`$30F6`): the side shows them as they are now, which is right.

## 2. Camera and level edges

The camera is never moved: Aladdin's X (`$C402`) is an 8-bit screen position kept in `$10`..`$E8`, so a camera kept
E px inside the level would make its first / last E px unreachable. Instead the masks hide what the original
cannot show: world x outside [`$C4B9`, `$C480` + 256) (a flat map can end sooner: Jafar's room is 384 px wide):
`mask_left = clamp(min + E - cam, 0, E)`, `mask_right = clamp(cam + E - max, 0, E)`, lines 16-175. The
auto-scrolling stages (0 1 5 7 8) start with the left side masked until the camera has moved E px, and end with
the right side masked; the free-scrolling ones mask while the camera is at either end.

Boss arena — Jafar (stage 9): the original camera moves 0..136 over a 384-px room, all of it within Aladdin's reach;
past the room's ends there is no level. There the extension carries the room's pillars on (its middle part, x
128..255, repeats every 128 px: pillars at 148, 276, …; the hourglass at the right end is not repeated) and
`post_frame` reports those columns as `dim_left` / `dim_right` instead of masks: shown dimmed, as the Castle of
Illusion port shows its arenas.

## 3. Objects and sprites

ROM patches (`wide_patches(E)`, code caves in bank 1's unused zero fill `$7A00`-`$7A3F`):

| at | original | patched | purpose |
|---|---|---|---|
| `$3E50` | `ld hl,($C482)` | `call $7A00` | a waiting object (stages 0 1 5 7 8) that moves with the camera wakes at camera + E ≥ its X |
| `$4151` | `call $41B3` | `call $7A10` | ... and is loaded E px further right (`$410D`): same place in the level |
| `$3EC5` | `ld de,$0020` | `ld de,$20+E` | it leaves (next record takes the slot) at x < -32 - E |
| `$3E25` / `$3E29` | `ld de,$0040` / `$0170` | `$40+E` / `$170+2E` | stages 2 3 4 6 9: awake while x in [-64 - E, 304 + E) |
| `$3D86` / `$3D6B` | `ld (ix+$1D),0; ret` | `jp $7A30` / `$7A38` | sprite table full: skip the draw (see below) |

* Waking E px earlier with an E px shift keeps every object's world position; objects then run their code E px
  earlier (as in the Castle of Illusion port). Objects defined in screen space (`+$20` = 0: stage 7's flyers enter
  at x = -16 / -31, stage 1/8 screen objects) are left alone: they appear at their screen position, which can be
  inside the left side (a known limitation, see the end).
* A width change in the middle of a stage moves the waiting objects of stages 0 1 5 7 8 by the difference
  (`configure`, module_state[7] = the width they were loaded for).
* The original's allocators (`$3D74`, `$3D5E`) put an object that does not fit in the 64 entries to sleep, but the
  draw call after them still writes its pieces beyond `$C3BF` — over the palette buffer `$C3C0` (seen as a magenta
  palette in stage 8) — and a sleeping object of the queue stages stops following the camera that frame. With more
  objects awake this is common (stage 5 at E=102: ~150 object-frames in 1500). The patched path keeps the object
  awake and skips the draw; a PC hook on the cave marks it (module_state[6]) and at the VBlank the module computes
  its pieces (frame data in bank `$12`) into `SmsVdp::extra` — drawn after the table's 64, behind them.
* True X: PC hooks before the "off screen" test of each writer — Aladdin `$23C2`, the second figure `$3C63`
  (hl = the piece's 16-bit X, de = its table byte), objects `$0C99` (the table pointer on the stack) — record the
  piece's 256-px page (2 bits per entry) and, inside (-8 - E, 256 + E), clear H so the game stores the low byte.
  Pieces the original parks at X = 0 under the blanked column are marked hidden, and so are the pieces of an object
  parked at x = -64 (the keep window's left end) that does not move with the camera: Jafar's room keeps two there
  (types $0B, $44), never seen in the original, which a view wider than 16:10 would show. The VBlank upload hook `$02D9`
  turns the pages into the displayed order (reversed on odd frames: `$C20E` bit 0) in module_state[0..3];
  `sprite_x` adds ±256 or hides. Off the stages the hooks change nothing.
* Pop-in (`sms_popin.py`, objects whose first awake frame is inside the view, 1500 frames from the stage start):
  stage 5: 5 at E = 0, 40 and 102 — the designed ones (fire bursts out of the lava), at their place; stage 0: none;
  stage 7: 0 / 5 / 4, the screen-space flyers entering at x = -16. Objects drawn as extra sprites in those runs:
  stage 5 0 / 22 / 207 object-frames, stage 7 0 / 0 / 306.

## 4. Stage 7's section graphics

A Whole New World reloads tiles 259-374 (`$31EE`) when the camera passes `$400`, `$B00`, `$1400`, `$1B00` (table
`$32A0`; lists in bank `$1A`: source, VRAM address, tile count). Each section's cells use only its own list's
tiles, and no reloaded tile is used within 320 px after a threshold (the view while a list loads). So a side can
show a section whose tiles are not, or no longer, in VRAM — seen as garbage left of the city after the camera
passed `$400`. The module copies each section's tiles from ROM (`SectionTiles`) and returns those cells with
bit 15 set: `SmsVdp::alt_patterns` points at the section's copy (section of world x = the last threshold ≤ x - 256).

## 5. Status bar and other screens

* Rows 0-1, fixed (reg 0 bit 6): gems at columns 15-17, the stone counter (stages 2 3 4 6) at 26-29, PAUSE in row
  0. With `hud_anchor` the line hook moves x 192..255 of lines 0-15 to the right edge; the gems and PAUSE stay in
  the middle. There are no status sprites.
* `FrameInfo.gameplay` = modes 2..$B (the stages, also the attract demo); title, password, game over, the story
  scenes (mode $13), the menus and the ending are pillarboxed. With the core ignoring reg 0 bit 5 in widescreen,
  the line hook draws the blanked column (backdrop colour, no sprites) on those screens.
* In the stages lines 176-191 are blank rows in colour 0 (black; red in Jafar's room), where the lamp's chamber and
  other story moments put a text box: the line hook gives the sides colour 0, so the box stays 256 px wide in the
  middle and the border runs the whole width (sprites crossing it are kept).

## 6. Verification

| check | how | result |
|---|---|---|
| map model | `sms_map.check` every frame, all stages, 400-3000 frames | 0 mismatching cells |
| extension background | `sms_wide_check.py 102` | see §1: only animated tiles differ |
| object pop-in | `sms_popin.py 0 40 102` | §3 |
| sprites across the edges | sprite-only filmstrips (stage 3, 5) | smooth, no jumps at x = 0 / 256 |
| width changes | E 0 → 102 → 40 in stage 5 | waiting objects wake at 256 + E for the current E |
| screens | `sms_wide_shots.py` (6, 22, 40, 74, 102), `sms_app_test.py` (app, 21:9) | reviewed: stages, edges, HUD, scenes |

## 7. Known limitations

* Screen-space objects that enter from the left (stage 7's companions, x = -16 / -31) appear at that position,
  inside the left side of a view wider than 16:10.
* Objects run their code E px earlier than in the original (as in the Castle of Illusion port); the attract
  demos drift as a result.
* The extension shows animated tiles as they are now (stage 0's brick wall shifts its pattern with the camera:
  correct for the current frame).
