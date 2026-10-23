// smstool.dll - headless Master System core with a C API for Python-driven analysis (tools/smst.py):
// screenshots, RAM/VRAM access, PC coverage, breakpoint logs, RAM write watches, save states.
#include "../core/sms.h"
#include "../game/game.h"
#include <memory>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <iterator>
#include <algorithm>

#define API extern "C" __declspec(dllexport)

namespace {
// regs: af bc de hl ix iy sp pc af' bc' de' hl', banks
struct BpHit { uint32_t frame, pc, line, bank; uint32_t regs[12]; };
struct WatchHit { uint32_t frame, pc, addr, value; };

struct Tool {
    Sms sms;
    uint16_t buttons[2] = {0, 0};
    std::vector<uint32_t> coverage;      // execution count per ROM byte (by ROM offset)
    bool cov_on = false;
    std::vector<BpHit> bp_log;
    std::vector<WatchHit> watch_log;
    uint16_t watch_lo = 0, watch_hi = 0;
    size_t   log_limit = 200000;
    std::unique_ptr<GameModule> module;  // optional game module under test
    GameConfig cfg;
    FrameInfo info;
};

uint32_t sms_rgb(uint8_t c) {
    static const int lv[4] = {0, 85, 170, 255};
    return 0xFF000000u | (lv[c & 3] << 16) | (lv[(c >> 2) & 3] << 8) | lv[(c >> 4) & 3];
}

void trace_cb(void* ctx, uint16_t pc) {
    Tool* t = (Tool*)ctx;
    const long o = t->sms.rom_offset(pc);
    if (o >= 0 && (size_t)o < t->coverage.size()) t->coverage[o]++;
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

GameModule* make_module(const std::string& id) {
    GameModule* m = create_module(id);
    return m && m->system() == System::MasterSystem ? m : (delete m, nullptr);
}
SmsModule* sms_module(Tool* t) { return static_cast<SmsModule*>(t->module.get()); }
}  // namespace

API void* smst_create(const char* rom_path) {
    std::ifstream f(rom_path, std::ios::binary);
    if (!f) return nullptr;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Tool* t = new Tool;
    if (!t->sms.load_rom(data)) { delete t; return nullptr; }
    t->sms.power_on();
    return t;
}
API void smst_destroy(void* h) { delete (Tool*)h; }
API void smst_power_on(void* h) { ((Tool*)h)->sms.power_on(); }
API void smst_set_buttons(void* h, int port, int buttons) { ((Tool*)h)->buttons[port & 1] = (uint16_t)buttons; }
API void smst_run_frames(void* h, int n) {
    Tool* t = (Tool*)h;
    for (int i = 0; i < n; i++) {
        t->sms.set_pad(0, t->buttons[0]);
        t->sms.set_pad(1, t->buttons[1]);
        t->sms.run_frame();
    }
}
API uint64_t smst_frame_count(void* h) { return ((Tool*)h)->sms.frame_count; }
API uint8_t* smst_ram(void* h) { return ((Tool*)h)->sms.ram; }
API uint8_t* smst_vram(void* h) { return ((Tool*)h)->sms.vdp.vram; }
API uint8_t* smst_cram(void* h) { return ((Tool*)h)->sms.vdp.cram; }
API uint8_t* smst_vdp_regs(void* h) { return ((Tool*)h)->sms.vdp.reg; }
API uint8_t* smst_rom(void* h) { return ((Tool*)h)->sms.rom.data(); }
API int smst_rom_size(void* h) { return (int)((Tool*)h)->sms.rom.size(); }
API uint8_t* smst_banks(void* h) { return ((Tool*)h)->sms.bank; }
API int smst_peek(void* h, int addr) { return ((Tool*)h)->sms.peek((uint16_t)addr); }
API void smst_set_ext(void* h, int ext, int sprite_limit) {
    Sms& s = ((Tool*)h)->sms;
    s.vdp.ext = ext;
    s.vdp.sprite_limit = sprite_limit != 0;
    s.vdp.ignore_left_blank = ext > 0;
}
API void smst_debug_layers(void* h, int mask) { ((Tool*)h)->sms.vdp.debug_layers = (uint8_t)mask; }
API void smst_get_cpu(void* h, uint32_t* out) {   // af bc de hl ix iy sp pc af' bc' de' hl' iff1 im
    const Z80& z = ((Tool*)h)->sms.z80;
    const uint32_t v[14] = {z.af, z.bc, z.de, z.hl, z.ix, z.iy, z.sp, z.pc, z.af2, z.bc2, z.de2, z.hl2, (uint32_t)z.iff1, z.im};
    memcpy(out, v, sizeof(v));
}
API int smst_frame(void* h, uint32_t* out, int* w, int* hgt) {
    const SmsVdp& v = ((Tool*)h)->sms.vdp;
    *w = v.fb_width;
    *hgt = v.fb_lines;
    if (out)
        for (int y = 0; y < v.fb_lines; y++)
            for (int x = 0; x < v.fb_width; x++) out[y * v.fb_width + x] = sms_rgb(v.fb[y][x] & 0x3F);
    return v.fb_width * v.fb_lines;
}
API int smst_save_png(void* h, const char* path, int scale) {
    const SmsVdp& v = ((Tool*)h)->sms.vdp;
    std::vector<uint32_t> px((size_t)v.fb_width * v.fb_lines);
    int w, hh;
    smst_frame(h, px.data(), &w, &hh);
    return write_png(path, px.data(), w, hh, scale) ? 1 : 0;
}
API int smst_write_png(const char* path, const uint32_t* px, int w, int h, int scale) {
    return write_png(path, px, w, h, scale) ? 1 : 0;
}

// ---- coverage: execution counts per ROM offset ----
API void smst_coverage_enable(void* h, int on) {
    Tool* t = (Tool*)h;
    t->cov_on = on != 0;
    if (t->cov_on) t->coverage.assign(t->sms.rom.size(), 0);
    t->sms.set_trace(t->cov_on ? trace_cb : nullptr, t);
}
API uint32_t* smst_coverage(void* h) { return ((Tool*)h)->coverage.data(); }
API void smst_coverage_clear(void* h) { Tool* t = (Tool*)h; std::fill(t->coverage.begin(), t->coverage.end(), 0u); }

// ---- breakpoints (logged, not stopping) ----
API void smst_bp_add(void* h, uint32_t addr) {
    Tool* t = (Tool*)h;
    t->sms.add_pc_hook((uint16_t)addr, [t](Sms& s) {
        if (t->bp_log.size() >= t->log_limit) return;
        const Z80& z = s.z80;
        BpHit b;
        b.frame = (uint32_t)s.frame_count;
        b.pc = z.pc;
        b.line = (uint32_t)s.vdp.line;
        b.bank = s.bank[0] | (s.bank[1] << 8) | (s.bank[2] << 16) | (s.ram_ctrl << 24);
        const uint32_t r[12] = {z.af, z.bc, z.de, z.hl, z.ix, z.iy, z.sp, z.pc, z.af2, z.bc2, z.de2, z.hl2};
        memcpy(b.regs, r, sizeof(r));
        t->bp_log.push_back(b);
    });
}
API void smst_bp_clear(void* h) { Tool* t = (Tool*)h; t->sms.clear_pc_hooks(); t->bp_log.clear(); }
API int smst_bp_log(void* h, uint32_t* out, int max) {   // 16 words per hit
    Tool* t = (Tool*)h;
    int n = std::min<int>(max, (int)t->bp_log.size());
    if (out) memcpy(out, t->bp_log.data(), (size_t)n * sizeof(BpHit));
    t->bp_log.erase(t->bp_log.begin(), t->bp_log.begin() + n);
    return n;
}

// ---- RAM write watch ----
API void smst_watch(void* h, uint32_t lo, uint32_t hi) {
    Tool* t = (Tool*)h;
    t->watch_lo = (uint16_t)lo;
    t->watch_hi = (uint16_t)hi;
    t->watch_log.clear();
    if (hi < lo) { t->sms.on_ram_write = nullptr; return; }
    t->sms.on_ram_write = [t](Sms& s, uint16_t a, uint8_t v) {
        if (a < t->watch_lo || a > t->watch_hi || t->watch_log.size() >= t->log_limit) return;
        t->watch_log.push_back({(uint32_t)s.frame_count, s.z80.pc, a | ((uint32_t)s.vdp.line << 16), v});
    };
}
API int smst_watch_log(void* h, uint32_t* out, int max) {   // 4 words per hit: frame, pc, addr | line << 16, value
    Tool* t = (Tool*)h;
    int n = std::min<int>(max, (int)t->watch_log.size());
    if (out) memcpy(out, t->watch_log.data(), (size_t)n * sizeof(WatchHit));
    t->watch_log.erase(t->watch_log.begin(), t->watch_log.begin() + n);
    return n;
}

// ---- save states / audio / patches ----
API int smst_state_size(void* h) { return (int)((Tool*)h)->sms.save_state().size(); }
API int smst_save_state(void* h, uint8_t* out, int max) {
    std::vector<uint8_t> s = ((Tool*)h)->sms.save_state();
    if ((int)s.size() > max) return 0;
    memcpy(out, s.data(), s.size());
    return (int)s.size();
}
API int smst_load_state(void* h, const uint8_t* data, int n) {
    return ((Tool*)h)->sms.load_state(std::vector<uint8_t>(data, data + n)) ? 1 : 0;
}
API int smst_audio(void* h, int32_t* out, int max_frames) {
    Sms& s = ((Tool*)h)->sms;
    int n = std::min(max_frames, s.audio_frames);
    if (out) memcpy(out, s.audio.data(), (size_t)n * 2 * sizeof(int32_t));
    return n;
}
API double smst_sample_rate(void* h) { return ((Tool*)h)->sms.sample_rate(); }
API void smst_patch8(void* h, uint32_t addr, uint32_t v) { ((Tool*)h)->sms.patch_rom8(addr, (uint8_t)v); }

// ---- game module testing -----------------------------------------------------------
API int smst_module_create(void* h, const char* id) {
    Tool* t = (Tool*)h;
    t->module.reset(make_module(id ? id : ""));
    return t->module ? 1 : 0;
}
// cheats: comma separated keys
API void smst_module_configure(void* h, int ext, int hud_anchor, const char* cheats) {
    Tool* t = (Tool*)h;
    if (!t->module) return;
    t->cfg = GameConfig();
    t->cfg.ext = ext;
    t->cfg.hud_anchor = hud_anchor != 0;
    std::stringstream ss(cheats ? cheats : "");
    std::string k;
    while (std::getline(ss, k, ',')) if (!k.empty()) t->cfg.cheats[k] = true;
    t->sms.vdp.ext = ext;
    t->sms.vdp.sprite_limit = ext == 0;
    t->sms.vdp.ignore_left_blank = ext > 0;
    sms_module(t)->configure(t->sms, t->cfg);
}
API int smst_module_start_level(void* h, int entry, int variant) {
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    int ok = sms_module(t)->start_level(t->sms, t->cfg, entry, variant) ? 1 : 0;
    sms_module(t)->configure(t->sms, t->cfg);
    return ok;
}
// Runs frames through the module hooks with the current buttons. Returns info packed:
// bit0 gameplay, bits 8-15 mask_left, bits 16-23 mask_right.
API int smst_module_frames(void* h, int n) {
    Tool* t = (Tool*)h;
    for (int i = 0; i < n; i++) {
        t->sms.set_pad(0, t->buttons[0]);
        t->sms.set_pad(1, t->buttons[1]);
        if (t->module) sms_module(t)->pre_frame(t->sms, t->cfg);
        t->sms.run_frame();
        t->info = FrameInfo();
        if (t->module) sms_module(t)->post_frame(t->sms, t->cfg, t->info);
    }
    return (t->info.gameplay ? 1 : 0) | ((t->info.mask_left & 0xFF) << 8) | ((t->info.mask_right & 0xFF) << 16);
}
API int smst_module_levels(void* h, char* out, int max) {   // "group|name|variants\n" lines
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    std::string s;
    for (auto& l : t->module->levels()) s += l.group + "|" + l.name + "|" + std::to_string(l.variants) + "\n";
    if (out && max > 0) { strncpy(out, s.c_str(), max - 1); out[max - 1] = 0; }
    return (int)t->module->levels().size();
}
API int smst_module_sounds(void* h, char* out, int max) {   // "id|music|name\n" lines
    Tool* t = (Tool*)h;
    if (!t->module) return 0;
    std::string s;
    for (auto& e : t->module->sounds()) s += std::to_string(e.id) + "|" + (e.music ? "1" : "0") + "|" + e.name + "\n";
    if (out && max > 0) { strncpy(out, s.c_str(), max - 1); out[max - 1] = 0; }
    return (int)t->module->sounds().size();
}
API void smst_module_jukebox_boot(void* h) { Tool* t = (Tool*)h; if (t->module) sms_module(t)->jukebox_boot(t->sms); }
API void smst_module_jukebox_frame(void* h) { Tool* t = (Tool*)h; if (t->module) sms_module(t)->jukebox_frame(t->sms); }
API void smst_module_play(void* h, int id) { Tool* t = (Tool*)h; if (t->module) sms_module(t)->play_sound(t->sms, id); }
API void smst_module_stop(void* h) { Tool* t = (Tool*)h; if (t->module) sms_module(t)->stop_sound(t->sms); }

// ---- achievement detectors on RAM images (headless checks of achievement_frame) ----
namespace {
struct AchTool {
    std::unique_ptr<GameModule> module;
    std::unique_ptr<Sms> sms;   // only its RAM is used
    AchievementCtx ctx;
};
}
API void* smst_ach_create(const char* game_id) {
    auto* a = new AchTool;
    a->module.reset(make_module(game_id ? game_id : ""));
    if (!a->module) { delete a; return nullptr; }
    a->sms.reset(new Sms);
    return a;
}
API void smst_ach_destroy(void* h) { delete (AchTool*)h; }
API void smst_ach_reset(void* h, int run_counts, uint32_t run_bits, const uint8_t* ram) {
    AchTool* a = (AchTool*)h;
    a->ctx = AchievementCtx();
    a->ctx.run_counts = run_counts != 0;
    a->ctx.run_bits = run_bits;
    if (ram) a->ctx.prev.assign(ram, ram + sizeof(a->sms->ram));
}
// One frame: `ram` = the 8 KB after it. Events go to out[] as kind << 16 | value << 8 | index
// (kind 0 unlocked, 1 counted, 2 marked with value = bit, 3 progress with value = count & 0xFF); returns how many.
API int smst_ach_step(void* h, const uint8_t* ram, int* out, int max) {
    AchTool* a = (AchTool*)h;
    memcpy(a->sms->ram, ram, sizeof(a->sms->ram));
    static_cast<SmsModule*>(a->module.get())->achievement_frame(*a->sms, a->ctx);
    a->ctx.prev.assign(ram, ram + sizeof(a->sms->ram));
    int n = 0;
    auto put = [&](int kind, int value, int index) { if (n < max) out[n++] = (kind << 16) | ((value & 0xFF) << 8) | (index & 0xFF); };
    for (int i : a->ctx.unlocked) put(0, 0, i);
    for (int i : a->ctx.counted) put(1, 0, i);
    for (auto& m : a->ctx.marked) put(2, m.second, m.first);
    for (auto& pr : a->ctx.progress) put(3, pr.second, pr.first);
    a->ctx.unlocked.clear();
    a->ctx.counted.clear();
    a->ctx.marked.clear();
    a->ctx.progress.clear();
    return n;
}
API uint32_t smst_ach_run_bits(void* h) { return ((AchTool*)h)->ctx.run_bits; }
