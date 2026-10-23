# Aladdin (Master System, SIMS 1994, Europe) — game research

ROM: `Disney's Aladdin (Europe).sms`, 512 KB, CRC32 `C8718D40`, product code 9027. Sega mapper; bank 0 at
`$0000`, bank 1 at `$4000` (never changed), slot 2 (`$8000`, mapper `$FFFF`, RAM copy `$DFFF`) switched freely.
The game runs at 60 Hz in the collection (NTSC timing; it reads the TV type at boot into `$C102`).

Tools: `tools/z80dis.py` (disassembler), `tools/re/sms_listing.py` (coverage-guided listings: `cov`, `list`,
`show <rom offset> [n] [force]`), `tools/re/sms_xref.py` (instructions using an address), `tools/re/sms_map.py`
(level maps), `tools/re/sms_ach_test.py`, `tools/re/sms_sound.py`. All addresses below are Z80 addresses; code in
banks 0/1 is also its ROM offset, other banks are written as ROM offsets (`$4B64C`).

## 1. Program structure

* Reset `$006B`: RAM cleared unless `$C100 = $5A` (warm start), VDP set up, then the main loop `$008C`:
  `jp table[$C200]` (`$092E`, word per mode, via `rst $18` = `$095E`: hl = word at hl + 2a). Each mode handler
  loops by itself; a mode change is `ld ($C200),a; call $0913 (fade out); rst $28` (`$0028`: restart the main loop
  with a fresh stack).
* Frame IRQ `$011D` (line `$C1`): reset button test, the line-interrupt setup (`$0259`: reg 0 | line IRQ, reg 10 =
  `$C499`, reg 9 = `$C4BE`, reg 8 = -`$C4A7`), the VBlank job `$C201` (`$016A`: 1 = `$0170`, 2 = `$0179`; the
  gameplay one uploads the sprite table `$02D9`, one of four jobs by `$C20E & 3` and the queued map column
  `$2E88`), then the PSG driver (bank 7 `$8000`) when `$C203` is set.
* Line IRQ `$01E9`: `jp ($C91D)`, a chain `$01F3` → `$0205` → `$0222` → `$023F` that loads reg 10 with `$C49A..C`
  and reg 8 with -`$C4A8..A` (parallax bands, section 3).
* Pause button (NMI `$0066` → `$0ABD`): toggles `$C205` in modes 2..$B.

