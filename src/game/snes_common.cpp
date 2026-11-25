#include "snes_common.h"
#include "game.h"
#include "../core/snes.h"
#include <algorithm>
#include <cstdio>

namespace snes_game {

long lorom_offset(uint32_t addr) {
    const uint32_t bank = (addr >> 16) & 0x7f, a = addr & 0xffff;
    if (a < 0x8000 || bank >= 0x7e) return -1;
    return (long)((bank << 15) | (a & 0x7fff));
}

int apply_patches(Snes& s, const std::vector<RomPatch>& patches) {
    if (s.romPristine.size() == s.rom.size()) s.rom = s.romPristine;
    int applied = 0;
    for (const RomPatch& p : patches) {
        const long off = lorom_offset(p.addr);
        if (off < 0 || off + (long)p.orig.size() > (long)s.rom.size() || p.orig.size() != p.repl.size()) continue;
        bool match = true;
        for (size_t i = 0; i < p.orig.size(); i++) match = match && s.rom[off + i] == p.orig[i];
        if (!match) {
#ifndef NDEBUG
            fprintf(stderr, "patch at %06X: original bytes don't match\n", p.addr);
#endif
            continue;
        }
        for (size_t i = 0; i < p.repl.size(); i++) s.rom[off + i] = p.repl[i];
        applied++;
    }
    return applied;
}

bool patches_safe(const Snes& s, const std::vector<RomPatch>& patches) {
    // The ROM offsets whose contents would change.
    std::vector<long> changed;
    for (const RomPatch& p : patches) {
        const long off = lorom_offset(p.addr);
        if (off < 0 || off + (long)p.repl.size() > (long)s.rom.size()) continue;
        for (size_t i = 0; i < p.repl.size(); i++)
            if (s.rom[off + i] != p.repl[i]) changed.push_back(off + (long)i);
    }
    // ... and the bytes the pristine ROM has there now differ from (a configuration being removed)
    if (s.romPristine.size() == s.rom.size())
        for (size_t i = 0; i < s.rom.size(); i++)
            if (s.rom[i] != s.romPristine[i]) {
                bool kept = false;
                for (const RomPatch& p : patches) {
                    const long off = lorom_offset(p.addr);
                    if (off >= 0 && (long)i >= off && (long)i < off + (long)p.repl.size() && p.repl[i - off] == s.rom[i]) kept = true;
                }
                if (!kept) changed.push_back((long)i);
            }
    std::sort(changed.begin(), changed.end());
    return rom_change_safe(s, changed);
}

bool rom_change_safe(const Snes& s, const std::vector<long>& changed) {
    if (changed.empty()) return true;
    // Code at an offset is reached through any bank that maps it: compare 15-bit offsets within a 32 KB bank
    // plus the bank, for the PC; for return addresses on the stack only the 16-bit part is known (JSR), so any
    // bank is assumed (a false alarm only delays the switch by a frame).
    auto hits_off = [&](long off) { return std::binary_search(changed.begin(), changed.end(), off); };
    auto hits16 = [&](uint32_t a16) {
        if (a16 < 0x8000) return false;
        for (long off : changed)
            if ((uint32_t)(off & 0x7fff) == (a16 & 0x7fff)) return true;
        return false;
    };
    const CpuRegs& r = s.cpu.r;
    for (int k = 0; k < 4; k++) {   // the next few instruction bytes
        const long off = lorom_offset((uint32_t(r.pb) << 16) | ((r.pc + k) & 0xffff));
        if (off >= 0 && hits_off(off)) return false;
    }
    // Stack: from the top of stack upwards, 48 bytes (the games' call depth is shallow).
    for (int i = 1; i <= 48; i++) {
        const uint16_t sp = (uint16_t)(r.sp + i);
        if (sp >= 0x2000) break;
        const uint16_t w = (uint16_t)(s.wram[sp] | (s.wram[(sp + 1) & 0x1fff] << 8));
        for (int d = 0; d <= 3; d++)
            if (hits16((uint16_t)(w + d))) return false;
    }
    return true;
}

void run_frames(Snes& s, int n, uint16_t pad) {
    for (int i = 0; i < n; i++) {
        s.input[0] = pad;
        s.input[1] = 0;
        s.runFrame();
        s.apu.samples.clear();
    }
}

bool run_until(Snes& s, const std::function<bool()>& cond, int max_frames) {
    for (int i = 0; i < max_frames; i++) {
        if (cond()) return true;
        run_frames(s, 1, s.input[0]);
    }
    return cond();
}

bool press_until(Snes& s, uint16_t button, const std::function<bool()>& cond, int hold_frames, int gap_frames, int tries) {
    for (int t = 0; t < tries; t++) {
        if (cond()) { s.input[0] = 0; return true; }
        for (int i = 0; i < hold_frames && !cond(); i++) run_frames(s, 1, button);
        for (int i = 0; i < gap_frames && !cond(); i++) run_frames(s, 1, 0);
    }
    s.input[0] = 0;
    return cond();
}

void PatchAsm::b8(uint8_t v) {
    s.poke(pc, v);
    pc++;
}
void PatchAsm::b(std::initializer_list<uint8_t> bytes) { for (uint8_t v : bytes) b8(v); }
void PatchAsm::w16(uint16_t v) { b8((uint8_t)v); b8((uint8_t)(v >> 8)); }
void PatchAsm::l24(uint32_t v) { b8((uint8_t)v); b8((uint8_t)(v >> 8)); b8((uint8_t)(v >> 16)); }

std::vector<uint8_t> jsl(uint32_t t) { return {0x22, (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)(t >> 16)}; }
std::vector<uint8_t> jml(uint32_t t) { return {0x5C, (uint8_t)t, (uint8_t)(t >> 8), (uint8_t)(t >> 16)}; }

}  // namespace snes_game
