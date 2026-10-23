// Helpers shared by the game modules: verified ROM patching, scripted input and code calls.
#pragma once
#include <cstdint>
#include <vector>
#include <initializer_list>
#include <functional>

class Snes;

namespace snes_game {

struct RomPatch {
    uint32_t addr;                 // CPU address (LoROM, e.g. $80:8123)
    std::vector<uint8_t> orig;     // expected original bytes (patch skipped if they don't match)
    std::vector<uint8_t> repl;     // replacement bytes
};

// LoROM: the ROM offset of a CPU address in banks $00-$7D / $80-$FF ($8000-$FFFF), or -1.
long lorom_offset(uint32_t addr);

// Restores the pristine ROM, then applies the patches (Snes::romPristine is the reference). Returns the number
// applied; patches whose original bytes don't match are skipped (and reported in debug builds).
int apply_patches(Snes& s, const std::vector<RomPatch>& patches);

// Whether the ROM can go from what it holds now to what `patches` give without pulling code from under the CPU:
// no byte that would change may be at the PC or behind a possible return address on the stack. A width or cheat
// change made while the game runs patched code waits (Session) until this holds.
bool patches_safe(const Snes& s, const std::vector<RomPatch>& patches);

// Scripted-input helpers for level select / jukebox boots (run headless, as fast as possible).
bool run_until(Snes& s, const std::function<bool()>& cond, int max_frames);
// Taps `button` (pad bits, snes.h kPad*) until cond() holds: holds up to hold_frames, releases, waits
// gap_frames, retries. Returns with the pad released.
bool press_until(Snes& s, uint16_t button, const std::function<bool()>& cond, int hold_frames = 3, int gap_frames = 4,
                 int tries = 30);
void run_frames(Snes& s, int n, uint16_t pad = 0);

// Patch code space: banks $60-$6F ($8000-$FFFF), Snes::patchRom. Modules write their routines there at configure.
struct PatchAsm {
    Snes& s;
    uint32_t pc;                   // next address to write
    PatchAsm(Snes& snes, uint32_t at) : s(snes), pc(at) {}
    void b(std::initializer_list<uint8_t> bytes);
    void b8(uint8_t v);
    void w16(uint16_t v);
    void l24(uint32_t v);
};
// A JSL / JML instruction to `target` as bytes.
std::vector<uint8_t> jsl(uint32_t target);
std::vector<uint8_t> jml(uint32_t target);

}  // namespace snes_game
