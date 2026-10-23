# Aladdin (Mega Drive / Genesis, USA) — game logic

ROM `Aladdin (U) [!].bin`, 2 MB, CRC32 `ED427EA9` (header "ALADDIN", GM MK-1058 -00, August 1993). Virgin Games
(David Perry's team); the music and effects run on the GEMS sound driver.

Scripts (all in `tools/re/`): `md_util.py` (helpers), `md_xref.py` (absolute-address cross references in the code
areas), `md_levels.py` (the level select, level states), `md_cheats.py` (cheat checks), `md_sound.py` (sound bank,
request sites, jukebox renders), `md_ach.py` (achievement checks). `tools/m68dis.py md <addr> [n]` disassembles.
RAM `$FFxxxx` is written `$xxxx` below; ROM addresses are plain.

## 0. Program structure

* Code lives at `$200-$6A8` (boot), `$1A8A4A-$1B8xxx` (the game) and `$1E56C0-$1E5Axx` (the GEMS 68000 library).
  `$1A8A4A` is the main entry (power-on, and again after a game over): `$1AA344` (sound init, flags), `$1AA41C`
  (new-game state: level 1, play-order script), the title routine `$1B3B96` (logos, title, OPTIONS, attract demos),
  then the level loop.
* The VINT only sets `$7E1E`; the main code waits for it (`$1B249E`) and does all VDP work itself.
* Level loop: `$1A8B50` loads level `$7E26` (story/title card `$1B1486`, level init `$1AA484`), then the gameplay
  loop at `$1A8C16` runs until the level ends (`$1A8E3E`) or Aladdin dies (`$1A8F0C`). The pause loop is `$1A9230`.
  The port's module marks gameplay frames with PC hooks on `$1A8C16` / `$1A9230` and leaves gameplay at `$1A8E5C`
  (level over), `$1A8F82` (death screen) and `$1A902E` (fell off the bottom of the map). A level's palette fade-in
  (`$1AE1A0`, called at `$1A8BFC` when a level starts and at `$1A91B8` after a life lost, the level rebuilt at the
  restart point) counts as gameplay too, up to the gameplay loop.

## 1. RAM map

| RAM | size | meaning |
|---|---|---|
| `$7E26` | b | **level id** (table below) |
| `$7E21` | b | difficulty: 0 Practice, 1 Normal (power-on default), 2 Difficult (OPTIONS) |
| `$7E3C` | b | **lives**, one ASCII digit (`'3'`); start 5 / 3 / 2 by difficulty (`$1B0046`) |
| `$7E3F` | b | **continues**; set at the main entry from the difficulty (3 / 1 / 0, `$1AA45A`) — so the first game after power-on always has Normal's 1 |
| `$EFE0` | 2 b | **apples**, two ASCII digits (`"10"`); start 15 / 10 / 5 (`$1AA66E`); +1 `$1B0340`, −1 `$1B0360` (a throw) |
| `$EFE2` | 2 b | **gems**, two ASCII digits; +1 `$1B0394`, −1 `$1B03BE` (spent at the peddler); 0 at a new game and a continue |
| `$EFFA` / `$EFFB` | b | **health** (the lamp's smoke, 8 at each level start) / maximum (8) |
| `$7E29` | 6 b | score, ASCII digits; `$7E30` high score (`"100000"`); `$F14E` points still to add |
| `$7DF6` / `$7DF8` | w | **camera X / Y**; the map pixel at screen (0, 0) is (camX + 16, camY + 16) |
| `$7DFA` / `$7DFC` | w | Aladdin's position on screen (the camera follows it toward `$7DFE` / `$7E00`: x `$70` facing right, `$F0` facing left) |
| `$7E02` / `$7E04` | w | **Aladdin X / Y** in the map (camera + screen position; also object 0's +2 / +4) |
| `$7E06` / `$7E08` | w | streamed camera X / Y: the plane holds map x [L, L+368), y [Y, Y+256) (docs/re_md_wide.md) |
| `$7E40` | 32 × `$42` | **object table**; slot 0 is Aladdin (`$83` while he is in play). +0 type/flags, +2 X, +4 Y, +6 flags (bit 3 screen coordinates), +9 / +`$35` H / V flip, +`$14` sprite frame, +`$20` animation script, +`$32` / +`$34` the map block it was spawned from and its object type |
| `$7DB6` / `$7DBA` | w | map width / height in 16-px blocks; `$7DBE` block definitions; `$9884` row pointers (one long per 4 px of map y) |
| `$F0E9` | b | **level over**: countdown to the next level (`$FF` at once) |
| `$F0E6` | b | Aladdin dying: countdown to the death screen (`$FF` at once) |
| `$F0F2` | b | invulnerability after a hit (`$28` frames); Aladdin blinks |
| `$F158` | b | `$FF` while paused (`$F168` is the pause loop's START latch) |
| `$F003` | b | Genie tokens: one spin of the Genie's wheel each after the level (`$1B16E0`, not after the Sultan's Palace) |
| `$F176` | b | Abu token found: the play-order script's bonus entries are taken |
| `$F572` | l | **play-order script position** (ROM `$4082`, see §2) |
| `$F57C` | b | 1 during the attract demos (table `$4B04`: input recording + level, 5 demos) |
| `$F57D` / `$F57F` | b | sound effects / music on (OPTIONS) |
| `$F165` | b | planes swapped: the level map is on plane B (Sultan's Dungeon) |
| `$F173` | b | Rug Ride mode |
| `$F006` | b | Rug Ride crashes: the first two cost a life, the third ends the ride (`$1A8F5A`, the game's mercy) |
| `$7E20` / `$7273` / `$7E25` | b | debug screen options: CHEAT MODE, MAP VIEW MODE, FREEZEABILITY (§3.2) |
| `$7E28` | b | frame counter of the gameplay loop |
| `$7276` | l | position in the current button code (`$4128` pause code, `$413A` OPTIONS code) |

**Bosses** have no variables of their own: a boss is an object in the table (Jafar's Quarters: the level routine
`$1B62B6` re-arms Jafar's magic — slot 1, template `$1B8278`, at (`$184`, `$166`) — every 128 frames). The camera
always follows Aladdin (no locked arenas): `$7DFE` / `$7E00` are only set to the facing offsets, and the clamps
are the map's own (§2).

## 2. Levels and the play order

Level header table at `$2C78`, `$42` bytes per level id (`$1AA4B0`): +0/+2 camera X/Y, +4/+6 Aladdin's screen
position, +8 object attributes (compressed → `$AE84`), +`$0C` tiles, +`$10` map (→ `$FF0000`), +`$14`/+`$18` DMA
tile animation list, +`$1A` **music**, +`$1C` plane B picture (512×256, → VRAM `$E000`, or `$C000` when swapped),
+`$24` block definitions, +`$28` level init routine, +`$2C` per-frame routine, +`$30`/+`$32` map width/height in
blocks, +`$34` scroll routine, +`$40` planes swapped. Names: the developers' debug screen (`$1265B5`).

| id | level | map (blocks) | music |
|---|---|---|---|
| 1 | Agrabah Market | 300 × 45 | 73 Prince Ali |
| 2 | Abu in Agrabah (Abu's bonus round) | 21 × 18 | 15 Arab Rock 1 |
| 3 | The Desert | 450 × 32 | 38 Camel Jazz |
| 0 | Agrabah Rooftops | 155 × 88 | 20 One Jump Ahead |
| 4 | Sultan's Dungeon | 145 × 73 | 82 Arabian Nights |
| 5 | Cave of Wonders | 222 × 64 | 35 Gloomy Tune |
| 6 | Abu in the Cave (bonus round) | 21 × 18 | 15 |
| 7 | Escape from the Cave | 654 × 22 | 16 Arab Rock 2 |
| 8 | Rug Ride (auto-scrolling; nominal map 15000 × 16) | — | 29 Rug Ride |
| 9 | Inside the Lamp | 448 × 32 | 27 Friend Like Me |
| A | Sultan's Palace | 200 × 69 | 22 Turban Jazz |
| C | Jafar's Quarters | 50 × 22 | 50 Boss Tune |
| B | Jafar's Palace | 200 × 70 | 15 |

**Play order**: power-on sets level 1 and `$F572 = $4082`; a finished level reads pairs (command, level) from there
(`$1A8E86`): 0 = play the level; 1 = only with Abu's token (`$F176`, after the Abu intro `$1B50EE`), else skip;
2 = only without `$F005`; `$FF` = the ending (`$1B4F7C`). Script: [1,2] [0,3] [0,0] [1,2] [0,4] [0,5] [1,6] [0,7]
[0,8] [0,9] [0,A] [0,C] [0,B] [FF] — Market, (Abu), Desert, Rooftops, (Abu), Dungeon, Cave, (Abu), Escape, Rug Ride,
Lamp, Sultan's Palace, Jafar's Quarters, Jafar's Palace, the ending ("Level Complete", the carpet ride, "The End",
credits). Each level has a story cut-scene / title card first (`$1B0F8E`: music 84 / 85 / 86).

**Level select (the port)**: power-on, 30 frames, then `$7E26` = level, `$F572` = the script position after the
level's own entry, `$7E21` = difficulty, and START is tapped through the title and the new game's instructions
screen (`$1B2E9A`) until the level loop begins (`$1A8B50`): the title routine keeps the level (the developers' debug
screen sets its START ON LEVEL the same way). `start_level` in the module returns there (about 160-190 frames,
START released in the hook so the held button doesn't skip anything): the game then shows that level's story
cut-scene and title card as after the level before it — the player skips them with START or watches them run out
(Agrabah Market's is the game's long opening story) — and starts the level. Lives, apples and gems come from the
difficulty as at a new game (`$1B0046`, `$1AA66E`); continues stay as power-on gave them (1).
`tools/re/md_util.start_play` (used by the md_* scripts) taps START through the card until the level init
(`$1AA484`) and runs to the first frame of play; `tools/re/md_levels.py` makes a state of every level.

## 3. Cheats

### 3.1 Patches (`md_aladdin.cpp`, checked with `tools/re/md_cheats.py`)

| cheat | patch | effect |
|---|---|---|
| Invincibility | `$1AE4F8` `4A39` → `4E75` (rts) | Aladdin's hit routine (knock-back, sound 49, then the health loss `$1B03F2` once on Practice, twice on Normal, three times on Difficult) does nothing. Not covered: falling off the map (`$1A902E`), lava / crushing tiles that set `$F0E6` directly (`$1AEBD4`, `$1AF814`, `$1B032E`), the Rug Ride's walls |
| Infinite Health | `$1B041A` `subq.b #1, $EFFA` → nops | hits still knock back; the smoke never shrinks |
| Infinite Lives | `$1A90BE` `subq.b #1, d0` → nop | the lives digit isn't decremented after a death |
| Infinite Apples | `$1B0360` → rts | a throw never decrements; the module tops a stock below 10 up to 10 |
| Infinite Gems | `$1B03BE` → rts | buying never costs; topped up to 10 |
| Infinite Continues | `$1B0A1C` `subq.b #1, $7E3F` → nops | YES on the continue screen (RIGHT) keeps the count |
| Developer Level Skip | `$412A` `0010` → `FF00` | the pause screen's code (below) shortened to its first press, A |

Verified: health over the Market's hot coals stays 8 (8 → 6 without), lives 3 → 3 after a death, apples 10 → 10
after three throws, continues 1 → 1 after a continue.

### 3.2 The developers' features (built into the game)

* **Level skip**: pause, then A B B A A B B A (table `$4128`: pairs of the pad's TH=0 / TH=1 bits A/START, B/C;
  checker `$1B0A46` in the pause loop) — sets `$F0E9 = $FF` and the level ends.
* **Debug screen**: on OPTIONS, A C A C A C A C B B B B (table `$413A`, checker `$1B0BBE`) shows David Perry
  ("AH!, DAVID PERRY, WHAT IS YOUR WISH?") and a screen with RAM/ROM statistics, the build date (FRI 6TH. AUGUST
  1993) and four options: CHEAT MODE (`$7E20`: no health or life is ever lost, `$1B0402` / `$1A90B0`), START ON LEVEL
  (`$7E26`, all 13 ids), FREEZEABILITY (`$7E25`: the game stands still while START is held — the frame wait `$1B24A4` loops — and the pause is off) and MAP VIEW MODE
  (`$7273`: `$1A9C9A` takes Aladdin out of play every frame — clears object 0 — and the D-pad moves the view 8 px a
  frame).
* The port's cheats **Developer Cheat Mode** and **Developer Map View** set `$7E20` / `$7273` while on and clear
  them once when switched off; leaving map view puts Aladdin back (`$7E40 = $83`) where the view's point is.

## 4. Sound (GEMS)

The 68000 library at `$1E56C0` queues commands in the Z80's FIFO (Z80 RAM `$1B40`, 64 bytes, write index `$0036`):
`$FF`, command, arguments (`$1E57F4` / `$1E5800`). `gemsinit` (`$1E584A`, called at `$1AA35C`) gets the banks:
patches `$1B9D06`, envelopes `$1BAF46`, sequences `$1BAF6F`, samples `$1C73CB`. The game uses only: `$12` n (stop
sequence n, `$1E58B8`) then `$10` n (start, `$1E589A`) — every request is "pea #n; jsr $1E58B8; jsr $1E589A" —,
`$16` (stop all, `$1E58F4`), `$0C` / `$0D` (pause / resume all, the pause loop).

The sequence bank holds 114 sequences (offset table, little-endian); music and effects are both sequences. The
**names are the developers' own**: OPTIONS → SOUND TEST lists 16-byte entries at `$12675E` (sequence, 14 characters,
`$FF` ends). Music by where it plays: level music is header +`$1A` (§2); 26 NEW WORLD (title, `$1B3CDE`, and the
ending `$1B4F9A`), 84 STORYLINE / 85 DRAMA / 86 MUSHY ONE (story cut-scenes before levels, `$1B0F8E`), 17 LEVEL
COMPLETE (`$1B0D9A`), 18 BONUS TUNE (Abu's round intro `$1B5196`), 10 NE NAW TUNE (continue screen `$1B0900`).
Ten long sequences are never requested and not in the sound test (21, 28, 30, 36, 37, 51-54, 74: listed as
"Unused Tune 1-10"); 2 (SILENT LAMB), 23-25, 40, 48, 87-89, 111 are silent. Renders: `md_sound.py render`.

Jukebox (module): power-on, 40 frames (the SEGA logo), `freeze_68k`, then commands written straight into the FIFO:
a tune = stop all + start; an effect = stop it + start; stop = `$16`. Loudness matches Castle of Illusion /
QuackShot in the same core (music RMS about 2600-3000 vs. 2400-3600), so `audio_gain` stays 1.

## 5. Achievements (`achievement_frame`, RAM only; `tools/re/md_ach.py`)

* Level over: the rising edge of `$F0E9` with the level in `$7E26`; when `$F158` (paused) is set on either frame it
  was the level-skip code — not counted, and the run is marked.
* A level attempt starts when object 0 becomes `$83` (Aladdin put into the level; every restart after a death too)
  or at a level the port's level select started. Health falling (`$EFFA`) / apples falling (`$EFE0`, ASCII) while
  Aladdin is in play and not paused spoil Not a Scratch / Sword Play.
* A new game (run): Aladdin first in Agrabah Market with the script at `$4082`, score `"000000"` and the
  difficulty's full lives. Lost lives: the lives digit or the continues falling. Developer options (`$7E20`,
  `$7273`, `$7E25`) or the level-skip code mark the run. Iron / Difficult need a clean run from a new game started
  with achievements on (`run_counts`).
* Abu's round: the script position changes onto level 2 or 6. The Genie's wheel: a level over with `$F003` > 0
  (not the Sultan's Palace). The peddler: gems falling while Aladdin is in play (a continue resets them off play).
  The attract demos (`$F57C`) are skipped.
* Magic Carpet Ride: the end of the Rug Ride, not by the mercy after a third crash (`$F006` ≥ 3).
* `md_jafar`: the end of Jafar's Palace, the last level (the ending follows).
