"""Python wrapper around smstool.dll (headless Master System core) for analysis.

Usage:
    from smst import SMS, ROM
    s = SMS()               # Disney's Aladdin (Europe)
    s.run(300)              # run 300 frames
    s.press('1', 5)         # hold button 1 for 5 frames then release
    s.png('out.png')        # screenshot (scale 2 by default)
    s.ram8(0xC123)          # read work RAM
"""
import ctypes as C
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, "Disney's Aladdin (Europe).sms")

_DLL_CANDIDATES = [
    os.environ.get('SMSTOOL_DLL', ''),
    os.path.join(ROOT, 'build', 'tools', 'smstool.dll'),
]

BTN = {'UP': 0x01, 'DOWN': 0x02, 'LEFT': 0x04, 'RIGHT': 0x08, '1': 0x10, '2': 0x20, 'PAUSE': 0x40, 'RESET': 0x80}


def _load():
    for p in _DLL_CANDIDATES:
        if p and os.path.exists(p):
            return C.CDLL(p)
    raise FileNotFoundError('smstool.dll not found; build it (cmake --build build --target smstool) or set SMSTOOL_DLL')


_dll = _load()
_dll.smst_create.restype = C.c_void_p
_dll.smst_create.argtypes = [C.c_char_p]
for _n in ['smst_destroy', 'smst_power_on']:
    getattr(_dll, _n).argtypes = [C.c_void_p]
_dll.smst_set_buttons.argtypes = [C.c_void_p, C.c_int, C.c_int]
_dll.smst_run_frames.argtypes = [C.c_void_p, C.c_int]
_dll.smst_frame_count.restype = C.c_uint64
_dll.smst_frame_count.argtypes = [C.c_void_p]
for _n in ['smst_ram', 'smst_vram', 'smst_cram', 'smst_vdp_regs', 'smst_rom', 'smst_banks']:
    getattr(_dll, _n).restype = C.POINTER(C.c_uint8)
    getattr(_dll, _n).argtypes = [C.c_void_p]
_dll.smst_rom_size.argtypes = [C.c_void_p]
_dll.smst_peek.argtypes = [C.c_void_p, C.c_int]
_dll.smst_set_ext.argtypes = [C.c_void_p, C.c_int, C.c_int]
_dll.smst_vdp_extra.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.c_int]
_dll.smst_view_shift.argtypes = [C.c_void_p, C.c_int]
_dll.smst_module_state.restype = C.c_uint32
_dll.smst_module_state.argtypes = [C.c_void_p, C.c_int]
_dll.smst_line_hscroll.restype = C.POINTER(C.c_uint8)
_dll.smst_line_hscroll.argtypes = [C.c_void_p]
_dll.smst_debug_layers.argtypes = [C.c_void_p, C.c_int]
_dll.smst_get_cpu.argtypes = [C.c_void_p, C.POINTER(C.c_uint32)]
_dll.smst_set_cpu.argtypes = [C.c_void_p, C.c_int, C.c_uint32]
_dll.smst_write.argtypes = [C.c_void_p, C.c_int, C.c_int]
_dll.smst_frame.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.POINTER(C.c_int), C.POINTER(C.c_int)]
_dll.smst_save_png.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
_dll.smst_write_png.argtypes = [C.c_char_p, C.POINTER(C.c_uint32), C.c_int, C.c_int, C.c_int]
_dll.smst_coverage_enable.argtypes = [C.c_void_p, C.c_int]
_dll.smst_coverage.restype = C.POINTER(C.c_uint32)
_dll.smst_coverage.argtypes = [C.c_void_p]
_dll.smst_coverage_clear.argtypes = [C.c_void_p]
_dll.smst_bp_add.argtypes = [C.c_void_p, C.c_uint32]
_dll.smst_bp_clear.argtypes = [C.c_void_p]
_dll.smst_bp_log.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.c_int]
_dll.smst_watch.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
_dll.smst_watch_log.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.c_int]
_dll.smst_vdp_watch.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
_dll.smst_vdp_log.argtypes = [C.c_void_p, C.POINTER(C.c_uint32), C.c_int]
_dll.smst_state_size.argtypes = [C.c_void_p]
_dll.smst_save_state.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_int]
_dll.smst_load_state.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_int]
_dll.smst_audio.argtypes = [C.c_void_p, C.POINTER(C.c_int32), C.c_int]
_dll.smst_sample_rate.restype = C.c_double
_dll.smst_sample_rate.argtypes = [C.c_void_p]
_dll.smst_patch8.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32]
_dll.smst_module_create.argtypes = [C.c_void_p, C.c_char_p]
_dll.smst_module_configure.argtypes = [C.c_void_p, C.c_int, C.c_int, C.c_char_p]
_dll.smst_module_start_level.argtypes = [C.c_void_p, C.c_int, C.c_int]
_dll.smst_module_frames.argtypes = [C.c_void_p, C.c_int]
_dll.smst_module_levels.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
_dll.smst_module_sounds.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
for _n in ['smst_module_jukebox_boot', 'smst_module_jukebox_frame', 'smst_module_stop']:
    getattr(_dll, _n).argtypes = [C.c_void_p]
