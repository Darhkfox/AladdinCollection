# Trailer tools

Builds `K:\projects\Aladdin\trailer_work\Aladdin_Collection_PC_trailer.mp4` (1080p60, about 3:20) and its 720p preview
from footage recorded with the real app, in the look of the Mickey & Donald Collection trailers (the pipeline is a port
of that project's `tools/trailer`, adapted to the three consoles). Work files live in `trailer_work\` (`cap\` footage,
states and pads, `inst\` one exe copy per recording, `scripts\` the autotest scripts, `art\` our own covers,
`packs\` copies of the soundtrack packs, `botlog\`); set `TRAILER_WORK` to use another folder.

1. **Gameplay** - `python bot.py <clip> ...` (clips in `clips.py`). A headless search (the tool DLLs + the real game
   modules, `games.py`: `mdt` / `snt` / `smst`) finds inputs that play the clip without hits, lost lives, falls or a
   level change, checks the plan by replaying it without state loads, and writes `cap\<clip>__<k>.state` (app
   save-state format, one per searched variant) + `<clip>.pads` + `<clip>.json` (RAM fingerprints). Clips with takes
   in `lockstep` (the 4:3 / 16:9 / 21:9 comparisons, CRT / square pixels, HUD) are searched in all of them at once.
2. **Recording** - `python record.py <clip> ...` runs the app hidden (`--game <id> --autotest ... --capture-size
   1920x1080`), loads the state, replays the pads and records `<name>.mkv` (lossless) + `.pcm`; the app's RAM
   fingerprint (`marks.txt`) must equal the bot's (takes with only display settings are reported as not searched).
3. **App screens and music** - `python ui.py <name> ...` (`all`, `music`): launcher, menu styles, covers, Show Games,
   update badge (a newer release faked in the instance's settings.ini; `--autotest` never goes online), Credits,
   controls, achievements, level select, cheats, save states, options, the Music Player (MD+ / MSU-1 with Tab), the
   Mega Drive title and the Master System story scenes, and the music bed's recordings (`recaudio`, sound only).
   Soundtrack packs: copy `soundtracks\Aladdin CD - MD+` and `msu\Aladdin (USA) ... (Audio Orchestral) (Super Nes)` from
   `dist\Aladdin Collection` into `trailer_work\packs\soundtracks` / `packs\msu` first (never move or change dist\);
   instances that need them get junctions to those copies, removed again (only the link) when the run ends.
4. **Art** - `python art.py`: our own typographic covers and a dusk background (PIL, no game or box art).
5. **Android** - the phone segment (`cap\android_phone_60.mkv`) was recorded on the emulator (`bbl_phone`, headless)
   with `adb shell screenrecord` and `input tap/swipe`, ROMs pushed to the app's `files\roms`.
6. **Compositing** - `python trailer.py` (full encode + 720p preview + texts + contact sheet), `python trailer.py
   preview [scene]`, `stills <scene> <n>`, `bed` (music bed only), `sheet`, `text`. Scenes, captions and the medley
   (`MUSIC`) are in `timeline()`; the music bed is normalised with ffmpeg loudnorm (-14 LUFS, true peak -1.5 dBTP).
7. **Checks** - `python check.py`: stills at every scene and caption, black-frame scan, loudness per section and the
   pack sections correlated against the pack files.

Helpers: `tb.py` (paths, instances, scripts, app-format states), `sheets.py` (contact sheets of recordings),
`probe.py` / `probe_sounds.py` (quick looks at the app and the track lists), `dups.py` / `lagframes.py` / `lagprobe.py`
(looking for SNES slowdown: in clean runs there is too little for a visible before / after, so the trailer shows the
option instead), `snes_staff.py` (an attempt to reach the SNES staff roll headlessly: not working yet).
App script commands used (autotest only, all already in the app): `record` / `stoprec` / `recaudio`, `loadstate`,
`pads`, `set`, `mark`, `click`, `award`.
