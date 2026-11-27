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
| `$7262` / `$7266` / `$726A` / `$726E` | l | the CRAM lines' sources (lines 0-3), kept for the fades and the hit flash's restore (`$1ACDA2`); `$1B2678` / `$1B2664` / `$1B2650` / `$1B263C` load a line and set its source. Line 2 is the objects': several kinds load their colours into it as they spawn (docs/re_md_wide.md §3) |
| `$F175` | b | set while a level start or a restart fills the plane (`$1AA73E` … `$1AA812`): spawn handlers load no colours then |
| `$F10E` / `$F10F` / `$F110` | b | the Cave of Wonders' monkey statues `$35` / `$36` / `$37` (blocks 38,56 / 81,25 / 87,6) hit with the sword (`$F10E` lets the stepping stone `$F4` at block 61,58 spawn: its handler `$1B65F4` tests it); `$36` and `$37` also clear the map blocks whose attribute (`$FFAE86` + word / 2) is `$4F` / `$FE` resp. `$4E` / `$FD` across the whole map (`$1AC386` / `$1AC3BC`, 14,400 words: the game stands still for 9 frames) - a wall elsewhere |
| `$F125` / `$F112` | b | the Cave of Wonders' golden idol: hits taken (`$1AEAA8`; the tenth beats it) / `$FF` once beaten (`$1AEB36`), which lets its carpet (map object `$F9`, block 144,29: handler `$1B6B5C`) spawn |
| `$F11C` | b | the Sultan's Palace: Abu freed from his cage (`$1AF400`, Aladdin touching object `$29`: sound 93 ABU UH OH, Abu runs off right). The cage hangs over the top corridor (map type `$57`, block 154,8, handler `$1B6BE4`, which doesn't test `$F11C`). The hole in the floor of the room below the rope (blocks 182-184, 23) is the shaft down to the room with the level's exit; while `$F11C` is clear, map type `$82` (block 183,23, handler `$1B68D6`) puts a carpet in it, object `$5F` (template `$1B78B4`: no movement script; its touch routine `$1AF6AC` only lets Aladdin stand on it), so the way down is shut until Abu is free. A carpet spawned before that stays until it despawns (back up at the cage it has); its block then spawns nothing. `tools/re/md_wide_abu.py` |
| `$F003` | b | Genie tokens: one spin of the Genie's wheel each after the level (`$1B16E0`, not after the Sultan's Palace) |
| `$F176` | b | Abu token found: the play-order script's bonus entries are taken |
| `$F572` | l | **play-order script position** (ROM `$4082`, see §2) |
| `$F57C` | b | 1 during the attract demos (table `$4B04`: input recording + level, 5 demos) |
| `$F57D` / `$F57F` | b | sound effects / music on (OPTIONS) |
| `$F165` | b | planes swapped: the level map is on plane B (Sultan's Dungeon) |
| `$F173` | b | Abu's bonus rounds (set by their inits `$1B6394` / `$1B6478`): no camera moves |
| `$F006` | b | Rug Ride crashes: the first two cost a life, the third ends the ride (`$1A8F5A`, the game's mercy) |
| `$F086` / `$F088` / `$F084` | w | Rug Ride: speed (px an iteration, 6 at the start), iterations toward the next speed-up (310), distance toward the next script entry (192) (§2.1) |
| `$F12E` | l | Rug Ride: script position (ROM `$262F`, §2.1) |
| `$F0A2` | w | Rug Ride: plane B's lava wave scroll (320 at the start, +1 every 64 frames) |
| `$F005` | b | `$FF` once the Rug Ride is completed (its script's `$F2`, `$1B5B42`); cleared at a new game (`$1AA6EE` clears `$EFDC`-`$F006`) |
| `$7DEA` | l | random seed (`$1B3032`: × 13 + 7); `$BC614E` as an attract demo's level starts (`$1A8C0C`), in a game it runs on from power-on |
| `$7E20` / `$7273` / `$7E25` | b | debug screen options: CHEAT MODE, MAP VIEW MODE, FREEZEABILITY (§3.2) |
| `$7E28` | b | frame counter of the gameplay loop |
| `$7276` | l | position in the current button code (`$4128` pause code, `$413A` OPTIONS code) |

**Bosses** are objects in the table, their hits left in the object's +1 (Jafar's Quarters: the level routine
`$1B62B6` re-arms Jafar's magic — slot 1, template `$1B8278`, at (`$184`, `$166`) — every 128 frames). The Rooftops'
boss (object `$10` from map type `$18`, 9 hits; or `$13` from `$28`, 10) ends the level from its kill (`$F0E9` = `$20`:
`$1AC4A2`, `$1AED04`); Jafar in his palace (`$12`, 10 hits) becomes the snake (`$11`, 20) in the same slot
(`$1AC5A4`, `$F11D` set), whose kill ends the game's last level (`$F0E9` = `$14`, `$1AC59A`). The Cave of Wonders'
idol counts its hits in `$F125` instead; the tenth (`$1AEB22`) leaves its death effect, sets `$F112` and starts the
level music again, and the carpet then waiting right of the far pedestal (`$F9`, spawned as its block column is
streamed once `$F112` is set) carries Aladdin over the treasure and down to the river's bank, where the level goes on.
The camera
always follows Aladdin (no locked arenas): `$7DFE` / `$7E00` are only set to the facing offsets, and the clamps
are the map's own (§2). The arenas are bounded by walls: the Cave of Wonders' idol between its two pedestals
(Aladdin x 2047-2305, camera 1821-2178), Jafar in the middle of his hall (477-1138, camera 252-947), the Rooftops'
boss in the level's last screen (2270-2432, camera 2045 to the map's end, 2126).

**Map objects** spawn from their map block when its column or row is streamed (`docs/re_md_wide.md` §3): the handler
(table `$4154`) spawns through `$1B526C`, which clears the block's type until the object despawns, or - the Cave of
Wonders' stepping stones (`$EF`, `$F4`), the Escape's lava rocks (`$EF`) and the apples that appear while Aladdin has
none (`$61`) - through `$1B52A0`, which leaves it: those come again each time their column is streamed. A stepping
stone (object `$62`) sinks while stood on (`$63`) and is gone once under.

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

**A story cut short by a button held at the level's end (the original's).** The story routine (`$1B0F66`, called from
the level loop at `$1A8B7C`) starts with `$1B3258`: `$FF7E23` set (skipping allowed), `$FF7E22` cleared, `$FFF155` set
to `$FF`; then each step of the story tests `$FF7E22` and jumps to the title card (`$1B1260`) once it is set. The VBlank
handler sets it while `$FF7E23` is on and any of four bits reads 0 (pressed): `$FFF155` bits 5 / 4 (START / A, the pad
read with TH low) and `$FFF156` bits 4 / 5 (B / C, TH high). Those two bytes are written by the gameplay loop's pad read
(`$1A8DC6` / `$1A8E02`, every iteration) and by the level's init (`$1AA6BA` / `$1AA6C0`, `$FF`); nothing between a
level's end and the next story writes them - not the score tally, "Level Complete", the Genie's wheel (it reads the pad
itself), the scarab, nor `$1B3258` for `$FFF156`. So the story sees the gameplay loop's last read: B (sword) or C (jump)
held as the level ended - typical at a boss's kill - sets the skip flag on the story's first frame, and only the title
card shows. Measured from the user's save state (the Genie's wheel after Agrabah Rooftops, `$FFF156` = `$5F`: C held):
the Dungeon's story ("With both halves of the scarab, Aladdin is ambushed...", 1,004 frames) skipped at 4:3 and at 21:9
alike, its title card shown (623 frames of the routine in all); with `$FFF156` set to `$7F` (released) the story plays at
both. The same in the original game with widescreen off: the cut is the game's own.

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