_dll.smst_module_play.argtypes = [C.c_void_p, C.c_int]

CPU_NAMES = ['af', 'bc', 'de', 'hl', 'ix', 'iy', 'sp', 'pc', "af'", "bc'", "de'", "hl'", 'iff1', 'im']


class SMS:
    def __init__(self, rom=ROM):
        self.h = _dll.smst_create(rom.encode('mbcs'))
        if not self.h:
            raise RuntimeError('failed to load ' + rom)
        self.rom_path = rom
        self.buttons = 0

    def __del__(self):
        if getattr(self, 'h', None):
            _dll.smst_destroy(self.h)
            self.h = None

    # ---- execution ----
    def reset(self):
        _dll.smst_power_on(self.h)

    def run(self, frames=1, buttons=None):
        if buttons is not None:
            self.hold(buttons)
        _dll.smst_run_frames(self.h, frames)

    @staticmethod
    def _bits(buttons):
        if isinstance(buttons, str):
            return sum(BTN[b] for b in buttons.split('+') if b)
        return buttons

    def hold(self, buttons):
        self.buttons = self._bits(buttons)
        _dll.smst_set_buttons(self.h, 0, self.buttons)

    def press(self, buttons, frames=4, after=4):
        self.hold(buttons)
        self.run(frames)
        self.hold(0)
        self.run(after)

    @property
    def frame(self):
        return _dll.smst_frame_count(self.h)

    # ---- memory ----
    def _arr(self, fn, n, t=C.c_uint8):
        return C.cast(fn(self.h), C.POINTER(t * n)).contents

    @property
    def ram(self):
        return self._arr(_dll.smst_ram, 0x2000)

    @property
    def vram(self):
        return self._arr(_dll.smst_vram, 0x4000)

    @property
    def cram(self):
        return self._arr(_dll.smst_cram, 32)

    @property
    def regs(self):
        return self._arr(_dll.smst_vdp_regs, 16)

    @property
    def banks(self):
        return list(self._arr(_dll.smst_banks, 3))

    @property
    def rom(self):
        return self._arr(_dll.smst_rom, _dll.smst_rom_size(self.h))

    def ram8(self, a):
        return self.ram[a & 0x1FFF]

    def ram16(self, a):
        r = self.ram
        return r[a & 0x1FFF] | (r[(a + 1) & 0x1FFF] << 8)

    def sram16(self, a):
        v = self.ram16(a)
        return v - 0x10000 if v & 0x8000 else v

    def set8(self, a, v):
        self.ram[a & 0x1FFF] = v & 0xFF

    def set16(self, a, v):
        self.set8(a, v)
        self.set8(a + 1, v >> 8)

    def peek(self, a):
        """a Z80 address as the CPU sees it now (current banking)"""
        return _dll.smst_peek(self.h, a)

    def cpu(self):
        out = (C.c_uint32 * 14)()
        _dll.smst_get_cpu(self.h, out)
        return dict(zip(CPU_NAMES, out))

    def set_cpu(self, name, v):
        """sets a register ('pc', 'sp', 'af', ... or 'iff1')"""
        _dll.smst_set_cpu(self.h, CPU_NAMES.index(name), v)

    def write(self, a, v):
        """a bus write (RAM, mapper registers at $FFFC-$FFFF)"""
        _dll.smst_write(self.h, a, v)

    def goto_mode(self, mode):
        """Aladdin: restarts the main loop (rst $28) in game mode `mode` ($C200)"""
        self.set8(0xC200, mode)
        self.set_cpu('pc', 0x0028)

    # ---- video ----
    def set_ext(self, ext, sprite_limit=None):
        _dll.smst_set_ext(self.h, ext, (1 if ext == 0 else 0) if sprite_limit is None else int(sprite_limit))

    def extra_sprites(self):
        """the module's extra sprites for the next frame: [(x, y, pattern)]"""
        buf = (C.c_int * 192)()
        n = _dll.smst_vdp_extra(self.h, buf, 64)
        return [tuple(buf[i * 3:i * 3 + 3]) for i in range(n)]

    def view_shift(self, line=-1):
        """the view shift: sprites (line -1) or line `line`'s background"""
        return _dll.smst_view_shift(self.h, line)

    def module_state(self, i):
        return _dll.smst_module_state(self.h, i)

    def line_hscroll(self):
        """the horizontal scroll each line of the last frame was drawn with"""
        return list(C.cast(_dll.smst_line_hscroll(self.h), C.POINTER(C.c_uint8 * 240)).contents)

    def layers(self, mask):
        _dll.smst_debug_layers(self.h, mask)

    def pixels(self):
        w, h = C.c_int(), C.c_int()
        n = _dll.smst_frame(self.h, None, C.byref(w), C.byref(h))
        buf = (C.c_uint32 * n)()
        _dll.smst_frame(self.h, buf, C.byref(w), C.byref(h))
        return list(buf), w.value, h.value

    def png(self, path, scale=2):
        return _dll.smst_save_png(self.h, path.encode('mbcs'), scale)

    # ---- debugging ----
    def coverage_on(self, on=True):
        _dll.smst_coverage_enable(self.h, 1 if on else 0)

    def coverage(self):
        n = _dll.smst_rom_size(self.h)
        return C.cast(_dll.smst_coverage(self.h), C.POINTER(C.c_uint32 * n)).contents

    def coverage_clear(self):
        _dll.smst_coverage_clear(self.h)

    def bp(self, addr):
        _dll.smst_bp_add(self.h, addr)

    def bp_clear(self):
        _dll.smst_bp_clear(self.h)

    def bp_log(self):
        """[(frame, pc, line, banks, {reg: value})]"""
        out = []
        buf = (C.c_uint32 * (16 * 4096))()
        while True:
            n = _dll.smst_bp_log(self.h, buf, 4096)
            for i in range(n):
                w = buf[i * 16:(i + 1) * 16]
                out.append((w[0], w[1], w[2], w[3], dict(zip(CPU_NAMES[:12], w[4:16]))))
            if n < 4096:
                return out

    def watch(self, lo, hi=None):
        _dll.smst_watch(self.h, lo, lo if hi is None else hi)

    def watch_off(self):
        _dll.smst_watch(self.h, 1, 0)

    def watch_log(self):
        """[(frame, pc, addr, line, value)]"""
        out = []
        buf = (C.c_uint32 * (4 * 4096))()
        while True:
            n = _dll.smst_watch_log(self.h, buf, 4096)
            for i in range(n):
                f, pc, a, v = buf[i * 4:(i + 1) * 4]
                out.append((f, pc, a & 0xFFFF, a >> 16, v))
            if n < 4096:
                return out

    def vdp_watch(self, lo, hi=None):
        """logs VRAM writes in [lo, hi] (CRAM writes as $8000 | index)"""
        _dll.smst_vdp_watch(self.h, lo, lo if hi is None else hi)

    def vdp_watch_off(self):
        _dll.smst_vdp_watch(self.h, 1, 0)

    def vdp_log(self):
        """[(frame, pc, addr, line, value)]"""
        out = []
        buf = (C.c_uint32 * (4 * 4096))()
        while True:
            n = _dll.smst_vdp_log(self.h, buf, 4096)
            for i in range(n):
                f, pc, a, v = buf[i * 4:(i + 1) * 4]
                out.append((f, pc, a & 0xFFFF, a >> 16, v))
            if n < 4096:
                return out

    def save(self):
        n = _dll.smst_state_size(self.h)
        buf = (C.c_uint8 * n)()
        _dll.smst_save_state(self.h, buf, n)
        return bytes(buf)

    def load(self, data):
        buf = (C.c_uint8 * len(data)).from_buffer_copy(data)
        if not _dll.smst_load_state(self.h, buf, len(data)):
            raise RuntimeError('state does not load')

    def save_ext(self):
        """the module's own part of a save state (what the app stores next to save())"""
        _dll.smst_module_save_ext.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_int]
        n = _dll.smst_module_save_ext(self.h, None, 0)
        buf = (C.c_uint8 * max(1, n))()
        _dll.smst_module_save_ext(self.h, buf, n)
        return bytes(buf[:n])

    def load_ext(self, data):
        _dll.smst_module_load_ext.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.c_int]
        buf = (C.c_uint8 * max(1, len(data))).from_buffer_copy(data or bytes(1))
        _dll.smst_module_load_ext(self.h, buf, len(data))

    def save_file(self, path):
        with open(path, 'wb') as f:
            f.write(self.save())

    def load_file(self, path):
        with open(path, 'rb') as f:
            self.load(f.read())

    def audio(self):
        buf = (C.c_int32 * (2 * 8192))()
        n = _dll.smst_audio(self.h, buf, 8192)
        return list(buf[:n * 2])

    def patch8(self, addr, v):
        _dll.smst_patch8(self.h, addr, v)

    # ---- the game module ----
    def module(self, gid='sms'):
        if not _dll.smst_module_create(self.h, gid.encode()):
            raise RuntimeError('no module ' + gid)

    def configure(self, ext=0, hud_anchor=True, cheats=''):
        _dll.smst_module_configure(self.h, ext, 1 if hud_anchor else 0, cheats.encode())

    def start_level(self, entry, variant=0):
        return _dll.smst_module_start_level(self.h, entry, variant)

    def mframes(self, n=1, buttons=None):
        if buttons is not None:
            self.hold(buttons)
        r = _dll.smst_module_frames(self.h, n)
        return {'gameplay': bool(r & 1), 'mask_left': (r >> 8) & 0xFF, 'mask_right': (r >> 16) & 0xFF}

    def levels(self):
        buf = C.create_string_buffer(65536)
        _dll.smst_module_levels(self.h, buf, 65536)
        return [l.split('|') for l in buf.value.decode().splitlines()]

    def sounds(self):
        buf = C.create_string_buffer(65536)
        _dll.smst_module_sounds(self.h, buf, 65536)
        return [(int(i), m == '1', n) for i, m, n in (l.split('|', 2) for l in buf.value.decode().splitlines())]

    def jukebox_boot(self):
        _dll.smst_module_jukebox_boot(self.h)

    def jukebox_frame(self):
        _dll.smst_module_jukebox_frame(self.h)

    def play(self, sid):
        _dll.smst_module_play(self.h, sid)

    def stop(self):
        _dll.smst_module_stop(self.h)


