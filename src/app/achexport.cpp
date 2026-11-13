// Achievements exported for other tools: see achexport.h. (The badge drawing follows the Mickey & Donald Collection's
// and the Super Mario All-Stars port's, with Aladdin's own symbols.)
#include "achexport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "achievements.h"
#include "png.h"
#include "sys.h"

namespace {

// ---- what each badge shows ------------------------------------------------------------------------------------

enum Glyph {
    kStar, kHeart, kTrophy, kTrophyStar, kClock, kFire, kArrowR, kMoon, kChest, kDoor, kShield, kCrown, kApple, kKey,
    kCloud, kLamp, kSword, kCarpet, kScarab, kGem, kStones, kDome, kSnake, kMonkey, kWheel, kBars, kCave, kText
};

struct Look {
    const char* id;
    Glyph glyph;
    const char* text;   // (kText)
    int tier;           // 0 bronze, 1 silver, 2 gold, 3 platinum
};

const Look kLooks[] = {
    // Mega Drive
    {"md_market", kDome, nullptr, 0},         {"md_desert", kScarab, nullptr, 0},
    {"md_rooftops", kMoon, nullptr, 0},       {"md_dungeon", kBars, nullptr, 1},
    {"md_cave", kLamp, nullptr, 1},           {"md_escape", kCarpet, nullptr, 1},
    {"md_lamp", kCloud, nullptr, 1},          {"md_palace", kCrown, nullptr, 1},
    {"md_quarters", kDoor, nullptr, 1},       {"md_jafar", kSnake, nullptr, 2},
    {"md_abu", kMonkey, nullptr, 0},          {"md_genie", kWheel, nullptr, 0},
    {"md_peddler", kChest, nullptr, 0},       {"md_apples", kApple, nullptr, 1},
    {"md_flawless", kShield, nullptr, 1},     {"md_sword", kSword, nullptr, 1},
    {"md_iron", kGem, nullptr, 2},            {"md_difficult", kText, "HARD", 2},

    // SNES
    {"snes_agrabah", kDome, nullptr, 0},      {"snes_cave", kGem, nullptr, 0},
    {"snes_escape", kCarpet, nullptr, 1},     {"snes_lamp", kLamp, nullptr, 1},
    {"snes_dungeon", kBars, nullptr, 1},      {"snes_carpet", kMoon, nullptr, 1},
    {"snes_palace", kCrown, nullptr, 1},      {"snes_jafar", kSnake, nullptr, 2},
    {"snes_no_continue", kArrowR, nullptr, 2}, {"snes_stage_clean", kStar, nullptr, 1},
    {"snes_untouched", kShield, nullptr, 1},  {"snes_wheel", kWheel, nullptr, 0},
    {"snes_hearts", kHeart, nullptr, 1},      {"snes_gems", kText, "100", 1},
    {"snes_lives", kText, "X9", 1},           {"snes_jafar_flawless", kFire, nullptr, 2},
    {"snes_debug", kKey, nullptr, 1},

    // Master System
    {"sms_market", kDome, nullptr, 0},        {"sms_rooftops", kArrowR, nullptr, 0},
    {"sms_cave", kCave, nullptr, 0},          {"sms_treasure", kChest, nullptr, 0},
    {"sms_lamp", kLamp, nullptr, 1},          {"sms_escape", kFire, nullptr, 1},
    {"sms_prince", kCrown, nullptr, 1},       {"sms_carpet", kCarpet, nullptr, 1},
    {"sms_palace", kDoor, nullptr, 1},        {"sms_jafar", kSnake, nullptr, 2},
    {"sms_newgame", kGem, nullptr, 1},        {"sms_nocontinue", kHeart, nullptr, 2},
    {"sms_noscratch", kShield, nullptr, 1},   {"sms_flawless", kStar, nullptr, 2},
    {"sms_jafar_flawless", kSword, nullptr, 2}, {"sms_stones", kStones, nullptr, 1},
    {"sms_skip", kClock, nullptr, 0},

    // the collection
    {"all_grandtour", kTrophyStar, nullptr, 2}, {"all_complete", kTrophy, nullptr, 3},
};

const Look& look_of(const char* id) {
    static const Look kDefault = {"", kStar, nullptr, 0};
    for (const Look& l : kLooks)
        if (std::strcmp(l.id, id) == 0) return l;
    return kDefault;
}

// ---- drawing: shapes in a unit square (y down), sampled 4x4 per pixel ---------------------------------------------

// A 5x7 font for the badges' words (rows top to bottom, bit 4 the left column).
struct FontChar { char c; uint8_t rows[7]; };
const FontChar kFont[] = {
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},     {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},     {'4', {2, 6, 10, 18, 31, 2, 2}},    {'5', {31, 16, 30, 1, 1, 17, 14}},
    {'6', {6, 8, 16, 30, 17, 17, 14}},   {'7', {31, 1, 2, 4, 8, 8, 8}},      {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 2, 12}},   {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}}, {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 17, 25, 21, 19, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'R', {30, 17, 17, 30, 20, 18, 17}}, {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
};

