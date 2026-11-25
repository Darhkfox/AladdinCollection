// What a DirectInput controller's reads go through before they count (input.cpp; on its own here so that
// tests/app_test.cpp can check it without a device):
// - settle: a device just opened, or back after a failed read, can report buttons down (some send all of them in
//   their first report, some drop out and come back every second or so): what it holds then is not a press; a button
//   counts once it has been let go and pressed again. (Otherwise B / Back / Guide from a device nobody touches kept
//   closing the menus.)
// - noise: a device sending random presses - more button changes in a second, across more buttons, than any player
//   makes (60+ changes over 6+ buttons; seen from a Switch Pro Controller left in its full report mode by Steam or
//   another program, which DirectInput reads as garbage) - is left out until it has been quiet for 3 s. (Otherwise
//   the stray B / Back / Guide presses closed every menu a moment after it opened.)
#pragma once
#include <cstdint>

struct PadFilter {
    bool settle = true;              // the next read that counts is a baseline, not presses
    uint64_t last_raw = 0;           // noise: button changes counted per second (trans; toggled = which buttons)
    bool raw_valid = false;
    int trans = 0;
    uint64_t toggled = 0;
    uint64_t window_start = 0;       // (ms)
    int quiet_windows = 0;
    bool noisy = false;

    // A failed read (unplugged, unavailable): the next good one is a baseline.
    void read_failed() { settle = true; }

    // A good read of the raw buttons at `now_ms`. Returns false if it is to be left out (the device is sending random
    // presses, or `enabled` is off): report it idle.
    bool read(uint64_t raw, uint64_t now_ms, bool enabled) {
        if (raw_valid) {
            const uint64_t ch = raw ^ last_raw;
            for (uint64_t c = ch; c; c &= c - 1) trans++;
            toggled |= ch;
        }
        last_raw = raw;
        raw_valid = true;
        if (!window_start) window_start = now_ms;
        if (now_ms - window_start >= 1000) {
            int distinct = 0;
            for (uint64_t c = toggled; c; c &= c - 1) distinct++;
            if (trans >= 60 && distinct >= 6) { noisy = true; quiet_windows = 0; }
            else if (noisy && ++quiet_windows >= 3 && trans == 0) { noisy = false; settle = true; }
            else if (noisy && trans) quiet_windows = 0;
            trans = 0; toggled = 0; window_start = now_ms;
        }
        if (noisy || !enabled) {
            settle = true;
            return false;
        }
        return true;
    }

    // After a read that counts: true if it is the baseline (what is held now is not pressed). Clears it.
    bool take_settle() {
        const bool s = settle;
        settle = false;
        return s;
    }
};
