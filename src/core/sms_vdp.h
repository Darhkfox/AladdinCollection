// Sega 315-5124/5246 VDP (Master System video display processor), mode 4 only.
// Renders extra columns on both sides of the display (widescreen): what the 256-pixel name table can't hold
// there comes from the game module (bg_entry / line_hook), sprites can be given their true X (sprite_x).
#pragma once
#include <cstdint>
#include <cstddef>

class SmsVdp {
public:
    static constexpr int kMaxExt = 128;            // widest extension per side
    static constexpr int kMaxWidth = 256 + 2 * kMaxExt;
    static constexpr int kMaxLines = 240;
    static constexpr int kLinesNtsc = 262;

    SmsVdp();
    void reset();

    // ---- CPU ports ----------------------------------------------------------
    void    write_data(uint8_t v);
    void    write_ctrl(uint8_t v);
    uint8_t read_data();
    uint8_t read_status();
    uint8_t read_vcount() const;
    uint8_t read_hcount() const { return hcount_latch; }
    void    latch_hcount(int cycle_in_line);   // TH edge on an I/O port

    // ---- host interface -----------------------------------------------------
    void (*irq_changed)(void* host, bool asserted) = nullptr;
    void* host = nullptr;

    // Timing state maintained by the system.
    int  line = 0;                 // current scanline (0..261)
    void start_frame();            // line 0: latches the vertical scroll
    void line_counter_tick();      // once per line at hblank: line interrupt counter
    void frame_irq();              // line $C1: frame interrupt pending

    // ---- rendering ----------------------------------------------------------
    // Extra pixels rendered on each side (0 = original 256 px).
    int  ext = 0;
    bool sprite_limit = true;       // 8 sprites per line, as the hardware
    bool ignore_left_blank = false; // ignore reg 0 bit 5 (the blanked leftmost column) - used in widescreen
    // Mark sprite pixels in the frame buffer (kSpritePixel) so the front end can treat scenery and sprites apart.
    bool mark_sprites = false;
    static constexpr uint8_t kSpritePixel = 0x80;
    uint8_t debug_layers = 3;       // test tool only: bit 0 background, bit 1 sprites

    // Background cells the hardware name table doesn't show: screen cells left of hw_x0 or from hw_x1 on
    // (screen x, 0 = the original picture's left edge) ask bg_entry for their name-table entry. vcol / vrow are
    // the unwrapped table position: vcol = floor((x - hscroll) / 8) (columns 0-31 are the table; less or more
    // lie beyond its left / right edge), vrow = (line + vscroll) / 8 (rows 0-27 the table, 28+ wrapped). Return
    // the 13-bit entry (pattern, flips, palette, priority) or -1 to use the hardware table (wrapping).
    int  hw_x0 = 0, hw_x1 = 256;
    // View shift (widescreen): a module can slide the picture near a level's ends instead of showing what lies
    // outside it. Line ln's background is sampled bg_shift[ln] px further right (screen x of the original picture:
    // output x + shift; the bg_entry / hw_x0..hw_x1 tests use the shifted position), sprites sprite_shift px.
    int16_t bg_shift[kMaxLines] = {};
    int  sprite_shift = 0;
    // When set, asked for each line's background shift as the line is drawn (bg_shift[ln] keeps the answer): the
    // registers a line interrupt set for the line are known then.
    int  (*line_shift)(void* host, int line) = nullptr;
    // bg_entry may set bit 15 of an entry: that cell's pattern comes from alt_patterns (32 bytes a tile, from tile
    // alt_first, alt_count of them) instead of VRAM - a module's copy of tiles the game has reloaded with other
    // graphics since (or not yet). The module may point it elsewhere from one bg_entry call to the next.
    const uint8_t* alt_patterns = nullptr;
    int  alt_first = 0, alt_count = 0;
    // bg_entry may set bit 14 of an entry: that cell's colours come from alt_cram (32 CRAM values, as cram) - a
    // module's copy of the colours a part of the level had before the game reused the palette entries. (The hardware
    // ignores bits 13-15 of the table's own entries.)
    const uint8_t* alt_cram = nullptr;
    int  (*bg_entry)(void* host, int line, int vcol, int vrow) = nullptr;
    // A sprite's screen X (default: its table X, less 8 when reg 0 bit 3 shifts sprites): lets a module place a
    // sprite in the extension, beyond the 8-bit X of the hardware. index = SAT index; x, y, pattern from the table.
    int  (*sprite_x)(void* host, int index, int x, int y, int pattern) = nullptr;
    // Sprites a module adds after the table's 64 (widescreen: pieces of objects a game could not fit in its table).
    // Same meaning as the table's entries (y = the line above the sprite's first, pattern before reg 6 bit 2), with
    // the true screen X; drawn behind the table's sprites. Not kept in save states (the module sets them every frame).
    struct ExtraSprite { int16_t x; uint8_t y, pattern; };
    static constexpr int kMaxExtra = 64;
    ExtraSprite extra[kMaxExtra];
    int  extra_count = 0;
    // Called after a line's layers are rendered, before composition: a module can rewrite the line buffers
    // (index = screen x + ext). Format: bit 7 priority (background) / opaque (sprites), bit 4 palette (sprites
    // use the upper 16 colours), bits 3-0 colour index (0 = transparent).
    void (*line_hook)(void* host, int line, uint8_t* bg, uint8_t* spr) = nullptr;
    void* hook_host = nullptr;

