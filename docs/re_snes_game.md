# Aladdin (Super Nintendo, Capcom 1993): game logic

Notes behind `src/game/snes_aladdin.cpp` (level select, cheats, sound, achievements). Widescreen: `re_snes_wide.md`.
ROM: `Aladdin (USA).sfc`, 1.25 MB, CRC32 `124D8E4D`, LoROM FastROM (map mode $30), no SRAM. Addresses are FastROM CPU
addresses (`$80:xxxx`); RAM is `$7E` (low RAM `$0000-$1FFF` mirrored), written as `$xxxx`. Scripts: `tools/re/snes_*.py`
(run with plain `python`; they use `build/tools/snestool.dll` through `tools/snt.py`).

## Core fix: no battery RAM

The game checks for a copier's SRAM in many places (`$83:9894` in the attract demo, `$80:A167`, `$81:D724`, `$83:B291`,
`$84:863A` at Jafar's cobra, ...): it writes `$70:xxxx` and reads it back. With SRAM there it hangs (`BRA *` at
`$83:98AC`, the attract demo stood still) or makes the cobra unbeatable (127 hit points instead of 7). The cartridge has
none (header `$FFD8` = 0); `src/core/snes.cpp` now maps battery RAM only when the header declares some (open bus
otherwise).

## Core fix: HDMA switched on below the top of the screen

