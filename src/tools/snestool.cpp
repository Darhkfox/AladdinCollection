// snestool.dll - headless SNES core with a C API for Python-driven analysis (tools/snt.py):
// screenshots, WRAM/VRAM/CGRAM/OAM/ARAM access, PC coverage, breakpoint logs, memory watches, APU port logs.
#include "../core/snes.h"
#include "../core/disasm.h"
#include "../game/game.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#define API extern "C" __declspec(dllexport)

namespace {
struct BpHit { uint32_t frame, pc, a, x, y, sp, dp, db, p, line, s0, s1, s2, s3; };
struct WatchHit { uint32_t frame, pc, addr, value, line; };
struct PortHit { uint32_t frame, pc, port, value; };

struct Tool;
struct ModuleExt : Snes::StateExt {
    Tool* t = nullptr;
    std::vector<uint8_t> saveExt() const override;
    void loadExt(const uint8_t* data, size_t size) override;
};

struct Tool {
    Snes snes;
    ModuleExt ext_adapter;
    uint16_t buttons[2] = {0, 0};
    std::vector<uint32_t> coverage;      // execution count per ROM byte (LoROM offset)
    std::vector<uint8_t> covflags;       // per ROM byte: bit 0 seen with m=1, 1 m=0, 2 x=1, 3 x=0, 4 emulation
    std::vector<uint8_t> ramcov;         // per WRAM byte executed: same flag bits (code run from RAM)
    bool cov_on = false;
    std::vector<uint32_t> bps;
    std::vector<BpHit> bp_log;
    std::vector<WatchHit> watch_log, rwatch_log;
    std::vector<PortHit> port_log;
    struct VramHit { uint32_t frame, line, pc, adr, val; };
    std::vector<VramHit> vram_log;
    uint32_t vram_lo = 1, vram_hi = 0;
    bool port_on = false;
    size_t log_limit = 400000;
    std::vector<uint32_t> rgb;
    std::unique_ptr<SnesModule> module;  // optional game module under test
    GameConfig cfg;
    FrameInfo info;
    uint64_t trace_count = 0;
    std::vector<uint32_t> trace;         // ring of recent PCs
    size_t trace_pos = 0;
};

std::vector<uint8_t> ModuleExt::saveExt() const { return t->module ? t->module->save_ext() : std::vector<uint8_t>(); }
void ModuleExt::loadExt(const uint8_t* data, size_t size) { if (t->module) t->module->load_ext(data, size); }

uint32_t lorom_offset(uint32_t pc) {
    const uint32_t bank = (pc >> 16) & 0x7f, a = pc & 0xffff;
    if (a < 0x8000) return 0xffffffff;
    return (bank << 15) | (a & 0x7fff);
}

void trace_cb(void* ctx, uint32_t pc) {
    Tool* t = (Tool*)ctx;
    if (t->cov_on) {
        const uint32_t o = lorom_offset(pc);
        const CpuRegs& r = t->snes.cpu.r;
        const uint8_t f = (r.mf ? 1 : 2) | (r.xf ? 4 : 8) | (r.e ? 16 : 0);
        if (o < t->coverage.size()) { t->coverage[o]++; t->covflags[o] |= f; }
        else {
            const uint32_t bank = pc >> 16, a = pc & 0xffff;
            uint32_t w = 0xffffffff;
            if (bank == 0x7e || bank == 0x7f) w = ((bank & 1) << 16) | a;
            else if (!(bank & 0x40) && a < 0x2000) w = a;
            if (w < t->ramcov.size()) t->ramcov[w] |= f;
        }
    }
    if (!t->trace.empty()) {
        t->trace[t->trace_pos] = pc;
        t->trace_pos = (t->trace_pos + 1) % t->trace.size();
    }
    t->trace_count++;
}

void update_trace(Tool* t) {
    const bool need = t->cov_on || !t->trace.empty();
    t->snes.traceFn = need ? trace_cb : nullptr;
    t->snes.traceCtx = t;
}

bool exec_hook(void* ctx, uint32_t pc) {
    Tool* t = (Tool*)ctx;
    if (t->module && t->module->exec_hook(t->snes, pc)) return true;
    if (std::find(t->bps.begin(), t->bps.end(), pc) != t->bps.end() && t->bp_log.size() < t->log_limit) {
        const CpuRegs& r = t->snes.cpu.r;
        BpHit h = {t->snes.frameCount(), pc, r.a, r.x, r.y, r.sp, r.dp, r.db, t->snes.cpu.getP() | (r.e ? 0x100u : 0u),
                   t->snes.vPos(), 0, 0, 0, 0};
        // the top of the stack (return addresses)
        uint32_t s[4];
        for (int i = 0; i < 4; i++) s[i] = t->snes.peek(((r.sp + 1 + i * 2) & 0xffff)) | (t->snes.peek(((r.sp + 2 + i * 2) & 0xffff)) << 8);
        h.s0 = s[0]; h.s1 = s[1]; h.s2 = s[2]; h.s3 = s[3];
        t->bp_log.push_back(h);
    }
    return false;
}

void watch_cb(void* ctx, uint32_t adr, uint8_t v, uint32_t pc) {
    Tool* t = (Tool*)ctx;
    if (t->watch_log.size() < t->log_limit) t->watch_log.push_back({t->snes.frameCount(), pc, adr, v, t->snes.vPos()});
}
void rwatch_cb(void* ctx, uint32_t adr, uint8_t v, uint32_t pc) {
    Tool* t = (Tool*)ctx;
    if (t->rwatch_log.size() < t->log_limit) t->rwatch_log.push_back({t->snes.frameCount(), pc, adr, v, t->snes.vPos()});
}
void port_cb(void* ctx, int port, uint8_t v) {
    Tool* t = (Tool*)ctx;
    if (t->port_on && t->port_log.size() < t->log_limit)
        t->port_log.push_back({t->snes.frameCount(), t->snes.cpu.lastPc, (uint32_t)port, v});
}

void vram_cb(void* ctx, uint16_t adr, uint16_t val) {
    Tool* t = (Tool*)ctx;
    if (adr >= t->vram_lo && adr <= t->vram_hi && t->vram_log.size() < t->log_limit)
        t->vram_log.push_back({t->snes.frameCount(), t->snes.vPos(), t->snes.cpu.lastPc, adr, val});
}

void frame_rgb(Tool* t, int& w, int& h) {
    const Ppu& p = t->snes.ppu;
    w = 256 + p.extL + p.extR;
    h = p.frameHeight();
    t->rgb.resize((size_t)w * h);
    for (int y = 0; y < h; y++) {
        const uint32_t* row = p.frame + (size_t)y * kLineW + (kMaxExt - p.extL);
        for (int x = 0; x < w; x++) t->rgb[(size_t)y * w + x] = 0xFF000000u | Ppu::to_rgb(row[x]);
    }
}

// ---- minimal PNG writer (stored deflate) ----
uint32_t crc_table[256];
void crc_init() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void be32(std::vector<uint8_t>& v, uint32_t x) { v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}
bool write_png(const char* path, const uint32_t* px, int w, int h, int scale) {
    static bool inited = false;
    if (!inited) { crc_init(); inited = true; }
    int W = w * scale, H = h * scale;
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(W * 3 + 1) * H);
    for (int y = 0; y < H; y++) {
        raw.push_back(0);
        const uint32_t* row = px + (size_t)(y / scale) * w;
        for (int x = 0; x < W; x++) {
            uint32_t c = row[x / scale];
            raw.push_back((uint8_t)(c >> 16)); raw.push_back((uint8_t)(c >> 8)); raw.push_back((uint8_t)c);
        }
    }
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back((uint8_t)n); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)~n); z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, W); be32(ihdr, H);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}
}  // namespace