// Inside the text `s`, centred at (cx, cy), `h` tall (narrowed to `max_w` wide)?
bool in_text(const char* s, float cx, float cy, float h, float max_w, float x, float y) {
    const int n = (int)std::strlen(s);
    float cell = h / 7;
    float w = (6 * n - 1) * cell;
    if (w > max_w) { cell *= max_w / w; w = max_w; }
    const float x0 = cx - w / 2, y0 = cy - 7 * cell / 2;
    const int col = (int)std::floor((x - x0) / cell), row = (int)std::floor((y - y0) / cell);
    if (col < 0 || row < 0 || row >= 7 || col >= 6 * n - 1) return false;
    const int ch = col / 6, bit = col % 6;
    if (bit == 5) return false;
    for (const FontChar& f : kFont)
        if (f.c == s[ch]) return (f.rows[row] >> (4 - bit)) & 1;
    return false;
}

struct P { float x, y; };

bool circ(P p, float cx, float cy, float r) { return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r; }
bool ell(P p, float cx, float cy, float rx, float ry) {
    const float a = (p.x - cx) / rx, b = (p.y - cy) / ry;
    return a * a + b * b <= 1;
}
bool rect(P p, float x0, float y0, float x1, float y1) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
bool rrect(P p, float x0, float y0, float x1, float y1, float r) {
    if (!rect(p, x0, y0, x1, y1)) return false;
    const float cx = std::clamp(p.x, x0 + r, x1 - r), cy = std::clamp(p.y, y0 + r, y1 - r);
    return circ(p, cx, cy, r);
}
bool ring(P p, float cx, float cy, float r0, float r1) { return circ(p, cx, cy, r1) && !circ(p, cx, cy, r0); }
bool seg(P p, float x0, float y0, float x1, float y1, float w) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float t = std::clamp(((p.x - x0) * dx + (p.y - y0) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    return circ(p, x0 + t * dx, y0 + t * dy, w);
}
bool poly(P p, std::initializer_list<P> pts) {
    bool in = false;
    const P* v = pts.begin();
    const size_t n = pts.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++)
        if ((v[i].y > p.y) != (v[j].y > p.y) && p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x)
            in = !in;
    return in;
}
bool star_at(P p, float cx, float cy, float R, float r, int points) {
    const float a = std::atan2(p.y - cy, p.x - cx) + 3.14159265f / 2;
    const float d = std::sqrt((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy));
    const float seg = 2 * 3.14159265f / points;
    float t = std::fmod(a + 2 * 3.14159265f * 4, seg) / seg;   // 0 at a point, 0.5 between two
    t = std::abs(t - 0.5f) * 2;                                // 1 at a point, 0 between
    const float edge = r + (R - r) * t * t * (3 - 2 * t) * 0.92f + (R - r) * 0.08f * t;
    return d <= edge;
}
// A stroke along the quadratic curve a-b-c, `w0` wide at a narrowing to `w1` at c.
bool curve(P p, P a, P b, P c, float w0, float w1) {
    constexpr int kSteps = 20;
    P prev = a;
    for (int i = 1; i <= kSteps; i++) {
        const float t = (float)i / kSteps, u = 1 - t;
        const P q = {u * u * a.x + 2 * u * t * b.x + t * t * c.x, u * u * a.y + 2 * u * t * b.y + t * t * c.y};
        if (seg(p, prev.x, prev.y, q.x, q.y, w0 + (w1 - w0) * (t - 0.5f / kSteps))) return true;
        prev = q;
    }
    return false;
}

