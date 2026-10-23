# Analysis tooling

Everything used to reverse-engineer and test the three ROMs headlessly. Each console's core is built as a DLL with
a C API (`src/tools/*tool.cpp`, built by CMake into `build/tools/`) and a Python wrapper in `tools/`. The ROMs are
expected in the project root (they are never committed).

| Console | ROM | DLL | Wrapper | Game module id |
|---|---|---|---|---|
| Mega Drive | `Aladdin (U) [!].bin` | `mdtool.dll` | `tools/mdt.py` (`MD`) | `md` |
| SNES | `Aladdin (USA).sfc` | `snestool.dll` | `tools/snt.py` (`SNES`) | `snes` |
| Master System | `Disney's Aladdin (Europe).sms` | `smstool.dll` | `tools/smst.py` (`SMS`) | `sms` |

All three wrappers share the same shape: run frames with buttons held, read and write RAM / VRAM / palette, take PNG
screenshots, render a widened picture (`set_ext`), log breakpoints (registers at a PC) and RAM writes, save / load
states, measure coverage, and drive the game module the app uses (configure a width and cheats, start a level, run
frames through its hooks, play sounds, check achievements on RAM images).

```python
import sys; sys.path.insert(0, r'K:\projects\Aladdin\tools')
from smst import SMS
s = SMS()                    # the ROM in the project root
s.module('sms'); s.configure(ext=74)
s.start_level(5)             # the level select's entry 5, from power-on
s.mframes(120, 'RIGHT')      # frames through the module's hooks, RIGHT held
s.png(r'K:\projects\Aladdin\build\shot.png', 2)
```

Disassemblers: `tools/m68dis.py` (68000, capstone), `tools/z80dis.py` (Z80, table-driven); the SNES tool has its
own 65816 disassembler (`SNES.dis`).

The app itself can be scripted for end-to-end checks: `AladdinCollection.exe [--game md|snes|sms] --autotest <dir>
<script> --capture-size 1600x900` (commands in `src/app/app.cpp`, `load_script`: key taps, `shot`, `set
widescreen=4`, `loadstate`, `pads`, `quit`). A state for `loadstate` is a core state followed by the module's
bookkeeping (a 4-byte length, 0 for none) and the tag `<id>|<ext>|` with its length byte; see
`tools/re/md_apptest.py` and `tools/re/sms_app_test.py`.

Per-game notes and the scripts behind them: `docs/re_md_*.md` + `tools/re/md_*.py`, `docs/re_snes_*.md` +
`tools/re/snes_*.py`, `docs/re_sms_*.md` + `tools/re/sms_*.py`.
