// Helpers shared by the Master System game modules: verified ROM patching, scripted input and code calls.
#pragma once
#include <cstdint>
#include <vector>
#include <initializer_list>
#include <functional>

class Sms;

namespace sms_game {

struct RomPatch {
    uint32_t addr;                 // ROM offset
    std::vector<uint8_t> orig;     // expected original bytes (patch skipped if they don't match)
    std::vector<uint8_t> repl;     // replacement bytes
};

// Restores the pristine ROM, then applies the patches. Returns the number applied; patches whose original bytes
// don't match are skipped.
int apply_patches(Sms& s, const std::vector<RomPatch>& patches);

// Whether the ROM can go from what it holds now to what `patches` give without pulling code from under the Z80:
// no byte that would change may be at the PC or behind a return address near the top of the stack (as the
// current banking maps them). A width or cheat change made while the game runs patched code waits until this holds.
bool patches_safe(const Sms& s, const std::vector<RomPatch>& patches);

// Scripted-input helpers for level select / jukebox boots (run headless, as fast as possible).
bool run_until(Sms& s, const std::function<bool()>& cond, int max_frames);
void run_frames(Sms& s, int n, uint16_t pad = 0);
// Taps `button` (SmsButton bits) until cond() holds: holds up to hold_frames, releases, waits gap_frames, retries.
// Returns with the pad released.
bool press_until(Sms& s, uint16_t button, const std::function<bool()>& cond, int hold_frames = 3, int gap_frames = 4,
                 int tries = 30);
// Runs the Z80 subroutine at `entry` (current banking) until it returns, with interrupts off; the Z80's registers
// are restored afterwards. False if it hasn't returned after `max_cycles`.
bool call_z80(Sms& s, uint16_t entry, int max_cycles = 2000000);

}  // namespace sms_game
