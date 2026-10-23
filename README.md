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
and all of these inside a `.zip` work. ROMs copied in while the launcher is open are picked up within a second.

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

**Widescreen**
- 16:10, 16:9, 2:1 and 21:9 ultrawide (fills 3440×1440 and 2560×1080 screens), for all three games. The width
  in pixels follows each console's picture and the pixel shape: with the default CRT pixels (each console's
  picture on a 4:3 television) 21:9 is 576×224 on the Mega Drive, 468×224 on the SNES and 404×192 on the
  Master System; with square pixels 536, 536 and 460.
- More of each level is really drawn at the sides: the games are patched so the level, enemies and items are
  streamed, spawned, moved and drawn across the whole width, instead of popping in at the edges of the original
  screen. Where the console's own background memory can't hold the wider view, the extra columns are drawn
  straight from the level map.
- The view stays inside the level; level edges are masked. The HUD is re-anchored to the screen edges (can be
  switched off). Title screens, menus and cut-scenes keep their original shape automatically.
- The width (and cheats) can be changed at any time, also mid-level; save states remember the width they were
  made at.

**Video**
- Borderless fullscreen or windowed, monitor selection (by name; dragging the window to another screen selects
  it), window size 1×–6×, integer scaling, CRT (4:3 TV) or square pixels, or stretch.
- Filters: nearest, sharp bilinear, bilinear, scanlines, CRT. Direct3D 11.
- Sprite flicker removal (no sprites-per-line limit), V-Sync, FPS counter.
- Black frame insertion for sharper motion on 120, 180 and 240 Hz displays.

**Colours**
- Profiles: Accurate (each console's measured output levels), Linear, Vivid, Warm CRT, Cool, Grayscale, Sepia,
  Handheld LCD. Brightness, saturation and gamma.

**Audio**
- Mega Drive: YM2612 + SN76489 with the YM2612 "ladder effect"; SNES: S-SMP / S-DSP with original (gaussian) or
  crisper (cubic) interpolation; Master System: SN76489. Volume and an output filter (off, the console's own,
  or warm).
- MD+ soundtrack packs can be used with the Mega Drive game (drop the pack's folder into `soundtracks`).

**Music player & sound test**
- Every music track and sound effect of each game, played by the game's own sound driver, with channel meters
  (6 FM + 4 PSG on the Mega Drive, 8 voices on the SNES, 4 PSG channels on the Master System).

**Level select, cheats and achievements** — see *The games* below. With achievements on (the default), cheats,
the level select, fast forward and rewind are switched off; save states still work. The collection adds two
achievements of its own: *A Whole New World* (defeat Jafar in all three versions) and *Completionist*.

**Other**
- Keyboard and gamepads: Xbox-compatible pads (XInput) and other USB pads (DirectInput), fully rebindable per
  console, hot-plugging.
- 5 save-state slots per game (F5 / F9), fast forward (hold Tab or the right trigger, 4×), rewind (hold R or
  the left trigger, up to the last minute of play), screenshots (F12).

## The games

### Aladdin — Mega Drive / Genesis (Virgin Games, 1993)

- **Widescreen** up to 21:9 (576 px wide with CRT pixels, 536 with square ones). The columns beyond the 4:3 view
  are drawn per line straight from the level map in RAM, so the planes and the game's own camera stay untouched;
  objects spawn just outside the wider view, and their despawn window and sprite clip are widened. The HUD (lamp,
  lives, apples, gems) is moved to the screen edges; level edges are blacked out with the HUD staying on top.
  Abu's bonus rounds and the Rug Ride stay 4:3 (the Rug Ride sends its obstacles in at the 4:3 edge on a timer).
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
  works, a mid-level width change is instant and the game's own column streaming is untouched. The view stops at
  level edges and where the camera can't go, the background's parallax is corrected where the view is split, and
  the Carpet Escape's wave and A Whole New World widen too. Objects appear, act and are drawn across the whole
  width; the HUD and the PAUSE / ACTION texts are re-anchored. Boss arenas (the Market at Dusk, Jafar) keep the
  original camera with the unreachable sides dimmed.
- **Level select**: all 19 levels in the order the game plays them, A Whole New World included, started through
  GAME START with the game's own hidden stage select.
- **Cheats**: invincibility, infinite health, lives, apples and gems, and Capcom's developer mode (10 hearts and 99
  apples at the start, a stage select on the title). The developer code works on its own too: on the OPTIONS
  screen press L, R, START, SELECT, X, Y, A, B on controller 2.
- **Music player & sound test**: 39 songs and 80 effects, played by the game's own driver (songs written to follow
  another one are preceded by it, so every one is heard as in the game).
- **Achievements**: 17 — one per stage and for defeating Jafar's cobra, plus feats such as finishing without a
  continue, clearing a stage without losing a life, ten hearts, a hundred gems, beating Jafar without losing a
  heart, and finding the developer code.
- Emulation fix: battery RAM is only mapped when the cartridge has some. Aladdin uses that as a copier check — with
  it answering, the attract demo froze and Jafar's cobra had 127 hit points instead of 7.

### Aladdin — Master System (SIMS, 1994)

- **Widescreen** up to 21:9 (404 px wide with CRT pixels, 460 with square ones) — the Master System's background
  is only 256 pixels wide, so everything beyond it is drawn from the level map (the parallax bands too), and
  stage 7's reloaded tile graphics come from the ROM. Objects wake, stay awake and are drawn across the whole
  width at their true positions past the hardware's 256-pixel sprite limit; objects that no longer fit in the
  console's 64-entry sprite table are drawn as extra sprites. Level edges are masked, Jafar's room carries its
  pillars on past its ends (dimmed), the stone counter moves to the right edge. Title, menus, story scenes, game
  over and the ending stay in their original shape.
