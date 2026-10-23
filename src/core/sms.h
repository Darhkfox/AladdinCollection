// Sega Master System: Z80 memory map with the Sega mapper, I/O ports, frame scheduling (NTSC).
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <functional>
#include <unordered_map>
#include "z80.h"
#include "sms_vdp.h"
#include "psg.h"

// Control pad bits (1 = pressed). PAUSE is the console's pause button (an NMI), RESET its reset button.
enum SmsButton : uint16_t {
    SMS_UP = 0x01, SMS_DOWN = 0x02, SMS_LEFT = 0x04, SMS_RIGHT = 0x08,
    SMS_1 = 0x10, SMS_2 = 0x20, SMS_PAUSE = 0x40, SMS_RESET = 0x80,
};

class Sms {
public:
    static constexpr double kZ80Clock = 3579545.0;     // NTSC
    static constexpr int kCyclesPerLine = 228;
    static constexpr int kLines = 262;
    static constexpr int kCyclesPerSample = 64;        // audio output period (55.9 kHz)

    Sms();

    bool load_rom(const std::vector<uint8_t>& data);
    void power_on();
    void soft_reset();                                 // the reset button: the games read it, nothing is reset
    void run_frame();

    void set_pad(int port, uint16_t buttons) { pad_buttons[port & 1] = buttons; }

    double sample_rate() const { return kZ80Clock / kCyclesPerSample; }
    double frame_rate() const { return kZ80Clock / (kCyclesPerLine * (double)kLines); }

    // Audio of the last frame, interleaved stereo int32 at sample_rate().
    std::vector<int32_t> audio;
    int   audio_frames = 0;
    float psg_gain = 1.0f;

    // Components (public for tools / game modules)
    Z80    z80;
    SmsVdp vdp;
    PSG    psg;
    uint8_t ram[0x2000];
    uint8_t cart_ram[0x8000];
    std::vector<uint8_t> rom;
    uint8_t bank[3] = {0, 1, 2};   // mapper slots ($FFFD-$FFFF)
    uint8_t ram_ctrl = 0;          // $FFFC: bit 3 cartridge RAM at $8000, bit 2 its bank
    uint8_t mem_ctrl = 0xAB;       // port $3E
    uint8_t io_ctrl = 0xFF;        // port $3F
    uint64_t frame_count = 0;
    uint32_t module_state[8] = {};   // scratch for game modules: cleared on power-on, kept in save states

    uint16_t pad_buttons[2] = {0, 0};

    // ---- hooks for game modules and tools ------------------------------------
    // PC hooks: called before the instruction at Z80 address `addr` executes (any bank: check bank[] if it matters).
    void add_pc_hook(uint16_t addr, std::function<void(Sms&)> fn);
    void clear_pc_hooks();
    void remove_pc_hook(uint16_t addr);
    // Execution trace (tools: coverage): called before every instruction while set.
    void set_trace(void (*fn)(void* ctx, uint16_t pc), void* ctx);
    // Called at the start of each frame / at the frame interrupt (line $C1) / at the end of the frame.
    std::function<void(Sms&)> on_frame_start, on_vblank, on_frame_end;
    // RAM write watch: called after every write to work RAM when set (addr $C000-$DFFF).
    std::function<void(Sms&, uint16_t addr, uint8_t value)> on_ram_write;
    // VRAM / CRAM write watch (tools): called before every data-port write when set (addr = the VDP address,
    // cram = a palette write).
    std::function<void(Sms&, uint16_t addr, uint8_t value, bool cram)> on_vdp_write;

    // RAM / ROM helpers (address = Z80 address in $C000-$FFFF, or ROM offset)
    uint8_t  ram8(uint16_t a) const { return ram[a & 0x1FFF]; }
    uint16_t ram16(uint16_t a) const { return (uint16_t)(ram[a & 0x1FFF] | (ram[(a + 1) & 0x1FFF] << 8)); }
    void set_ram8(uint16_t a, uint8_t v) { ram[a & 0x1FFF] = v; }
    void set_ram16(uint16_t a, uint16_t v) { ram[a & 0x1FFF] = (uint8_t)v; ram[(a + 1) & 0x1FFF] = (uint8_t)(v >> 8); }
    uint8_t rom8(uint32_t a) const { return rom[a % rom.size()]; }
    // ROM offset of a Z80 address in $0000-$BFFF with the current banking (-1 for RAM)
    long rom_offset(uint16_t a) const;
    // ROM patching (applied to the working copy; original kept for reset)
    void patch_rom8(uint32_t a, uint8_t v);
    void restore_rom();
    uint8_t rom_orig8(uint32_t a) const { return rom_orig[a % rom_orig.size()]; }

    // Save states
    std::vector<uint8_t> save_state();
    bool load_state(const std::vector<uint8_t>& data);

    // ---- bus -------------------------------------------------------------------
    uint8_t read(uint16_t a);
    void    write(uint16_t a, uint8_t v);
    uint8_t port_in(uint8_t p);
    void    port_out(uint8_t p, uint8_t v);
    uint8_t peek(uint16_t a) const;              // side-effect-free read (tools, modules)

    // (during the Z80's run: port handlers)
    int  line_cycle() const { return z80.executed() + (int)(cycles_ - line_start_); }
    int  current_line() const { return vdp.line; }

private:
    std::vector<uint8_t> rom_orig;
    int64_t cycles_ = 0, line_start_ = 0, audio_clock_ = 0;
    bool pause_prev_ = false;
    std::unordered_map<uint16_t, std::function<void(Sms&)>> pc_hooks_;
    uint8_t hook_map_[8192];
    void (*trace_fn_)(void* ctx, uint16_t pc) = nullptr;
    void* trace_ctx_ = nullptr;
    std::vector<int32_t> psg_tmp_;
    void sync_audio(int64_t now);
    void run_until(int64_t cycle);
    static void hook_dispatch(void* ctx, uint16_t pc);
};