In a level the NMI (`$80:8111`) clears HDMAEN, points every channel's table address ($43x8) at its table's start
and sets its line counter ($43xA) to 1 (`$80:82AB`), and arms an IRQ at line 8 (`$80:8284`: H `$98`, V 8 - or a few
lines after the NMI if that ran past the frame's top); the IRQ (`$80:8332`) turns the screen on and only then switches
HDMA on (`$03B6`). The channels so start mid-frame from those registers: the table's first entry (one line) is read
at the next HDMA point, the rest follow - the band routines build their tables for that (the first entry one line,
the bands from camera y + 8). The core cleared every channel not enabled at the frame's top, so they never ran: no
raster bands in Agrabah Market, Agrabah Rooftops (its sky), the Market at Dusk (BG2's scroll) and the Lava Cavern
(BG2's scroll, the main screen's layers per line) - their whole background scrolled at the first band's speed.
`Snes::hdmaInit` now does what bsnes models: at the top of the frame every channel's "terminated" state clears, the
enabled ones load their first entry (and set every channel's do-transfer flag); a channel enabled later starts from
its registers with the flag as left. Compared with the old core over the first 240 frames of all 19 levels and 6000
frames of the attract loop, only those levels' pictures change. In this core the table's second entry reaches row 11
(`AladdinWide::kBandRow0`).

## Remove Slowdown

The level's main loop (`$81:82CF`) does a frame's work and then waits at `$81:82E6` (`JSL $81:BD31`, the task
scheduler's wait) for the next frame; when a frame's work doesn't fit, the next iteration starts a frame late (a lag
frame). With the option on (`GameConfig::cpu_turbo` = 4, `Snes::cpuTurbo`) the module sets `Snes::turboActive` at the
loop's top and clears it at the wait (the wide view's `kLevelLoop` / `kLevelWait` hooks), and the CPU's memory accesses
and idle cycles take a quarter of their time in between, outside hardware interrupt handlers.

The core first counted BRK and COP as interrupt handlers too: Aladdin's object coroutines yield with them and resume
without a matching RTI, so the depth sat at 7 in the main loop and the speed-up never applied. Only hardware
interrupts (NMI, IRQ) count now.

`tools/re/snes_lag.py <ext> <turbo>` (a bot: Right held, jumping, throwing; invincible; 1,200 frames per level):
lag frames 0.7% of gameplay at 4:3 and 1.5% at 21:9 (A Whole New World 12.0%), 0.6% with the option on at 21:9
(A Whole New World 0.1%; what is left is the game's own pauses). Every level runs its main loop once per frame when
idle (none is built for less than 60 a second), Aladdin's position over 300 frames is identical with and without the
option in Agrabah Market, the Carpet Escape, A Whole New World and Jafar, and the level-start music is identical. The
first attract demo follows its path for ~25 s and then lands a few pixels off in places (its recorded input meets the
removed lag frames).

## Engine

- **Tasks.** A cooperative scheduler (`$81:BC6D`): 8 task slots at `$006F + n*$18` (`+0` state, `+1` frames to wait,
  `+2` saved stack pointer, `+4` stack base). `JSL $81:BD31` (A = frames) waits, `JSL $81:BCB8` + inline slot / param /
  address spawns a task, `JSL $81:BCF6` + inline address restarts the current one there. Task 0 is the main flow: its
  saved stack's return address tells the screen (`$81:82E9` the level's main loop waiting for the frame, `$81:83FA` the
  pause loop, `$80:9FBE` the title, `$83:8A6F` OPTIONS, `$84:ADF9` the password screen).
- **Objects.** 35 slots of `$50` bytes from `$092B`, run as coroutines with the slot as direct page (`BRK` = wait a frame,
  `COP n` = wait n frames; `$83:94A4` runs them). Fields: `+$00` state (0 free, 8 running, `$0C` new, `$8C` hit), `+$03`
  saved PC, `+$06` type, `+$08/$09` flags (`$08` bit `$4000`: in the draw range), `+$0B` x fraction, `+$0C` x, `+$0E`
  y fraction, `+$0F` y, `+$11` hit points, `+$12` range class (draw / keep tables), `+$34` level-object number. Slot 0
  is Aladdin (type 8); his position for the camera and exits is also in `$08E7` / `$08EA`.
- **Level layout, camera, streaming, spawning:** `re_snes_wide.md`.
- **Main loop** (level): `$81:82CF`: `$81:8311` (objects, camera `$81:C5A4`, BG streaming `$81:C1FA`, object spawner
  `$82:A79C`, draw list `$81:9198`, OAM `$81:9258`), HUD `$81:F456`, pause `$81:8398`, frame wait `$81:82E6`.

## Game state (RAM)

| address | meaning |
|---|---|
| `$0009` | developer mode (the controller-2 code on OPTIONS; warm-boot RAM): title stage select, 10 hearts / 99 apples at the start |
| `$0331` | 0 play, 1 recording input to SRAM (developer), 2 attract demo (input from `$84:F700`) |
| `$032E` | NMI frame counter |
| `$035B` | continues (3 at GAME START, 1 after a password) |
| `$035C` / `$035D` | stage (0-7) / act; `$035E` the level index from them (table `$80:B73E`), or a screen number (below) |
| `$0363` / `$0364` | lives shown / lives (the bar shows lives - 1; 100 at most; 0: game over) |
| `$0365` / `$0367` | hearts maximum (3-10) / hearts; `$0366` the bar's count-up |
| `$0368` / `$0369` | apples shown / apples (BCD) |
| `$036A` / `$036B` | gems shown / gems (BCD; a hundred: +1 heart maximum, or +1 life at 10 hearts) |
| `$036C` | a Genie token collected in this act: the bonus wheel follows it |
| `$036E` | an upgrade item taken (`$83:B71C`); set by the developer mode |
| `$0358` / `$0359` / `$035A` | items taken once per stage (heart container, ..., Genie tokens), bits by stage |
| `$0347` | Aladdin's state flags (2 hurt, `$80` leaving the level) |
| `$08E3` bit 7 | blinking after a hit (hits ignored) |
| `$1773` / `$1777` | camera x / y; `$1A04` / `$1A06` the level's last camera x / y; `$1A40` / `$1A42` a zone's limits; `$1A10` / `$1A12` camera held in x / y |
| `$1B2B` | Jafar's cobra beaten |

Screens (`$035E` >= `$14`): `$14-$1C` the attract demos' levels (stage 8; `$1C` is also the staff roll's carpet ride),
`$24` STAGE CLEAR (with the password), `$25` continue, `$2B` OPTIONS, `$2C` password, `$2F` the Genie's bonus wheel
(BREAK TIME!), `$30` a story scene between stages, `$35` the opening story, `$39` the ending.

## Levels

Stage / act -> level index (`$80:B73E`), the next stage after each stage (`$80:B988`: 0, 1, 2, 3, 4, **6**, 5, 7 -
A Whole New World comes between the dungeon and the palace), and each level's exit (`$80:EC1E`: x, y, next act, 0 =
next stage). An act's end: the player reaches the exit (`$81:872E`) or an event ends it; `$81:E019` advances the act;
the last act of a stage (`$80:C6F2` set) goes to STAGE CLEAR, the others to `$30`. Bonus token: the wheel first.

