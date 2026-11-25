// Sega 315-5313 VDP (Mega Drive video display processor), mode 5 only.
// Supports rendering extra columns on both sides of the display (widescreen).
#pragma once
#include <cstdint>
#include <cstddef>
#include "hfr_match.h"

class VDP {
public:
    static constexpr int kMaxWidth = 640;     // max output width incl. widescreen extension
    static constexpr int kMaxLines = 240;

    VDP();
    void reset();

    // ---- CPU ports ----------------------------------------------------------
    void     write_data(uint16_t v);
    void     write_ctrl(uint16_t v);
    uint16_t read_data();
    uint16_t read_status();
    uint16_t read_hv();

    // ---- host interface -----------------------------------------------------
    // DMA source reads (68000 bus, word address).
    uint16_t (*dma_read)(void* host, uint32_t addr) = nullptr;
    // Current position within the scanline in master clocks (0..3419).
    int      (*line_clock)(void* host) = nullptr;
    // 68000 stall for DMA (68000 cycles).
    void     (*cpu_stall)(void* host, int cycles68) = nullptr;
    // Interrupt level changed (call cpu.set_irq).
    void     (*irq_changed)(void* host, int level) = nullptr;
    void*    host = nullptr;

    // Timing state maintained by the system.
    int  line = 0;              // current scanline (0..261)
    bool vblank = false;
    bool odd_frame = false;
    bool pal = false;
    int64_t mclk_now = 0;       // absolute master clock (set by the system before port access)
    int64_t dma_busy_until = 0;

    // Interrupts
    bool vint_pending = false;
    bool hint_pending = false;
    int  hint_counter = 0;
    int  irq_level() const;     // highest enabled pending interrupt level
    int  acknowledge(int level);// CPU acknowledged interrupt `level`
    void update_irq();
    void trigger_vint();        // at start of vblank
    void hint_line();           // called once per line at hblank: decrements / reloads counter

    // ---- rendering ----------------------------------------------------------
    // Extra pixels rendered on each side (0 = original width).
    int  ext = 0;
    bool sprite_limit = true;   // hardware sprites-per-line / dots-per-line limits
    bool ignore_left_blank = false; // ignore reg0 bit5 (left column blanking) - used in widescreen
    // Output columns blanked at the left/right edge (level bounds in widescreen); HUD sprites stay.
    int  edge_mask_l = 0, edge_mask_r = 0;
    // Set bit 11 (kSpritePixel) of frame-buffer pixels that come from a sprite, so the front end can treat the
    // background and the sprites apart (e.g. dim only the scenery). Readers mask the colour with 0x7FF.
    bool mark_sprites = false;
    static constexpr uint16_t kSpritePixel = 0x800;
    // Output columns dimmed at the left/right edge (what a boss arena's walls keep out of reach in widescreen), HUD
    // sprites spared: those pixels carry `edge_dim` (0..15) in bits 12-15 (kDimShift), and the front end darkens them
    // by that many 30ths (dimmed(): 15 is half brightness). Readers mask the colour with 0x7FF.
    int  edge_dim_l = 0, edge_dim_r = 0;
    int  edge_dim = 0;
    static constexpr int kDimShift = 12;
    static uint32_t dimmed(uint32_t argb, int n) {
        if (n <= 0) return argb;
        const uint32_t k = 30 - (uint32_t)(n > 15 ? 15 : n);
        return (argb & 0xFF000000u) | (((argb >> 16) & 0xFF) * k / 30) << 16 | (((argb >> 8) & 0xFF) * k / 30) << 8 |
               (argb & 0xFF) * k / 30;
    }
    // Widescreen view shift: planes and sprites are sampled `view_shift` pixels further right
    // (negative = left) than the game's own camera, the window plane stays in place. Lets a module
    // keep the widened view inside the level without touching the game's camera.
    int  view_shift = 0;
    // Optional per-line shift for plane B (used instead of view_shift when view_shift_b_on): a background that
    // scrolls slower than the camera must also be shifted less, or it drifts while the view slides.
    bool    view_shift_b_on = false;
    int16_t view_shift_b[kMaxLines] = {};
    // Read sprite X as 10 bits (screen x up to 639; 768-1023 are -256..-1, left of X 0): lets a patched game place
    // sprites past the 9-bit limit of the real chip when the view is widened and shifted.
    bool sprite_x10 = false;
    // Draw this sprite a second time, this many px further right (0 = no copy) - a game module can show a
    // repeating background object twice when the widened view is wider than the game's repeat distance.
    int (*sprite_copy)(void* host, int index, uint16_t attr, int x, int y) = nullptr;
    uint8_t debug_layers = 7;   // test tool only: bit 0 plane A, bit 1 plane B, bit 2 sprites

