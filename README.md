# Aladdin Collection — PC Edition

A Windows (and Android) port of the three 16-bit and 8-bit versions of Disney's *Aladdin*, with a launcher,
widescreen up to 21:9, modern display options, gamepad support, cheats, level select, achievements, save states,
fast forward / rewind and a music player:

- **Aladdin** for the Sega **Mega Drive / Genesis** (Virgin Games, 1993)
- **Aladdin** for the **Super Nintendo** (Capcom, 1993)
- **Aladdin** for the Sega **Master System** (SIMS, 1994)

The games themselves run from **your own copies of the original ROMs**. The port contains no game code or
assets; it runs the ROMs on built-in, from-scratch Mega Drive, SNES and Master System cores, so they play exactly
like the originals, then adds game-specific patches and a PC frontend.

## Getting started

1. Unzip the `Aladdin Collection` folder anywhere (nothing needs installing: one self-contained
   `AladdinCollection.exe`).
2. Put the ROMs next to `AladdinCollection.exe`, or in a `roms` folder beside it:

   | Game | Expected dump | Size | CRC32 |
   |---|---|---|---|
   | Aladdin (Mega Drive) | `Aladdin (U) [!].bin` | 2 MB | `ED427EA9` |
   | Aladdin (SNES) | `Aladdin (USA).sfc` | 1.25 MB | `124D8E4D` |
   | Aladdin (Master System) | `Disney's Aladdin (Europe).sms` | 512 KB | `C8718D40` |

3. Run `AladdinCollection.exe`. The first launch opens fullscreen; F11 or Alt+Enter switches to a window.

ROMs are found by their contents (CRC32), so file names don't matter. Raw dumps (`.bin` / `.md` / `.gen`,
`.sfc`, `.sms`), interleaved `.smd` dumps, dumps with a 512-byte copier header (`.smd`, `.smc` / `.swc` / `.fig`)
and all of these inside a `.zip` (Deflate or LZMA) work. ROMs copied in while the launcher is open are picked up within a second.

**These exact versions are needed** (the USA cartridges, and the Master System's European one — it was only
released in Europe and Brazil): the widescreen patches, level select, cheats and achievements are matched to
their internals. A game whose ROM is missing shows *ROM not found* on its card, with the file it needs. Any other
dump of the game is recognised from the ROM's internal header, and the card says what it is instead (the
European or Japanese version, another revision, or a modified copy / bad dump). The other games still work.

To start a game directly, give `AladdinCollection.exe` the argument `--game md`, `--game snes` or `--game sms`
(e.g. in a shortcut).

**On Android** (64-bit phones and tablets, Android 6.0 or later): install the APK, copy your ROMs (or a zip of
them) to the phone, start the app and tap **Add ROMs**. The games are played with on-screen touch controls or a
Bluetooth / USB controller.

## Features

**Launcher**
- One card per console with a live attract-mode preview, plus Options, Achievements, the Music Player and
  Credits, over Agrabah by night.
