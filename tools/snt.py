"""Python wrapper around snestool.dll (headless SNES core) for analysis.

Usage:
    import sys; sys.path.insert(0, r'K:\\projects\\Super Star Wars Collection\\tools')
    from snt import SNES, ROM
    s = SNES(ROM)
    s.run(300)                  # run 300 frames
    s.press('START', 5, 60)     # hold START 5 frames, release, run 60
    s.hold('RIGHT+B')           # hold buttons until changed
    s.png(r'...\\shot.png', 2)   # screenshot (scale 2)
    s.r8(0x7E0123); s.r16(0x7E0123); s.w8(adr, v)   # memory through the bus (peek/poke; no side effects)
    s.wram / s.vram / s.cgram / s.oam / s.aram / s.dsp / s.rom   # raw arrays (live, writable)
    s.cpu()                     # a x y sp dp pc pb db p e
    st = s.save(); s.load(st)   # save states (bytes)
    s.set_ext(48, 48)           # render 48 extra px each side
    s.coverage(True); ...; s.covered(lo_off, hi_off)
    s.bp(0x808123); s.run(n); s.bp_hits()
    s.watch(lo, hi); s.run(n); s.watch_hits()       # CPU writes (WRAM normalised to $7Exxxx)
    s.rwatch(lo, hi); s.run(n); s.rwatch_hits()     # CPU reads
    s.ports(True); s.run(n); s.port_hits()          # CPU -> APU port writes
    s.dis(pc, n)                # disassembly lines (m/x flags tracked roughly via REP/SEP)
"""
import ctypes as C
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, 'Aladdin (USA).sfc')
GAMES = {'snes': ROM}

_DLL_CANDIDATES = [
    os.environ.get('SNESTOOL_DLL', ''),
    os.path.join(ROOT, 'tools', 'bin', 'snestool.dll'),
    os.path.join(ROOT, 'build', 'tools', 'snestool.dll'),
]

BTN = {'B': 0x8000, 'Y': 0x4000, 'SELECT': 0x2000, 'START': 0x1000, 'UP': 0x0800, 'DOWN': 0x0400,
       'LEFT': 0x0200, 'RIGHT': 0x0100, 'A': 0x0080, 'X': 0x0040, 'L': 0x0020, 'R': 0x0010}


def _load():
    for p in _DLL_CANDIDATES:
        if p and os.path.exists(p):
            return C.CDLL(p)
    raise FileNotFoundError('snestool.dll not found; set SNESTOOL_DLL')


_dll = _load()
V, I, U32, PU8, PU16, PU32 = C.c_void_p, C.c_int, C.c_uint32, C.POINTER(C.c_uint8), C.POINTER(C.c_uint16), C.POINTER(C.c_uint32)


def _sig(name, res, *args):
    f = getattr(_dll, name)
    f.restype = res
    f.argtypes = list(args)


_sig('snt_create', V, C.c_char_p)
_sig('snt_destroy', None, V)
_sig('snt_reset', None, V, I)
_sig('snt_set_buttons', None, V, I, I)
_sig('snt_run_frames', None, V, I)
_sig('snt_run_to', I, V, U32, I)
_sig('snt_step', None, V, I)
_sig('snt_frame_count', U32, V)
_sig('snt_vpos', I, V)
for n in ['snt_wram', 'snt_oam', 'snt_aram', 'snt_dsp', 'snt_rom']:
    _sig(n, PU8, V)
for n in ['snt_vram', 'snt_cgram']:
    _sig(n, PU16, V)
_sig('snt_rom_size', I, V)
_sig('snt_peek', C.c_uint8, V, U32)
_sig('snt_poke', None, V, U32, I)
_sig('snt_get_cpu', None, V, PU32)
_sig('snt_get_apu', None, V, PU32)
_sig('snt_ppu_info', None, V, C.c_char_p, I)
_sig('snt_ppu_regs', None, V, C.POINTER(C.c_int))
_sig('snt_set_ext', None, V, I, I, I)
_sig('snt_frame', None, V, PU32, C.POINTER(C.c_int), C.POINTER(C.c_int))
if hasattr(_dll, 'snt_sub_frame'):
    _sig('snt_set_sub_frames', None, V, I)
    _sig('snt_sub_frame', I, V, I, PU32, C.POINTER(C.c_int), C.POINTER(C.c_int))