bool glyph_at(const Look& l, P p) {
    switch (l.glyph) {
    case kStar: return star_at(p, 0.5f, 0.47f, 0.30f, 0.13f, 5);
    case kHeart:
        return circ(p, 0.395f, 0.37f, 0.125f) || circ(p, 0.605f, 0.37f, 0.125f) ||
               poly(p, {{0.28f, 0.41f}, {0.72f, 0.41f}, {0.5f, 0.68f}});
    case kTrophy:
    case kTrophyStar: {
        const bool cup = (rect(p, 0.33f, 0.20f, 0.67f, 0.38f) || (ell(p, 0.5f, 0.38f, 0.17f, 0.15f) && p.y >= 0.38f));
        const bool handles = (ring(p, 0.33f, 0.30f, 0.045f, 0.085f) && p.x < 0.33f) ||
                             (ring(p, 0.67f, 0.30f, 0.045f, 0.085f) && p.x > 0.67f);
        const bool foot = rect(p, 0.465f, 0.50f, 0.535f, 0.61f) || rrect(p, 0.36f, 0.60f, 0.64f, 0.69f, 0.02f);
        const bool star = l.glyph == kTrophyStar && star_at(p, 0.5f, 0.33f, 0.10f, 0.045f, 5);
        return (cup && !star) || handles || foot;
    }
    case kCrown:
        return (poly(p, {{0.25f, 0.62f}, {0.25f, 0.30f}, {0.375f, 0.47f}, {0.5f, 0.25f}, {0.625f, 0.47f}, {0.75f, 0.30f},
                         {0.75f, 0.62f}}) ||
                rect(p, 0.25f, 0.62f, 0.75f, 0.69f) || circ(p, 0.25f, 0.29f, 0.04f) || circ(p, 0.5f, 0.24f, 0.04f) ||
                circ(p, 0.75f, 0.29f, 0.04f)) &&
               !circ(p, 0.5f, 0.53f, 0.04f);
    case kClock:
        return ring(p, 0.5f, 0.47f, 0.19f, 0.245f) || seg(p, 0.5f, 0.47f, 0.5f, 0.32f, 0.025f) ||
               seg(p, 0.5f, 0.47f, 0.61f, 0.53f, 0.025f) || rrect(p, 0.45f, 0.15f, 0.55f, 0.21f, 0.015f);
    case kFire:   // (a tall flame with a tongue each side)
        return (circ(p, 0.5f, 0.53f, 0.165f) || poly(p, {{0.337f, 0.50f}, {0.47f, 0.17f}, {0.66f, 0.47f}}) ||
                circ(p, 0.33f, 0.60f, 0.085f) || poly(p, {{0.25f, 0.60f}, {0.27f, 0.36f}, {0.40f, 0.56f}}) ||
                circ(p, 0.68f, 0.60f, 0.08f) || poly(p, {{0.61f, 0.58f}, {0.75f, 0.40f}, {0.76f, 0.60f}})) &&
               !(circ(p, 0.5f, 0.58f, 0.075f) || poly(p, {{0.428f, 0.57f}, {0.49f, 0.39f}, {0.572f, 0.57f}}));
    case kArrowR:
        return poly(p, {{0.24f, 0.39f}, {0.50f, 0.39f}, {0.50f, 0.26f}, {0.77f, 0.46f}, {0.50f, 0.66f}, {0.50f, 0.53f},
                        {0.24f, 0.53f}});
    case kMoon: return circ(p, 0.48f, 0.46f, 0.23f) && !circ(p, 0.58f, 0.39f, 0.20f);
    case kChest:
        return ((rrect(p, 0.27f, 0.37f, 0.73f, 0.68f, 0.03f) || (ell(p, 0.5f, 0.37f, 0.23f, 0.13f) && p.y <= 0.37f)) &&
                !rect(p, 0.27f, 0.40f, 0.73f, 0.43f)) &&
               !rrect(p, 0.465f, 0.44f, 0.535f, 0.52f, 0.01f);
    case kDoor:   // (an arched palace door, studded)
        return (rect(p, 0.33f, 0.34f, 0.67f, 0.70f) || (circ(p, 0.5f, 0.34f, 0.17f) && p.y <= 0.34f) ||
                poly(p, {{0.44f, 0.20f}, {0.5f, 0.12f}, {0.56f, 0.20f}})) &&
               !circ(p, 0.60f, 0.52f, 0.025f) && !rect(p, 0.493f, 0.30f, 0.507f, 0.70f);
    case kShield:
        return poly(p, {{0.27f, 0.22f}, {0.73f, 0.22f}, {0.73f, 0.44f}, {0.5f, 0.71f}, {0.27f, 0.44f}}) &&
               !(seg(p, 0.38f, 0.44f, 0.47f, 0.53f, 0.03f) || seg(p, 0.47f, 0.53f, 0.63f, 0.35f, 0.03f));
    case kApple:
        return ((circ(p, 0.43f, 0.51f, 0.18f) || circ(p, 0.57f, 0.51f, 0.18f)) && !circ(p, 0.5f, 0.32f, 0.045f) &&
                !ell(p, 0.42f, 0.45f, 0.035f, 0.06f)) ||
               seg(p, 0.5f, 0.35f, 0.53f, 0.22f, 0.018f) || ell(p, 0.61f, 0.25f, 0.075f, 0.035f);
    case kKey:
        return ring(p, 0.34f, 0.46f, 0.06f, 0.13f) || rect(p, 0.46f, 0.43f, 0.77f, 0.49f) ||
               rect(p, 0.64f, 0.49f, 0.68f, 0.58f) || rect(p, 0.72f, 0.49f, 0.77f, 0.60f);
    case kCloud:   // (the Genie's smoke, rising from a lamp's spout)
        return circ(p, 0.37f, 0.40f, 0.12f) || circ(p, 0.52f, 0.31f, 0.16f) || circ(p, 0.66f, 0.40f, 0.11f) ||
               rrect(p, 0.30f, 0.38f, 0.72f, 0.52f, 0.06f) || curve(p, {0.52f, 0.50f}, {0.40f, 0.60f}, {0.52f, 0.68f}, 0.05f, 0.02f);
    case kLamp: {   // (the Genie's lamp: body, spout, handle, lid and foot)
        const bool body = ell(p, 0.46f, 0.53f, 0.20f, 0.10f) && p.y >= 0.47f;
        const bool top = ell(p, 0.46f, 0.49f, 0.16f, 0.05f);
        const bool spout = poly(p, {{0.58f, 0.49f}, {0.80f, 0.39f}, {0.83f, 0.42f}, {0.64f, 0.58f}});
        const bool handle = ring(p, 0.26f, 0.51f, 0.035f, 0.075f) && p.x < 0.29f;
        const bool lid = ell(p, 0.46f, 0.43f, 0.07f, 0.035f) || circ(p, 0.46f, 0.38f, 0.025f);
        const bool foot = rect(p, 0.43f, 0.60f, 0.49f, 0.64f) || rrect(p, 0.37f, 0.63f, 0.55f, 0.68f, 0.015f);
        return body || top || spout || handle || lid || foot;
    }
    case kSword: {   // (a scimitar: the curved blade, the guard, the grip)
        const bool blade = curve(p, {0.40f, 0.58f}, {0.68f, 0.57f}, {0.75f, 0.19f}, 0.058f, 0.01f);
        const bool guard = seg(p, 0.36f, 0.47f, 0.42f, 0.69f, 0.022f);
        const bool grip = seg(p, 0.38f, 0.585f, 0.27f, 0.62f, 0.022f) || circ(p, 0.25f, 0.625f, 0.032f);
        return blade || guard || grip;
    }
    case kCarpet: {   // (the magic carpet, rippling, with its tassels and a diamond in the middle)
        const P q = {p.x, p.y - 0.035f * std::sin((p.x - 0.2f) * 11.0f)};
        const bool c = poly(q, {{0.22f, 0.44f}, {0.70f, 0.36f}, {0.80f, 0.52f}, {0.32f, 0.60f}});
        const bool inner = poly(q, {{0.28f, 0.455f}, {0.67f, 0.39f}, {0.74f, 0.505f}, {0.35f, 0.57f}}) &&
                           !poly(q, {{0.30f, 0.46f}, {0.665f, 0.40f}, {0.72f, 0.50f}, {0.355f, 0.56f}});
        const bool diamond = poly(q, {{0.43f, 0.485f}, {0.51f, 0.44f}, {0.59f, 0.465f}, {0.51f, 0.51f}});
        const bool tassels = seg(q, 0.22f, 0.44f, 0.16f, 0.46f, 0.012f) || seg(q, 0.32f, 0.60f, 0.26f, 0.63f, 0.012f) ||
                             seg(q, 0.70f, 0.36f, 0.76f, 0.33f, 0.012f) || seg(q, 0.80f, 0.52f, 0.86f, 0.50f, 0.012f);
        return (c && !inner && !diamond) || tassels;
    }
    case kScarab:   // (the scarab: wing cases, head and legs)
        return ((ell(p, 0.5f, 0.50f, 0.13f, 0.17f) && !rect(p, 0.492f, 0.38f, 0.508f, 0.68f)) ||
                ell(p, 0.5f, 0.31f, 0.075f, 0.05f) || circ(p, 0.5f, 0.25f, 0.045f) ||
                seg(p, 0.40f, 0.42f, 0.28f, 0.36f, 0.014f) || seg(p, 0.60f, 0.42f, 0.72f, 0.36f, 0.014f) ||
                seg(p, 0.38f, 0.50f, 0.25f, 0.52f, 0.014f) || seg(p, 0.62f, 0.50f, 0.75f, 0.52f, 0.014f) ||
                seg(p, 0.40f, 0.59f, 0.29f, 0.68f, 0.014f) || seg(p, 0.60f, 0.59f, 0.71f, 0.68f, 0.014f) ||
                seg(p, 0.47f, 0.22f, 0.42f, 0.16f, 0.010f) || seg(p, 0.53f, 0.22f, 0.58f, 0.16f, 0.010f));
    case kGem:   // (a cut diamond, its facets drawn in)
        return poly(p, {{0.26f, 0.37f}, {0.36f, 0.24f}, {0.64f, 0.24f}, {0.74f, 0.37f}, {0.5f, 0.69f}}) &&
               !seg(p, 0.26f, 0.37f, 0.74f, 0.37f, 0.009f) && !seg(p, 0.40f, 0.37f, 0.5f, 0.69f, 0.008f) &&
               !seg(p, 0.60f, 0.37f, 0.5f, 0.69f, 0.008f) && !seg(p, 0.36f, 0.24f, 0.40f, 0.37f, 0.008f) &&
               !seg(p, 0.64f, 0.24f, 0.60f, 0.37f, 0.008f) && !seg(p, 0.5f, 0.24f, 0.40f, 0.37f, 0.008f) &&
               !seg(p, 0.5f, 0.24f, 0.60f, 0.37f, 0.008f);
    case kStones: {   // (a pile of three stones)
        const bool top = ell(p, 0.52f, 0.41f, 0.12f, 0.09f);
        const bool left = ell(p, 0.40f, 0.58f, 0.15f, 0.10f) && !ell(p, 0.52f, 0.41f, 0.14f, 0.11f);
        const bool right = ell(p, 0.64f, 0.59f, 0.12f, 0.09f) && !ell(p, 0.52f, 0.41f, 0.14f, 0.11f) &&
                           !ell(p, 0.40f, 0.58f, 0.17f, 0.12f);
        return top || left || right;
    }
    case kDome:   // (Agrabah's palace: an onion dome on its drum, two minarets, an arched gate)
        return ((circ(p, 0.5f, 0.38f, 0.14f) || poly(p, {{0.39f, 0.31f}, {0.5f, 0.16f}, {0.61f, 0.31f}}) ||
                 seg(p, 0.5f, 0.17f, 0.5f, 0.12f, 0.012f) || rect(p, 0.38f, 0.44f, 0.62f, 0.52f) ||
                 rect(p, 0.32f, 0.52f, 0.68f, 0.70f) || rect(p, 0.235f, 0.40f, 0.285f, 0.70f) ||
                 circ(p, 0.26f, 0.38f, 0.04f) || seg(p, 0.26f, 0.35f, 0.26f, 0.30f, 0.008f) ||
                 rect(p, 0.715f, 0.40f, 0.765f, 0.70f) || circ(p, 0.74f, 0.38f, 0.04f) ||
                 seg(p, 0.74f, 0.35f, 0.74f, 0.30f, 0.008f)) &&
                !rect(p, 0.30f, 0.505f, 0.70f, 0.52f) && !(rect(p, 0.45f, 0.61f, 0.55f, 0.70f) || circ(p, 0.5f, 0.61f, 0.05f)));
    case kSnake:   // (Jafar's cobra: the hood, eyes, and the body coiling down)
        return ((ell(p, 0.5f, 0.35f, 0.19f, 0.15f) || poly(p, {{0.36f, 0.42f}, {0.64f, 0.42f}, {0.545f, 0.52f}, {0.455f, 0.52f}}) ||
                 curve(p, {0.5f, 0.50f}, {0.28f, 0.58f}, {0.50f, 0.62f}, 0.045f, 0.045f) ||
                 curve(p, {0.50f, 0.62f}, {0.74f, 0.66f}, {0.40f, 0.71f}, 0.045f, 0.015f)) &&
                !(ell(p, 0.5f, 0.34f, 0.075f, 0.10f) && !ell(p, 0.5f, 0.34f, 0.06f, 0.085f)) &&
                !circ(p, 0.465f, 0.31f, 0.018f) && !circ(p, 0.535f, 0.31f, 0.018f) &&
                !seg(p, 0.5f, 0.38f, 0.5f, 0.41f, 0.01f));
    case kMonkey: {   // (Abu: round head, big ears, his fez)
        const bool head = circ(p, 0.5f, 0.47f, 0.16f) || circ(p, 0.32f, 0.44f, 0.065f) || circ(p, 0.68f, 0.44f, 0.065f);
        const bool face = circ(p, 0.445f, 0.45f, 0.028f) || circ(p, 0.555f, 0.45f, 0.028f) ||
                          (ell(p, 0.5f, 0.54f, 0.065f, 0.035f) && p.y > 0.54f) || circ(p, 0.32f, 0.44f, 0.03f) ||
                          circ(p, 0.68f, 0.44f, 0.03f);
        const bool fez = poly(p, {{0.42f, 0.33f}, {0.58f, 0.33f}, {0.555f, 0.20f}, {0.445f, 0.20f}});
        const bool band = rect(p, 0.38f, 0.33f, 0.62f, 0.348f);
        const bool tassel = seg(p, 0.555f, 0.21f, 0.63f, 0.26f, 0.011f) || circ(p, 0.635f, 0.27f, 0.02f);
        return (head && !face && !band && !fez) || fez || tassel;
    }
    case kWheel: {   // (the Genie's bonus wheel: rim, spokes, hub and the pointer at the top)
        if (ring(p, 0.5f, 0.47f, 0.19f, 0.235f) || circ(p, 0.5f, 0.47f, 0.05f)) return true;
        if (poly(p, {{0.45f, 0.16f}, {0.55f, 0.16f}, {0.5f, 0.27f}})) return true;
        for (int k = 0; k < 8; k++) {
            const float a = k * 3.14159265f / 4 + 0.39f;
            if (seg(p, 0.5f, 0.47f, 0.5f + 0.19f * std::cos(a), 0.47f + 0.19f * std::sin(a), 0.016f)) return true;
        }
        return false;
    }
    case kBars:   // (a dungeon window, one bar bent aside: the way out)
        return (rrect(p, 0.27f, 0.22f, 0.73f, 0.68f, 0.03f) && !rect(p, 0.32f, 0.27f, 0.68f, 0.63f)) ||
               rect(p, 0.385f, 0.27f, 0.415f, 0.63f) || rect(p, 0.585f, 0.27f, 0.615f, 0.63f) ||
               seg(p, 0.5f, 0.27f, 0.5f, 0.36f, 0.016f) || seg(p, 0.5f, 0.36f, 0.555f, 0.45f, 0.016f) ||
               seg(p, 0.5f, 0.63f, 0.5f, 0.54f, 0.016f) || seg(p, 0.5f, 0.54f, 0.445f, 0.46f, 0.016f);
    case kCave:   // (the Cave of Wonders: a rocky mound with its dark mouth, and a lamp's glint inside)
        return ((ell(p, 0.5f, 0.70f, 0.32f, 0.44f) && p.y <= 0.70f) && !(ell(p, 0.5f, 0.70f, 0.12f, 0.20f) && p.y <= 0.70f) &&
                !seg(p, 0.30f, 0.42f, 0.36f, 0.48f, 0.01f) && !seg(p, 0.66f, 0.38f, 0.72f, 0.46f, 0.01f)) ||
               star_at(p, 0.5f, 0.61f, 0.045f, 0.018f, 4);
    case kText: return in_text(l.text, 0.5f, 0.45f, 0.30f, 0.62f, p.x, p.y);
    }
    return false;
}

