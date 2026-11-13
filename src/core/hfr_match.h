// High frame rate: the sprites' moves since the last frame, for the in-between pictures the three video cores draw
// (VDP::sprite_moves, Ppu::objMoves, SmsVdp::sprite_moves). Ported from the Super Mario All-Stars port's PPU.
#pragma once
#include <cstdint>

struct HfrSprite {
    int16_t x = 0, y = 0;   // where it is drawn (as the screen shows it)
    uint32_t key = 0;       // its exact look: pattern, palette, flips, size
    uint16_t look = 0;      // its kind: palette and size (a character's pieces animate within it)
    uint16_t group = 0;     // what makes touching sprites one character's pieces (the palette)
    uint8_t w = 8, h = 8;   // size in pixels
    bool on = false;        // drawn (not parked off the picture, not a masking sprite)
};

// Games reorder their sprite tables from frame to frame (flicker, priority rotation), so a sprite is matched by its
// look, not its entry: last frame's sprite of the same key, a short way off (20 px), each taken once, nearest pairs
// first - unless last frame had a sprite of its kind right where it is (within half its width): then it's that one,
// animated. Otherwise it moves as the matched sprite of its kind nearest it (the rest of the character); failing that,
// one not drawn near there last frame (flickered) is looked for in the frame before, its move halved; else it's new
// and doesn't move. Then a character of three or more touching sprites of a group moves as one: a move clearly most
// of its pieces offer (two thirds) is taken by the pieces whose own move was a guess. `ywrap`: 256 where Y wraps
// (8-bit coordinates), else 0. n <= 128.
void hfr_match(const HfrSprite* cur, const HfrSprite* prev, const HfrSprite* prev2, int n, int ywrap, int16_t* dx,
               int16_t* dy);