API void* snt_create(const char* rom_path) {
    std::ifstream f(rom_path, std::ios::binary);
    if (!f) return nullptr;
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Tool* t = new Tool;
    if (!t->snes.loadRom(rom)) { delete t; return nullptr; }
    t->snes.reset(true);
    t->snes.setExecHook(exec_hook, t);
    t->snes.setPortWatch(port_cb, t);
    return t;
}
API void snt_destroy(void* h) { delete (Tool*)h; }
API void snt_reset(void* h, int hard) { ((Tool*)h)->snes.reset(hard != 0); }
API void snt_set_buttons(void* h, int port, int bits) { ((Tool*)h)->buttons[port & 1] = (uint16_t)bits; }

API void snt_run_frames(void* h, int n) {
    Tool* t = (Tool*)h;
    for (int i = 0; i < n; i++) {
        t->snes.input[0] = t->buttons[0];
        t->snes.input[1] = t->buttons[1];
        if (t->module) t->module->pre_frame(t->snes, t->cfg);
        t->snes.runFrame();
        if (t->module) { t->info = FrameInfo(); t->module->post_frame(t->snes, t->cfg, t->info); }
        t->snes.apu.samples.clear();
    }
}
// Runs until the CPU reaches `pc` (or max_instr instructions). Returns 1 if reached.
API int snt_run_to(void* h, uint32_t pc, int max_instr) {
    Tool* t = (Tool*)h;
    for (int i = 0; i < max_instr; i++) {
        t->snes.input[0] = t->buttons[0];
        t->snes.input[1] = t->buttons[1];
        t->snes.stepInstruction();
        if (((uint32_t(t->snes.cpu.r.pb) << 16) | t->snes.cpu.r.pc) == pc) return 1;
    }
    return 0;
}
API void snt_step(void* h, int n) { for (int i = 0; i < n; i++) ((Tool*)h)->snes.stepInstruction(); }
API uint32_t snt_frame_count(void* h) { return ((Tool*)h)->snes.frameCount(); }
API int snt_vpos(void* h) { return ((Tool*)h)->snes.vPos(); }