    // Optional hook: horizontal offset added to a sprite's screen X (HUD re-anchoring).
    // index = sprite table index, attr = pattern word, x/y = raw table coordinates.
    int  (*sprite_xoff)(void* host, int index, uint16_t attr, int x, int y) = nullptr;
    // Optional hook: called after a line's layers are rendered (before composition) so a game
    // module can patch the plane/sprite line buffers (index = x + ext). Pixel format:
    // bit7 priority, bits5-4 palette, bits3-0 colour (0 = transparent).
    void (*line_hook)(void* host, int line, uint8_t* plane_a, uint8_t* plane_b, uint8_t* sprites) = nullptr;
    // Optional hook: called before a line is rendered (display on), so a game module can set view_shift /
    // view_shift_b for it from the scroll as it stands then (a scroll written while the frame is drawn).
    void (*pre_line_hook)(void* host, int line) = nullptr;
    // Optional hook: sprites for which it returns true are HUD elements (exempt from edge masks).
    bool (*sprite_is_hud)(void* host, int index) = nullptr;
    void* hook_host = nullptr;   // context passed to sprite_xoff / line_hook / pre_line_hook / sprite_is_hud
    uint32_t hook_flags = 0;     // free for the installer of the hooks

    void render_line(int ln);   // render scanline into framebuffer
    void blank_line(int ln);    // output backdrop colour (display disabled)

    // ---- high frame rate: in-between pictures ------------------------------------
    // With sub_frames = K > 1 every line is also drawn into sub_fb[k-1] (k = 1 .. K-1), k/K of the way from the last
    // frame to this one: each plane's scroll part of the way back to the last frame's on that line, as the screen
    // shows it (the view shift taken off: a slide that holds the picture still holds it still here too), the sprites
    // part of the way back to where they were (matched by their look, see sprite_moves). A scroll jump of more than
    // 32 px, a line drawn with the display off, another width or other plane / sprite tables than the last frame's
    // are cuts: that line (or the frame) is shown as it is. The module's line hook runs for the in-between lines too,
    // with sub_dx / sub_dy telling it how far each plane is drawn from its scroll (so what it draws from the level
    // goes along); pre_line_hook doesn't (the view's slide is the frame's). Shown on a 100+ Hz display between the
    // game's frames, a frame late (what lies between two frames is known once the second is drawn). Nothing the
    // game can read changes: the status bits stay the frame's.
    static constexpr int kMaxSubFrames = 4;
    int sub_frames = 1;
    uint16_t sub_fb[kMaxSubFrames - 1][kMaxLines][kMaxWidth];
    void hfr_forget();          // nothing in between with the last frame (a state loaded, a reset)
    // While an in-between line is drawn (else 0): each plane is drawn sub_dx px further right than its scroll and
    // view shift say (render_plane applies it), and sub_dy px further down in the plane; how far back it is
    // (0 = the frame, 1 = the last frame).
    bool  sub_line() const { return sub_; }
    int   sub_dx(bool plane_b) const { return sub_ ? sub_h_[plane_b ? 1 : 0] : 0; }
    int   sub_dy(bool plane_b) const { return sub_ ? sub_v_[plane_b ? 1 : 0] : 0; }
    float sub_back() const { return sub_ ? sub_back_ : 0.0f; }
    // A line hook that draws plane A itself with another shift than view_shift (Sultan's Dungeon's swapped planes)
    // tells it here, so the line's motion is measured as the screen shows it.
    void plane_a_drawn_with(int shift) { line_shift_a_ = shift; }

    int  active_width() const { return (reg[12] & 0x81) ? 320 : 256; }
    int  active_lines() const { return (reg[1] & 0x08) ? 240 : 224; }
    bool h40() const { return (reg[12] & 0x81) != 0; }
    bool display_enabled() const { return (reg[1] & 0x40) != 0; }