    void render_line(int ln);       // renders an active line into the frame buffer
    // One 8-pixel row of the pattern a name-table entry points at, in line-buffer format.
    void pattern_row(uint16_t entry, int row, uint8_t* out8) const;
    // Renders background pixels for screen x in [x0, x1) of line `ln` from the table (with bg_entry), line format.
    void render_bg(uint8_t* dst, int ln, int x0, int x1);

    int  active_lines() const { return 192; }
    bool display_enabled() const { return (reg[1] & 0x40) != 0; }
    int  hscroll_for(int ln) const { return ((reg[0] & 0x40) && ln < 16) ? 0 : reg[8]; }
    int  vscroll() const { return vscroll_latch; }
    uint16_t name_table() const { return (uint16_t)((reg[2] & 0x0E) << 10); }
    uint16_t sat_base() const { return (uint16_t)((reg[5] & 0x7E) << 7); }
    uint16_t name_entry(int col, int row) const {
        const uint16_t a = (uint16_t)(name_table() + ((row % 28) * 32 + (col & 31)) * 2);
        return (uint16_t)(vram[a & 0x3FFF] | (vram[(a + 1) & 0x3FFF] << 8));
    }

    // Frame buffer: 6-bit colours (--BBGGRR), kSpritePixel set on sprite pixels when mark_sprites.
    uint8_t fb[kMaxLines][kMaxWidth];
    int  fb_width = 256;            // width of the last rendered frame (256 + 2 * ext)
    uint8_t line_hscroll[kMaxLines] = {};   // the horizontal scroll each line was drawn with (tools)
    int  fb_lines = 192;

    // ---- memory / registers ----------------------------------------------------
    uint8_t vram[0x4000];
    uint8_t cram[32];
    uint8_t reg[16];
    uint8_t status = 0;             // bit 7 frame interrupt, bit 6 sprite overflow, bit 5 collision
    bool    line_pending = false;
    uint8_t line_counter = 0xFF;
    uint8_t vscroll_latch = 0;
    bool    pending = false;        // first control byte written
    uint8_t code = 0;
    uint16_t addr = 0;
    uint8_t read_buffer = 0;
    uint8_t hcount_latch = 0;

    size_t state_size() const;
    void   save_state(uint8_t* dst) const;
    void   load_state(const uint8_t* src);

private:
    void update_irq();
    void render_sprites(uint8_t* dst, int ln);
};