API uint8_t* snt_wram(void* h) { return ((Tool*)h)->snes.wram; }
API uint16_t* snt_vram(void* h) { return ((Tool*)h)->snes.ppu.vram; }
API uint16_t* snt_cgram(void* h) { return ((Tool*)h)->snes.ppu.cgram; }
API uint8_t* snt_oam(void* h) { return ((Tool*)h)->snes.ppu.oam; }
API uint8_t* snt_aram(void* h) { return ((Tool*)h)->snes.apu.ram; }
API uint8_t* snt_dsp(void* h) { return ((Tool*)h)->snes.apu.dsp.regs; }
API uint8_t* snt_rom(void* h) { return ((Tool*)h)->snes.rom.data(); }
API int snt_rom_size(void* h) { return (int)((Tool*)h)->snes.rom.size(); }
API uint8_t snt_peek(void* h, uint32_t a) { return ((Tool*)h)->snes.peek(a); }
API void snt_poke(void* h, uint32_t a, int v) { ((Tool*)h)->snes.poke(a, (uint8_t)v); }

// a x y sp dp pc pb db p e
API void snt_get_cpu(void* h, uint32_t* out) {
    const Tool* t = (Tool*)h;
    const CpuRegs& r = t->snes.cpu.r;
    out[0] = r.a; out[1] = r.x; out[2] = r.y; out[3] = r.sp; out[4] = r.dp; out[5] = r.pc; out[6] = r.pb; out[7] = r.db;
    out[8] = t->snes.cpu.getP(); out[9] = r.e;
}
API void snt_get_apu(void* h, uint32_t* out) {
    const Apu::Regs& r = ((Tool*)h)->snes.apu.r;
    out[0] = r.a; out[1] = r.x; out[2] = r.y; out[3] = r.sp; out[4] = r.pc;
    for (int i = 0; i < 4; i++) { out[5 + i] = ((Tool*)h)->snes.apu.inPorts[i]; out[9 + i] = ((Tool*)h)->snes.apu.outPorts[i]; }
}
API void snt_ppu_info(void* h, char* out, int max) {
    std::string s = ((Tool*)h)->snes.ppu.debugString();
    snprintf(out, max, "%s", s.c_str());
}
// PPU registers for scripts: mode, bg hofs/vofs/map/size/chr x4, tm, ts, bright, fblank
API void snt_ppu_regs(void* h, int* out) {
    const Ppu& p = ((Tool*)h)->snes.ppu;
    out[0] = p.bgMode();
    for (int l = 0; l < 4; l++) {
        out[1 + l * 5] = p.bgHofs(l); out[2 + l * 5] = p.bgVofs(l); out[3 + l * 5] = p.bgMapBase(l);
        out[4 + l * 5] = p.bgMapSize(l); out[5 + l * 5] = p.bgCharBase(l);
    }
    out[21] = p.mainScreen(); out[22] = p.subScreen(); out[23] = p.brightness(); out[24] = p.forcedBlank();
    out[25] = p.objBase(); out[26] = p.objSizeSel();
}