- **Level select**: all 10 stages, numbered as the developers' TEST MODE numbers them (1-1 to 7), entered through
  the password screen as a player would.
- **Cheats**: invincibility (the game's own, as the developers' password AIQY gives it), infinite health, infinite
  stones, and the developers' hidden TEST MODE (the title's PASSWORD opens it: character viewer, stage select,
  sound test, input test, the story scenes). The game has no lives (continues are unlimited), and its gems are the
  health bar.
- **Music player & sound test**: 20 music tracks and 60 effects, played by the game's own driver (the effects that
  only play in menus and story scenes, or not at all, are listed by number).
- **Achievements**: 17 — one per stage and for defeating Jafar, plus finishing from GAME START without a password
  or a game over, clearing a stage without losing a gem, a flawless tour and beating Jafar without a scratch.

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
that is already in use swaps the two.

## Files

Settings are stored in `settings.ini`, earned achievements in `achievements.ini`, save states in `saves\`,
screenshots in `screenshots\`; soundtrack packs go in `soundtracks\`. These live next to the exe (the folder is
portable), or in `%LOCALAPPDATA%\Aladdin Collection` if that folder isn't writable. Delete `settings.ini` to
restore the defaults.

## Building from source

Requirements: Windows 10/11, Visual Studio 2022 (or the Build Tools) with the C++ workload, CMake 3.20+.
The Windows build has no third-party dependencies.

```
build.bat                                         :: builds build\bin\AladdinCollection.exe (Release, static CRT)
package.bat                                       :: dist\Aladdin Collection: the exe, per-game shortcuts and the
                                                  ::   ROMs in this folder, for local play
powershell -ExecutionPolicy Bypass -File release.ps1
                                                  :: dist\Aladdin-Collection-v<version>-win64.zip:
                                                  ::   the exe and readme.txt only (no ROMs)
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
| `src/core/` | Mega Drive (`m68k`, `vdp`, `ym2612`, `md`), SNES (`cpu65816`, `apu`, `dsp`, `ppu`, `snes`) and Master System (`sms`, `sms_vdp`) hardware; the Z80 and SN76489 (`z80`, `psg`) serve both Sega consoles. Each video chip renders a widened picture with hooks for the game modules |
| `src/game/` | `game.h` (the module interface, typed per console), the three modules `md_aladdin.cpp`, `snes_aladdin.cpp`, `sms_aladdin.cpp` and each console's helpers (`*_common.cpp`: verified ROM patching, scripted input) |
| `src/app/` | Launcher, sessions (`session.cpp` and one per console: `session_md/snes/sms.cpp`), menus, music player / sound test, rewind, achievements, settings, ROM finder, PNG writer, touch controls; per platform `app_win.cpp` / `app_android.cpp` and `sys_win.cpp` / `sys_android.cpp` |
| `src/platform/` | Windows: Direct3D 11 video and DirectWrite UI text, XAudio2 audio, keyboard / XInput / DirectInput input. `android/`: OpenGL ES 3.0 video, SDL audio and controllers |
| `src/tools/` + `tools/` | Headless cores as DLLs with Python wrappers (`mdt.py`, `snt.py`, `smst.py`) used for the reverse engineering and testing; `tools/re/` the scripts behind the notes in `docs/` |
| `docs/` | Reverse-engineering notes per game (`re_*_game.md`: logic, levels, cheats, sound; `re_*_wide.md`: widescreen) |

## Known limitations

- Mega Drive: the game's camera is kept exactly as the original (Aladdin's walking limits are in screen
  coordinates), so at a level's edges the extension is black. After a mid-level width change, objects in the
  newly shown columns appear when the view next scrolls over them. Invincibility covers hits only: pits, lava and
  the Rug Ride's walls still cost a life. Developer Map View puts Aladdin back where the view is when it's
  switched off, so switch it off over solid ground. Attract demos are recorded inputs and can play out differently
  in widescreen.
- Master System: with widescreen on the camera is the original's (Aladdin's position is an 8-bit screen
  coordinate), so level edges are masked. Objects start acting a little earlier than in the original, so the
  attract demos drift. In stage 7 a few companion sprites that fly in from the left appear inside the left side of
  views wider than 16:10. Animated tiles at the sides show their current frame.
- SNES: invincibility and infinite health don't prevent pits, crushes or being left behind by the scrolling. Infinite
  Gems keeps the counter at 99, so every gem completes a hundred (an extra heart, then an extra life). The dimmed
  sides of a boss arena show the level as stored, not what the fight paints on it (Jafar's fire floor stays in the
  4:3 area). Backgrounds scrolled in horizontal bands (Agrabah's skyline, the lava cavern) get no parallax
  correction where the view stops at a level edge. The staff roll stays 4:3. The level select starts with
  new-game stats (3 lives, 3 hearts, 10 apples).
- Save states made by a newer version may not load in an older one.

## Credits and licences

- PC port by Darhkfox — [YouTube](https://www.youtube.com/adhgaming) ·
  [Reddit u/Darhkwing](https://www.reddit.com/user/Darhkwing) · [GitHub](https://github.com/Darhkfox).
- The port's own code is released under the MIT licence (`LICENSE`). The emulation cores are written from
  scratch. UI text uses the system font (Segoe UI through DirectWrite on Windows, Android's own font on Android).
- The Android build uses SDL2 by Sam Lantinga and contributors, zlib licence (fetched by
  `android/fetch_sdl.ps1`, not stored in this repository).
- *Aladdin* © Disney. The Mega Drive version © 1993 Sega / Virgin Games; the SNES version © 1993 Capcom; the
  Master System version © 1994 Sega. This project contains no ROM data. It is an unofficial fan project, not
  affiliated with or endorsed by Disney, Sega, Virgin, Capcom or Nintendo.