- The Team RedTail logo at the top left, with the [Patreon page](https://www.patreon.com/c/TeamRedTail) under it:
  click (or tap) it to open the page.
- **The mouse** (PC): click a game to choose it and again to play it; click the buttons, the rows of the menus (on
  a setting's value, the left half steps back and the right half on) and the Music Player's tracks; the wheel
  scrolls lists; right-click, or click outside a menu, to go back.
- **Menu Style** (Options > Launcher & Menus): *Arabian Nights* (the default: deep indigo panels in gold
  arabesque frames with eight-pointed stars, gold titles, and the night sky with a glowing moon, glints among the
  stars and gold dust rising from the palace) or *Classic* (the original dark panels). It styles the launcher,
  every menu, the pause menu and the achievement pages.
- **Show Games** (Options > Launcher & Menus): switch off a game you don't want on the launcher. Its card goes
  (the other one or two stay the same size, centred), it runs no preview, and the launcher's Music Player and
  Achievements page leave it out. Nothing is deleted: its saves, settings, cheats and achievements are kept and
  come back when it's switched on again, and its desktop shortcut (`--game`) still starts it. One game is always
  shown. Saved as `show_md` / `show_snes` / `show_sms` in `[launcher]` in `settings.ini`.
- **Your own covers and background**: cover pictures on the cards in place of the previews, and a picture behind
  the launcher (PNG or JPEG; none are included). On PC put `md`, `snes`, `sms` and `background` (.png / .jpg) in
  the `covers` folder next to the exe (*Open Covers Folder*); on Android pick them with *Choose Cover* /
  *Choose Background*. Covers can be shown whole or fill the frame. A card with a cover runs no preview; a missing
  or unreadable picture is skipped (that card keeps its preview, the launcher its night sky).

**Widescreen**
- 16:10, 16:9, 2:1 and 21:9 ultrawide (fills 3440×1440 and 2560×1080 screens), for all three games. The width
  in pixels follows each console's picture and the pixel shape: with the default CRT pixels (each console's
  picture on a 4:3 television) 21:9 is 576×224 on the Mega Drive, 468×224 on the SNES and 404×192 on the
  Master System; with square pixels 536, 536 and 460.
- More of each level is really drawn at the sides: the games are patched so the level, enemies and items are
  streamed, spawned, moved and drawn across the whole width, instead of popping in at the edges of the original
  screen. Where the console's own background memory can't hold the wider view, the extra columns are drawn
  straight from the level map.
- The view stays inside the level: near a level's ends the picture slides, so the extra width shows more of the
  level instead of black (the game's own camera, and so how Aladdin moves, is untouched). The HUD is re-anchored to
  the screen edges (can be switched off). Title screens, menus and cut-scenes keep their original shape
  automatically; where such a screen's background is one plain colour, it fills the width in that colour. The
  Master System's story scenes, intro, title and ending and the Mega Drive's title, story cards, "Level Complete" and
  ending are drawn wide too (see below).
- The width (and cheats) can be changed at any time, also mid-level; save states remember the width they were
  made at.

**Video**
- Borderless fullscreen or windowed, monitor selection (by name; dragging the window to another screen selects
  it), window size 1×–6×, integer scaling, CRT (4:3 TV) or square pixels, or stretch.
- Filters: nearest, sharp bilinear, bilinear, scanlines, CRT. Direct3D 11.
- RetroArch slang shader presets (Windows) in place of the filter, through librashader: seven CRT presets are
  included (crt-royale, crt-geom, crt-lottes, crt-easymode, crt-aperture, crt-pi, zfast-crt), and any other
  `.slangp` preset put in the `shaders` folder appears in the list (e.g. the libretro slang-shaders collection).
  They work with every widescreen width and pixel aspect; some presets were made for a 4:3 picture and can
  stretch or frame oddly in widescreen.
- Sprite flicker removal (no sprites-per-line limit), V-Sync, FPS counter.
- Black frame insertion for sharper motion on 120, 180 and 240 Hz displays.
- High frame rate (Windows) for smoother motion on 100 Hz and faster displays: the refreshes between the games'
  60 frames show in-between pictures, drawn by each console's core with each layer's scroll and the sprites part
  of the way back to the last frame (sprites are matched by their look, as games reorder them; jumps of more than
  32 px are cuts). The game itself still runs at 60, so nothing about how it plays changes; the picture is one
  frame later. 2 pictures per frame at 100 / 120 Hz, 3 at 144 / 165 / 180 Hz, 4 at 240 Hz. With V-Sync off
  (G-SYNC / FreeSync) they are shown at that rate. Not together with black frame insertion; it works with a slang
  preset (the in-betweens go through it too). The widescreen sides move with the 4:3 part: what is drawn from the
  level data beside it is drawn at the in-between's own place, the slide at a level's ends and the SNES raster
  bands move band by band, and the HUD stays still. Scene cuts, fades to black, level starts, a loaded state, a
  width change and anything that jumps are shown as they are, never mixed with the frame before.

**Colours**
- Profiles: Accurate (each console's measured output levels), Linear, Vivid, Warm CRT, Cool, Grayscale, Sepia,
  Handheld LCD. Brightness, saturation and gamma.

**Audio**
- Mega Drive: YM2612 + SN76489 with the YM2612 "ladder effect"; SNES: S-SMP / S-DSP with original (gaussian) or
  crisper (cubic) interpolation; Master System: SN76489. Volume and an output filter (off, the console's own,
  or warm).
- CD-quality soundtrack packs: MD+ packs for the Mega Drive game and MSU-1 packs for the SNES game (see
  *Soundtrack packs* below).

**Music player & sound test**
- Every music track and sound effect of each game, played by the game's own sound driver, with channel meters
  (6 FM + 4 PSG on the Mega Drive, 8 voices on the SNES, 4 PSG channels on the Master System).
- With a soundtrack pack for the game, *Tab* / *(Y)* switches between the original music and the pack's (it starts on
  the one the game is set to play): each tune plays the track the pack's own patch plays for it in the game (asked for
  the way the game asks), tunes the pack doesn't arrange play the original, and the pack's other tracks are listed
  after the game's tunes.

**Level select, cheats and achievements** — see *The games* below. With achievements on (the default), cheats,
the level select, fast forward and rewind are switched off; save states still work. The collection adds two
achievements of its own: *A Whole New World* (defeat Jafar in all three versions) and *Completionist* — 54 in all.

**Achievements export**
- *Export to CSV* on the Achievements page writes an `achievements export` folder: `achievements.csv` in the
  columns of Playnite's achievements spreadsheet import (ID, title, description, points, bronze / silver / gold /
  platinum tier, hidden, game, progress and its total, unlocked, and when — date and time) and an `icons` folder
  with a 128×128 PNG badge for each achievement, earned and locked. The badges are drawn by the port: Mega Drive
  blue, SNES purple, Master System red and gold for the collection's own, each with a symbol of its own (the lamp,
  the carpet, Abu, Jafar's cobra...) and the tier's rim. Achievements earned with an older version kept only the
  day: they export at 00:00:00 of it. On Windows the folder goes next to the exe (or in `%LOCALAPPDATA%\Aladdin Collection`);
  on Android to `Android/data/com.darhkfox.aladdincollection/files`, which a file manager or a PC over USB can open.

**Update notifications**
- Once a day the launcher reads the version number of the latest release on GitHub (the public release page of
  `Darhkfox/AladdinCollection`; nothing about the player or their games is sent). When it's newer than this
  version, a badge on the launcher says so; the badge (clicked or tapped) or *Options → Updates → Latest Version*
  opens the release's page. Nothing is downloaded or installed. An offline or failed check stays silent (only
  *Check Now* says it couldn't reach GitHub). *Options → Updates → Check for Updates* switches it off.

**Other**
- Keyboard and gamepads: Xbox-compatible pads (XInput) and other USB pads (DirectInput), fully rebindable per
  console, hot-plugging.
