// Master System system bus and scheduler.
#include "sms.h"
#include <cstring>
#include <algorithm>

static uint8_t cb_read(void* c, uint16_t a) { return ((Sms*)c)->read(a); }
static void    cb_write(void* c, uint16_t a, uint8_t v) { ((Sms*)c)->write(a, v); }
static uint8_t cb_in(void* c, uint16_t p) { return ((Sms*)c)->port_in((uint8_t)p); }
static void    cb_out(void* c, uint16_t p, uint8_t v) { ((Sms*)c)->port_out((uint8_t)p, v); }
static void    cb_irq(void* c, bool on) { ((Sms*)c)->z80.set_irq(on); }

Sms::Sms() {
    z80.ctx = this;
    z80.read = cb_read;
    z80.write = cb_write;
    z80.in = cb_in;
    z80.out = cb_out;
    z80.hook_fn = hook_dispatch;
    z80.hook_ctx = this;
    vdp.host = this;
    vdp.irq_changed = cb_irq;
    memset(ram, 0, sizeof(ram));
    memset(cart_ram, 0, sizeof(cart_ram));
    memset(hook_map_, 0, sizeof(hook_map_));
    audio.reserve(4096);
}

bool Sms::load_rom(const std::vector<uint8_t>& data) {
    if (data.size() < 0x4000) return false;
    size_t sz = 0x4000;
    while (sz < data.size()) sz <<= 1;           // whole pages, a power of two for mirroring
    rom.assign(sz, 0xFF);
    memcpy(rom.data(), data.data(), data.size());
    rom_orig = rom;
    return true;
}

void Sms::patch_rom8(uint32_t a, uint8_t v) { rom[a % rom.size()] = v; }
void Sms::restore_rom() { rom = rom_orig; }

void Sms::power_on() {
    z80.reset();
    z80.sp = 0xDFF0;
    z80.im = 1;
    vdp.reset();
    psg.reset();
    memset(ram, 0, sizeof(ram));
    bank[0] = 0; bank[1] = 1; bank[2] = 2;
    ram_ctrl = 0;
    mem_ctrl = 0xAB;
    io_ctrl = 0xFF;
    cycles_ = line_start_ = audio_clock_ = 0;
    pause_prev_ = false;
    frame_count = 0;
    memset(module_state, 0, sizeof(module_state));
}

