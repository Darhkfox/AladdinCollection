// Master System VDP: ports, interrupts and the mode 4 line renderer (with the widescreen extension).
#include "sms_vdp.h"
#include <cstring>
#include <algorithm>

SmsVdp::SmsVdp() { reset(); }

void SmsVdp::reset() {
    memset(vram, 0, sizeof(vram));
    memset(cram, 0, sizeof(cram));
    memset(fb, 0, sizeof(fb));
    // The registers as the BIOS leaves them (the games start without it).
    static const uint8_t init[16] = {0x36, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0xFF};
    memcpy(reg, init, sizeof(reg));
    status = 0;
    line_pending = false;
    line_counter = 0xFF;
    vscroll_latch = 0;
    pending = false;
    code = 0;
    addr = 0;
    read_buffer = 0;
    hcount_latch = 0;
    line = 0;
    extra_count = 0;
    memset(bg_shift, 0, sizeof(bg_shift));
    sprite_shift = 0;
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------
void SmsVdp::update_irq() {
    const bool on = ((status & 0x80) && (reg[1] & 0x20)) || (line_pending && (reg[0] & 0x10));
    if (irq_changed) irq_changed(host, on);
}

void SmsVdp::write_ctrl(uint8_t v) {
    if (!pending) {
        addr = (uint16_t)((addr & 0x3F00) | v);
        pending = true;
        return;
    }
    pending = false;
    code = v >> 6;
    addr = (uint16_t)(((v & 0x3F) << 8) | (addr & 0xFF));
    if (code == 0) {
        read_buffer = vram[addr];
        addr = (addr + 1) & 0x3FFF;
    } else if (code == 2) {
        const int r = v & 0x0F;
        if (r < 11) reg[r] = (uint8_t)(addr & 0xFF);
        update_irq();
    }
}

void SmsVdp::write_data(uint8_t v) {
    pending = false;
    if (code == 3) cram[addr & 0x1F] = v & 0x3F;
    else vram[addr & 0x3FFF] = v;
    read_buffer = v;
    addr = (addr + 1) & 0x3FFF;
}

uint8_t SmsVdp::read_data() {
    pending = false;
    const uint8_t r = read_buffer;
    read_buffer = vram[addr & 0x3FFF];
    addr = (addr + 1) & 0x3FFF;
    return r;
}

uint8_t SmsVdp::read_status() {
    const uint8_t r = status | 0x1F;
    status = 0;
    line_pending = false;
    pending = false;
    update_irq();
    return r;
}

// NTSC, 192 lines: 0-$DA, then $D5-$FF.
uint8_t SmsVdp::read_vcount() const { return (uint8_t)(line <= 0xDA ? line : line - 6); }

void SmsVdp::latch_hcount(int cycle) {
    // 342 pixels in 228 CPU cycles; the counter counts pixel pairs from the start of the active area.
    const int px = (cycle * 342 / 228 + 342 - 94) % 342;
    hcount_latch = (uint8_t)(px < 0x128 ? px >> 1 : (px + 0xE9 * 2 - 0x128) >> 1);
}

void SmsVdp::start_frame() { vscroll_latch = reg[9]; }

void SmsVdp::line_counter_tick() {
    if (line <= 192) {
        if (line_counter == 0) {
            line_counter = reg[10];
            line_pending = true;
            update_irq();
        } else {
            line_counter--;
        }
    } else {
        line_counter = reg[10];
    }
}

void SmsVdp::frame_irq() {
    status |= 0x80;
    update_irq();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void SmsVdp::pattern_row(uint16_t e, int row, uint8_t* out) const {
    const int pat = e & 0x1FF;
    const bool hflip = (e & 0x200) != 0, vflip = (e & 0x400) != 0;
    const uint8_t pal = (e & 0x800) ? 0x10 : 0;
    const uint8_t pri = (e & 0x1000) ? 0x80 : 0;
    const uint8_t alt = (e & 0x4000) && alt_cram ? 0x20 : 0;   // (line buffer bit 5: alt_cram's colours)
    const int r = vflip ? 7 - (row & 7) : (row & 7);
    const uint8_t* p = &vram[(pat * 32 + r * 4) & 0x3FFF];
    if ((e & 0x8000) && alt_patterns && pat >= alt_first && pat < alt_first + alt_count)
        p = alt_patterns + (pat - alt_first) * 32 + r * 4;
    for (int i = 0; i < 8; i++) {
        const int bit = hflip ? i : 7 - i;
        const uint8_t c = (uint8_t)(((p[0] >> bit) & 1) | (((p[1] >> bit) & 1) << 1) | (((p[2] >> bit) & 1) << 2) |
                                    (((p[3] >> bit) & 1) << 3));
        out[i] = (uint8_t)(c | pal | alt | (c ? pri : 0));
    }
}

void SmsVdp::render_bg(uint8_t* dst, int ln, int x0, int x1) {
    const int hs = hscroll_for(ln);
    const bool vlock = (reg[0] & 0x80) != 0;
    int key = -0x7FFFFFFF;
    uint8_t row8[8] = {};
    for (int x = x0; x < x1; x++) {
        const int tx = x - hs;
        const int vcol = tx >> 3;                       // (arithmetic shift: floor for the left extension)
        const bool locked = vlock && x >= 192;          // the right 8 columns ignore the vertical scroll
        const bool outside = x < hw_x0 || x >= hw_x1;
        const int k = vcol * 4 + (locked ? 1 : 0) + (outside ? 2 : 0);
        if (k != key) {
            key = k;
            const int yy = locked ? ln : ln + vscroll_latch;
            int e = -1;
            if (outside && bg_entry) e = bg_entry(hook_host, ln, vcol, yy >> 3);
            if (e < 0) e = name_entry(vcol & 31, (yy % 224) >> 3) & 0x1FFF;   // (bits 14, 15: alt_cram / alt_patterns, modules only)
            pattern_row((uint16_t)e, yy & 7, row8);
        }
        dst[x - x0] = row8[tx & 7];
    }
}

void SmsVdp::render_sprites(uint8_t* dst, int ln) {
    const uint16_t sat = sat_base();
    const bool tall = (reg[1] & 0x02) != 0, zoom = (reg[1] & 0x01) != 0;
    const int h = (tall ? 16 : 8) * (zoom ? 2 : 1);
    const int lo = -ext, hi = 256 + ext;
    int count = 0;
    // the table's 64 entries (up to a $D0 Y), then the module's extra ones
    const int n_table = 64;
    for (int i = 0; i < n_table + extra_count; i++) {
        int y, x, pat;
        if (i < n_table) {
            y = vram[(sat + i) & 0x3FFF];
            if (y == 0xD0) { i = n_table - 1; continue; }   // end of the list (192-line mode)
            x = vram[(sat + 0x80 + i * 2) & 0x3FFF];
            pat = vram[(sat + 0x81 + i * 2) & 0x3FFF];
        } else {
            const ExtraSprite& e = extra[i - n_table];
            y = e.y;
            x = e.x;
            pat = e.pattern;
        }
        const int dy = (ln - (y + 1)) & 0xFF;
        if (dy >= h) continue;
        if (++count > 8) {
            status |= 0x40;
            if (sprite_limit) break;
        }
        if (reg[6] & 0x04) pat |= 0x100;
        if (tall) pat &= ~1;
        if (i < n_table) {
            if (reg[0] & 0x08) x -= 8;
            if (sprite_x) x = sprite_x(hook_host, i, x, y, pat);
        }
        const int row = zoom ? dy >> 1 : dy;
        const uint8_t* p = &vram[((pat + (row >> 3)) * 32 + (row & 7) * 4) & 0x3FFF];
        const int step = zoom ? 2 : 1;
        for (int px = 0; px < 8; px++) {
            const int bit = 7 - px;
            const uint8_t c = (uint8_t)(((p[0] >> bit) & 1) | (((p[1] >> bit) & 1) << 1) | (((p[2] >> bit) & 1) << 2) |
                                        (((p[3] >> bit) & 1) << 3));
            if (!c) continue;
            for (int k = 0; k < step; k++) {
                const int sx = x + px * step + k - sprite_shift;
                if (sx < lo || sx >= hi) continue;
                uint8_t& d = dst[sx + ext];
                if (d) { status |= 0x20; continue; }    // the first sprite in the list stays in front
                d = (uint8_t)(0x80 | c);
            }
        }
    }
}

void SmsVdp::render_line(int ln) {
    if (ln < 0 || ln >= kMaxLines) return;
    const int W = 256 + 2 * ext;
    fb_width = W;
    fb_lines = active_lines();
    uint8_t* out = fb[ln];
    line_hscroll[ln] = (uint8_t)hscroll_for(ln);
    const uint8_t backdrop = cram[16 + (reg[7] & 0x0F)];
    if (!display_enabled()) {
        memset(out, backdrop, W);
        return;
    }
    uint8_t bg[kMaxWidth], spr[kMaxWidth];
    if (line_shift) bg_shift[ln] = (int16_t)line_shift(hook_host, ln);
    render_bg(bg, ln, -ext + bg_shift[ln], 256 + ext + bg_shift[ln]);
    memset(spr, 0, W);
    render_sprites(spr, ln);
    if (!(debug_layers & 1)) memset(bg, 0, W);
    if (!(debug_layers & 2)) memset(spr, 0, W);
    if (line_hook) line_hook(hook_host, ln, bg, spr);
    for (int i = 0; i < W; i++) {
        const uint8_t b = bg[i], s = spr[i];
        if ((s & 0x0F) && !((b & 0x80) && (b & 0x0F)))
            out[i] = (uint8_t)(cram[16 + (s & 0x0F)] | (mark_sprites ? kSpritePixel : 0));
        else
            out[i] = (b & 0x20) && alt_cram ? alt_cram[b & 0x1F] : cram[b & 0x1F];
    }
    if ((reg[0] & 0x20) && !ignore_left_blank) memset(out + ext, backdrop, 8);
}

// ---------------------------------------------------------------------------
// Save states
// ---------------------------------------------------------------------------
namespace {
struct VdpState {
    uint8_t reg[16], cram[32];
    uint8_t status, line_pending, line_counter, vscroll_latch, pending, code, read_buffer, hcount_latch;
    uint16_t addr;
    int32_t line;
};
}

size_t SmsVdp::state_size() const { return sizeof(VdpState) + sizeof(vram); }

void SmsVdp::save_state(uint8_t* dst) const {
    VdpState s;
    memset(&s, 0, sizeof(s));
    memcpy(s.reg, reg, 16);
    memcpy(s.cram, cram, 32);
    s.status = status;
    s.line_pending = line_pending;
    s.line_counter = line_counter;
    s.vscroll_latch = vscroll_latch;
    s.pending = pending;
    s.code = code;
    s.read_buffer = read_buffer;
    s.hcount_latch = hcount_latch;
    s.addr = addr;
    s.line = line;
    memcpy(dst, &s, sizeof(s));
    memcpy(dst + sizeof(s), vram, sizeof(vram));
}

void SmsVdp::load_state(const uint8_t* src) {
    VdpState s;
    memcpy(&s, src, sizeof(s));
    memcpy(reg, s.reg, 16);
    memcpy(cram, s.cram, 32);
    status = s.status;
    line_pending = s.line_pending != 0;
    line_counter = s.line_counter;
    vscroll_latch = s.vscroll_latch;
    pending = s.pending != 0;
    code = s.code;
    read_buffer = s.read_buffer;
    hcount_latch = s.hcount_latch;
    addr = s.addr;
    line = s.line;
    memcpy(vram, src + sizeof(s), sizeof(vram));
    extra_count = 0;
    update_irq();
}