- 5 save-state slots per game (F5 / F9), fast forward (hold Tab or the right trigger, 4×), rewind (hold R or
  the left trigger, up to the last minute of play), screenshots (F12).
- Remove Slowdown (Options, SNES, on by default): where the SNES version slowed down, its CPU does the game's work
  for the frame faster, so busy scenes keep a steady 60 frames a second. Interrupts, sound and the game's own pauses
  keep the console's timing, and nothing runs faster than the original. Off: the original's speed, slowdown
  included.

## The games

### Aladdin — Mega Drive / Genesis (Virgin Games, 1993)

- **Widescreen** up to 21:9 (576 px wide with CRT pixels, 536 with square ones). The columns beyond the 4:3 view
  are drawn per line straight from the level map in RAM, so the planes and the game's own camera stay untouched;
  objects spawn just outside the wider view, and their despawn window and sprite clip are widened. The HUD (lamp,
  lives, apples, gems) is moved to the screen edges. Near a level's ends the picture slides instead of showing
  black: the view starts at the level's first pixel and Aladdin walks across still scenery until the camera moves
  on (the game's own camera, and so how Aladdin moves, stays the original); the parallax background is shifted
  row by row with the level's own scroll routine, so it holds still with the level. When the wider view fills the
  console's object slots or sprite memory, room is made from objects out of sight (the sprite memory is compacted,
  and objects past the edge are put back on the map to return later), so nothing appears late inside the view.
  Objects that stay on the map once spawned (the Cave of Wonders' stepping stones, the Escape's lava rocks, the
  apples that appear when Aladdin has none) come in once, at the view's edge, as in the original. In a boss fight
  (the idol in the Cave of Wonders, Jafar and his snake) the wider view settles on the arena's centre instead of
  following Aladdin, so the extra width shows the arena rather than what lies beyond its far end; it eases in and out
  over about a second, and lets go as soon as the boss is beaten (the carpet then flies Aladdin away with the view
  following as anywhere). While a fight holds Aladdin between its walls (the idol's pedestals, Jafar's pillars, the
  Rooftops' end), whatever the view shows beyond them is dimmed to half brightness, the HUD kept bright; the dim fades
  in as he enters the arena and out over a quarter of a second once the boss is beaten (so the carpet after the idol
  comes in bright) or if he leaves. Inside a wide arena (Jafar's hall) nothing is dimmed until a wall comes into view.
  In widescreen the magic carpet comes as soon as the golden idol is beaten, unlike the original, where a win beside
  the far pedestal makes you walk away and back before it appears (at 4:3 it stays the original's way).
  The Rug Ride is wide too, with all the extra width behind the carpet: the ride sends its rocks and treasures in at
  the 4:3 screen's right edge, so the view ends there as in the original (you see as far ahead, so the ride plays
  and is as hard as ever: it runs on the original game, unchanged to the frame) and shows the cave you have flown
  through on the left, with the lava wave chasing you and what you passed drifting away until it leaves the screen.
  A level fades in and out in the shape it is played in (a death, a fall, a Rug Ride crash or the level's end fades
  wide, without the picture jumping); the picture becomes 4:3 only once the screen is black.
  The screens around the levels are wide too: the title and OPTIONS (the night sky, stars and palace skyline run
  across the width, the logo and menu stay centred), the story cards (their starry sky and dunes, the palace
  skyline), the title cards, "Level Complete", the death and continue screens and the ending. Their sides carry the
  picture's own edges on (mirrored or repeated, never the moon, the domes, the Cave's tiger head or any text), and
  whatever moves across them goes on into the sides instead of being cut at the 4:3 edge: the Genie flying round
  the Disney and Virgin logos, Aladdin and Abu running through "Level Complete", the scarab's halves, the carpet
  across the moon. The Genie's bonus wheel, the instructions screen and the full scarab between its halves' flights
  stay 4:3 (their pictures are cut off by the screen's edges). Abu's bonus rounds stay 4:3, and so do the attract
  demos, which play exactly as recorded at every width and with any cheat. A save state always goes on with the game it was saved in, wherever it is loaded
  (the title screen, the attract demos, the Rug Ride).
- **Level select**: all 13 entries in the game's own order (both of Abu's bonus rounds and the Rug Ride included),
  on Practice, Normal or Difficult. Each level starts with its own story card, as after the one before.
- **Cheats**: invincibility, infinite health, lives, apples, gems and continues, and three of the developers'
  extras: the debug screen's Cheat Mode, its Map View (fly the view around the level) and a level skip (pause,
  then A). The game's own codes still work too: pause + A B B A A B B A skips a level, and A C A C A C A C
  B B B B on the OPTIONS screen opens David Perry's debug screen.
- **Music player & sound test**: 28 tunes (10 of them unused by the game) and 76 effects under the developers' own
  names from the game's sound test, played by the game's GEMS driver.
- **Achievements**: 18 — one per level and for defeating Jafar, plus feats like *Not a Scratch* (clear a level
  without losing health), *Sword Play* (no apples thrown), *Diamond in the Rough* (finish without losing a life)
  and finishing on Difficult.

### Aladdin — Super Nintendo (Capcom, 1993)

