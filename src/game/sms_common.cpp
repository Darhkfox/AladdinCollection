#include "sms_common.h"
#include "../core/sms.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace sms_game {

int apply_patches(Sms& s, const std::vector<RomPatch>& patches) {
    s.restore_rom();
    int applied = 0;
    for (const RomPatch& p : patches) {
        bool ok = true;
        for (size_t i = 0; i < p.orig.size() && ok; i++) ok = s.rom8(p.addr + (uint32_t)i) == p.orig[i];
        if (!ok) continue;
        for (size_t i = 0; i < p.repl.size(); i++) s.patch_rom8(p.addr + (uint32_t)i, p.repl[i]);
        applied++;
    }
    return applied;
}

bool patches_safe(const Sms& s, const std::vector<RomPatch>& patches) {
    // the ROM the patches give (applied in order on the pristine ROM, as apply_patches does)
    std::unordered_map<uint32_t, uint8_t> next;
    auto byte = [&](uint32_t a) { auto i = next.find(a); return i != next.end() ? i->second : s.rom_orig8(a); };
    for (const RomPatch& p : patches) {
        bool ok = true;
        for (size_t i = 0; i < p.orig.size() && ok; i++) ok = byte(p.addr + (uint32_t)i) == p.orig[i];
        if (!ok) continue;
        for (size_t i = 0; i < p.repl.size(); i++) next[p.addr + (uint32_t)i] = p.repl[i];
    }
    std::vector<uint32_t> changed;
    for (uint32_t a = 0; a < s.rom.size(); a++)
        if (byte(a) != s.rom[a]) changed.push_back(a);
    if (changed.empty()) return true;
    auto hit = [&](long lo, long hi) {      // a changed byte in [lo, hi)
        if (lo < 0) return false;
        auto i = std::lower_bound(changed.begin(), changed.end(), (uint32_t)lo);
        return i != changed.end() && *i < (uint32_t)hi;
    };
    const long pc = s.rom_offset(s.z80.pc);
    if (hit(pc, pc + 4)) return false;
    // return addresses (call, interrupts) near the top of the stack
    for (int o = 0; o < 64; o += 1) {
        const uint16_t a = (uint16_t)(s.z80.sp + o);
        if (a < 0xC000) break;
        const uint16_t v = (uint16_t)(s.peek(a) | (s.peek((uint16_t)(a + 1)) << 8));
        const long r = v < 0xC000 ? s.rom_offset(v) : -1;
        if (r >= 0 && hit(r - 3, r + 4)) return false;
    }
    return true;
}

bool run_until(Sms& s, const std::function<bool()>& cond, int max_frames) {
    for (int i = 0; i < max_frames; i++) {
        if (cond()) return true;
        s.run_frame();
    }
    return cond();
}

void run_frames(Sms& s, int n, uint16_t pad) {
    s.set_pad(0, pad);
    for (int i = 0; i < n; i++) s.run_frame();
    s.set_pad(0, 0);
}

bool press_until(Sms& s, uint16_t button, const std::function<bool()>& cond, int hold_frames, int gap_frames, int tries) {
    for (int t = 0; t < tries; t++) {
        s.set_pad(0, button);
        for (int f = 0; f < hold_frames; f++) {
            s.run_frame();
            if (cond()) { s.set_pad(0, 0); return true; }
        }
        s.set_pad(0, 0);
        for (int f = 0; f < gap_frames; f++) {
            s.run_frame();
            if (cond()) return true;
        }
    }
    return false;
}

bool call_z80(Sms& s, uint16_t entry, int max_cycles) {
    // The return address pushed is $FFF0 (RAM, never code): reaching it ends the call before it executes.
    constexpr uint16_t kReturn = 0xFFF0;
    Z80& z = s.z80;
    std::vector<uint8_t> regs(z.state_size());
    z.save_state(regs.data());
    bool done = false;
    s.add_pc_hook(kReturn, [&](Sms& m) { done = true; m.z80.end_run(); });
    z.sp = (uint16_t)(z.sp - 2);
    s.write(z.sp, (uint8_t)kReturn);
    s.write((uint16_t)(z.sp + 1), (uint8_t)(kReturn >> 8));
    z.pc = entry;
    z.iff1 = z.iff2 = false;
    z.halted = false;
    int used = 0;
    while (!done && used < max_cycles) used += z.run(4000);
    s.remove_pc_hook(kReturn);
    z.load_state(regs.data());
    return done;
}

}  // namespace sms_game