    // Framebuffer of 11-bit colour codes: (intensity << 9) | BGR333,
    // intensity 0 = normal, 1 = shadow, 2 = highlight.
    uint16_t fb[kMaxLines][kMaxWidth];
    int  fb_width = 320;        // width of the last rendered frame (active + 2*ext)
    int  fb_lines = 224;

    // ---- memory -------------------------------------------------------------
    uint8_t  vram[0x10000];
    uint16_t cram[64];
    uint16_t vsram[64];
    uint8_t  reg[32];

    // command state
    bool     pending = false;
    uint8_t  code = 0;
    uint16_t addr = 0;
    bool     fill_pending = false;
    bool     sprite_overflow = false;
    bool     sprite_collision = false;
    uint16_t hv_latch = 0;
    bool     hv_latched = false;
    bool     dot_overflow_prev = false;

    size_t state_size() const;
    void   save_state(uint8_t* dst) const;
    void   load_state(const uint8_t* src);

    uint16_t vram16(uint32_t a) const { a &= 0xFFFE; return (uint16_t)((vram[a] << 8) | vram[a + 1]); }
    // One 8-pixel row of the pattern a name-table entry points at, in line-buffer format
    // (bit 7 priority, bits 4-5 palette, bits 0-3 colour) - for game modules that draw plane cells.
    void pattern_row(uint16_t entry, int row, uint8_t* out8) const;

    // Span renderers (screen x in [x0, x1), may be negative / beyond the active width).
    // Output is in line-buffer format; public so game line hooks can re-render regions.
    void render_plane(uint8_t* dst, int ln, bool plane_b, int x0, int x1);
    void render_window(uint8_t* dst, int ln, int x0, int x1);

private:
    void do_dma_68k();
    void do_dma_fill(uint16_t data);
    void do_dma_copy();
    void write_reg(int r, uint8_t v);
    void write_vram16(uint16_t a, uint16_t v);
    void render_sprites(uint8_t* dst, int ln);
    uint8_t spr_hud_[kMaxWidth];
    int  dma_rate_bytes() const;

    void window_span(int ln, int* ws, int* we) const;
    void draw_plane_a(uint8_t* la, int ln, int x0, int x1, int ws, int we);
    void compose(uint16_t* out, const uint8_t* la, const uint8_t* lb, const uint8_t* ls, int TW);
    int  line_hscroll(int ln, bool plane_b) const;

    // high frame rate state (see sub_frames)
    static constexpr int kSat = 80;
    bool    sub_ = false;                    // an in-between line is being drawn
    int     sub_h_[2] = {}, sub_v_[2] = {};  // ...each plane this much further right / down (sub_dx, sub_dy)
    float   sub_back_ = 0;
    int16_t spr_sub_dx_[kSat] = {}, spr_sub_dy_[kSat] = {};   // ...and the sprites moved this far
    int     line_shift_a_ = 0;               // plane A's view shift on the line being drawn
    int     hfr_cur_ = 0;                    // which half of the line records is this frame's
    uint32_t hfr_frame_ = 0;                 // frames begun (line 0)
    uint32_t hfr_rec_[2] = {};               // ...the frame each half recorded
    uint32_t hfr_sig_[2] = {};               // ...and its tables and width (another: a cut)
    bool    hfr_prev_ok_ = false;            // the last frame was recorded and is this one's kind
    int16_t hfr_h_[2][2][kMaxLines] = {};    // [frame][plane] each line's H scroll as shown (scroll - view shift)
    int16_t hfr_v_[2][2][kMaxLines] = {};    // [frame][plane] each line's V scroll
    uint8_t hfr_ok_[2][kMaxLines] = {};      // the line was drawn with the display on

    HfrSprite spr_prev_[kSat], spr_prev2_[kSat], spr_cur_[kSat];   // last frame, the frame before (flicker), this one
    int16_t spr_dx_[kSat] = {}, spr_dy_[kSat] = {};   // their moves since the last frame
    uint8_t spr_seen_[kSat * 8] = {};        // the sprite table the moves were worked out from (and its view shift)
    int     spr_seen_shift_ = 0;
    void sprite_moves(bool first);   // (first: at the frame's first line)
    void render_sub_line(int ln, int k, int subs);
};