- **Widescreen** up to 21:9 (468 px wide with CRT pixels, 536 with square ones). The sides are drawn straight from
  the level data the game keeps in RAM (the decoded tiles match video memory exactly in every level), so any width
  works, a mid-level width change is instant and the game's own column streaming is untouched. At every level's
  ends the view stops, showing the level to the edges, and it never pans by itself; the background follows it line
  by line — its parallax, and the raster bands, phases and objects the game scrolls it with in seven levels, worked
  out as the game's own code would place them with its camera at the picture's middle — so every layer moves as on
  the console. The Carpet Escape's wave and A Whole New World widen too. Objects appear, act and are drawn across the
  whole width; the HUD and the PAUSE / ACTION texts are re-anchored. A width change mid-level (or a save state made
  at another width) brings out only the objects the new sides add: never an enemy already defeated, collected or
  swept away by an event. Boss arenas and events (the Market at Dusk,
  the end of the Cave of Wonders, Sultan's Dungeon and Jafar's Palace, Jafar) keep the original camera; their sides
  show the level at full brightness, dimmed only during the boss fights at the ends of Sultan's Dungeon and Jafar's
  Palace (the sides fade dark as the fight begins and Aladdin is kept on the original screen, and fade back in once
  the boss is beaten and the way on to the exit opens) and the final fight with Jafar. That fight is framed on its
  arena, centred, with the cobra in full view, and its sides are dimmed the same way from the camera's pan into the
  arena to the ending's fade; once the cobra sets the floor on fire, both sides are the arena going on (the room's
  pillars on the left give way to its arched panels, as on the right), the burning floor and the spiked arch over it
  run on across them, with the game's own animated tiles and the arch's wave travelling through them, and the
  fireballs fly on out into the sides. Past a level's ends the sides show only
  its far background, at full brightness: no platforms, props or sprites, nothing that looks like a way on. In the Lava
  Cavern, which scrolls by itself, the screen's limits follow the wide picture's edges (in widescreen only, a small
  change to the original's rules): Aladdin can run on to 16 px from the picture's right edge, is only left behind
  once he is 48 px past its left edge (before, he was killed while still in view in the left side), and the falling
  lava and fire stay until they leave the picture; on a width change these limits ease to the new edges instead of
  moving Aladdin. A Whole New World's gems, too, fly on until they leave the picture. The level's end and exit are the same at every width. The attract
  demos stay in their original shape and play exactly as recorded at every width, with Remove Slowdown and cheats
  on too.
- **Level select**: all 19 levels in the order the game plays them, A Whole New World included, started through
  GAME START with the game's own hidden stage select.
- **Cheats**: invincibility, infinite health, lives, apples and gems, and Capcom's developer mode (10 hearts and 99
  apples at the start, a stage select on the title: hold L or R, then up / down for the stage, left / right for the
  act). The developer code works on its own too: on the OPTIONS
  screen press L, R, START, SELECT, X, Y, A, B on controller 2.
- **Music player & sound test**: 39 songs and 76 effects, played by the game's own driver (songs written to follow
  another one are preceded by it, so every one is heard as in the game).
- **Achievements**: 17 — one per stage and for defeating Jafar's cobra, plus feats such as finishing without a
  continue, clearing a stage without losing a life, ten hearts, a hundred gems, beating Jafar without losing a
  heart, and finding the developer code.
- Emulation fixes: battery RAM is only mapped when the cartridge has some. Aladdin uses that as a copier check — with
  it answering, the attract demo froze and Jafar's cobra had 127 hit points instead of 7. HDMA switched on partway
  down the screen now runs as on the console: Aladdin turns it on at line 8, and the raster bands of Agrabah's
  skyline and the Lava Cavern's lava sea were missing (the whole background scrolled at one speed).

### Aladdin — Master System (SIMS, 1994)