// The last frame's rows (240): BG1 hofs, vofs, BG2 hofs, vofs, the hook's BG1 / BG2 hofs adjustments (6 ints a row)
API void snt_line_regs(void* h, int* out) {
    const Ppu& p = ((Tool*)h)->snes.ppu;
    for (int r = 0; r < kFrameH; r++) {
        const Ppu::LineRegs& l = p.lineRegs[r];
        int* o = out + r * 6;
        o[0] = l.hofs[0]; o[1] = l.vofs[0]; o[2] = l.hofs[1]; o[3] = l.vofs[1]; o[4] = l.adj[0]; o[5] = l.adj[1];
    }
}

API void snt_set_ext(void* h, int l, int r, int no_limit) {
    Tool* t = (Tool*)h;
    t->snes.ppu.setExt(l, r);
    t->snes.ppu.noSpriteLimit = no_limit != 0;
}
API void snt_edge_mirror(void* h, int on) { ((Tool*)h)->snes.ppu.edgeMirror = on != 0; }
API void snt_frame(void* h, uint32_t* out, int* w, int* hh) {
    Tool* t = (Tool*)h;
    frame_rgb(t, *w, *hh);
    if (out) memcpy(out, t->rgb.data(), t->rgb.size() * 4);
}
// High frame rate: draw K-1 in-between pictures with each frame (1 = off), and read one (k = 1 .. K-1; 0 = the
// frame itself) as RGB pixels.
API void snt_set_sub_frames(void* h, int k) { ((Tool*)h)->snes.ppu.subFrames = k; }
API int snt_sub_frame(void* h, int k, uint32_t* out, int* w, int* hh) {
    const Ppu& p = ((Tool*)h)->snes.ppu;
    *w = 256 + p.extL + p.extR;
    *hh = p.frameHeight();
    if (k < 0 || k >= Ppu::kMaxSubFrames) return 0;
    const uint32_t* src = k ? p.subFrame[k - 1] : p.frame;
    if (out)
        for (int y = 0; y < *hh; y++) {
            const uint32_t* row = src + (size_t)y * kLineW + (kMaxExt - p.extL);
            for (int x = 0; x < *w; x++) out[(size_t)y * *w + x] = 0xFF000000u | Ppu::to_rgb(row[x]);
        }
    return 1;
}
API int snt_save_png(void* h, const char* path, int scale) {
    Tool* t = (Tool*)h;
    int w, hh;
    frame_rgb(t, w, hh);
    return write_png(path, t->rgb.data(), w, hh, scale < 1 ? 1 : scale);
}
API int snt_write_png(const char* path, const uint32_t* px, int w, int h, int scale) { return write_png(path, px, w, h, scale); }
API void snt_layers(void* h, int mask) { ((Tool*)h)->snes.ppu.debugLayers = (uint8_t)mask; }