_sig('snt_save_png', I, V, C.c_char_p, I)
_sig('snt_write_png', I, C.c_char_p, PU32, I, I, I)
_sig('snt_layers', None, V, I)
_sig('snt_coverage_enable', None, V, I)
_sig('snt_coverage', PU32, V)
_sig('snt_coverage_clear', None, V)
_sig('snt_covflags', PU8, V)
_sig('snt_ramcov', PU8, V)
_sig('snt_trace_enable', None, V, I)
_sig('snt_trace', I, V, PU32, I)
_sig('snt_bp_add', None, V, U32)
_sig('snt_bp_clear', None, V)
_sig('snt_bp_log', I, V, PU32, I)
_sig('snt_watch', None, V, U32, U32)
_sig('snt_watch_log', I, V, PU32, I)
_sig('snt_rwatch', None, V, U32, U32)
_sig('snt_rwatch_log', I, V, PU32, I)
_sig('snt_port_watch', None, V, I)
if hasattr(_dll, 'snt_vram_watch'):
    _sig('snt_vram_watch', None, V, U32, U32)
    _sig('snt_vram_log', I, V, PU32, I)
_sig('snt_port_log', I, V, PU32, I)
_sig('snt_state_size', I, V)
_sig('snt_save_state', I, V, PU8, I)
_sig('snt_load_state', I, V, PU8, I)
_sig('snt_audio_enable', None, V, I)
_sig('snt_run_frames_audio', I, V, I, C.POINTER(C.c_int16), I)
_sig('snt_voice_env', I, V, I)
_sig('snt_disasm', I, V, U32, I, I, C.c_char_p, I)
_sig('snt_module_create', I, V, C.c_char_p)
_sig('snt_module_configure', None, V, I, I, C.c_char_p)
_sig('snt_module_can_configure', I, V, I, C.c_char_p)
if hasattr(_dll, 'snt_module_turbo'):   # (not in older DLLs)
    _sig('snt_module_turbo', None, V, I)
if hasattr(_dll, 'snt_turbo_state'):
    _dll.snt_turbo_state.argtypes = [C.c_void_p, C.POINTER(C.c_int)]
if hasattr(_dll, 'snt_module_view_left'):
    _sig('snt_module_view_left', I, V)
if hasattr(_dll, 'snt_edge_mirror'):
    _sig('snt_edge_mirror', None, V, I)
if hasattr(_dll, 'snt_line_regs'):
    _sig('snt_line_regs', None, V, C.POINTER(C.c_int))
_sig('snt_module_info', None, V, C.POINTER(C.c_int))
_sig('snt_module_start_level', I, V, I, I)
_sig('snt_module_jukebox_boot', None, V)
_sig('snt_module_play', None, V, I)
_sig('snt_module_jukebox_frame', None, V)
_sig('snt_module_ach', I, V, C.POINTER(C.c_int), I)


def buttons(spec):
    if isinstance(spec, int):
        return spec
    v = 0
    for b in str(spec).upper().split('+'):
        b = b.strip()
        if b:
            v |= BTN[b]
    return v


