================================================================================
  ALADDIN COLLECTION  --  PC Edition V1.0
================================================================================

  A custom PC port of the three console versions of Disney's Aladdin, with a
  launcher to pick between them:

     Aladdin  (Sega Mega Drive / Genesis) ....... Virgin Games, 1993
     Aladdin  (Super Nintendo) .................. Capcom, 1993
     Aladdin  (Sega Master System) .............. SIMS, 1994

  The consoles' hardware (Mega Drive, SNES and Master System CPUs, graphics
  and sound chips) is reproduced in native code, so the games run directly
  on Windows -- no separate emulator needed -- with a set of modern extras:
  true widescreen up to 21:9 ultrawide in all three games, fullscreen,
  filters and colour profiles, save states, fast forward and rewind, 54
  achievements, and an in-game menu (cheats, level select, music player and
  sound test).

  This is a personal, non-commercial project. It does NOT include the games
  themselves -- you must supply your own ROMs of cartridges you own.

  >>> IMPORTANT: this needs good dumps of these exact versions. <<<
  The port is built and tuned for them -- the widescreen mode, level select,
  cheats and achievements are matched to their internals. Other regions,
  other revisions, bad dumps and hacks are NOT supported: the launcher tells
  you what the file it found is (for example "is the European version")
  instead of starting it. The ones you want are commonly named:

     Aladdin (U) [!].bin
        2048 KB (2 MB), CRC32 ED427EA9     (Mega Drive / Genesis, USA)
     Aladdin (USA).sfc
        1280 KB, CRC32 124D8E4D            (Super Nintendo, USA)
     Disney's Aladdin (Europe).sms
        512 KB, CRC32 C8718D40             (Master System -- only released
                                            in Europe and Brazil)

  You only need the ROMs of the games you want to play; the others just
  show "ROM not found" in the launcher.

--------------------------------------------------------------------------------
  GETTING STARTED
--------------------------------------------------------------------------------

  1. Unzip the "Aladdin Collection" folder anywhere you like.
  2. Put your ROMs in that folder, next to AladdinCollection.exe, or in a
     "roms" folder inside it.
  3. Double-click AladdinCollection.exe.

  Nothing needs installing. The first launch opens fullscreen; press F11 (or
  Alt+Enter) for a window.

  FINDING THE ROMS (automatic):
     The ROMs are recognised by their contents (CRC32), so the file names
     don't matter. These all work:
       - plain dumps: .bin / .md / .gen, .sfc, .sms
       - interleaved .smd dumps, and dumps with a 512-byte copier header
         (.smd, .smc / .swc / .fig)
       - any of the above inside a .zip (no need to unzip), packed the
         usual way (Deflate) or with LZMA. .7z and .rar files are not
         read -- extract those first.
     The launcher looks next to the program, in its "roms" folder, and in
     the folder it was started from. ROMs you copy in while the launcher is
     open are picked up within a second or so.

  WRONG VERSION?
     If a file is another version of one of the games, its card in the
     launcher says "Wrong ROM version", names the file and says what it is:
     "the European version", "the Japanese version", "the REV 01 version",
     or "a modified copy or a bad dump". Swap it for the one listed above.

  CAN'T UNZIP THE ROM?
     If a zip holds the right ROM but can't be unpacked (another kind of
     compression, such as BZip2, or a password), the card says "Can't
     unzip the ROM" and why. Unzip it, or zip it again the usual way.


--------------------------------------------------------------------------------
  CONTROLS