| # | stage-act | level | name used | notes |
|---|---|---|---|---|
| 0 | 0-0 | `$00` | Agrabah Market | |
| 1 | 0-1 | `$01` | Desert Ruins | |
| 2 | 0-2 | `$02` | Agrabah Rooftops | climbs from the bottom row |
| 3 | 0-3 | `$03` | Market at Dusk | boss in a camera-limited arena (`$1A42` = `$0400`, then `$0500`) |
| 4 | 1-0 | `$04` | Cave of Wonders | ends by an event (object `$6B`) |
| 5 | 1-1 | `$05` | Underground River | ~80 frames of object set-up at its start |
| 6 | 2-0 | `$06` | Lava Cavern | |
| 7 | 2-1 | `$07` | Carpet Escape | auto-scroller; the wave is a picture on BG2 |
| 8-10 | 3-0..2 | `$08-$0A` | Inside the Lamp 1-3 | `$0A` climbs a one-page-wide tower |
| 11-13 | 4-0..2 | `$0B-$0D` | Sultan's Dungeon 1-3 | boss at the end of `$0D` |
| 14 | 6-0 | `$12` | A Whole New World | carpet ride |
| 15-17 | 5-0..2 | `$0E-$10` | Jafar's Palace 1-3 | |
| 18 | 7-0 | `$11` | Jafar | Jafar, then his cobra (7 hit points, `+$11` of type `$73`) |

(Stage 5 act 3 is also level `$11`; the game's progression never uses it.)

**Level select** (`start_level`): power-on, START through the logos and story, GAME START on the title; the new
game's first level computation (`$81:8157`, from `$81:8118`) gets the chosen stage / act - exactly what the game's own
developer stage select on the title does (`$80:A05C`, `$80:9FC3`). The level then starts as usual (lives 3, hearts 3,
apples 10). About 1100 frames, half a second headless. `tools/re/snes_levels.py` starts all 19.

**Developer mode** (`$0009`): entered on OPTIONS with controller 2: L, R, START, SELECT, X, Y, A, B, each within 32
frames (`$83:8A74`, table `$80:CE63`; sound `$28`). The title then shows a stage-act number and left / right / up / down
change it (`$80:A05C`); a game started then has 10 hearts and 99 apples (`$81:80ED`, `$84:AEFD`). Exposed as the cheat
*Debug Mode*.

## Cheats

| cheat | how |
|---|---|
| Invincibility | `$83:8024` (a hit; skipped while blinking) returns as if blinking (A = `$80`); `$83:8004` (a hit regardless) returns at once. Instant deaths (`$83:8052`: pits, being left behind by the scrolling, crushes; `$81:DBD4`, `$81:E7FF`) still happen. |
| Infinite Health | the `DEC $0367` of both hit paths (`$83:8010`, `$83:8031`) skipped: knocked back, no heart lost |
| Infinite Lives | `$0364` raised by one before the `DEC` at `$81:DC67` / `$81:E837` (the DEC's flags still decide) |
| Infinite Apples | the store at `$81:CE5C` skipped |
| Infinite Gems | `$036B` kept at `$99` |
| Debug Mode | `$0009` = 1 (cleared again when switched off) |

`tools/re/snes_cheats.py` plays with each.

## Sound (Capcom's driver)

- Boot (`$80:B4AA`, from `$81:8021`): three uploads through the IPL protocol (`$85:8000` table, entries 0-2: the driver
  and all samples, 45 KB from ARAM `$360C`).
- Commands: a 32-byte ring at `$03BF` (write index `$03BD`, read `$03BB`); the NMI (`$80:8323` -> `$80:B463`) sends one
  a frame when the driver has acknowledged the last (`$0002` against port 2). `$F5` / `$F6` take a parameter byte (port
  3). `JSL $81:FA00` queues A (a sound effect `$01-$50`, or a command); `$81:F9D4` queues `$F6, A` (a fade), `$81:F9F7`
  `$F2` (stop; `$F0` and `$F3` stop too).
- Songs: `JSL $81:FA23` with A = the upload index: command `$FA`, then the song's sequence data (all to ARAM `$0D20`,
  table `$85:8000` entries `$10-$36`); the song starts when the upload ends. OPTIONS' B.G.M. test plays `$10 + n`. Level
  music by level index: `$80:B8EF`.
- Some songs leave the driver's volume settings to the song before them and are silent after the boot alone (`$1C`,
  `$1F` follow `$10`; the story scenes `$11`, `$1E`, `$1D`, `$35` follow `$27`; `$30` follows `$16`): the music player
  plays that song first.