- **Widescreen** up to 21:9 (404 px wide with CRT pixels, 460 with square ones) — the Master System's background
  is only 256 pixels wide, so everything beyond it is drawn from the level map (the parallax bands too), and
  stage 7's reloaded tile graphics come from the ROM. Objects wake, stay awake and are drawn across the whole
  width at their true positions past the hardware's 256-pixel sprite limit; objects that no longer fit in the
  console's 64-entry sprite table are drawn as extra sprites. Near a stage's ends the picture slides instead of
  showing black, with the parallax bands holding still with the level; Jafar's room carries its pillars on past
  its ends (dimmed); the stone counter moves to the right edge. The story scenes are wide too: each carries its
  own picture on into the sides (31 of the 36; the framed pictures, text and characters stay where the game puts
  them, characters who walk off go on into the sides, and those the game brings in from off its screen walk in
  from the wide picture's edge with their own steps), the Genie's intro and the title fill the width with
  their background, and the ending's starfield and sea run the whole width. Menus, password, game over and the
  five scenes whose picture can't be carried on stay in their original shape, and so do the attract demos, which
  play exactly as recorded at every width.
- **A visual fix to the original** (at every width, 4:3 and widescreen off too): the game changes the scroll of
  its parallax bands a line late, so in The Rooftops (1-2) a line of the roofs' sky-orange edge floated sideways
  in the brown city above them, and the far city and the Market's floor (1-1) had stray one-line dashes at their
  bands' edges. Those lines are now drawn with their own band's scroll, as if the split came on time. Only the
  picture changes: the game plays exactly as the original.
- **Level select**: all 10 stages, numbered as the developers' TEST MODE numbers them (1-1 to 7), entered through
  the password screen as a player would.
- **Cheats**: invincibility (the game's own, as the developers' password AIQY gives it, extended to the palace
  guards' grab), infinite health, infinite stones, and the developers' hidden TEST MODE (the title's PASSWORD opens
  it: character viewer, stage select, sound test, input test, the story scenes). The game has no lives (continues are unlimited), and its gems are the
  health bar.
- **Music player & sound test**: 20 music tracks and 60 effects, played by the game's own driver (the effects that
  only play in menus and story scenes, or not at all, are listed by number).
- **Achievements**: 17 — one per stage and for defeating Jafar, plus finishing from GAME START without a password
  or a game over, clearing a stage without losing a gem, a flawless tour and beating Jafar without a scratch.

## Soundtrack packs (MD+ and MSU-1)

CD-quality fan soundtracks made for flash carts and emulators can replace the music of the Mega Drive game (**MD+**
packs, made for the MegaSD and the Mega EverDrive Pro) and of the SNES game (**MSU-1** packs, made for the SD2SNES /
FXPak and higan-style emulators). None are included: you supply them, as you do the ROMs. A pack's patch is applied
to the console's copy of the ROM in memory (the files are never changed), and the game's sound effects stay the
console's own; the patch mutes the console's music where the pack has a track.

| | Mega Drive: MD+ | SNES: MSU-1 |
|---|---|---|
| A pack is | a `.cue` sheet, a `.bps` (or `.ips`) patch for the USA ROM (`ED427EA9`) and the music as `.wav` files | the tracks `NAME-1.pcm`, `NAME-2.pcm`, ... with an MSU-1 patch (`.bps` / `.ips`) for the USA ROM (`124D8E4D`), or a ROM already patched for them (`.sfc`) |
| Its folder goes in | `soundtracks` next to `AladdinCollection.exe` | `msu` next to `AladdinCollection.exe` |
| Example | `soundtracks\Aladdin CD - MD+\Aladdin (USA).cue`, `...\Aladdin (USA)[MDplus, ArcadeTV](...).bps`, `...\01-NENAW_nl.wav` ... `20-GAMEOVER.wav` | `msu\Aladdin (USA) (1993) (Action Platform) (MSU1) (Audio Orchestral) (Super Nes)\Aladdin_(USA)_(MSU1)-2.pcm` ... `-41.pcm` and `Aladdin_(USA)_(MSU1).sfc` |
| Switching it | starts with the game (or *Reset Game*) | at once, also in a level: the game holds for about a second while the console's sound driver restarts, and the song playing starts again in the new music |

Choose a pack under *Options → Audio → Soundtracks* (per game; *Open Soundtracks Folder* / *Open MSU-1 Folder* on
Windows) and set its level with *Soundtrack Volume*. A pack is recognised by its contents, not its name: the BPS
patch's source CRC says which ROM it is for, an IPS patch must add MSU-1 code to the SNES ROM, and a pre-patched ROM is
only used once checked to be your ROM with MSU-1 code added and nothing else changed (the same size; every difference
in the header's checksum, in the ROM's free space or in at most 16 short hooks). A downloaded folder that wraps the pack
in one more folder works too, and either kind of pack is found in either folder (or in a folder next to the ROMs). On
Android, *Options → Audio → Add Soundtrack…* copies a pack's folder into the app; MSU-1 packs can also be copied over
USB into `Android/data/com.darhkfox.aladdincollection/files/msu`.

What works with a pack: the music changes as in the game (each level's and screen's tune, the boss tunes - in
widescreen held until the boss is in the 4:3 view, as with the original music -, a life lost, the level's end);
the game's pause pauses it; save states and rewind take the track back to where it was (its place is
saved with the state); widescreen, cheats, the level select, Remove Slowdown and achievements work as usual, and the
attract demos play as recorded (checked against the original ROM: SNES, the same state in every demo frame; Mega
Drive, every demo ends on the same frame with its input at the same place, Aladdin a frame early or late in a few
frames of each, where the MD+ code's own time shifts a frame's work). Tracks play once or repeat from
their loop point as the pack says: an MSU-1 track's header loop point and the patch's repeat bit; for MD+, the cue
sheet's `REM LOOP [n]` / `REM NOLOOP` override the command's own (as on the MegaSD and in Genesis Plus GX); a track the
pack doesn't have leaves the music as it is (MD+, as on the MegaSD) or is reported missing to the patch
(MSU-1), which decides what plays.

Notes on the packs tested:
- *Aladdin CD - MD+* (ArcadeTV, 2023): the patch puts an "MD+ SOUND ENABLED" screen of its own before the game (about
  7 s, and the game's start-up then runs late for the level select and the launcher's preview): it is skipped (the
  original reset vector). Its tune table has the boss tune (`$32`) and the bonus jingle (`$12`) the wrong way round (a
  boss fight played the 5.5 s jingle, which ends in silence): the collection corrects those two bytes of that exact patch
  (`kPackFixes` in `src/app/soundtrack.cpp`, by the patched ROM's CRC `4A7C033C`). Its cue sheet marks every track
  `REM LOOP`, the jingles too, so one heard for longer than itself (the continue screen's after 8 s) starts again, as on
  a MegaSD. The pack's tracks 19 (`CONTINUE`) and 20 (`GAMEOVER`) aren't played by its patch; the Music Player lists
  them. The pre-patched `Aladdin (USA).bin` in the pack isn't used: the `.bps` is applied to your verified ROM.
- *Aladdin (USA) (MSU1) (Audio Orchestral)*: no patch file, only `Aladdin_(USA)_(MSU1).sfc` (CRC `88E2EA66`): it is the
  verified ROM with two 4-byte hooks (`$00:B49C`, `$00:B544`, the sound driver's command and song uploads), the
  header's checksum and 905 bytes of MSU-1 code in what was free space at `$27:E940`, so it is used. 40 tracks (2-41).

## Default controls

| | Keyboard | Xbox pad |
|---|---|---|
| D-Pad | Arrow keys | D-Pad / left stick |
| Mega Drive A / B / C | Z / X / C | X / A / B |
| SNES B / A / Y / X | Z / X / A / S | A / B / X / Y |
| SNES L / R / Select | Q / W / Space | LB / RB / left stick press |
| Master System 1 / 2 | Z / X | A / B |
| Start (Master System: Pause) | Enter | Start |
| Pause menu | Esc | Back / View |
| Fast forward (hold) | Tab | Right trigger |
| Rewind (hold) | R | Left trigger |

| Shortcut | Keyboard |
|---|---|
| Quick save / load | F5 / F9 |
| Fullscreen | F11, Alt+Enter |
| Screenshot | F12 |

Every key and button can be changed under *Options → Controls*, separately for each console; so can the keys and
pad buttons for fast forward, rewind, the menu, quick save, quick load and screenshot. Picking a key or button
that is already in use swaps the two. Pressing a key on a gamepad row (or a pad button on a keyboard row) says so
instead of waiting. Also under *Controls*:

- *Menu Buttons*: in the menus the bottom face button confirms and the right one goes back (Xbox's layout, the
  default), or the other way round (Nintendo's).
- *Other Controllers* (PC): read controllers that aren't Xbox-style (DirectInput: a Switch Pro or PlayStation pad
  without Steam). On by default; switch it off if one sends presses you didn't make.

A button held while a controller connects (or reconnects) doesn't count as a press until it is let go. A
controller that sends random presses by itself (a Switch Pro Controller left in its full report mode by Steam or
another program is a known case) is ignored, with a message saying which, until it has been quiet for a few
seconds: reconnect it, or play it through Steam. On Android, controllers without an SDL mapping (some PlayStation
and 8BitDo pads) are read as plain joysticks in the PlayStation button order.

The mouse works on the launcher, its menus and the Music Player (see *Launcher* above). In fullscreen the pointer
appears when the mouse moves on the launcher and hides again at a key or button press or when a game starts. In a
game the mouse does nothing.

## Files

Settings are stored in `settings.ini`, earned achievements in `achievements.ini` (each with the date and time it
was earned; older files, with the date only, still load), save states in `saves\`, screenshots in
`screenshots\`, the achievements export in `achievements export\`; soundtrack packs go in `soundtracks\` (MD+) and `msu\`
(MSU-1), launcher pictures in `covers\`. These live next to the exe (the folder is
portable), or in `%LOCALAPPDATA%\Aladdin Collection` if that folder isn't writable. Delete `settings.ini` to
restore the defaults.

## Building from source

Requirements: Windows 10/11, Visual Studio 2022 (or the Build Tools) with the C++ workload, CMake 3.20+.
Slang shaders use librashader through its C header (`third_party/librashader`, loaded at run time): when
`third_party/librashader/librashader.dll` is there the build copies it, with the presets of
`third_party/slang-shaders`, next to the exe; without it the game runs on its built-in filters.

```
build.bat                                         :: builds build\bin\AladdinCollection.exe (Release, static CRT)
package.bat                                       :: dist\Aladdin Collection: the exe, per-game shortcuts and the
                                                  ::   ROMs in this folder, librashader.dll and the shader
                                                  ::   presets, for local play
powershell -ExecutionPolicy Bypass -File release.ps1
                                                  :: dist\Aladdin-Collection-v<version>-win64.zip:
                                                  ::   the exe, readme.txt, LICENSE, librashader.dll and the
                                                  ::   bundled shader presets (no ROMs)
build\tests\app_tests.exe                         :: (built with the exe) tests of the update check's version
                                                  ::   comparison and the achievements export, offline; add
                                                  ::   --fetch <url> for one live GET of a "latest release" page,
                                                  ::   --export <dir> [achievements.ini] or --sheet <file.png>
```

### Android

The same code builds for Android (`android/`), with SDL2 2.32.10 compiled from source for the window, sound and
controllers. You need the Android SDK with NDK 28.2 and CMake 3.31.6 (both from the SDK manager) and JDK 17–21.

```
powershell android\fetch_sdl.ps1
cd android
gradlew assembleDebug
```

### Source layout

| Path | Contents |
|---|---|
| `src/core/` | Mega Drive (`m68k`, `vdp`, `ym2612`, `md`: with the MD+ cartridge overlay), SNES (`cpu65816`, `apu`, `dsp`, `ppu`, `snes`, and `msu1`: the MSU-1 chip) and Master System (`sms`, `sms_vdp`) hardware; the Z80 and SN76489 (`z80`, `psg`) serve both Sega consoles. Each video chip renders a widened picture with hooks for the game modules, and the high frame rate's in-between pictures (`hfr_match`: matching the sprites of two frames) |
| `src/game/` | `game.h` (the module interface, typed per console), the three modules `md_aladdin.cpp`, `snes_aladdin.cpp`, `sms_aladdin.cpp` and each console's helpers (`*_common.cpp`: verified ROM patching, scripted input) |
| `src/app/` | Launcher, sessions (`session.cpp` and one per console: `session_md/snes/sms.cpp`), soundtrack packs (`soundtrack.cpp`: finding MD+ and MSU-1 packs, the ROM a pack runs on, the MD+ player), menus, music player / sound test, rewind, achievements and their export (`achexport.cpp`), the update check (`updatecheck.cpp`, `app_updates.cpp`; the repository is `kUpdateRepo`), settings, ROM finder, PNG writer, touch controls; per platform `app_win.cpp` / `app_android.cpp` and `sys_win.cpp` / `sys_android.cpp` |
| `src/platform/` | Windows: Direct3D 11 video and DirectWrite UI text, slang shader presets through librashader (`slang.cpp`), XAudio2 audio, keyboard / XInput / DirectInput input. `android/`: OpenGL ES 3.0 video, SDL audio and controllers |
| `third_party/` | `librashader` (the DLL, its C headers and licence), `slang-shaders` (the bundled presets, with their licences) and `stb` (`stb_image.h`, which reads the cover pictures) |
| `src/tools/` + `tools/` | Headless cores as DLLs with Python wrappers (`mdt.py`, `snt.py`, `smst.py`) used for the reverse engineering and testing; `tools/re/` the scripts behind the notes in `docs/` |
| `docs/` | Reverse-engineering notes per game (`re_*_game.md`: logic, levels, cheats, sound; `re_*_wide.md`: widescreen) |

## Known limitations

- Mega Drive: near a level's ends the scenery holds still while Aladdin walks for the first stretch of camera
  travel (54 px at 16:9, 128 px at 21:9), and the level's start shows less behind Aladdin and more ahead, as the
  game's own camera is kept. After a mid-level width change, objects in the newly shown columns appear when the
  view next scrolls over them. A Rug Ride save state from an older version doesn't hold what had already passed
  behind the carpet: it is missing on the left of the screen until it would have left it. Invincibility covers hits
  only: pits, lava and the Rug Ride's walls still cost a life. Developer Map View puts Aladdin back where the view
  is when it's switched off, so switch it off over solid ground. The cheats apply in a game, not on the title screen or in the attract demos. In Jafar's Palace at 21:9
  with CRT pixels, a knife thrown from the left edge can vanish as it hits when every other sprite is in view
  (making room would mean removing a guard who is partly on screen). Some kinds of enemy share one set of colours
  (the game gives it to whichever appeared last), so one that only the wider view shows can take another's colours
  at the sides until it comes into the 4:3 area - e.g. the Cave of Wonders' idol, for a few frames at the right edge
  as it comes in at 21:9 while a bat is in view. A boss arena is framed only as far as the extra width reaches: the
  Cave's idol fight spans 677 px at 4:3, so at 16:9 the view still follows Aladdin near its ends; at the end of
  Agrabah Rooftops the arena is the level's last screen, so the extra width can only go to its left, as before.
  On the screens around the levels the game puts its flyers where they begin just off its own screen, so at 21:9
  some appear inside a side rather than at its outer edge: the Genie at the Disney / Virgin logo (whole, 13 px in),
  Aladdin and Abu at "Level Complete" (40 px in), the carpet across the moon (about 30 px left of the 4:3 picture,
  on the scene's first frame). The Genie's bonus wheel (its curtains), the instructions screen and the full
  scarab (its rings) are cut off by the screen's edges and stay 4:3; the story cards' dunes at the sides are the
  picture's edges mirrored, as the picture doesn't carry on across its own edges.
- Master System: the game's own camera is kept (Aladdin's position is an 8-bit screen coordinate), so near a
  stage's ends the scenery holds still while Aladdin walks for the first stretch of camera travel. Objects wake as
  they come within the original's margins of the view as shown, so those entering from the right act as much
  earlier as the extra width takes the camera to cover (and A Whole New World's birds from the left start
  correspondingly earlier) - in the Market the barrels that trip the guard chasing Aladdin roll that much earlier,
  so he is tripped earlier too, and in a wide view he shows trailing behind for a couple of seconds where the 4:3
  screen loses him. The caves' and the Sultan's Palace's fixed hazards (falling and rolling boulders, geysers,
  spikes, crumbling ledges) are the exception: they act, and are put back, at the moment the 4:3 game has them, so one put
  back just past the original screen (a fallen boulder back at the ceiling) can be seen doing so at a side's far end. The walls the game draws in perspective (the Market's sky gaps, the Rooftops' roof ends) have
  no look of their own off the original screen: at the sides they show the nearest look the game has for them. In a wide view
  Aladdin left behind by the scrolling (or pushed back by a wall) is lost only at the picture's edge, not at the
  4:3 screen's, and can still walk back in until then.
  Infinite Health keeps the gems, but the guards' grabs, the carpet's crashes, pits and lava still cost a life;
  Invincibility covers all but pits and lava. With Remove Sprite Flicker off, sprites in the sides count towards
  the 8-per-line limit. The attract demos play at 4:3: they replay recorded input, which only stays on its path
  with the original code. Five story scenes stay 4:3 with black bars, as their picture is cut off by its edges
  and can't be carried on: night by the sea (a rock at the right edge), the two falls through the collapsing cave
  (rock walls), the palace garden at night (a roof's slope) and the Genie's cave (lit from the middle). In the
  wide scenes a character the game walks in from off its screen is drawn walking in from the wide picture's edge
  with the game's own frames and speed, reaching the 4:3 edge just when the game brings it there (the scenes'
  timing is the original's; one the game moves in right as a scene starts is already partway across the side as
  it fades in), and one the game removes once it has walked off walks on out of the picture. Characters who leap
  or swoop in (the pole vault, the carpet in the escape) still appear as they come, Jafar's snake grows out of the
  4:3 picture's right edge, and a character who walks off and stops nearby stays visible in the side.
- SNES: the attract demos play at 4:3 and without Remove Slowdown or cheats: they replay recorded input (lag frames
  included), which only stays on its path with the original code. Invincibility and infinite health don't prevent
  pits, crushes or being left behind by the scrolling. Infinite Gems keeps the counter at 99, so every gem completes
  a hundred (an extra heart, then an extra life). Beside Jafar's arena the burning floor and the spiked arch are
  the game's own tiles repeated (the game paints them only on its screen), the arch's wave on the left side worked
  out ahead from the game's own motion; the room's right side, past its end, is the room mirrored, and once the
  floor burns the left side is the arena mirrored too. The carpet rides keep Aladdin within the 4:3 area, and
  the game's screen-wide boxes (the "ABU NO!" box after the lamp) stay 4:3 with the sides dark outside them.
  Narrowing the picture while Aladdin presses against the Lava Cavern's wall leaves him past the new wall (even
  beside the picture) until the scrolling catches up with him, rather than pushing him. In the levels with a half-speed
  background, up to about 70 px of that background is shown mirrored at a level's start at 21:9 (the level itself
  is real to the edge). In Agrabah Rooftops the far hills behind the palace can show a seam in the left side area:
  the game never scrolls that far into them, so their picture doesn't join up there. Inside the Lamp 3's tower is
  one screen wide and holds nothing beside it: the view is centred on it, and its gold pillars go on into both sides
  as a solid gold wall (made from the pillars' own plain outer edge, their bands running on at the same height and
  scrolling with them), so the tower reads as a shaft in the wall, without statues, platforms or sprites beside it
  (black with *Past Level Ends: Black*); between the tower and the corridor at its top the view glides over as the
  screen scrolls, the corridor coming down onto the wall. The corridor's start stays in the left side as Aladdin
  walks on, also after the game closes it behind him. The STAGE CLEAR panel keeps its
  4:3 frame on a purple background that fills the width; the Genie flies out into the purple whole, but only about
  his own width: the game starts, turns and removes him just past the 4:3 edges, so he appears and vanishes beside
  the panel. The
  bonus wheel and the story scenes between stages are on black and so look the same as before. The staff roll is wide:
  its night sky, clouds and city run the whole width (the moon once), the credits stay in the middle, and the birds
  and the carpet fly out into the sides at their real place. The birds come in a little past the 4:3 edge, where the
  game creates them. For the roll's first few seconds (left side) and beside THE END (right side) the sides lie past
  the roll's ends and show its sky mirrored (black with *Past Level Ends: Black*). The
  level select starts with new-game stats (3 lives, 3 hearts, 10 apples).
- Soundtrack packs: an MD+ pack changes only at a game's start (*Reset Game*), as its patch changes the game's start-up
  and interrupt vectors. A save state goes on with the soundtrack chosen now: one made with an MD+ pack and loaded
  without it (or the other way round) keeps the music it has until the game asks for the next tune; with an MSU-1 pack,
  in a level the song starts again in the chosen music (elsewhere at the next song). Switching an MSU-1 pack outside a
  level (the title, a story scene) takes effect from the next song. In fast forward an MD+ track plays on at its own speed (it
  isn't sped up with the game), an MSU-1 track keeps the game's time.
- Save states made by a newer version may not load in an older one.

## Credits and licences

- PC port by Darhkfox — [YouTube](https://www.youtube.com/adhgaming) ·
  [Reddit u/Darhkwing](https://www.reddit.com/user/Darhkwing) · [GitHub](https://github.com/Darhkfox) ·
  [X](https://x.com/ADH_Gaming) · [Patreon](https://www.patreon.com/c/TeamRedTail). The Team RedTail logo at the
  launcher's top left opens the Patreon page too.
- The port's own code is released under the MIT licence (`LICENSE`). The emulation cores are written from
  scratch. UI text uses the system font (Segoe UI through DirectWrite on Windows, Android's own font on Android).
- The MSU-1 chip (`src/core/msu1.cpp`) and the MSU-1 pack handling (finding a pack's tracks, applying its patch) are
  ported from the Super Mario All-Stars PC port by the same author (MIT licence, as here); the chip here resamples with a
  cubic interpolation and keeps its state with the session's save-state tag. MD+ is the CD-audio standard of the MegaSD
  (Terraonion) and the Mega EverDrive Pro (Krikzz), MSU-1 byuu's (higan) and the SD2SNES's. Soundtrack packs, their
  patches and music belong to their makers (e.g. ArcadeTV for *Aladdin CD - MD+*) and aren't part of this project.
- The Android build uses SDL2 by Sam Lantinga and contributors, zlib licence (fetched by
  `android/fetch_sdl.ps1`, not stored in this repository).
- Slang shaders run through librashader by Ronny Chan (chyyran), Mozilla Public License 2.0
  (`librashader-LICENSE.md` next to the exe). The bundled presets are unmodified files from the libretro
  slang-shaders collection, each with its own licence (public domain, GPL v2 or later, MIT): see
  `shaders/README.txt`. They are separate files loaded at run time, not part of the program.
- Cover and background pictures are read with stb_image by Sean Barrett, public domain (or MIT, at your choice;
  `third_party/stb`). The Arabian Nights style's ornaments are drawn by the program itself.
- *Aladdin* © Disney. The Mega Drive version © 1993 Sega / Virgin Games; the SNES version © 1993 Capcom; the
  Master System version © 1994 Sega. This project contains no ROM data. It is an unofficial fan project, not
  affiliated with or endorsed by Disney, Sega, Virgin, Capcom or Nintendo.