API void snt_coverage_enable(void* h, int on) {
    Tool* t = (Tool*)h;
    t->cov_on = on != 0;
    if (t->cov_on && t->coverage.size() != t->snes.rom.size()) {
        t->coverage.assign(t->snes.rom.size(), 0);
        t->covflags.assign(t->snes.rom.size(), 0);
        t->ramcov.assign(0x20000, 0);
    }
    update_trace(t);
}
API uint32_t* snt_coverage(void* h) { return ((Tool*)h)->coverage.data(); }
API uint8_t* snt_covflags(void* h) { return ((Tool*)h)->covflags.data(); }
API uint8_t* snt_ramcov(void* h) { return ((Tool*)h)->ramcov.data(); }
API void snt_coverage_clear(void* h) {
    Tool* t = (Tool*)h;
    std::fill(t->coverage.begin(), t->coverage.end(), 0);
    std::fill(t->covflags.begin(), t->covflags.end(), 0);
    std::fill(t->ramcov.begin(), t->ramcov.end(), 0);
}
API void snt_trace_enable(void* h, int n) {
    Tool* t = (Tool*)h;
    t->trace.assign(n > 0 ? n : 0, 0);
    t->trace_pos = 0;
    update_trace(t);
}
// The last `n` PCs, oldest first.
API int snt_trace(void* h, uint32_t* out, int n) {
    Tool* t = (Tool*)h;
    int sz = (int)t->trace.size();
    if (!sz) return 0;
    n = std::min(n, sz);
    for (int i = 0; i < n; i++) out[i] = t->trace[(t->trace_pos + sz - n + i) % sz];
    return n;
}

API void snt_bp_add(void* h, uint32_t pc) {
    Tool* t = (Tool*)h;
    t->bps.push_back(pc & 0xffffff);
    t->snes.markExec(pc, true);
}
API void snt_bp_clear(void* h) {
    Tool* t = (Tool*)h;
    t->bps.clear();
    t->bp_log.clear();
    t->snes.clearExecMarks();
    if (t->module) t->module->configure(t->snes, t->cfg);   // the module's own hooks stay
}
// 14 uint32 per hit; returns the number copied (and clears the log)
API int snt_bp_log(void* h, uint32_t* out, int max) {
    Tool* t = (Tool*)h;
    int n = std::min<int>(max, (int)t->bp_log.size());
    if (out) memcpy(out, t->bp_log.data(), (size_t)n * sizeof(BpHit));
    t->bp_log.clear();
    return n;
}
API void snt_watch(void* h, uint32_t lo, uint32_t hi) {
    Tool* t = (Tool*)h;
    t->watch_log.clear();
    if (lo > hi) t->snes.setWriteWatch(1, 0, nullptr, nullptr);
    else t->snes.setWriteWatch(lo, hi, watch_cb, t);
}
API int snt_watch_log(void* h, uint32_t* out, int max) {
    Tool* t = (Tool*)h;
    int n = std::min<int>(max, (int)t->watch_log.size());
    if (out) memcpy(out, t->watch_log.data(), (size_t)n * sizeof(WatchHit));
    t->watch_log.clear();
    return n;
}
API void snt_rwatch(void* h, uint32_t lo, uint32_t hi) {
    Tool* t = (Tool*)h;
    t->rwatch_log.clear();
    if (lo > hi) t->snes.setReadWatch(1, 0, nullptr, nullptr);
    else t->snes.setReadWatch(lo, hi, rwatch_cb, t);
}
API int snt_rwatch_log(void* h, uint32_t* out, int max) {
    Tool* t = (Tool*)h;
    int n = std::min<int>(max, (int)t->rwatch_log.size());
    if (out) memcpy(out, t->rwatch_log.data(), (size_t)n * sizeof(WatchHit));
    t->rwatch_log.clear();
    return n;
}
// VRAM word writes (CPU or DMA) to word addresses [lo, hi]: frame, line, pc (of the CPU, e.g. the DMA start), adr, value
API void snt_vram_watch(void* h, uint32_t lo, uint32_t hi) {
    Tool* t = (Tool*)h;
    t->vram_lo = lo; t->vram_hi = hi;
    t->vram_log.clear();
    t->snes.ppu.vramWatch = lo <= hi ? vram_cb : nullptr;
    t->snes.ppu.vramWatchCtx = t;
}
API int snt_vram_log(void* h, uint32_t* out, int max) {
    Tool* t = (Tool*)h;
    int n = std::min<int>(max, (int)t->vram_log.size());
    if (out) memcpy(out, t->vram_log.data(), (size_t)n * 20);
    t->vram_log.clear();
    return n;
}
API void snt_port_watch(void* h, int on) { Tool* t = (Tool*)h; t->port_on = on != 0; t->port_log.clear(); }
API int snt_port_log(void* h, uint32_t* out, int max) {
    Tool* t = (Tool*)h;
    int n = std::min<int>(max, (int)t->port_log.size());
    if (out) memcpy(out, t->port_log.data(), (size_t)n * sizeof(PortHit));
    t->port_log.clear();
    return n;
}