struct Col { float r, g, b; };
Col mix(Col a, Col b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Col hex(uint32_t v) { return {((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f}; }
uint32_t rgb(float r, float g, float b) {
    auto c = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255); };
    return c(r) << 16 | c(g) << 8 | c(b);
}

// The game an achievement belongs to, by its id's prefix: its colour (the launcher card's accent, deepened) and its
// short name along the badge's foot.
Col game_colour(const char* id) {
    if (!std::strncmp(id, "md_", 3)) return hex(0x2F74C4);     // Mega Drive: blue
    if (!std::strncmp(id, "snes_", 5)) return hex(0x7651C9);   // SNES: purple
    if (!std::strncmp(id, "sms_", 4)) return hex(0xC4473A);    // Master System: red
    return hex(0xB8862A);                                      // the collection: gold
}
const char* game_tag(const char* id) {
    if (!std::strncmp(id, "md_", 3)) return "MD";
    if (!std::strncmp(id, "snes_", 5)) return "SNES";
    if (!std::strncmp(id, "sms_", 4)) return "SMS";
    return "ALL";
}

// One sample: the badge's colour at (x, y).
Col sample_badge(const char* id, const Look& l, float x, float y) {
    static const uint32_t kTier[4] = {0xC27A3A, 0xC9D1DB, 0xF3C431, 0x9FE3F0};
    const Col back = hex(0x0F1117);
    const P p = {x, y};
    if (!rrect(p, 0.0f, 0.0f, 1.0f, 1.0f, 0.17f)) return back;
    // the tier's rim, lit from above
    if (!rrect(p, 0.045f, 0.045f, 0.955f, 0.955f, 0.13f)) {
        const Col t = hex(kTier[std::clamp(l.tier, 0, 3)]);
        return mix(mix(t, hex(0xFFFFFF), 0.35f), mix(t, back, 0.35f), y);
    }
    // the game's colour, lighter towards the top left
    const Col g = game_colour(id);
    const float light = std::clamp(1.0f - (x * 0.4f + y * 0.9f), 0.0f, 1.0f);
    Col c = mix(mix(g, back, 0.35f), mix(g, hex(0xFFFFFF), 0.18f), light);
    // the game's name along the bottom
    if (y > 0.775f && y < 0.925f) {
        c = mix(c, back, 0.45f);
        if (in_text(game_tag(id), 0.5f, 0.852f, 0.085f, 0.6f, x, y)) c = hex(0xF2F2F2);
        return c;
    }
    // the symbol, white over a soft shadow
    if (glyph_at(l, {x - 0.022f, y - 0.03f})) c = mix(c, back, 0.55f);
    if (glyph_at(l, p)) c = mix(hex(0xFFFFFF), hex(0xFFE9B8), y);
    return c;
}

void csv_field(std::string& out, const std::string& v) {
    if (v.find_first_of(",\"\r\n") == std::string::npos) { out += v; return; }
    out += '"';
    for (char c : v) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
}

}  // namespace

