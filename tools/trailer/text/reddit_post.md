TITLE:
I made a PC (and Android) collection of all three 90s Aladdin games – Mega Drive, SNES and Master System – with true widescreen up to 21:9

BODY:
Hi everyone! 👋

After the Mickey & Donald Collection, I've been working on the next one: **Aladdin Collection – PC Edition**, all three classic versions of Disney's Aladdin in one launcher:

- **Mega Drive / Genesis** (Virgin Games, 1993)
- **Super Nintendo** (Capcom, 1993)
- **Master System** (SIMS, 1994)

🎬 **Trailer:** [YouTube link]
⬇️ **Download + source (free, MIT):** https://github.com/Darhkfox/AladdinCollection

**What you get:**
- True widescreen (16:10, 16:9, 2:1 and 21:9) for all three games, with the extra space showing real level, enemies and items – not stretched or empty
- The screens around the levels go wide too: the Mega Drive title and Rug Ride, the Master System's story scenes, the SNES staff roll
- Level select, cheats (including the developers' own leftovers), 54 achievements with a CSV export, save states, rewind and fast forward
- Remove Slowdown for the SNES game, High Frame Rate for 100 Hz+ monitors, CRT shaders, filters and colour profiles
- A music player for every tune, and support for MD+ and MSU-1 CD-quality soundtrack packs (bring your own)
- Your own covers and launcher background, two menu styles, any controller – and an Android version with touch controls

Quick heads-up so nobody feels misled: **this isn't a decomp.** The original games run on Mega Drive, SNES and Master System cores I wrote for the project, and each game is patched on the fly for the new features. Working out *where* to patch meant a lot of reverse engineering, and some of the behind-the-scenes bits were fun:

- 🖥️ **The Master System's background is only 256 pixels wide**, so everything beyond it in widescreen is drawn straight from the level map – parallax bands included.
- 🌆 **The SNES version scrolls its skylines in raster bands**, so the wide sides work out each band's scroll the way the game's own code would, to keep every layer moving as on the console.
- 🪔 **Inside the Lamp's tower on the SNES is one screen wide with nothing beside it**, so in widescreen its gold pillars carry on into the sides as a solid gold wall.
- 🧞 **Story scenes on the Master System**: characters the game walks in from off its screen now walk in from the edge of the wide picture, with the game's own steps, arriving exactly when they would have.
- 🕹️ **Developer leftovers**: the Mega Drive version's debug screen (level skip, map view, cheat mode), Capcom's debug mode with its title-screen stage select, and the Master System's AIQY invincibility password and TEST MODE are all switchable cheats.

All the notes are in `docs/` on GitHub if you want to dig in.

You'll need your own ROMs (nothing's included): the USA Mega Drive and SNES versions and the European Master System one. Drop them in the folder (zipped is fine) and the launcher finds them, and it will tell you if you've got a different version.

Hope you enjoy it, and I'm happy to answer any questions! 😊
