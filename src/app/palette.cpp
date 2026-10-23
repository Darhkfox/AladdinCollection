#include "palette.h"
#include <cmath>
#include <algorithm>

namespace {
// Mega Drive levels measured from real video output.
const float kMdNormal[8] = {0, 52, 87, 116, 144, 172, 206, 255};
const float kMdShadow[8] = {0, 29, 52, 70, 87, 101, 116, 130};
const float kMdHigh[8]   = {130, 144, 158, 172, 187, 206, 228, 255};
// The SNES on a CRT television: 5-bit output levels through the set's gamma (the curve bsnes uses for its
// colour emulation). Darker colours come out darker than spread evenly.
const float kSnesCrt[32] = {0x00, 0x01, 0x03, 0x06, 0x0a, 0x0f, 0x15, 0x1c, 0x24, 0x2d, 0x37, 0x42, 0x4e, 0x5b, 0x69, 0x78,
                            0x88, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8, 0xc0, 0xc8, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8, 0xff};
// The Master System's 2-bit DAC: evenly spaced.
const float kSms[4] = {0, 85, 170, 255};

inline float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// Applies the profile to a display colour (channels 0..1).
struct Shaper {
    float bright, sat, gam, contrast = 1.0f;
    ColorMode mode;
    explicit Shaper(const ColorProfile& p) : mode(p.mode) {
        bright = p.brightness / 100.0f;                 // -0.5 .. 0.5
        sat = 1.0f + p.saturation / 100.0f;             // 0 .. 2
        gam = std::pow(2.0f, -p.gamma / 50.0f);         // 0.5 .. 2
        switch (mode) {
        case ColorMode::Vivid: sat *= 1.32f; contrast = 1.07f; break;
        case ColorMode::Grayscale: case ColorMode::Sepia: case ColorMode::Handheld: sat = 0; break;
        default: break;
        }
    }
    uint32_t operator()(float r, float g, float b) const {
        // saturation around Rec.601 luma
        const float y = 0.299f * r + 0.587f * g + 0.114f * b;
        r = y + (r - y) * sat; g = y + (g - y) * sat; b = y + (b - y) * sat;
        // contrast around mid grey, brightness, gamma
        r = (r - 0.5f) * contrast + 0.5f + bright;
        g = (g - 0.5f) * contrast + 0.5f + bright;
        b = (b - 0.5f) * contrast + 0.5f + bright;
        r = std::pow(clamp01(r), gam); g = std::pow(clamp01(g), gam); b = std::pow(clamp01(b), gam);
        switch (mode) {
        case ColorMode::Warm:
            r = clamp01(r * 1.04f + 0.02f); g = clamp01(g * 0.99f + 0.015f); b = clamp01(b * 0.88f + 0.02f);
            break;
        case ColorMode::Cool:
            r = clamp01(r * 0.94f); g = clamp01(g * 0.99f); b = clamp01(b * 1.06f + 0.01f);
            break;
        case ColorMode::Sepia: {
            const float l = r;   // already grey
            r = clamp01(l * 1.07f + 0.03f); g = clamp01(l * 0.86f + 0.02f); b = clamp01(l * 0.62f + 0.01f);
            break;
        }
        case ColorMode::Handheld: {
            static const float shades[4][3] = {{0.06f, 0.22f, 0.06f}, {0.19f, 0.38f, 0.19f},
                                               {0.55f, 0.67f, 0.06f}, {0.61f, 0.74f, 0.06f}};
            const int s = std::min(3, (int)(r * 4.0f));
            r = shades[s][0]; g = shades[s][1]; b = shades[s][2];
            break;
        }
        default: break;
        }
        const uint32_t R = (uint32_t)(clamp01(r) * 255.0f + 0.5f);
        const uint32_t G = (uint32_t)(clamp01(g) * 255.0f + 0.5f);
        const uint32_t B = (uint32_t)(clamp01(b) * 255.0f + 0.5f);
        return 0xFF000000u | (R << 16) | (G << 8) | B;
    }
};
}  // namespace

void build_md_palette(uint32_t lut[2048], const ColorProfile& p) {
    const Shaper shape(p);
    auto level = [&](int inten, int v) {
        if (p.mode == ColorMode::Linear) {
            const float n = v * 255.0f / 7.0f;
            return (inten == 0 ? n : inten == 1 ? n * 0.5f : 127.5f + n * 0.5f) / 255.0f;
        }
        return (inten == 0 ? kMdNormal[v] : inten == 1 ? kMdShadow[v] : kMdHigh[v]) / 255.0f;
    };
    for (int in = 0; in < 3; in++)
        for (int c = 0; c < 512; c++)
            lut[(in << 9) | c] = shape(level(in, c & 7), level(in, (c >> 3) & 7), level(in, (c >> 6) & 7));
    // Code 0x600-0x7FF (intensity 3) never occurs; keep it defined.
    for (int i = 1536; i < 2048; i++) lut[i] = lut[i - 1536];
}

void build_snes_palette(uint32_t lut[32768], const ColorProfile& p, int master) {
    const Shaper shape(p);
    const float m = std::clamp(master, 0, 15) / 15.0f;
    float lv[32];
    for (int c = 0; c < 32; c++) {
        const float v = c * m;
        if (p.mode == ColorMode::Linear) {
            lv[c] = v / 31.0f;
        } else {
            const int i = std::min(30, (int)v);
            const float f = v - i;
            lv[c] = (kSnesCrt[i] + (kSnesCrt[i + 1] - kSnesCrt[i]) * f) / 255.0f;
        }
    }
    // A black screen stays black whatever the adjustments (fades and blanking).
    for (int c = 0; c < 32768; c++)
        lut[c] = master == 0 ? 0xFF000000u : shape(lv[c & 31], lv[(c >> 5) & 31], lv[(c >> 10) & 31]);
}

void build_sms_palette(uint32_t lut[64], const ColorProfile& p) {
    const Shaper shape(p);
    for (int c = 0; c < 64; c++)
        lut[c] = shape(kSms[c & 3] / 255.0f, kSms[(c >> 2) & 3] / 255.0f, kSms[(c >> 4) & 3] / 255.0f);
}
