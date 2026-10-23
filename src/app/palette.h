// Colour look-up tables: each console's colours -> 0xAARRGGBB through the user's colour profile.
#pragma once
#include <cstdint>
#include "settings.h"

struct ColorProfile {
    ColorMode mode = ColorMode::Accurate;
    int brightness = 0, saturation = 0, gamma = 0;
    bool operator==(const ColorProfile& o) const {
        return mode == o.mode && brightness == o.brightness && saturation == o.saturation && gamma == o.gamma;
    }
    bool operator!=(const ColorProfile& o) const { return !(*this == o); }
};

// Mega Drive: lut[(intensity << 9) | BGR333], intensity 0 normal, 1 shadow, 2 highlight.
void build_md_palette(uint32_t lut[2048], const ColorProfile& p);
// SNES: lut[BGR555] for the console's master brightness `master` (0-15; the PPU's output levels are scaled by
// master / 15 before the profile's display response).
void build_snes_palette(uint32_t lut[32768], const ColorProfile& p, int master);
// Master System: lut[--BBGGRR].
void build_sms_palette(uint32_t lut[64], const ColorProfile& p);