- Contexts (`tools/re/snes_song_log.py`, `snes_song_play.py`, `snes_interludes.py`, `snes_sound_calls.py`): `$10`
  Capcom logo, `$1C` / `$1F` opening story, `$14` title logo flash, `$12` / `$13` title, `$15-$1B` the stages, `$2E`
  Jafar's room, `$22` Jafar, `$21` bosses, `$25` / `$30` / `$2F` level events, `$31` an event in the Cave of Wonders,
  `$28` act clear, `$27` stage clear, `$29` bonus wheel, `$2A` life lost, `$2B` continue, `$2D` password, story scenes
  `$1E` / `$11` (after stage 1), `$35` (2), `$1D` (3, 4), `$1E` (5), `$1F` / `$1C` (A Whole New World), ending `$36`
  (cobra beaten), `$32`, `$34`, `$23`, `$24`, `$13`, staff roll `$26`.
- Music player (`jukebox_boot`): the CPU is parked right after the boot's uploads (`$81:8025`, before the logos) in a
  stub in the patch ROM (`$60:8100`) that waits for a frame and runs `$81:FA23` / `$81:FA00` / `$81:F9F7` for a request
  in a RAM mailbox (`$1FF8` command, `$1FF9` id, `$1FFA` the song to play ahead). The NMI keeps sending the queue.
  Loudness: about 8 dB below Super Star Wars on the same core (`audio_gain` 1.8; `tools/re/snes_sound.py survey`).

## Achievements

RAM only (`achievement_frame`), checked while the screen and the level index change:

| id | when |
|---|---|
| `snes_agrabah`, `snes_cave`, `snes_escape`, `snes_lamp`, `snes_dungeon`, `snes_carpet`, `snes_palace` | STAGE CLEAR (`$24`) after the stage's last level (`$03`, `$05`, `$07`, `$0A`, `$0D`, `$12`, `$10`) |
| `snes_jafar` | level `$11` hands over to the ending (`$39`) |
| `snes_no_continue` | the same, in a game begun with GAME START on the title (not a password) with no continue used (`$035B` never fell) |
| `snes_stage_clean` | a stage from its first act (entered from another stage or a new game, not after a game over) to STAGE CLEAR with `$0364` never falling |
| `snes_untouched` | a level from its start to its end (next act, STAGE CLEAR, bonus wheel, ending) with `$0367` never falling |
| `snes_wheel` | the bonus wheel screen (`$2F`) |
| `snes_hearts` | `$0365` reaches 10 |
| `snes_gems` | the gem counter goes from `$9x` to `$0x` (a hundred) |
| `snes_lives` | `$0364` reaches 10 (nine spare) |
| `snes_jafar_flawless` | level `$11` to the ending without a heart lost |
| `snes_debug` | `$0009` set on the OPTIONS screen |

Not counted during the attract demo or while the developer mode is on (its 10 hearts / 99 apples); a jump of the NMI
counter (a loaded state) resets the conditions in progress. `tools/re/snes_achievements.py` plays the scenarios.