API int snt_state_size(void* h) { return (int)((Tool*)h)->snes.saveState().size(); }
API int snt_save_state(void* h, uint8_t* out, int max) {
    std::vector<uint8_t> s = ((Tool*)h)->snes.saveState();
    if ((int)s.size() > max) return -(int)s.size();
    memcpy(out, s.data(), s.size());
    return (int)s.size();
}
API int snt_load_state(void* h, const uint8_t* data, int n) {
    std::vector<uint8_t> s(data, data + n);
    return ((Tool*)h)->snes.loadState(s) ? 1 : 0;
}

// Audio generated since the last call (stereo int16 at 32040 Hz); enable first.
API void snt_audio_enable(void* h, int on) { ((Tool*)h)->snes.apu.generateAudio = on != 0; }
API int snt_run_frames_audio(void* h, int n, int16_t* out, int max_frames) {
    Tool* t = (Tool*)h;
    int got = 0;
    for (int i = 0; i < n; i++) {
        t->snes.input[0] = t->buttons[0];
        t->snes.input[1] = t->buttons[1];
        if (t->module) t->module->pre_frame(t->snes, t->cfg);
        t->snes.runFrame();
        if (t->module) { t->info = FrameInfo(); t->module->post_frame(t->snes, t->cfg, t->info); }
        std::vector<int16_t>& s = t->snes.apu.samples;
        int frames = (int)s.size() / 2;
        int take = std::min(frames, max_frames - got);
        if (take > 0 && out) memcpy(out + got * 2, s.data(), (size_t)take * 4);
        got += std::max(0, take);
        s.clear();
    }
    return got;
}
API int snt_voice_env(void* h, int v) { return ((Tool*)h)->snes.apu.dsp.voiceLevel(v & 7); }

API int snt_disasm(void* h, uint32_t pc, int mflag, int xflag, char* out, int max) {
    Tool* t = (Tool*)h;
    std::string s;
    int len = disasm65816(pc, mflag != 0, xflag != 0, [t](uint32_t a) { return t->snes.peek(a); }, s);
    snprintf(out, max, "%s", s.c_str());
    return len;
}