class SNES:
    def __init__(self, rom=ROM):
        if rom in GAMES:
            rom = GAMES[rom]
        self.h = _dll.snt_create(rom.encode('mbcs'))
        if not self.h:
            raise RuntimeError('could not load ' + rom)
        self.wram = _dll.snt_wram(self.h)
        self.vram = _dll.snt_vram(self.h)
        self.cgram = _dll.snt_cgram(self.h)
        self.oam = _dll.snt_oam(self.h)
        self.aram = _dll.snt_aram(self.h)
        self.dsp = _dll.snt_dsp(self.h)
        self.rom = _dll.snt_rom(self.h)
        self.rom_size = _dll.snt_rom_size(self.h)

    def __del__(self):
        if getattr(self, 'h', None):
            _dll.snt_destroy(self.h)
            self.h = None

    # ---- running
    def reset(self, hard=True):
        _dll.snt_reset(self.h, 1 if hard else 0)

    def hold(self, spec=0, port=0):
        _dll.snt_set_buttons(self.h, port, buttons(spec))

    def run(self, n=1):
        _dll.snt_run_frames(self.h, n)

    def press(self, spec, frames=3, after=0):
        self.hold(spec)
        self.run(frames)
        self.hold(0)
        if after:
            self.run(after)

    def run_to(self, pc, max_instr=5000000):
        return _dll.snt_run_to(self.h, pc, max_instr) != 0

    def step(self, n=1):
        _dll.snt_step(self.h, n)

    @property
    def frame(self):
        return _dll.snt_frame_count(self.h)

    @property
    def vpos(self):
        return _dll.snt_vpos(self.h)

    # ---- memory
    def r8(self, a):
        return _dll.snt_peek(self.h, a)

    def r16(self, a):
        return _dll.snt_peek(self.h, a) | (_dll.snt_peek(self.h, a + 1) << 8)

    def r24(self, a):
        return self.r16(a) | (self.r8(a + 2) << 16)

    def s16(self, a):
        v = self.r16(a)
        return v - 0x10000 if v & 0x8000 else v

    def w8(self, a, v):
        _dll.snt_poke(self.h, a, v & 0xff)

    def w16(self, a, v):
        self.w8(a, v)
        self.w8(a + 1, v >> 8)

    def rd(self, a, n):
        return bytes(self.r8(a + i) for i in range(n))

    def wram_bytes(self):
        return bytes(self.wram[:0x20000])

    def cpu(self):
        o = (C.c_uint32 * 10)()
        _dll.snt_get_cpu(self.h, o)
        k = ['a', 'x', 'y', 'sp', 'dp', 'pc', 'pb', 'db', 'p', 'e']
        return dict(zip(k, list(o)))

    def apu(self):
        o = (C.c_uint32 * 13)()
        _dll.snt_get_apu(self.h, o)
        return {'a': o[0], 'x': o[1], 'y': o[2], 'sp': o[3], 'pc': o[4], 'in': list(o[5:9]), 'out': list(o[9:13])}

    def ppu(self):
        b = C.create_string_buffer(2048)
        _dll.snt_ppu_info(self.h, b, 2048)
        return b.value.decode()

    def ppu_regs(self):
        o = (C.c_int * 32)()
        _dll.snt_ppu_regs(self.h, o)
        d = {'mode': o[0], 'tm': o[21], 'ts': o[22], 'bright': o[23], 'fblank': o[24], 'objbase': o[25], 'objsize': o[26]}
        for l in range(4):
            d['bg%d' % (l + 1)] = {'hofs': o[1 + l * 5], 'vofs': o[2 + l * 5], 'map': o[3 + l * 5], 'size': o[4 + l * 5],
                                   'chr': o[5 + l * 5]}
        return d

    # ---- video
    def set_ext(self, l, r=None, no_limit=False):
        _dll.snt_set_ext(self.h, l, l if r is None else r, 1 if no_limit else 0)

    def edge_mirror(self, on):
        """Past a level's end: the level mirrored (the app's default) or black."""
        _dll.snt_edge_mirror(self.h, 1 if on else 0)

    def line_regs(self):
        """The last frame's rows (240) as rendered: (bg1 hofs, bg1 vofs, bg2 hofs, bg2 vofs, adj1, adj2) each, the
        adjustments being what the game module added to the layers' horizontal scroll (Ppu::bgHofsAdj)."""
        o = (C.c_int * (240 * 6))()
        _dll.snt_line_regs(self.h, o)
        return [tuple(o[r * 6:(r + 1) * 6]) for r in range(240)]

    def pixels(self):
        w, h = C.c_int(), C.c_int()
        _dll.snt_frame(self.h, None, C.byref(w), C.byref(h))
        buf = (C.c_uint32 * (w.value * h.value))()
        _dll.snt_frame(self.h, buf, C.byref(w), C.byref(h))
        return w.value, h.value, buf

    def set_sub_frames(self, k):
        """high frame rate: K-1 in-between pictures drawn with each frame (1 = off)"""
        _dll.snt_set_sub_frames(self.h, k)

    def sub_pixels(self, k):
        """in-between picture k (1 .. K-1; 0 = the frame itself): (w, h, 0xAARRGGBB pixels)"""
        w, h = C.c_int(), C.c_int()
        _dll.snt_sub_frame(self.h, k, None, C.byref(w), C.byref(h))
        buf = (C.c_uint32 * (w.value * h.value))()
        _dll.snt_sub_frame(self.h, k, buf, C.byref(w), C.byref(h))
        return w.value, h.value, buf

    def png(self, path, scale=2):
        return _dll.snt_save_png(self.h, path.encode('mbcs'), scale)

    def layers(self, mask=0x1f):
        _dll.snt_layers(self.h, mask)

    # ---- analysis
    def coverage(self, on=True):
        _dll.snt_coverage_enable(self.h, 1 if on else 0)

    def coverage_clear(self):
        _dll.snt_coverage_clear(self.h)

    def covered(self, lo=0, hi=None):
        """ROM offsets executed (as an opcode) in [lo, hi) with their counts."""
        hi = self.rom_size if hi is None else hi
        c = _dll.snt_coverage(self.h)
        return {i: c[i] for i in range(lo, hi) if c[i]}

    def coverage_arrays(self):
        """(counts, flags) for every ROM byte as numpy-free lists' ctypes views (length rom_size)."""
        return _dll.snt_coverage(self.h), _dll.snt_covflags(self.h)

    def covflags_bytes(self):
        return C.string_at(_dll.snt_covflags(self.h), self.rom_size)

    def ramcov_bytes(self):
        return C.string_at(_dll.snt_ramcov(self.h), 0x20000)

    def trace_on(self, n=4096):
        _dll.snt_trace_enable(self.h, n)

    def trace(self, n=4096):
        o = (C.c_uint32 * n)()
        k = _dll.snt_trace(self.h, o, n)
        return list(o[:k])

    def bp(self, pc):
        _dll.snt_bp_add(self.h, pc)

    def bp_clear(self):
        _dll.snt_bp_clear(self.h)

    def bp_hits(self, max_hits=100000):
        o = (C.c_uint32 * (14 * max_hits))()
        n = _dll.snt_bp_log(self.h, o, max_hits)
        k = ['frame', 'pc', 'a', 'x', 'y', 'sp', 'dp', 'db', 'p', 'line', 's0', 's1', 's2', 's3']
        return [dict(zip(k, o[i * 14:(i + 1) * 14])) for i in range(n)]

    def watch(self, lo, hi=None):
        _dll.snt_watch(self.h, lo, lo if hi is None else hi)

    def unwatch(self):
        _dll.snt_watch(self.h, 1, 0)

    def watch_hits(self, max_hits=200000):
        o = (C.c_uint32 * (5 * max_hits))()
        n = _dll.snt_watch_log(self.h, o, max_hits)
        return [dict(zip(['frame', 'pc', 'addr', 'value', 'line'], o[i * 5:(i + 1) * 5])) for i in range(n)]

    def rwatch(self, lo, hi=None):
        _dll.snt_rwatch(self.h, lo, lo if hi is None else hi)

    def rwatch_hits(self, max_hits=200000):
        o = (C.c_uint32 * (5 * max_hits))()
        n = _dll.snt_rwatch_log(self.h, o, max_hits)
        return [dict(zip(['frame', 'pc', 'addr', 'value', 'line'], o[i * 5:(i + 1) * 5])) for i in range(n)]

    def vwatch(self, lo, hi):
        """Log VRAM word writes (CPU or DMA) to word addresses [lo, hi] (developer DLL only)."""
        _dll.snt_vram_watch(self.h, lo, hi)

    def vwatch_hits(self, max_hits=400000):
        o = (C.c_uint32 * (5 * max_hits))()
        n = _dll.snt_vram_log(self.h, o, max_hits)
        return [dict(zip(['frame', 'line', 'pc', 'adr', 'val'], o[i * 5:(i + 1) * 5])) for i in range(n)]

    def ports(self, on=True):
        _dll.snt_port_watch(self.h, 1 if on else 0)

    def port_hits(self, max_hits=200000):
        o = (C.c_uint32 * (4 * max_hits))()
        n = _dll.snt_port_log(self.h, o, max_hits)
        return [dict(zip(['frame', 'pc', 'port', 'value'], o[i * 4:(i + 1) * 4])) for i in range(n)]

    # ---- states
    def save(self):
        n = _dll.snt_state_size(self.h)
        buf = (C.c_uint8 * (n + 4096))()
        k = _dll.snt_save_state(self.h, buf, n + 4096)
        return bytes(buf[:k])

    def load(self, data):
        buf = (C.c_uint8 * len(data)).from_buffer_copy(data)
        if not _dll.snt_load_state(self.h, buf, len(data)):
            raise RuntimeError('state rejected')

    def save_file(self, path):
        open(path, 'wb').write(self.save())

    def load_file(self, path):
        self.load(open(path, 'rb').read())

    # ---- audio
    def audio_on(self, on=True):
        _dll.snt_audio_enable(self.h, 1 if on else 0)

    def run_audio(self, n):
        """Runs n frames and returns the stereo int16 samples (32040 Hz) as a list [l, r, l, r, ...]."""
        mx = n * 600
        buf = (C.c_int16 * (mx * 2))()
        k = _dll.snt_run_frames_audio(self.h, n, buf, mx)
        return buf[:k * 2]

    def voice_env(self, v):
        return _dll.snt_voice_env(self.h, v)

    # ---- disassembly
    def dis1(self, pc, m=True, x=True):
        b = C.create_string_buffer(128)
        n = _dll.snt_disasm(self.h, pc, 1 if m else 0, 1 if x else 0, b, 128)
        return n, b.value.decode()

    def dis(self, pc, count=20, m=True, x=True, show=True):
        out = []
        for _ in range(count):
            n, t = self.dis1(pc, m, x)
            raw = ' '.join('%02X' % self.r8(((pc & 0xff0000) | ((pc + i) & 0xffff))) for i in range(n))
            line = '%02X:%04X  %-12s %s' % (pc >> 16, pc & 0xffff, raw, t)
            out.append(line)
            op = self.r8(pc)
            if op == 0xC2:   # REP
                v = self.r8(pc + 1)
                m = m and not (v & 0x20)
                x = x and not (v & 0x10)
            elif op == 0xE2:  # SEP
                v = self.r8(pc + 1)
                m = m or bool(v & 0x20)
                x = x or bool(v & 0x10)
            pc = (pc & 0xff0000) | ((pc + n) & 0xffff)
        if show:
            print('\n'.join(out))
        return out

    # ---- game module under test
    def module(self, gid):
        return _dll.snt_module_create(self.h, gid.encode()) != 0

    def configure(self, ext=0, hud=True, cheats=''):
        _dll.snt_module_configure(self.h, ext, 1 if hud else 0, cheats.encode())
        if os.environ.get('SNT_TURBO') and hasattr(_dll, 'snt_module_turbo'):   # (runs as the app's Remove Slowdown)
            self.turbo(int(os.environ['SNT_TURBO']))

    def turbo_state(self):
        """(cpuTurbo, turboActive, interrupt depth)"""
        o = (C.c_int * 3)()
        _dll.snt_turbo_state(self.h, o)
        return tuple(o)

    def turbo(self, k):
        """CPU speed for the game's frame work (1 = the original's slowdown); kept across configure()."""
        _dll.snt_module_turbo(self.h, k)

    def view_left(self):
        """The module's view split (left extension) after the last frame; None in older DLLs or outside gameplay."""
        if not hasattr(_dll, 'snt_module_view_left'):
            return None
        v = _dll.snt_module_view_left(self.h)
        return v if v >= 0 else None

    def can_configure(self, ext, cheats=''):
        return _dll.snt_module_can_configure(self.h, ext, cheats.encode()) != 0

    def info(self):
        o = (C.c_int * 4)()
        _dll.snt_module_info(self.h, o)
        return {'gameplay': o[0], 'mask_left': o[1], 'mask_right': o[2], 'mask_lines': o[3]}

    def start_level(self, index, variant=0):
        return _dll.snt_module_start_level(self.h, index, variant) != 0

    def jukebox_boot(self):
        _dll.snt_module_jukebox_boot(self.h)

    def play(self, sid):
        _dll.snt_module_play(self.h, sid)

    def jukebox_frame(self):
        _dll.snt_module_jukebox_frame(self.h)

    def ach_reset(self):
        _dll.snt_module_ach(self.h, None, -1)

    def ach(self):
        """This frame's achievement events: an index earned; 1000 + index * 32 + bit marked; 5000 + index counted;
        100000 + index * 1000 + count, progress reported."""
        o = (C.c_int * 64)()
        n = _dll.snt_module_ach(self.h, o, 64)
        return list(o[:n])


def write_png(path, px, w, h, scale=1):
    buf = (C.c_uint32 * (w * h))(*px) if not isinstance(px, C.Array) else px
    return _dll.snt_write_png(path.encode('mbcs'), buf, w, h, scale)
