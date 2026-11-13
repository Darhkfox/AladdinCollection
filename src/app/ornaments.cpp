// The Arabian Nights style's ornaments (see ornaments.h): each is a few layers of simple shapes, rasterised with
// 4x4 supersampling into a picture the first time it's drawn.
#include "ornaments.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {
using Shape = std::function<bool(float, float)>;    // inside the shape at a point
using Paint = std::function<uint32_t(float, float)>;  // the colour at a point (0xAARRGGBB)

uint32_t mix(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        const float x = ((a >> s) & 255) + (((float)((b >> s) & 255)) - ((a >> s) & 255)) * t;
        out |= (uint32_t)std::clamp(x + 0.5f, 0.0f, 255.0f) << s;
    }
    return out;
}

// Points are in units: y runs from -1 (top) to 1 (bottom), x from -w/h to w/h (0, 0 is the middle).
struct Canvas {
    int w, h;
    std::vector<float> px;   // premultiplied r, g, b, a
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0.0f) {}
    float ux(float x) const { return (x / w * 2.0f - 1.0f) * w / (float)h; }
    float uy(float y) const { return y / h * 2.0f - 1.0f; }
    void over(int x, int y, uint32_t col, float cover) {
        const float a = ((col >> 24) & 255) / 255.0f * cover;
        if (a <= 0) return;
        float* p = &px[((size_t)y * w + x) * 4];
        p[0] = ((col >> 16) & 255) / 255.0f * a + p[0] * (1 - a);
        p[1] = ((col >> 8) & 255) / 255.0f * a + p[1] * (1 - a);
        p[2] = (col & 255) / 255.0f * a + p[2] * (1 - a);
        p[3] = a + p[3] * (1 - a);
    }
    void layer(const Shape& in, const Paint& paint) {
        const int n = 4;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int hits = 0;
                for (int j = 0; j < n; j++)
                    for (int i = 0; i < n; i++)
                        if (in(ux(x + (i + 0.5f) / n), uy(y + (j + 0.5f) / n))) hits++;
                if (hits) over(x, y, paint(ux(x + 0.5f), uy(y + 0.5f)), hits / (float)(n * n));
            }
    }
    void layer(const Shape& in, uint32_t col) { layer(in, [col](float, float) { return col; }); }
    // a soft layer: its cover at each point from `f` (0..1)
    void glow(const std::function<float(float, float)>& f, uint32_t col) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) over(x, y, col, std::clamp(f(ux(x + 0.5f), uy(y + 0.5f)), 0.0f, 1.0f));
    }
    std::vector<uint32_t> argb() const {
        std::vector<uint32_t> out((size_t)w * h);
        for (size_t i = 0; i < out.size(); i++) {
            const float* p = &px[i * 4];
            const float a = std::min(p[3], 1.0f);
            auto ch = [a](float v) { return (uint32_t)std::clamp(a > 0 ? v / a * 255.0f + 0.5f : 0.0f, 0.0f, 255.0f); };
            out[i] = (uint32_t)(a * 255.0f + 0.5f) << 24 | ch(p[0]) << 16 | ch(p[1]) << 8 | ch(p[2]);
        }
        return out;
    }
};

// The shape grown / shrunk by `d` (tested at 16 points around each point).
Shape grow(Shape s, float d) {
    return [s, d](float x, float y) {
        if (s(x, y)) return true;
        for (int k = 0; k < 16; k++)
            if (s(x + std::cos(k * 0.3926991f) * d, y + std::sin(k * 0.3926991f) * d)) return true;
        return false;
    };
}
Shape shrink(Shape s, float d) {
    return [s, d](float x, float y) {
        if (!s(x, y)) return false;
        for (int k = 0; k < 16; k++)
            if (!s(x + std::cos(k * 0.3926991f) * d, y + std::sin(k * 0.3926991f) * d)) return false;
        return true;
    };
}

const uint32_t kInkC = 0xFF1A0B2E, kGoldLight = 0xFFFFEBA6, kGoldMid = 0xFFF2C14E, kGoldDark = 0xFFA8701E;
// Burnished gold: light towards the top left, deep towards the bottom right.
uint32_t gold(float x, float y) {
    const float k = 0.5f - 0.42f * y - 0.18f * x;
    return k > 0.5f ? mix(kGoldMid, kGoldLight, (k - 0.5f) * 2.0f) : mix(kGoldDark, kGoldMid, k * 2.0f);
}