| mode `$C200` | handler | what |
|---|---|---|
| 0 | `$4479` | intro story and title (script `$CF01`, table `$44DB`; `$12` = title menu, `$CF24` bit 0 = PASSWORD) |
| 1 | `$6A7C` | developers' menu (not reachable in the retail flow; cheat "Developer Menu") |
| 2, 3, $A | `$33B7` | engine A: stages 0, 1, 8 (Aladdin runs, the camera scrolls by itself) |
| 4, 5, 6, 8 | `$12FC` | engine B: stages 2, 3, 4, 6 (free scrolling, follows Aladdin) |
| 7, 9 | `$3840` | engine C: stages 5, 7 (magic carpet, the camera scrolls by itself) |
| $B | `$2641` | stage 9, Jafar (engine B scrolling with the boss's own code) |
| $C, $D, $E | `$6AB3` `$6AFA` `$6B42` | developers' sprite / animation viewers |
| $F | `$735D` | password screen |
| $10 | `$7307` | after NO on the game over screen |
| $11 | `$7270` | game over: TRY AGAIN? YES (restart the stage) / NO |
| $12 | `$758E` | ending and credits |
| $13 | `$5F8A` | story scene `$C11F` (36 scenes, script table `$5F42`); with `$C120` set a scene select first |

The ten stages (`$C105`, mode = stage + 2):

| stage | name (port) | mode | engine | music | password | after it |
|---|---|---|---|---|---|---|
| 0 | Agrabah Market | 2 | A | `$83` | AJGJ | scene 1 |
| 1 | Rooftop Chase | 3 | A | `$83` | LAEA | scenes 2-8 |
| 2 | The Cave of Wonders | 4 | B | `$84` | ASNF | scene 9 |
| 3 | The Treasure Room | 5 | B | `$84` | DMIA | stage 4 directly |
| 4 | The Lamp | 6 | B | `$84` | DIER | scenes $A-$B |
| 5 | Escape from the Cave | 7 | C | `$85` | INSI | scenes $C-$F |
| 6 | Prince Ali | 8 | B | `$86` | NEUA | scenes $10-$11 |
| 7 | A Whole New World | 9 | C | `$87` | AALG | scenes $12-$18 |
| 8 | Jafar's Palace | $A | A | `$88` | BLTO | scenes $19-$1C |
| 9 | Jafar | $B | boss | `$89` | UIAN | scenes $20-$23, then mode $12 |

Flow: power-on → mode 0 (intro, title) → GAME START: mode $13 scene 0 → stage 0 → … A stage is left through
`$35B8` (stage + 1, scene `$381E[stage]`, mode `$3829[stage]`) or `$1504` (the same, from the exits). Jafar is beaten
by 8 hits (`$C6AD`, `$27DE`): `$2787` starts scene `$20`. A death (`$C116` = 0, or a pit / the lava / a crash) waits
`$DC` frames and goes to mode $11; YES restarts the stage (checkpoint `$C121..$C128` if one was reached). There are
no lives: continues are unlimited. Passwords (`$7477`, ten 4-letter entries in bank `$0C` at `$A906`, letter =
0..25) start a stage; `AIQY` sets `$C12C` = `$FF`, which every stage copies to `$C467` (permanent invulnerability,
Aladdin flickers) and starts stage 0. Attract mode: the title runs out after `$3A0` frames and plays a demo
(`$C104` = its mode, recorded input `$34F8`).

Story scenes (`$13`) scroll their own picture with a text box on lines 176-191. Pressing both buttons at once
(`$66B7`: `$C207 & $30 = $30` sets `$CF92`) skips a scene `0-$1F`.

## 2. RAM map ($C000-$DFFF)

| address | size | meaning |
|---|---|---|
| `$C100` | b | `$5A`: warm start (RAM kept) |
| `$C102` | b | TV type found at boot (1 = 60 Hz) |
| `$C104` | b | attract demo: the demo's mode, else 0; `$FF` in stage 4 (the lamp chamber's ending) |
| `$C105` | b | **stage** 0..9 (10 after Jafar) |
| `$C107` | b | which attract demo is next (0..2) |
| `$C116` | b | **health** in half gems: 6 = three gems |
| `$C11F` | b | **story scene** for mode $13 |
| `$C120` | b | developers' scene select (mode $13 shows a selector) |
| `$C121`-`$C128` | | checkpoint: flag, camera X (w), V scroll, Aladdin X, Y, sub-area, object index |
| `$C12C` | b | password AIQY: invulnerability for every stage |
| `$C200` | b | **main mode** (table above) |
| `$C201` | b | VBlank job |
| `$C203` | b | sound driver running |
| `$C205` | b | paused |
| `$C206` / `$C207` | b | pad held / newly pressed: bit 0 up, 1 down, 2 left, 3 right, 4 button 1, 5 button 2 |
| `$C20E` | b | frame counter (bit 0: sprite table upload order, the game's flicker) |
| `$C2F0` / `$C2F1` | b | copies of VDP registers 0 / 1 |
| `$C300`-`$C33F` | 64 | **sprite table buffer**: Y (`$C0` = hidden) |
| `$C340`-`$C3BF` | 128 | sprite table buffer: X, tile pairs |
| `$C3C0`-`$C3DF` | 32 | palette buffer (uploaded at VBlank) |
| `$C400` | b | **Aladdin's screen Y** |
| `$C402` | w | **Aladdin's screen X** (kept in 0..255: `$10`..`$E8`; the camera follows between `$68` and `$78`) |
| `$C404` | b | facing |
| `$C405`, `$C406` | b | Aladdin's action / state (`$0F`: crouching to pick up, `$11`: throwing, …) |
| `$C41F` | b | **camera movement this frame** (signed px) |
| `$C435` | b | bank of Aladdin's sprite graphics |
| `$C438` | b | caught / dying |
| `$C439` | b | death timer |
| `$C43B` | b | type of the object Aladdin touched |
| `$C456` | b | **stones** to throw (0..9; the HUD counter) |
| `$C461` | b | fell into a pit |
| `$C467` | b | **invulnerability timer** after a hit (`$FF` = for good) |
| `$C480` | w | **camera max X** |
| `$C482` | w | **camera X** |
| `$C484` / `$C485` / `$C487` | | queued map column: flag, VRAM address, buffer pointer |
| `$C489` | w | map: bytes per metatile row |
| `$C48F` | w | map: page table (engines A/C) or map (engine B), in slot 2 = bank `$3306[stage]` |
| `$C491`-`$C495` | b | parallax band positions |
| `$C499`-`$C49C` | b | line interrupt intervals (`$FF` = none) |
| `$C49D` | w | metatile definitions |
| `$C49F` | b | sub-area of the stage (tables `$C4A1` max, `$C4BB` min, `$C4A5` stride, `$C4A3` map) |
| `$C4A0` | b | sub-area change in progress |
| `$C4A7`-`$C4AA` | b | the bands' H scroll (reg 8 = -value) |
| `$C4AB`-`$C4B6` | | stage pattern jobs (animated tiles, stage 7's sections) |
| `$C4B9` | w | **camera min X**, also the flat map's origin |
| `$C4BE` | b | V scroll |
| `$C518` | 160 | column buffer: 20 metatiles × 8 bytes |
| `$C680` | b | engine B: number of objects; engines A/C: next record of the stage's object list |
| `$C682` | b | next free sprite table entry while objects are drawn |
| `$C685` / `$C686` | b | camera movement for the objects |
| `$C687` / `$C689` | b / w | second figure (17 sprites from entry 20: a pursuer, Jafar…) Y / X |
| `$C698` | b | second figure in use (its type) |
| `$C69F` | b | first sprite table entry for objects (20, 21 or 37) |
| `$C6A6` | b | stage 7: section whose graphics are loaded |
| `$C6AB` / `$C6AC` / `$C6AD` | b | Jafar: knock-back timers / **hits taken** (8 = defeated) |
| `$C900` | w | stage frame counter |
| `$C912` | b | a stage is loading |
| `$C91B`, `$C915`, `$C917` | b | menu cursor and values (password: cursor `$C91B`, letter count `$C923`, letters `$C924`-`$C927`) |
| `$C91D` | w | next line-interrupt handler |
| `$CA00`-`$CDFF` | | **objects**, `$40` bytes each (7 for engines A/C, up to 16 for B) |
| `$CF00`-`$CFFF` | | title / scene scripts (`$CF01` title state, `$CF24` title cursor, `$CF92` scene skip) |
| `$DE00`-`$DF8F` | | PSG driver (section 5) |

Object (`ix` = `$CA00 + n * $40`): `+$00` screen Y (w), `+$02` screen X (w), `+$0C`-`+$11` animation,
`+$0F` frame data pointer (bank `$12`: count, then dy / dx / tile per piece), `+$12` first sprite entry, `+$13`
entries used, `+$14` finished, `+$15` type, `+$1D` awake (drawn, runs its code), `+$1E` sub-type, `+$20` moves
with the camera, `+$25`-`+$27` world Y / X (engine B), `+$28` camera X that wakes it (engines A/C).

## 3. Level maps, camera, scrolling

All stages scroll horizontally only (rows 2..21 of the name table = lines 16..175; rows 0-1 are the status bar,
fixed by reg 0 bit 6; rows 22-23 the black border, or the text box of a story moment). Camera X `$C482` moves by
`$C41F` per frame in `$2C98` (engines A/C: 1 px a frame in stage 0, 2 in stages 1/7/8, 3 in stage 5) or `$2BFC`
(engine B, from Aladdin's position, `$1935` / `$197F` / `$1F86` / `$1FBF`), clamped to [`$C4B9`, `$C480`]. For
every 8 px crossed one column is built (`$2DA7`.. `$2DD9` → `$2E3A`, 20 metatiles into `$C518`) and written by the
VBlank (`$2E88`) into table column (X / 8) & 31: the column entering on the right replaces the one leaving on the
left, which the blanked left column (reg 0 bit 5) hides. So the 32-column name table holds exactly the 256-px view.

* Metatiles: 16×16 px, 8 bytes = the TL TR BL BR name-table entries, at `$C49D + 8 * n`.
* Engines A/C (stages 0 1 5 7 8, `$2E19`): `$C48F` points at a table of page pointers, one per 256 px; a page has
  16 metatile columns, rows `$C489` (`$10`) bytes apart.
* Engine B (stages 2 3 4 6 9, `$2DEC`): one map at `$C48F`, `$C489` bytes per row, starting at world X `$C4B9`.
* The map is read with slot 2 = `$3306[stage]` (`$30CB`).
`tools/re/sms_map.py` implements both and checks them against VRAM: no mismatch in any stage.

Parallax: stage 0's floor (lines 160-175) scrolls faster than the camera (`$C491`, `$C492`); stage 1's sky and city
(lines 0-127) slower (`$C495`, `$C494`, `$C493`); stage 9 has a band from line 80. These bands repeat every 256 px.

Pattern animation: stage 0 rewrites a few tiles with the camera (`$30F6`, the brick wall), stages 2/4/9 animate
water / torches; stage 7 reloads tiles 259-374 for each of its five sections (`$31EE`, thresholds `$32A0`: camera
X `$400`, `$B00`, `$1400`, `$1B00`, lists in bank `$1A`).

## 4. Objects, sprites, collisions

* Engines A/C keep 7 objects (`$3D09`): `$410D` loads the next 8-byte record of the stage's list (bank `$12`,
  table `$90F2`: type, Y, X (w), sub-type, moves-with-camera, camera X to wake (w)); a waiting object wakes when
  the camera reaches its X (`$3E46`) at its preset screen X (mostly 256); it leaves (`$3EB8`) below x = -32.
* Engine B keeps all objects of the sub-area (`$3CAE`, loader `$4072`): awake while x is in [-64, 304) (`$3E1F`).
* Drawing: Aladdin (`$2390`, entries 0-16, X from `$C402`), the second figure (`$3C13`, entries 20-36), objects
  (`$0C45`, entries from `$C682`). Pieces off the screen are hidden (Y = `$C0`) or, for Aladdin and the second
  figure, parked at X = 0 under the blanked column. The sprite table is uploaded at VBlank (`$02D9`), in reverse
  order on odd frames.
* Touching an object `$3ECF` → `$3F58`: ignored while `$C467` is non-zero (except type 5), types `$17` `$1A` `$1B`
  take one half gem but never the last, others one (`$3F85`), type `$43` two (`$4A6F1`); with `+$2E` set Aladdin is
  caught instead (`$4015`). Carpet crashes (`$38A9` → `$3A3F`..) and pits / lava kill at once.
* Pick-ups (bank `$12`): stones `$4B160` (+1 up to 9), health `$4B87C` (+1) and `$4B887` (full).

## 5. Sound

PSG driver in bank 7 (`$8000`, called by the frame IRQ while `$C203` is set). Requests: `$DE06`, `$DE07`, `$DE08`
(the one with the highest priority in `$8C40` beats the playing sound's priority `$DE11`); `$81`-`$94` music (table
`$8CA7`), `$95`-`$D0` effects (`$8CCF`), `$D1` fade out, `$D2` stop. The game asks for a stage's music through
`$0D01` (b = id: `$DE06`, and starts the driver). The developers' sound test (mode 1, third line) plays `$81`-`$D0`.
Music seen in the game: `$81` intro story, `$82` title, `$83`-`$89` stages (table `$2637`), `$8A`-`$92` story
scenes (`$8A` 9; `$8B` `$12` `$13` `$1A` `$23`; `$8C` 4 `$20`; `$8D` `$C` `$D` `$16` `$1D`; `$8E` 7 8; `$8F` 6;
`$90` `$F`; `$91` `$10`; `$92` `$21`), `$94` defeat jingle; `$93` is never requested. Loop lengths measured by
`sms_sound.py` (driver state repeats).

Music player (module): power-on, 120 frames (the intro starts the driver), then the CPU waits in RAM
(`$C0A0`: `ei / halt / jr`) and the frame IRQ only runs the driver; requests go to `$DE06` / `$DE07` with `$DE11`
cleared.

## 6. Cheats (module, ROM patches)

| cheat | patch |
|---|---|
| Invincibility | the three `$C467` tests read `$FF`: `$3F5F` (hits), `$38A9` (carpet crashes), `$4B64C` (the boss) |
| Infinite health | no decrement: `$3F88`, `$3F76`, `$4A6F4` (`sub 2` → `sub 0`) |
| Infinite stones | `$2156` `dec (hl)` → `nop` |
| Developer menu | `$4BC4` `ld c,$0F` → `ld c,$01`: the title's PASSWORD opens mode 1 (stage select, sound test, the viewers, the scene select) |

## 7. Level select (module)

From power-on: button 1 skips the intro to the title menu (`$CF01 = $12`), DOWN selects PASSWORD, button 1 → mode
$F; ~170 frames later the password loop reads the pad: the stage's letters are written to `$C924`-`$C927` and
button 1 pressed (`$744D` checks them, sets `$C105` and the mode); the stage handler then loads (`$C912` set and
cleared). About 700 frames, 0.13 s headless.

## 8. Achievements (module)

Checked on RAM every frame (`achievement_frame`): a stage clear is `$C105` going up while the mode is a stage
mode; Jafar's defeat is mode `$B` → `$13` with scene `$20`; a run starts at GAME START (mode 0 → `$13`, scene 0)
or a password (mode $F → a stage); a game over is mode $11. `tools/re/sms_ach_test.py` drives the real flow
(stage exits through `$35B8`, Jafar's counter) and prints the events.