int achievement_tier(const char* id) { return look_of(id).tier; }

int achievement_points(const char* id) {
    static const int kPoints[4] = {10, 25, 50, 100};
    return kPoints[std::clamp(look_of(id).tier, 0, 3)];
}

void draw_achievement_icon(const char* id, bool locked, int size, std::vector<uint32_t>& px) {
    const Look& l = look_of(id);
    px.assign((size_t)size * size, 0);
    constexpr int kSub = 4;
    for (int py = 0; py < size; py++)
        for (int pxx = 0; pxx < size; pxx++) {
            float r = 0, g = 0, b = 0;
            for (int sy = 0; sy < kSub; sy++)
                for (int sx = 0; sx < kSub; sx++) {
                    const float x = (pxx + (sx + 0.5f) / kSub) / size, y = (py + (sy + 0.5f) / kSub) / size;
                    const Col c = sample_badge(id, l, x, y);
                    r += c.r; g += c.g; b += c.b;
                }
            const float n = kSub * kSub;
            r /= n; g /= n; b /= n;
            if (locked) {   // (grey and dimmed, a little cool)
                const float v = (0.3f * r + 0.59f * g + 0.11f * b) * 0.55f;
                r = v * 0.95f; g = v; b = v * 1.08f;
            }
            px[(size_t)py * size + pxx] = rgb(r, g, b);
        }
}