void Sms::soft_reset() {
    // The Master System's reset button is only a bit the games read (port $DD bit 4): hold it for a few frames.
    pad_buttons[0] |= SMS_RESET;
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
long Sms::rom_offset(uint16_t a) const {
    const size_t pages = rom.size() / 0x4000;
    if (a < 0x0400) return a;
    if (a < 0x4000) return (long)((bank[0] % pages) * 0x4000 + a);
    if (a < 0x8000) return (long)((bank[1] % pages) * 0x4000 + (a & 0x3FFF));
    if (a < 0xC000 && !(ram_ctrl & 0x08)) return (long)((bank[2] % pages) * 0x4000 + (a & 0x3FFF));
    return -1;
}

uint8_t Sms::peek(uint16_t a) const {
    if (a >= 0xC000) return ram[a & 0x1FFF];
    if (a >= 0x8000 && (ram_ctrl & 0x08)) return cart_ram[((ram_ctrl & 0x04) ? 0x4000 : 0) + (a & 0x3FFF)];
    return rom[(size_t)rom_offset(a)];
}

uint8_t Sms::read(uint16_t a) { return peek(a); }

void Sms::write(uint16_t a, uint8_t v) {
    if (a >= 0xC000) {
        ram[a & 0x1FFF] = v;
        if (a >= 0xFFFC) {
            switch (a) {
            case 0xFFFC: ram_ctrl = v; break;
            case 0xFFFD: bank[0] = v; break;
            case 0xFFFE: bank[1] = v; break;
            case 0xFFFF: bank[2] = v; break;
            }
        }
        if (on_ram_write) on_ram_write(*this, (uint16_t)(0xC000 | (a & 0x1FFF)), v);
    } else if (a >= 0x8000 && (ram_ctrl & 0x08)) {
        cart_ram[((ram_ctrl & 0x04) ? 0x4000 : 0) + (a & 0x3FFF)] = v;
    }
}

// ---------------------------------------------------------------------------
// I/O ports
// ---------------------------------------------------------------------------
uint8_t Sms::port_in(uint8_t p) {
    switch (p & 0xC1) {
    case 0x40: return vdp.read_vcount();
    case 0x41: return vdp.read_hcount();
    case 0x80: return vdp.read_data();
    case 0x81: return vdp.read_status();
    case 0xC0: {   // port A: player 1, player 2's up / down
        const uint16_t a = pad_buttons[0], b = pad_buttons[1];
        return (uint8_t)~((a & 0x3F) | ((b & 0x03) << 6));
    }
    case 0xC1: {   // port B: player 2's left / right / buttons, reset, the TH lines
        const uint16_t b = pad_buttons[1];
        uint8_t v = (uint8_t)(~((b >> 2) & 0x0F) & 0x0F);
        if (!(pad_buttons[0] & SMS_RESET)) v |= 0x10;
        v |= 0x20;
        // TH as an output reads back what was written (an export console); as an input it is pulled high.
        const bool tha = (io_ctrl & 0x02) ? true : (io_ctrl & 0x20) != 0;
        const bool thb = (io_ctrl & 0x08) ? true : (io_ctrl & 0x80) != 0;
        if (tha) v |= 0x40;
        if (thb) v |= 0x80;
        return v;
    }
    default: return 0xFF;
    }
}

void Sms::port_out(uint8_t p, uint8_t v) {
    switch (p & 0xC1) {
    case 0x00: mem_ctrl = v; break;
    case 0x01: {
        // TH going high latches the H counter (light gun)
        const bool tha_before = (io_ctrl & 0x02) || (io_ctrl & 0x20), tha_after = (v & 0x02) || (v & 0x20);
        const bool thb_before = (io_ctrl & 0x08) || (io_ctrl & 0x80), thb_after = (v & 0x08) || (v & 0x80);
        if ((!tha_before && tha_after) || (!thb_before && thb_after)) vdp.latch_hcount(line_cycle());
        io_ctrl = v;
        break;
    }
    case 0x40: case 0x41: sync_audio(cycles_ + z80.executed()); psg.write(v); break;
    case 0x80:
        if (on_vdp_write) on_vdp_write(*this, vdp.addr & 0x3FFF, v, vdp.code == 3);
        vdp.write_data(v);
        break;
    case 0x81: vdp.write_ctrl(v); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------
void Sms::sync_audio(int64_t now) {
    const int64_t n = (now - audio_clock_) / kCyclesPerSample;
    if (n <= 0) return;
    const size_t need = (size_t)(audio_frames + n) * 2;
    if (audio.size() < need) audio.resize(need + 256);
    if (psg_tmp_.size() < (size_t)n) psg_tmp_.resize((size_t)n);
    psg.generate(psg_tmp_.data(), (int)n, kZ80Clock, sample_rate());
    int32_t* out = &audio[(size_t)audio_frames * 2];
    for (int64_t i = 0; i < n; i++) out[i * 2] = out[i * 2 + 1] = (int32_t)(psg_tmp_[i] * psg_gain);
    audio_frames += (int)n;
    audio_clock_ += n * kCyclesPerSample;
}

void Sms::run_until(int64_t t) {
    while (cycles_ < t) cycles_ += z80.run((int)(t - cycles_));
}

void Sms::run_frame() {
    if (on_frame_start) on_frame_start(*this);
    audio_frames = 0;
    // The pause button is wired to the NMI: one per press.
    const bool pause = (pad_buttons[0] & SMS_PAUSE) != 0;
    if (pause && !pause_prev_) z80.trigger_nmi();
    pause_prev_ = pause;
    for (int ln = 0; ln < kLines; ln++) {
        line_start_ = (int64_t)ln * kCyclesPerLine;
        vdp.line = ln;
        if (ln == 0) vdp.start_frame();
        if (ln < vdp.active_lines()) vdp.render_line(ln);
        if (ln == 0xC1) {
            if (on_vblank) on_vblank(*this);
            vdp.frame_irq();
        }
        // the line counter runs at the start of the horizontal blank, so a line interrupt's handler can set up
        // the next line (scroll) before it is drawn
        run_until(line_start_ + 200);
        vdp.line_counter_tick();
        run_until(line_start_ + kCyclesPerLine);
    }
    const int64_t frame_len = (int64_t)kLines * kCyclesPerLine;
    sync_audio(frame_len);
    cycles_ -= frame_len;
    line_start_ -= frame_len;
    audio_clock_ -= frame_len;
    frame_count++;
    if (pad_buttons[0] & SMS_RESET) pad_buttons[0] &= ~SMS_RESET;
    if (on_frame_end) on_frame_end(*this);
}

// ---------------------------------------------------------------------------
// PC hooks
// ---------------------------------------------------------------------------
void Sms::hook_dispatch(void* ctx, uint16_t pc) {
    Sms* s = (Sms*)ctx;
    if (s->trace_fn_) {
        s->trace_fn_(s->trace_ctx_, pc);
        if (!((s->hook_map_[pc >> 3] >> (pc & 7)) & 1)) return;
    }
    auto it = s->pc_hooks_.find(pc);
    if (it != s->pc_hooks_.end()) it->second(*s);
}

void Sms::add_pc_hook(uint16_t addr, std::function<void(Sms&)> fn) {
    pc_hooks_[addr] = std::move(fn);
    hook_map_[addr >> 3] |= (uint8_t)(1 << (addr & 7));
    if (!trace_fn_) z80.hook_map = hook_map_;
}

void Sms::clear_pc_hooks() {
    pc_hooks_.clear();
    memset(hook_map_, 0, sizeof(hook_map_));
    if (!trace_fn_) z80.hook_map = nullptr;
}

void Sms::remove_pc_hook(uint16_t addr) {
    pc_hooks_.erase(addr);
    hook_map_[addr >> 3] &= (uint8_t)~(1 << (addr & 7));
    if (pc_hooks_.empty() && !trace_fn_) z80.hook_map = nullptr;
}

void Sms::set_trace(void (*fn)(void* ctx, uint16_t pc), void* ctx) {
    static uint8_t all[8192];
    if (!all[0]) memset(all, 0xFF, sizeof(all));
    trace_fn_ = fn;
    trace_ctx_ = ctx;
    z80.hook_map = fn ? all : pc_hooks_.empty() ? nullptr : hook_map_;
}

// ---------------------------------------------------------------------------
// Save states
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kMagic = 0x534D5331;   // "SMS1"
constexpr uint32_t kVersion = 1;
struct SysState {
    uint32_t magic, version;
    uint8_t bank[3], ram_ctrl, mem_ctrl, io_ctrl, pause_prev, pad;
    int64_t cycles, line_start, audio_clock;
    uint64_t frame_count;
    uint32_t module_state[8];
};
}

std::vector<uint8_t> Sms::save_state() {
    SysState s;
    memset(&s, 0, sizeof(s));
    s.magic = kMagic;
    s.version = kVersion;
    memcpy(s.bank, bank, 3);
    s.ram_ctrl = ram_ctrl;
    s.mem_ctrl = mem_ctrl;
    s.io_ctrl = io_ctrl;
    s.pause_prev = pause_prev_;
    s.cycles = cycles_;
    s.line_start = line_start_;
    s.audio_clock = audio_clock_;
    s.frame_count = frame_count;
    memcpy(s.module_state, module_state, sizeof(module_state));
    std::vector<uint8_t> out(sizeof(s) + z80.state_size() + vdp.state_size() + psg.state_size() + sizeof(ram) + sizeof(cart_ram));
    uint8_t* p = out.data();
    memcpy(p, &s, sizeof(s)); p += sizeof(s);
    z80.save_state(p); p += z80.state_size();
    vdp.save_state(p); p += vdp.state_size();
    psg.save_state(p); p += psg.state_size();
    memcpy(p, ram, sizeof(ram)); p += sizeof(ram);
    memcpy(p, cart_ram, sizeof(cart_ram));
    return out;
}

bool Sms::load_state(const std::vector<uint8_t>& data) {
    const size_t need = sizeof(SysState) + z80.state_size() + vdp.state_size() + psg.state_size() + sizeof(ram) + sizeof(cart_ram);
    if (data.size() != need) return false;
    SysState s;
    memcpy(&s, data.data(), sizeof(s));
    if (s.magic != kMagic || s.version != kVersion) return false;
    const uint8_t* p = data.data() + sizeof(s);
    memcpy(bank, s.bank, 3);
    ram_ctrl = s.ram_ctrl;
    mem_ctrl = s.mem_ctrl;
    io_ctrl = s.io_ctrl;
    pause_prev_ = s.pause_prev != 0;
    cycles_ = s.cycles;
    line_start_ = s.line_start;
    audio_clock_ = s.audio_clock;
    frame_count = s.frame_count;
    memcpy(module_state, s.module_state, sizeof(module_state));
    z80.load_state(p); p += z80.state_size();
    vdp.load_state(p); p += vdp.state_size();
    psg.load_state(p); p += psg.state_size();
    memcpy(ram, p, sizeof(ram)); p += sizeof(ram);
    memcpy(cart_ram, p, sizeof(cart_ram));
    return true;
}
