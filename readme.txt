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
       - any of the above inside a .zip (no need to unzip). .7z and .rar
         are not read -- extract those first.
     The launcher looks next to the program, in its "roms" folder, and in
     the folder it was started from. ROMs you copy in while the launcher is
     open are picked up within a second or so.

  WRONG VERSION?
     If a file is another version of one of the games, its card in the
     launcher says "Wrong ROM version", names the file and says what it is:
     "the European version", "the Japanese version", "the REV 01 version",
     or "a modified copy or a bad dump". Swap it for the one listed above.


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

  In the games:
     Mega Drive ....... A throws an apple, B swings the sword, C jumps.
     Super Nintendo ... B jumps, Y throws an apple (the game's own OPTIONS
                        screen can rearrange them).
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
  game buttons. Esc always opens the menu too.


--------------------------------------------------------------------------------
  THE LAUNCHER
--------------------------------------------------------------------------------

  Each console has a card with a live attract-mode preview. Left / Right
  picks a game, Enter (or A on a pad) starts it. Below the cards:

     Options ........ The same settings as in the game (see OPTIONS below).
     Achievements ... Every achievement of the three games and what you've
                      earned. See ACHIEVEMENTS below.
     Music Player ... The soundtracks of all three games (Left / Right
                      switches game).
     Credits ........ Where to find me: YouTube, Reddit and GitHub.
     Quit ........... Exit to the desktop.

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
                          stage select on the title screen).
       Master System .... Invincibility, Infinite Health, Infinite Stones,
                          and the Developer Menu (the title's PASSWORD
                          opens the developers' TEST MODE). This version has
                          no lives -- continues are unlimited -- and its gems
                          are the health bar.
     Pits, lava, crushes and the Rug Ride's walls still cost a life with
     invincibility on -- turn on infinite lives as well if you want to be
     safe.

     The games' own secret codes work too:
       Mega Drive ....... Pause, then A B B A A B B A: skip the level. On
                          the OPTIONS screen, A C A C A C A C B B B B: the
                          developers' debug screen.
       Super Nintendo ... On the OPTIONS screen, on controller 2: L, R,
                          START, SELECT, X, Y, A, B: the developer mode.

  ACHIEVEMENTS
     The achievements of the game you're playing: what you've earned (and
     when), and progress towards the ones that count something. The switch
     at the top turns achievements on or off.

  SOUND TEST / MUSIC PLAYER
     Every music track and sound effect of the game, played by the game's
     own sound driver, with meters for the sound channels (Mega Drive: 6 FM
     + 4 PSG; SNES: 8 voices; Master System: 4 PSG channels).
       Mega Drive ....... 28 tunes (10 of them never used by the game) and
                          76 effects, under the developers' own names.
       Super Nintendo ... 39 songs and 80 effects.
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
                         original shape.                 (default: 16:9)
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
     VSync ............. Sync to the monitor's refresh rate. (default: ON)
     Black Frame Insertion  Sharper motion, like a CRT: each game frame is
                         followed by a black one. Needs V-Sync and the
                         display at 120, 180 or 240 Hz (144 or 165 Hz
                         can't line up with the games' 60 frames a second:
                         set 120 Hz in Windows' display settings). The
                         picture gets darker.            (default: OFF)
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
     Soundtracks ....... Shown when an MD+ soundtrack pack for the Mega
                         Drive game is found in a "soundtracks" folder:
                         Original or the pack, and Soundtrack Volume.

  CONTROLS
     Keyboard keys and gamepad buttons for each of the console's buttons,
     and for Fast Forward and Rewind (greyed out while achievements are
     on), Menu, Quick Save, Quick Load and Screenshot. Highlight one, press
     Enter, then the new key or button (one already in use swaps; Esc
     cancels). Also: Device (which pad), Left Stick Moves (default ON) and
     Reset to Defaults.

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
  items appear and move across the whole width. Where a level ends, the
  extra width is blacked out (Mega Drive, Master System) or, on the SNES,
  the view stops at the edge, with the background still moving as on the
  console. Boss arenas keep the original camera, with the parts you can't
  reach dimmed.

  Shown in their original shape: title screens, menus, story scenes, the
  Mega Drive's bonus rounds and Rug Ride, the SNES staff roll.

  Attract-mode demos are recorded button presses; in widescreen (where
  enemies wake a little earlier) they can play out differently. With
  widescreen off everything is exactly the original.


--------------------------------------------------------------------------------
  FILES
--------------------------------------------------------------------------------

  settings.ini ....... Your settings (delete it to restore the defaults).
  achievements.ini ... The achievements you've earned.
  saves\ ............. Save states.
  screenshots\ ....... F12 screenshots.
  soundtracks\ ....... MD+ soundtrack packs (Mega Drive), if you have any.

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

  Aladdin (c) Disney. The Mega Drive version (c) 1993 Sega / Virgin Games,
  the SNES version (c) 1993 Capcom, the Master System version (c) 1994
  Sega. This is an unofficial fan project, not affiliated with or endorsed
  by Disney, Sega, Virgin, Capcom or Nintendo, and it contains no game data.