bool export_achievements(const std::wstring& dir, const std::vector<AchievementGroup>& groups, const AchievementStore& store,
                         std::string* err) {
    const std::wstring icons = path_join(dir, L"icons");
    make_dir(dir);
    make_dir(icons);
    constexpr int kSize = 128;
    static const char* kTrophy[4] = {"bronze", "silver", "gold", "platinum"};
    std::string csv = "\xEF\xBB\xBF";   // (UTF-8, marked)
    csv += "ID,Title,Description,Points,Trophy Type,Hidden,Rarity,Category,Progress,Progress Total,Unlocked,Unlock Time,"
           "Unlocked Icon,Locked Icon\r\n";
    std::vector<uint32_t> px;
    for (const AchievementGroup& grp : groups)
        for (const AchievementDef& d : grp.defs) {
            for (int locked = 0; locked < 2; locked++) {
                draw_achievement_icon(d.id, locked != 0, kSize, px);
                const std::wstring file = path_join(icons, from_utf8(std::string(d.id) + (locked ? "_locked.png" : ".png")));
                if (!write_png(file, px.data(), kSize, kSize)) {
                    if (err) *err = "couldn't write " + to_utf8(file);
                    return false;
                }
            }
            const bool have = store.unlocked(d.id);
            std::string when = store.unlocked_at(d.id);
            if (when.size() == 10) when += " 00:00:00";   // (earned before times were kept: the day only)
            std::string progress, total;
            if (d.target > 0) {
                int v = store.progress(d.id);
                if (d.set) {   // a set of bits: how many were found
                    int n = 0;
                    for (int b = 0; b < 31; b++) n += (v >> b) & 1;
                    v = n;
                }
                if (have) v = d.target;
                progress = std::to_string(std::min(v, d.target));
                total = std::to_string(d.target);
            }
            const std::string fields[14] = {d.id, d.name, d.desc, std::to_string(achievement_points(d.id)),
                                            kTrophy[achievement_tier(d.id)], d.secret ? "True" : "False", "", grp.title,
                                            progress, total, have ? "True" : "False", have ? when : "",
                                            std::string("icons/") + d.id + ".png", std::string("icons/") + d.id + "_locked.png"};
            for (int i = 0; i < 14; i++) {
                if (i) csv += ',';
                csv_field(csv, fields[i]);
            }
            csv += "\r\n";
        }
    if (!write_file(path_join(dir, L"achievements.csv"), csv.data(), csv.size())) {
        if (err) *err = "couldn't write achievements.csv";
        return false;
    }
    return true;
}