--------------------------------------------------------------------------------

  Each console has its own buttons, and its own page under Options >
  Controls (from the launcher; in a game, Controls is the game's console).

                       Keyboard             Xbox-style pad
     D-Pad ........... Arrow keys           D-Pad or left stick
     MEGA DRIVE
       A ............. Z                    X  (left button)
       B ............. X                    A  (bottom button)
       C ............. C                    B  (right button)
       Start ......... Enter                Start
     SUPER NINTENDO
       B ............. Z                    A  (bottom)
       A ............. X                    B  (right)
       Y ............. A                    X  (left)
       X ............. S                    Y  (top)
       L / R ......... Q / W                LB / RB
       Start ......... Enter                Start
       Select ........ Space                Left stick press
     MASTER SYSTEM
       Button 1 ...... Z                    A  (bottom)
       Button 2 ...... X                    B  (right)
       Pause ......... Enter                Start

     Pause menu ...... Esc                  Back / View
     Fast forward .... Tab (hold)           Right trigger (hold)
     Rewind .......... R (hold)             Left trigger (hold)

  The pad buttons sit where each console's own are: the Mega Drive layout
  is the one Sega's Genesis collections use, the SNES's matches its pad by
  position. Xbox-compatible pads (XInput) and other USB controllers
  (DirectInput) are supported, a pad plugged in while the game runs is
  picked up automatically, and every key and button can be changed.
  A button held while a pad connects (or reconnects) only counts once it
  has been let go and pressed again. A controller that sends random
  button presses by itself (a Switch Pro Controller that Steam or another
  program left in its full mode does this) is ignored, and a message says
  which: reconnect it, or play it through Steam. On Android, controllers
  without an SDL mapping (some PlayStation and 8BitDo pads) work too, in
  the PlayStation button order.

  THE MOUSE (on the launcher): click a game to choose it, and again to
  play it; click the buttons, the rows of the menus (on a setting's
  value, the left half steps back and the right half on) and the Music
  Player's tracks; the wheel scrolls lists; right-click, or click outside
  a menu, to go back. In fullscreen the pointer appears when the mouse
  moves and hides again when you press a key or a game starts. In a game
  the mouse does nothing.

  In the games:
     Mega Drive ....... A throws an apple, B swings the sword, C jumps.
     Super Nintendo ... B jumps, A throws an apple, Y dashes (the game's
                        own OPTIONS screen can rearrange them).
     Master System .... Button 1 throws a stone (or attacks), button 2
                        jumps; Pause pauses.


--------------------------------------------------------------------------------
  HOTKEYS  (during play)
--------------------------------------------------------------------------------

     Esc ............ Pause menu
     F5 ............. Save state         (into the current slot)
     F9 ............. Load state         (from the current slot)
     Tab ............ Fast forward, 4x   (hold) -- can be switched off
     R .............. Rewind             (hold) -- up to the last minute of
                                         play; can be switched off
                     (fast forward and rewind only work while achievements
                     are switched off)
     F11 ............ Fullscreen / window (Alt+Enter works too)
     F12 ............ Screenshot         (saved in a "screenshots" folder)

  The fast forward, rewind, menu, quick save, quick load and screenshot
  keys and pad buttons can be changed under Options > Controls, like the
  game buttons. Esc always opens the menu too. Picking a key that is
  already in use swaps the two. Only when a game button takes a hotkey's
  key and its own old key is used by another console is the hotkey left
  without one; a message then tells you to choose a new one.


--------------------------------------------------------------------------------
  THE LAUNCHER
--------------------------------------------------------------------------------

  Each console has a card with a live attract-mode preview. Left / Right
  picks a game, Enter (or A on a pad) starts it. Below the cards:

     Options ........ The same settings as in the game (see OPTIONS below).
     Achievements ... Every achievement of the three games and what you've
                      earned. See ACHIEVEMENTS below.
     Music Player ... The soundtracks of all three games (Left / Right
                      switches game; with a soundtrack pack for the game,
                      Tab or Y switches between the original music and
                      the pack's).
     Credits ........ Where to find me (YouTube, Reddit, GitHub, X,
                      Patreon) and what the collection is made with.
     Quit ........... Exit to the desktop.

  The launcher and the menus come in two styles (Options > Launcher &
  Menus > Menu Style): Arabian Nights (deep indigo and gold arabesques
  under the stars) or Classic (the original dark panels). Your own cover
  pictures can take the place of the previews, and your own picture can go
  behind the launcher (see LAUNCHER & MENUS under OPTIONS).

  The Team RedTail logo at the top left opens my Patreon page
  (patreon.com/c/TeamRedTail) in your browser: click it, or tap it on
  Android.

  To start a game straight from a shortcut, make a shortcut to
  AladdinCollection.exe and add  --game md,  --game snes  or  --game sms  to
  the end of its Target.


--------------------------------------------------------------------------------
  THE PAUSE MENU
--------------------------------------------------------------------------------

  Press Esc (keyboard) or Back / View (gamepad) during play. The game pauses
  while the menu is open. Up / Down moves, Left / Right changes a setting,
  Enter (A) selects, Esc (B) goes back. A line at the bottom explains the
  highlighted item.

  RESUME
     Carry on playing.

  SAVE STATES:  SLOT / SAVE STATE / LOAD STATE
     5 slots per game. Save State and Load State use the slot shown (the
     same as F5 and F9).

  LEVEL SELECT
     Start any level. Your current game is not kept, so make a save state
     first if you want to come back to it. Only while achievements are off:
     with them on it's greyed out (see ACHIEVEMENTS below).
       Mega Drive ....... 13 entries in the game's own order: Agrabah Market,
                          Abu's bonus in Agrabah, the Desert, Agrabah
                          Rooftops, the Sultan's Dungeon, the Cave of
                          Wonders, Abu's bonus in the cave, the Escape, the
                          Rug Ride, Inside the Lamp, the Sultan's Palace,
                          Jafar's Quarters and Jafar's Palace. "Difficulty"
                          picks Practice, Normal or Difficult. Each level
                          starts with its own story card.
       Super Nintendo ... All 19 levels, from Agrabah Market to Jafar,
                          A Whole New World included, in the order the game
                          plays them.
       Master System .... All 10 stages, numbered as the developers' own
                          test mode numbers them (Stage 1-1 to Stage 7),
                          entered through the password screen.

  CHEATS
     Switch each cheat on or off; they work straight away and stay on until
     you turn them off (even after you quit). "Disable All" turns them all
     off. Only while achievements are off.
       Mega Drive ....... Invincibility, Infinite Health, Infinite Lives,
                          Infinite Apples, Infinite Gems, Infinite
                          Continues, and three of the developers' extras:
                          Developer Cheat Mode, Developer Map View (fly the
                          view around the level; switch it off over solid
                          ground) and Developer Level Skip (pause, then A).
       Super Nintendo ... Invincibility, Infinite Health, Infinite Lives,
                          Infinite Apples, Infinite Gems, and Capcom's Debug
                          Mode (10 hearts and 99 apples at the start, a
                          stage select on the title screen: hold L or R,
                          up / down for the stage, left / right for the
                          act).
       Master System .... Invincibility, Infinite Health, Infinite Stones,
                          and the Developer Menu (the title's PASSWORD
                          opens the developers' TEST MODE). This version has
                          no lives -- continues are unlimited -- and its gems
                          are the health bar.
     Pits, lava, crushes, the Rug Ride's walls and falling behind a
     screen that scrolls by itself still cost a life with invincibility
     on -- turn on infinite lives as well if you want to be safe.

     The games' own secret codes work too:
       Mega Drive ....... Pause, then A B B A A B B A: skip the level. On
                          the OPTIONS screen, A C A C A C A C B B B B: the
                          developers' debug screen.
       Super Nintendo ... On the OPTIONS screen, on controller 2: L, R,
                          START, SELECT, X, Y, A, B: the developer mode.

  ACHIEVEMENTS
     The achievements of the game you're playing: what you've earned (and
     when), and progress towards the ones that count something. The switch
     at the top turns achievements on or off; Export to CSV writes the list
     and badge pictures (see ACHIEVEMENTS below).

  SOUND TEST / MUSIC PLAYER
     Every music track and sound effect of the game, played by the game's
     own sound driver, with meters for the sound channels (Mega Drive: 6 FM
     + 4 PSG; SNES: 8 voices; Master System: 4 PSG channels).
       Mega Drive ....... 28 tunes (10 of them never used by the game) and
                          76 effects, under the developers' own names.
       Super Nintendo ... 39 songs and 76 effects.
       Master System .... 20 tracks and 60 effects.

  OPTIONS, CREDITS
     See below.

  RESET GAME / RETURN TO LAUNCHER / QUIT TO DESKTOP
     Each asks you to confirm first.


--------------------------------------------------------------------------------
  OPTIONS
--------------------------------------------------------------------------------

  Every change takes effect straight away and is remembered for next time.
  "(default: ...)" is how each setting starts out.

  VIDEO
     Display Mode ...... Windowed or Fullscreen (borderless).
                                                 (default: Fullscreen)
     Monitor ........... Which display to use (listed by name). Dragging the
                         window to another screen picks it too.
     Window Size ....... 1x to 6x, in windowed mode.      (default: 3x)
     Widescreen ........ The shape of the gameplay picture: Off (original),
                         16:10, 16:9, 2:1 or 21:9 (ultrawide, for 3440x1440
                         and 2560x1080 screens). How many pixels that is
                         depends on the console and Pixel Aspect; at 21:9
                         with CRT pixels:
                           Mega Drive ...... 576 x 224
                           Super Nintendo .. 468 x 224
                           Master System ... 404 x 192
                         Title screens, menus and cut-scenes keep their
                         original shape (the Master System's story
                         scenes, intro, title and ending are drawn wide
                         too).                           (default: 16:9)
     Re-anchor HUD ..... In widescreen, moves the status bar items out to
                         the screen edges.               (default: ON)
     Pixel Aspect ...... CRT (4:3 TV): each console's pixels as a 4:3
                         television showed them; Square pixels; or Stretch
                         to window.                      (default: CRT)
     Integer Scaling ... Scale by whole numbers only, for perfectly even
                         pixels.                         (default: OFF)
     Filter ............ Nearest (sharp), Sharp bilinear, Bilinear
                         (smooth), Scanlines or CRT.
                                                (default: Sharp bilinear)
     Slang Shader ...... A RetroArch slang shader preset instead of the
                         filter, for a richer CRT look: crt-royale,
                         crt-geom, crt-lottes, crt-easymode, crt-aperture,
                         crt-pi and zfast-crt are included, in the
                         "shaders" folder. Any other .slangp preset you put
                         there (with its files, e.g. from the libretro
                         slang-shaders collection) shows up in the list too.
                         They work at every widescreen width and pixel
                         aspect, though some presets were made for a 4:3
                         picture and can stretch or frame oddly in
                         widescreen; crt-royale needs a fairly fast
                         graphics card. Needs librashader.dll next to the
                         game. If a preset can't be found or compiled
                         ("Shader ... not loaded"), the game goes back to
                         the built-in filter.             (default: Off)
     VSync ............. Sync to the monitor's refresh rate. (default: ON)
     Black Frame Insertion  Sharper motion, like a CRT: each game frame is
                         followed by a black one. Needs V-Sync and the
                         display at 120, 180 or 240 Hz (144 or 165 Hz
                         can't line up with the games' 60 frames a second:
                         set 120 Hz in Windows' display settings). The
                         picture gets darker.            (default: OFF)
     High Frame Rate ... Smoother motion on 100 Hz and faster screens: the
                         refreshes between the games' 60 frames show
                         in-between pictures, with the scrolling and the
                         sprites part of the way (2 pictures a frame at
                         100 / 120 Hz, 3 at 144 / 165 / 180 Hz, 4 at
                         240 Hz). The games still run at 60 frames a
                         second, so nothing about how they play changes;
                         the picture is one frame later. With V-Sync off
                         (G-SYNC / FreeSync) the pictures come at the
                         screen's own rate. Works with a slang shader;
                         not together with Black Frame Insertion. Next to
                         the switch: the rate and how many in-between
                         pictures, or what's missing. Windows only.
                                                         (default: OFF)
     Remove Sprite Flicker .. Lifts the consoles' limit on sprites per
                         line, so busy scenes don't flicker. (default: ON)
     Show FPS .......... Frame rate in the corner.       (default: OFF)

  COLOUR
     Colour Profile .... Accurate (each console's measured output levels),
                         Linear, Vivid, Warm CRT, Cool, Grayscale, Sepia or
                         Handheld LCD.                   (default: Accurate)
     Brightness, Saturation, Gamma .. Fine adjustments. "Reset
                         Adjustments" puts them back to 0.

  AUDIO
     Volume ............ 0% to 100%.                     (default: 80%)
     Output Filter ..... Off (raw), Console (the low-pass of the original
                         console's output: the Mega Drive Model 1's, a TV's)
                         or Warm.                        (default: Console)
     FM Chip Distortion  Mega Drive: the YM2612 "ladder effect" heard on
                         original Model 1 consoles.      (default: ON)
     Sample Interpolation  SNES: Original (gaussian) or Crisper (cubic).
                                                 (default: Original)
     Soundtracks ....... Shown when a soundtrack pack is found (see
                         SOUNDTRACK PACKS below): per game Original or the
                         pack, and Soundtrack Volume. On Windows also Open
                         Soundtracks Folder (MD+) and Open MSU-1 Folder.

  CONTROLS
     Keyboard keys and gamepad buttons for each of the console's buttons,
     and for Fast Forward and Rewind (greyed out while achievements are
     on), Menu, Quick Save, Quick Load and Screenshot. Highlight one, press
     Enter, then the new key or button (one already in use swaps; Esc
     or any controller button cancels a keyboard row; a key pressed on a
     gamepad row says so). Also: Device (which pad), Left Stick Moves
     (default ON), Menu Buttons (in the menus, the bottom face button
     confirms - Xbox's layout, the default - or the right one, Nintendo's),
     Other Controllers (PC: read controllers that aren't Xbox-style;
     default ON - switch it off if one sends presses you didn't make) and
     Reset to Defaults.

  LAUNCHER & MENUS
     Menu Style ........ Arabian Nights: deep indigo panels in gold
                         arabesque frames, gold titles, and Agrabah's night
                         sky with glints and gold dust. Classic: the
                         original dark panels.   (default: Arabian Nights)
     Game Covers ....... Your own cover pictures on the game cards instead
                         of the live previews (a card with a cover runs no
                         preview).                         (default: OFF)
     Custom Background . Your own picture behind the launcher, filling the
                         screen.                           (default: OFF)
     Cover Shape ....... Whole Cover (all of it, bars beside it) or Fill the
                         Frame (trimmed to fill).
     Open Covers Folder  Opens the "covers" folder next to the exe. Put
                         md, snes, sms (the covers) and background there as
                         .png or .jpg; they're read when you leave the page.
                         None are included. A picture that's missing or
                         can't be read is skipped: that card keeps its
                         preview, the launcher its night sky.
                         On Android: Choose Cover / Choose Background pick
                         a picture from your device instead.

  ACHIEVEMENTS
     Achievements ...... Earn achievements as you play. While this is on,
                         cheats, the level select, fast forward and rewind
                         are switched off. Switched on during a game, they
                         count from the next Reset Game.  (default: ON)

  SPEED
     Fast Forward ...... Hold Tab or the right trigger for 4x speed. Only
                         while achievements are off.      (default: ON)
     Rewind ............ Hold R or the left trigger to step back through the
                         last minute of play. Only while achievements are
                         off.                             (default: ON)
     Remove Slowdown ... SNES: where the original slowed down, the console's
                         CPU does the game's work faster, so busy scenes
                         stay at 60 frames a second. Off: the original's
                         speed, slowdown included.       (default: ON)

  UPDATES
     Check for Updates . Once a day the launcher looks up the version
                         number of the latest release on GitHub (only
                         that: nothing about you or your games is sent).
                         When there's a newer version, a badge at the
                         top right of the launcher says so. Nothing is
                         downloaded or installed, and if you're offline
                         it simply says nothing.         (default: ON)
     Latest Version .... Opens the new version's page in your browser
                         (download it yourself: saves and settings carry
                         over). With no newer version known: Check Now.
                         Clicking the launcher's badge (or tapping it on
                         Android) opens the page too.


--------------------------------------------------------------------------------
  ACHIEVEMENTS
--------------------------------------------------------------------------------

  54 achievements to earn across the collection. A pop-up announces each
  one as it happens, with what it was for; the launcher's Achievements
  page and the pause menu list them all. Hidden ones show as "???" until
  you earn them.

  They're earned fair and square: while achievements are on (on by default;
  the switch is at the top of the Achievements page, and in Options),
  cheats, the level select, fast forward and rewind are switched off. Save
  states still work. Switch achievements off to use those again -- anything
  you've already earned stays earned. Achievements count in a game that
  was started with them on (from the launcher, or with Reset Game); the
  "finish the game" ones need the whole playthrough from a new game.

  EXPORT TO CSV (on the Achievements page) writes an "achievements export"
  folder next to the program: achievements.csv -- each achievement with
  its points, bronze / silver / gold / platinum tier, game, progress and
  when you earned it (date and time), in the columns Playnite's
  achievements spreadsheet import reads -- and an "icons" folder with a
  badge picture for each one, earned and locked (blue for the Mega Drive,
  purple for the SNES, red for the Master System, gold for the
  collection's own). Achievements earned with an older version kept only
  the day, so they show at midnight. On Android the folder is in
  Android/data/com.darhkfox.aladdincollection/files, which a file manager
  or a PC over USB can open.

  MEGA DRIVE  (18)
     One Jump Ahead ........ Escape the guards of Agrabah Market.
     Half a Scarab ......... Cross the Desert and find the first half of the
                             scarab.
     Street Rat ............ Clear Agrabah Rooftops.
     Jailbreak ............. Escape the Sultan's Dungeon.
     Touch Nothing but
       the Lamp ............ Find the lamp in the Cave of Wonders.
     Magic Carpet Ride ..... Escape from the Cave of Wonders on the carpet.
     Friend Like Me ........ Find your way out of the Genie's lamp.
     Prince Ali ............ Clear the Sultan's Palace.
     Snake in the Grass .... Clear Jafar's Quarters.
     Defeat Jafar .......... Defeat Jafar and finish the game.
     Monkey Business ....... Play one of Abu's bonus rounds.
     Phenomenal Cosmic
       Power ............... Spin the Genie's bonus wheel.
     It's a Deal ........... Buy something from the peddler.
     Apple of My Eye ....... Carry 50 apples at once.
     Not a Scratch ......... Clear a level without losing any health.
     Street Rat Supreme .... Finish the game on Difficult from a new game.
     ...and 2 hidden ones.

  SUPER NINTENDO  (17)
     Street Rat ............ Clear Stage 1: Agrabah's market and rooftops.
     Diamond in the Rough .. Make it through the Cave of Wonders.
     Hold On Tight ......... Fly out of the collapsing cave on the carpet.
     Phenomenal Cosmic
       Power ............... Find a way through the inside of the lamp.
     Jailbreak ............. Escape from the Sultan's dungeon.
     Magic Carpet Ride ..... Finish the moonlit ride of A Whole New World.
     Storming the Palace ... Fight your way through Jafar's palace.
     Snake Charmer ......... Defeat Jafar and his cobra form.
     One Jump Ahead ........ Finish a new game without using a continue.
     Riffraff, Street Rat .. Clear a whole stage without losing a life.
     Not a Scratch ......... Clear a part of a stage without losing a heart.
     Break Time! ........... Spin the Genie's bonus wheel.
     Heart of Gold ......... Raise Aladdin's hearts to the full ten.
     Treasure of the Cave .. Collect a hundred gems.
     ...and 3 hidden ones.

  MASTER SYSTEM  (17)
     Street Rat ............ Escape the guards through the Agrabah market.
     One Jump Ahead ........ Clear the rooftop chase.
     Into the Cave ......... Make it through the first halls of the Cave of
                             Wonders.
     Touch Nothing ......... Get through the Treasure Room.
     The Lamp .............. Reach the lamp.
     Out of the Fire ....... Escape the collapsing cave on the carpet.
     Prince Ali ............ Make it through the Sultan's palace.
     Magic Carpet Ride ..... Finish the night flight over Agrabah.
     Jafar's Palace ........ Make it through Jafar's palace.
     Jafar Defeated ........ Defeat Jafar and finish the game.
     Diamond in the Rough .. Finish the game from GAME START, without a
                             password.
     Not a Scratch ......... Clear a stage without losing a gem.
     Flawless Tour ......... Clear every stage, and beat Jafar, without
                             losing a gem.
     Rock Collector ........ Carry nine stones at once.
     ...and 3 hidden ones.

  THE COLLECTION  (2)
     A Whole New World ..... Defeat Jafar in all three versions.
     Completionist ......... Unlock every other achievement.


--------------------------------------------------------------------------------
  WIDESCREEN: GOOD TO KNOW
--------------------------------------------------------------------------------

  More of each level really is drawn at the sides: the level, enemies and
  items appear and move across the whole width, and the status bar moves
  out to the screen edges (Re-anchor HUD). Near a level's ends the
  picture slides, so the extra width shows more of the level instead of
  black, with the background still moving as on the console. The games'
  own cameras are untouched, so on the Mega Drive and the Master System
  the scenery holds still for the first stretch while Aladdin walks. The
  width (and the cheats) can be changed at any time, also mid-level, and
  save states remember the width they were made at. With CRT pixels the
  21:9 picture fills a 3440x1440 or 2560x1080 screen.

  Boss fights: on the Mega Drive the wider view settles on the arena (the
  golden idol, Jafar and his snake) instead of following Aladdin, so the
  extra width shows the arena; whatever lies beyond the arena's walls is
  dimmed to half brightness (the HUD stays bright) while the fight holds
  Aladdin there, fading in as he enters and out once the boss is beaten;
  in widescreen the magic carpet comes as soon as the idol is beaten. On
  the SNES the arenas keep the original camera, and their sides are
  shown at full brightness, except during the boss fights at the ends
  of Sultan's Dungeon and Jafar's Palace: there the
  sides fade dark while the fight keeps Aladdin on the original screen
  and fade back in once the boss is beaten, to show the way on to the
  exit. The SNES fight with Jafar is centred on its arena, with the cobra
  in full view, and its sides are dimmed the same way for the whole fight
  (there is nowhere else to go); when the cobra sets the floor on fire,
  both sides become the arena going on, with the flames and the spiked
  arch over them running on into them, moving as the game moves them. In the SNES Lava Cavern,
  which scrolls by itself, the screen's limits follow the wide picture's
  edges, so Aladdin can use the extra width (widescreen only).

  The Mega Drive's Rug Ride is wide too. Its rocks and treasures still
  come in at the original screen's right edge, so it plays exactly as it
  always did; the extra width shows the cave you've flown through, with
  the lava wave chasing you.

  Mega Drive screens between and around the levels are drawn wide: the
  title and options (sky, stars and palace skyline across the width, the
  logo and menu centred), the story cards, the title cards, "Level
  Complete", the death and continue screens and the ending. What flies
  across them - the Genie round the logos, Aladdin and Abu running, the
  scarab's halves, the carpet - goes on into the sides. The Genie's bonus
  wheel, the instructions and the full scarab picture stay in their
  original shape.

  Shown in their original shape: other title screens, menus, story
  scenes and the Mega Drive's bonus rounds. Where such a screen's
  background is one plain colour, it fills the width in that colour. The
  SNES STAGE CLEAR screen fills the width with its purple background.

  The SNES staff roll is wide: its night sky, clouds and city run the
  whole width (the moon only once), the credits stay in the middle, and
  the birds and the carpet fly out into the sides. The birds come in a
  little past the original screen's edge, where the game creates them.
  For the roll's first few seconds (left side) and beside THE END (right
  side) the sides show the roll's sky mirrored.

  Master System story screens are drawn wide: each story scene carries
  its own picture on into the sides (characters, text and framed
  pictures stay where the game puts them; a character who walks off goes
  on into the side), the Genie's intro and the title fill the width with
  their background, and the ending's stars and sea run the whole width.
  Five scenes whose picture is cut off at its edges stay in their
  original shape, with black bars.

  Narrow climbs: where an SNES level is a tower one screen wide (Inside
  the Lamp 3), the view is centred on it. Its gold pillars go on into
  both sides as a solid gold wall, so the tower reads as a shaft in the
  wall, with no statues, platforms or enemies beside it (or black, with
  Past Level Ends: Black). The corridor at its top keeps its start in
  the left side as Aladdin walks on. Past any other SNES level's ends
  the sides show only the background.

  Attract-mode demos are recorded button presses. All three games show
  them in their original shape, exactly as recorded, at every width and
  with any cheat switched on. With widescreen off everything is exactly
  the original.

  The wider view shows parts of the levels the original screen never
  did, and in a few places they aren't perfect: the README on GitHub
  lists the known limitations.


--------------------------------------------------------------------------------
  SOUNDTRACK PACKS (MD+ AND MSU-1)
--------------------------------------------------------------------------------

  CD-quality fan soundtracks made for flash carts and emulators can replace
  the music of the Mega Drive game (MD+ packs) and the SNES game (MSU-1
  packs). None are included: you supply them, as you do the ROMs. A pack's
  patch is applied to the game in memory only (no file is ever changed);
  the game's sound effects stay the console's own.

  Mega Drive - MD+ packs (a .cue sheet, a .bps patch for the USA ROM, CRC
  ED427EA9, and the music as .wav files):
     Put the pack's folder, as downloaded, in the "soundtracks" folder next
     to AladdinCollection.exe, e.g.
        soundtracks\Aladdin CD - MD+\Aladdin (USA).cue
        soundtracks\Aladdin CD - MD+\Aladdin (USA)[MDplus, ...].bps
        soundtracks\Aladdin CD - MD+\01-NENAW_nl.wav ... 20-GAMEOVER.wav
     (A pre-patched "Aladdin (USA).bin" in the folder isn't needed and
     isn't used: the .bps is applied to your own ROM.) The pack's own
     "MD+ SOUND ENABLED" start-up screen is skipped, so the game, the level
     select and the attract demos start as without the pack. The pack
     "Aladdin CD - MD+" (ArcadeTV) has the boss tune and the bonus jingle
     the wrong way round; the collection puts them right.

  SNES - MSU-1 packs (the tracks NAME-1.pcm, NAME-2.pcm, ... with either an
  MSU-1 patch, .bps or .ips, for the USA ROM, CRC 124D8E4D, or a ROM
  already patched for them, .sfc):
     Put the pack's folder in the "msu" folder next to
     AladdinCollection.exe, e.g.
        msu\Aladdin (USA) (MSU1) (Audio Orchestral)\Aladdin_(USA)_(MSU1)-2.pcm
           ... -41.pcm
        msu\Aladdin (USA) (MSU1) (Audio Orchestral)\Aladdin_(USA)_(MSU1).sfc
        (the .msu and .bml files may stay; they aren't needed)
     A pre-patched ROM is only used if it is your ROM with MSU-1 code added
     and nothing else changed: the same size, and every difference in the
     header's checksum, in the ROM's free space or in a few short hooks.

  Then choose the pack under Options > Audio > Soundtracks (Mega Drive /
  SNES), and set its level with Soundtrack Volume. An MD+ pack starts with
  the game (or Reset Game); an MSU-1 pack switches on or off at once, also
  in a level (the game holds for about a second while the console's sound
  driver restarts, then the song playing starts again in the new music).
  Folders inside a downloaded folder are found too, and an MD+ pack can
  also go in the msu folder or an MSU-1 pack in soundtracks.

  On Android: Options > Audio > Add Soundtrack... and pick the pack's
  folder (it is copied into the app), or copy MSU-1 packs over USB into
  Android/data/com.darhkfox.aladdincollection/files/msu.

  What works with a pack: the music changes as in the game (each level's
  tune, the boss tunes - in widescreen held until the boss is in the 4:3
  view, as the original's -, deaths, the level's end), pausing pauses it,
  save states and rewind take it back to where it was, widescreen, cheats,
  the level select, Remove Slowdown and achievements work as usual, and the
  attract demos play exactly as recorded. The Music Player plays the pack's
  tracks for each tune (and its tracks no tune uses), with Tab / Y between
  the original music and the pack's.


--------------------------------------------------------------------------------
  FILES
--------------------------------------------------------------------------------

  settings.ini ....... Your settings (delete it to restore the defaults).
  achievements.ini ... The achievements you've earned (and when).
  achievements export\  What Export to CSV writes (see ACHIEVEMENTS).
  saves\ ............. Save states.
  screenshots\ ....... F12 screenshots.
  soundtracks\ ....... MD+ soundtrack packs (Mega Drive), if you have any.
  msu\ ............... MSU-1 soundtrack packs (SNES), if you have any.
  covers\ ............ Your launcher pictures: md, snes, sms, background
                       (.png or .jpg).
  shaders\ ........... Slang shader presets (Options > Video > Slang
                       Shader), with librashader.dll next to the exe.

  These live next to AladdinCollection.exe (the folder is portable), or in
  %LOCALAPPDATA%\Aladdin Collection if that folder isn't writable. Save
  states made by a newer version may not load in an older one.


--------------------------------------------------------------------------------
  CREDITS
--------------------------------------------------------------------------------

  PC port by Darhkfox
     YouTube: https://www.youtube.com/adhgaming
     Reddit:  u/Darhkwing
     GitHub:  https://github.com/Darhkfox
     X:       https://x.com/ADH_Gaming
     Patreon: https://www.patreon.com/c/TeamRedTail

  The Team RedTail logo at the top left of the launcher opens the Patreon
  page too (click it, or tap it on Android).

  The port's own code is released under the MIT licence: see LICENSE.
  The MSU-1 chip and the MSU-1 pack handling are ported from my Super
  Mario All-Stars PC port (same author, same MIT licence). MD+ is the
  CD-audio standard of the MegaSD and the Mega EverDrive Pro; MSU-1 is
  byuu's (higan) / the SD2SNES's. Soundtrack packs and their music belong
  to their makers.

  Slang shaders run through librashader by Ronny Chan (chyyran), Mozilla
  Public License 2.0 (librashader-LICENSE.md). The bundled presets are
  unmodified files from the libretro slang-shaders collection, each with
  its own licence (public domain, GPL v2 or later, MIT): see
  shaders\README.txt. They are separate files loaded at run time, not part
  of the program.

  Cover and background pictures are read with stb_image by Sean Barrett,
  public domain (or MIT, at your choice).

  Aladdin (c) Disney. The Mega Drive version (c) 1993 Sega / Virgin Games,
  the SNES version (c) 1993 Capcom, the Master System version (c) 1994
  Sega. This is an unofficial fan project, not affiliated with or endorsed
  by Disney, Sega, Virgin, Capcom or Nintendo, and it contains no game data.