def write_png(path, px, w, h, scale=1):
    buf = (C.c_uint32 * len(px))(*px)
    return _dll.smst_write_png(path.encode('mbcs'), buf, w, h, scale)


# ---- achievement detectors on RAM images (headless checks of the module's achievement_frame) ----
_dll.smst_ach_create.restype = C.c_void_p
_dll.smst_ach_create.argtypes = [C.c_char_p]
_dll.smst_ach_destroy.argtypes = [C.c_void_p]
_dll.smst_ach_reset.argtypes = [C.c_void_p, C.c_int, C.c_uint32, C.POINTER(C.c_uint8)]
_dll.smst_ach_step.argtypes = [C.c_void_p, C.POINTER(C.c_uint8), C.POINTER(C.c_int), C.c_int]
_dll.smst_ach_run_bits.restype = C.c_uint32
_dll.smst_ach_run_bits.argtypes = [C.c_void_p]


class Ach:
    """feeds RAM images to the module's achievement_frame: step(ram) -> [(kind, value, index)]
    (kind 0 unlocked, 1 counted, 2 marked (value = bit), 3 progress (value = count))"""

    def __init__(self, gid='sms', run_counts=True):
        self.h = _dll.smst_ach_create(gid.encode())
        _dll.smst_ach_reset(self.h, 1 if run_counts else 0, 0, None)

    def __del__(self):
        if getattr(self, 'h', None):
            _dll.smst_ach_destroy(self.h)

    def step(self, ram):
        buf = (C.c_uint8 * 0x2000).from_buffer_copy(bytes(ram))
        out = (C.c_int * 64)()
        n = _dll.smst_ach_step(self.h, buf, out, 64)
        return [((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF) for v in out[:n]]

    @property
    def run_bits(self):
        return _dll.smst_ach_run_bits(self.h)