### 2.1 The Rug Ride (level 8)

The escape from the Cave of Wonders on the carpet, ahead of a lava wave. Header: init `$1B64D0`, per-iteration
routine `$1B6066`, scroll routine `$1AAEFC`; the map (15000 × 16) is nominal: it is never streamed.
* **Flight.** The gameplay loop's player routine goes to `$1A9D18` for level 8: the carpet (object 1, template
  `$1B7DC8`) and Aladdin move `$F086` px right an iteration and the camera keeps Aladdin at screen x 120 (124 after
  the camera code's own step), so camX runs on by exactly the speed; UP / DOWN move Aladdin's screen y (`$7DFC`) 4 px
  an iteration between `$10E` and `$18A`; the flight takes no other input. The speed starts at 6 and goes up by one
  every 310 iterations (`$F088`), with no cap: 24 px an iteration by the end, some 5,750 iterations (96 s) for the
  whole ride if it is flown without a crash. camX passes 65535 about 55,000 px into the ride and wraps (the word is
  unsigned; the game's own despawn test then takes objects at once as they are sent in, which the original shows).
* **Scroll.** H scroll per 8-line row (reg 11 = 2), V scroll 0. Plane A: rows 0-20 (the cave's curtain) at camX / 2,
  rows 21-27 (the lava floor) at camX. Plane B: rows 0-2 (the roof) at camX; rows 3-19 (lines 24-159) the **lava
  wave** at `$F0A2` (320 at the start, +1 every 64 frames: it creeps up from the left, its crest's right edge from
  screen x −3 to about 90 by the ride's end); rows 20-23 at 3/4 camX and 24-27 at 2 camX (lava). Both planes are
  512-px pictures loaded once (plane B: header +`$1C` = `$12B428`); everything but the wave is a seamless loop. The
  wave is a crest of 118 px at plane x 72-190 on an empty band, cut off straight at its back (x 72, its top at line 39).
* **The script.** Every iteration `$F084` += speed; each time it passes 192 (every 192 px of the ride, so at the same
  places whatever the speed) the next pair of bytes of the script at ROM `$262F` (`$F12E`) is run: a command `$E6`-
  `$FF` (handlers at `$20C0` + 4 × (command − `$E6`)) and a parameter. 443 entries up to `$F2` (`$1B5B3A`: the
  level over, `$F0E9` = 1 - counted down in the same iteration - and `$F005` = `$FF`). The ride's: `$FE` (321) a
  treasure (object `$42`, template `$1B814C`: a static 16 × 24 pickup that scores; their heights trace the way
  through), `$F7` (29) a pair of rock piles (`$54`, `$1B8160`, 32 px apart) from the lava (y `$88`), the roof (`$30`)
  or in mid-air, `$F3` (39) `$F10B` = parameter, `$FA` / `$FB` / `$F9` / `$F4` (29) the Genie's warning signs
  (template `$1B8138`, screen-fixed at VDP (398, 356) and rising), `$F8` (9) an extra life (`$46`, unless 9 lives),
  `$F5` / `$F6` (9) gem (`$3A`, unless 99) / `$34`, `$FF` (6) nothing. Each object is put at camX + 320 (screen x 304;
  its sprite shows from x 305-314, inside the 4:3 view's right edge) and y = camY + 256 + parameter, and stays there
  in the cave (no movement script, no speed); only its animation runs. After every entry a random number
  (`$1B3032`: seed `$7DEA` × 13 + 7, running on through the game - not reset for the ride or its restarts) below `$46`
  sends a lava splash (`$84`, `$1B7EE0`) at screen x 304 down in the lava: it erupts and sinks in 47 frames. The only other
  random numbers are drawn by collect effects' animations (`$1AC950`).
* **Crashes.** A rock pile ends the flight (invincibility doesn't cover it); `$1A8F5A` counts it in `$F006`: the first
  two take a life and the ride starts over from the beginning (script, speed, wave; the seed runs on), the third ends
  the level (`$F0E9` = `$FF`: the game's mercy; the next level follows as after a completed ride).
* `$F173` (often taken for the ride's flag) is set by Abu's bonus rounds' inits (`$1B6394`, `$1B6478`): the camera
  code returns at once while it is set.

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
* All the port's cheats apply in a game only: from power-on through the title routine and while an attract demo plays
  the ROM is the original and they write nothing, so the demos replay their recordings as the game was made
  (`docs/re_md_wide.md` §6, `tools/re/md_attract.py`).

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
Mid-level the music changes only through four object types' spawn handlers, which stop everything and start 50
BOSS TUNE (if the music option `$FFF57F` is on): `$18` (`$1B6C5A`) and `$28` (`$1B6D1E`, only while Aladdin's Y is in
[`$2AD`, `$2F4`)) at the Rooftops' end, `$4D` (`$1B6CCE`) in Jafar's Palace and `$F5` (`$1B6D90`, unless `$FFF175`) for
the Cave of Wonders' golden idol (block column 129, row 29) - the tune starts as the boss object spawns, i.e. as its
block is streamed into the plane (`tools/re/md_boss_tune.py`). Jafar's Quarters has it as its level music.
Ten long sequences are never requested and not in the sound test (21, 28, 30, 36, 37, 51-54, 74: listed as
"Unused Tune 1-10"); 2 (SILENT LAMB), 23-25, 40, 48, 87-89, 111 are silent. Renders: `md_sound.py render`.

Jukebox (module): power-on, 40 frames (the SEGA logo), `freeze_68k`, then commands written straight into the FIFO:
a tune = stop all + start; an effect = stop it + start; stop = `$16`. Loudness matches Castle of Illusion /
QuackShot in the same core (music RMS about 2600-3000 vs. 2400-3600), so `audio_gain` stays 1.

### 4.1 MD+ soundtrack packs (`soundtrack.cpp`, `session_md.cpp`; `tools/re/md_mdplus.py`)

The MD+ cartridge overlay: `$CD54` written to `$3F7FA` opens it, a command word goes to `$3F7FE` (`$11xx` play track
xx, `$12xx` play it looped, `$13xx` pause with a fade of xx/75 s, `$14xx` resume, `$15xx` volume), `0` to `$3F7FA`
closes it. While it is open `$3F7FE` reads 0 (ready): the patches poll it (`tst.b $3F7FE / bne`) before each command,
so the core answers that (`MegaDrive::set_mdplus_overlay`; a ROM read before, the *Aladdin CD - MD+* patch hung in its
boot code at `$20006E` - the existing MD+ support had never run it).

*Aladdin CD - MD+* (ArcadeTV, BPS for `ED427EA9`, 2 MB -> 4 MB, result CRC `4A7C033C`) changes: the reset vector
(`$200000`: an "MD+ SOUND ENABLED" screen, ~430 frames, then `jmp $21A`), the HBlank / VBlank vectors (`$200258` /
`$200276`: its screen's handlers while `$FF0014` = `$B0BB1B0B`, else `jmp $1B249C` / `$1B246E`), the checksum and ROM
end, `$1E589A` (the start of a sequence: `jmp $2050D8`, a table of the game's tunes -> CD tracks, the rest to GEMS as
before), `$1E5902` (the stop-all's end: `$1300` first), `$1A91EA` / `$1A92C0` (the pause's start / end: `$1300` /
`$1400`), `$1AC9D8` (an animation's sound 2: track 21, which the pack doesn't have) and `$125F65` (an animation's sound
`$2D` -> `$02`). The app skips the boot screen (the original reset vector: the screen only delays the game, and the
level select's RAM writes at frame 30 were lost to the game's own start-up after it) and corrects the table's boss tune
/ bonus jingle swap (`$205101` / `$205141`). Commands seen (`md_mdplus.py levels`): each level's title card `NENAW`,
then the level's tune; `$1300` wherever the original stops the music; the same frames as the GEMS commands of the
original ROM. The held boss tune (*Boss tune* in `re_md_wide.md`) is started through the game's own code when the
sequence start is hooked (`sound_hooked`: the 68000 calls the first boss handler's stop-all and start, `$1B6C64`, from
the streaming hook and returns there), so the pack plays it; queued straight into the GEMS FIFO, the FM boss tune had
played over the level's CD track.

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
* Magic Carpet Ride: the end of the Rug Ride, not by the mercy after a third crash (`$F006` ≥ 3). The ride's end sets
  `$F0E9` to 1 only for a moment (the gameplay loop counts it down in the same iteration: a frame's RAM hardly ever
  shows it, so the award was missed), and `$F005` for good: its rising edge in level 8 counts as the level over.
  `tools/re/md_wide_rug.py` flies the whole ride: `md_escape` (and the Genie's wheel) unlocked on the frame it ends.
* `md_jafar`: the end of Jafar's Palace, the last level (the ending follows).
