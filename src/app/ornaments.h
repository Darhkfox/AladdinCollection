// The Arabian Nights menu style's gold ornaments: small pictures drawn here once, procedurally (no image files),
// then drawn at any size with Video::draw_image.
#pragma once
#include <cstdint>
#include "../platform/video.h"

enum class Ornament : int {
    Star8,     // an eight-pointed star (two overlapping squares) in gold, an indigo medallion in its middle
    Diamond,   // a tall gold rhombus with an engraved inner line
    Sparkle,   // a four-pointed glint with a soft glow (the night sky, the lamp's magic)
    Glow,      // a soft round warm glow, fading out to its edge (the moon's halo)
    Count
};

// Draws `o` centred on (cx, cy), `size` pixels tall (its width follows its shape: ornament_aspect).
void draw_ornament(Video& v, Ornament o, float cx, float cy, float size, float alpha = 1.0f);
float ornament_aspect(Ornament o);   // width / height