// ---- game modules (tests of the port's per-game code) ----
API int snt_module_create(void* h, const char* id) {
    Tool* t = (Tool*)h;
    std::string s = id;
    GameModule* m = create_module(s);
    if (!m || m->system() != System::Snes) { delete m; return 0; }
    t->module.reset(static_cast<SnesModule*>(m));
    t->ext_adapter.t = t;
    t->snes.stateExt = &t->ext_adapter;
    return 1;
}
// cheats: comma-separated keys
API void snt_module_configure(void* h, int ext, int hud, const char* cheats) {
    Tool* t = (Tool*)h;
    if (!t->module) return;
    GameConfig c;
    c.ext = ext;
    c.hud_anchor = hud != 0;
    c.cpu_turbo = t->cfg.cpu_turbo;   // (snt_module_turbo)
    std::string list = cheats ? cheats : "";
    for (size_t a = 0; a < list.size();) {
        size_t b = list.find(',', a);
        if (b == std::string::npos) b = list.size();
        if (b > a) c.cheats[list.substr(a, b - a)] = true;
        a = b + 1;
    }
    t->cfg = c;
    t->snes.ppu.setExt(c.ext, c.ext);
    t->module->configure(t->snes, t->cfg);
    for (uint32_t pc : t->bps) t->snes.markExec(pc, true);
}
// CPU speed for the game's frame work (GameConfig::cpu_turbo; 1 = original)
// cpuTurbo, turboActive, the CPU's interrupt depth (Remove Slowdown checks)
API void snt_turbo_state(void* h, int* out) {
    Tool* t = (Tool*)h;
    out[0] = t->snes.cpuTurbo;
    out[1] = t->snes.turboActive ? 1 : 0;
    out[2] = t->snes.irqDepth();
}
API void snt_module_turbo(void* h, int k) {
    Tool* t = (Tool*)h;
    t->cfg.cpu_turbo = k < 1 ? 1 : k;
    if (t->module) t->module->configure(t->snes, t->cfg);
}
API int snt_module_can_configure(void* h, int ext, const char* cheats) {
    Tool* t = (Tool*)h;
    if (!t->module) return 1;
    GameConfig c = t->cfg;
    c.ext = ext;
    c.cheats.clear();
    std::string list = cheats ? cheats : "";
    for (size_t a = 0; a < list.size();) {
        size_t b = list.find(',', a);
        if (b == std::string::npos) b = list.size();
        if (b > a) c.cheats[list.substr(a, b - a)] = true;
        a = b + 1;
    }
    return t->module->can_configure(t->snes, c) ? 1 : 0;
}
// gameplay, mask_left, mask_right, mask_lines
API void snt_module_info(void* h, int* out) {
    const FrameInfo& i = ((Tool*)h)->info;
    out[0] = i.gameplay; out[1] = i.mask_left; out[2] = i.mask_right; out[3] = i.mask_lines;
}
API int snt_module_view_left(void* h) { return ((Tool*)h)->info.view_left; }   // (FrameInfo::view_left)
API int snt_module_start_level(void* h, int index, int variant) {
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    int ok = t->module->start_level(t->snes, t->cfg, index, variant) ? 1 : 0;
    t->module->configure(t->snes, t->cfg);
    return ok;
}
API void snt_module_jukebox_boot(void* h) { Tool* t = (Tool*)h; if (t->module) t->module->jukebox_boot(t->snes); }
API void snt_module_play(void* h, int id) { Tool* t = (Tool*)h; if (t->module) t->module->play_sound(t->snes, id); }
API void snt_module_jukebox_frame(void* h) { Tool* t = (Tool*)h; if (t->module) t->module->jukebox_frame(t->snes); }
// Achievement checks over a run: returns indices unlocked this frame (call after each frame)
API int snt_module_ach(void* h, int* out, int max) {
    static AchievementCtx ctx;
    static std::vector<uint8_t> prev;
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    if (max < 0) { ctx = AchievementCtx(); ctx.run_counts = true; return 0; }
    t->module->achievement_frame(t->snes, ctx);
    ctx.prev.assign(t->snes.wram, t->snes.wram + sizeof(t->snes.wram));
    int n = 0;
    for (int i : ctx.unlocked) if (n < max) out[n++] = i;
    for (auto& p : ctx.marked) if (n < max) out[n++] = 1000 + p.first * 32 + p.second;
    for (int i : ctx.counted) if (n < max) out[n++] = 5000 + i;
    for (auto& p : ctx.progress) if (n < max) out[n++] = 100000 + p.first * 1000 + p.second;   // (index, count)
    ctx.unlocked.clear(); ctx.marked.clear(); ctx.counted.clear(); ctx.progress.clear();
    return n;
}