Shape star8(float a) {   // a square of half-side a, and the same square turned 45 degrees
    return [a](float x, float y) {
        x = std::fabs(x), y = std::fabs(y);
        return std::max(x, y) <= a || x + y <= a * 1.41421356f;
    };
}
Shape circle(float r, float cx = 0, float cy = 0) {
    return [=](float x, float y) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r; };
}

std::vector<uint32_t> make_star8(int& w, int& h) {
    w = h = 128;
    Canvas c(w, h);
    const float a = 0.635f;
    c.layer(star8(a + 0.075f), kInkC);
    c.layer(star8(a), gold);
    // an engraved line inside the edge
    c.layer(star8(a - 0.10f), [](float x, float y) { return mix(gold(x, y), kGoldDark, 0.75f); });
    c.layer(star8(a - 0.15f), gold);
    // the medallion: indigo in a dark ring, a small gold star in it
    c.layer(circle(0.37f), kInkC);
    c.layer(circle(0.315f), [](float, float y) { return mix(0xFF3A2378, 0xFF1C1040, (y + 0.3f) / 0.6f); });
    c.layer(star8(0.12f), gold);
    return c.argb();
}

std::vector<uint32_t> make_diamond(int& w, int& h) {
    w = 80, h = 128;
    Canvas c(w, h);
    auto rh = [](float hw, float hh) { return [=](float x, float y) { return std::fabs(x) / hw + std::fabs(y) / hh <= 1.0f; }; };
    const Shape body = rh(0.5f, 0.9f);
    c.layer(grow(body, 0.075f), kInkC);
    c.layer(body, gold);
    c.layer(shrink(body, 0.13f), [](float x, float y) { return mix(gold(x, y), kGoldDark, 0.75f); });
    c.layer(shrink(body, 0.18f), gold);
    c.layer(rh(0.11f, 0.2f), kInkC);
    return c.argb();
}

std::vector<uint32_t> make_sparkle(int& w, int& h) {
    w = h = 128;
    Canvas c(w, h);
    c.glow([](float x, float y) { return 0.5f * std::exp(-(x * x + y * y) * 10.0f); }, 0xFFFFE2A0);
    // four long points (an astroid), and four short ones between them
    auto rays = [](float len) {
        return [len](float x, float y) { return std::sqrt(std::fabs(x)) + std::sqrt(std::fabs(y)) <= std::sqrt(len); };
    };
    const Shape long_rays = rays(0.97f);
    const Shape short_rays = [](float x, float y) {
        const float u = (x + y) * 0.70710678f, v = (x - y) * 0.70710678f;
        return std::sqrt(std::fabs(u)) + std::sqrt(std::fabs(v)) <= std::sqrt(0.45f);
    };
    const Paint shine = [](float x, float y) { return mix(0xFFFFFFFF, 0xFFFFD064, std::sqrt(x * x + y * y) * 1.6f); };
    c.layer(short_rays, shine);
    c.layer(long_rays, shine);
    c.layer(circle(0.09f), 0xFFFFFFFF);
    return c.argb();
}

struct Pic { int id = 0; float aspect = 1.0f; };
Pic g_pics[(int)Ornament::Count];

const Pic& pic(Video& v, Ornament o) {
    Pic& p = g_pics[(int)o];
    if (!p.id) {
        int w = 0, h = 0;
        std::vector<uint32_t> px = o == Ornament::Star8 ? make_star8(w, h) : o == Ornament::Diamond ? make_diamond(w, h)
                                                                                                    : make_sparkle(w, h);
        p.id = v.create_image(px.data(), w, h);
        p.aspect = (float)w / h;
    }
    return p;
}
}  // namespace

float ornament_aspect(Ornament o) {
    return o == Ornament::Diamond ? 80.0f / 128.0f : 1.0f;
}

void draw_ornament(Video& v, Ornament o, float cx, float cy, float size, float alpha) {
    if ((int)o < 0 || o >= Ornament::Count || size <= 0 || alpha <= 0) return;
    const Pic& p = pic(v, o);
    if (!p.id) return;
    const float w = size * p.aspect;
    v.draw_image(p.id, {cx - w * 0.5f, cy - size * 0.5f, w, size}, {0, 0, 0, 0}, alpha);
}
