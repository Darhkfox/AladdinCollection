// Master System VDP: ports, interrupts and the mode 4 line renderer (with the widescreen extension).
#include "sms_vdp.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdlib>

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
    line_irq_line = -1;
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
    hfr_forget();
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
        if (r == 8) hscroll_writes++;
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

void SmsVdp::start_frame() {
    vscroll_latch = reg[9];
    line_irq_line = -1;
}

void SmsVdp::line_counter_tick() {
    if (line <= 192) {
        if (line_counter == 0) {
            line_counter = reg[10];
            line_pending = true;
            line_irq_line = line;
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
    else if (bg_alt && pat >= bg_alt_first && pat < bg_alt_first + bg_alt_count)
        p = bg_alt + (pat - bg_alt_first) * 32 + r * 4;
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
    // (the partial cell at the right: from 248 + the fine scroll)
    const int hx1 = hw_right_partial_out && (hs & 7) ? std::min(hw_x1, 248 + (hs & 7)) : hw_x1;
    if (sub_) { x0 += sub_h_; x1 += sub_h_; }   // (an in-between line: sampled part of the way back)
    int key = -0x7FFFFFFF;
    uint8_t row8[8] = {};
    for (int x = x0; x < x1; x++) {
        const int tx = x - hs;
        const int vcol = tx >> 3;                       // (arithmetic shift: floor for the left extension)
        const bool locked = vlock && x >= 192;          // the right 8 columns ignore the vertical scroll
        const bool outside = x < hw_x0 || x >= hx1;
        const int k = vcol * 4 + (locked ? 1 : 0) + (outside ? 2 : 0);
        if (k != key) {
            key = k;
            int yy = locked ? ln : ln + vscroll_latch + (sub_ ? sub_v_ : 0);   // (an in-between line: part of the way back)
            if (yy < 0) yy += 224;
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
        if (sub_) y = (y + spr_sub_dy_[i]) & 0xFF;   // (an in-between line: part of the way back)
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
        if (sub_) {
            x += spr_sub_dx_[i];
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
    // (a module's scroll for the line: reg 8 stands in for it while the line and its in-between lines are drawn,
    // and is the game's again before the CPU runs on)
    const int fix = line_scroll ? line_scroll(hook_host, ln) : -1;
    if (fix < 0) { draw_line(ln); return; }
    const uint8_t r8 = reg[8];
    reg[8] = (uint8_t)fix;
    draw_line(ln);
    reg[8] = r8;
}

void SmsVdp::draw_line(int ln) {
    const int W = 256 + 2 * ext;
    fb_width = W;
    fb_lines = active_lines();
    uint8_t* out = fb[ln];
    line_hscroll[ln] = (uint8_t)hscroll_for(ln);
    line_hscroll_writes[ln] = hscroll_writes;
    const uint8_t backdrop = cram[16 + (reg[7] & 0x0F)];
    const int subs = sub_frames < 1 ? 1 : sub_frames > kMaxSubFrames ? kMaxSubFrames : sub_frames;
    if (ln == 0) {   // a new frame: its line records (the last frame's are the other half), the sprites' moves
        hfr_frame_++;
        if (subs > 1) {
            const int prev = hfr_cur_;
            hfr_cur_ ^= 1;
            hfr_rec_[hfr_cur_] = hfr_frame_;
            // (the width, the name table, the sprite table and size: another is a scene cut)
            hfr_sig_[hfr_cur_] = (uint32_t)ext | (uint32_t)name_table() << 8 | (uint32_t)(reg[5] & 0x7E) << 22 |
                                 (uint32_t)(reg[1] & 3) << 30;
            hfr_prev_ok_ = hfr_rec_[prev] == hfr_frame_ - 1 && hfr_sig_[prev] == hfr_sig_[hfr_cur_];
            sprite_moves();
        }
    }
    if (!display_enabled()) {
        memset(out, backdrop, W);
        if (subs > 1) {
            hfr_ok_[hfr_cur_][ln] = 0;
            for (int k = 1; k < subs; k++) memcpy(sub_fb[k - 1][ln], out, W);
        }
        return;
    }
    uint8_t bg[kMaxWidth], spr[kMaxWidth];
    if (line_shift) bg_shift[ln] = (int16_t)line_shift(hook_host, ln);
    render_bg(bg, ln, -ext + bg_shift[ln], 256 + ext + bg_shift[ln]);
    memset(spr, 0, W);
    const uint8_t status_in = status;
    render_sprites(spr, ln);
    if (!(debug_layers & 1)) memset(bg, 0, W);
    if (!(debug_layers & 2)) memset(spr, 0, W);
    if (line_hook) line_hook(hook_host, ln, bg, spr);
    compose(out, bg, spr, W);
    if (subs > 1) {
        // (the scroll the line was drawn with, as the screen shows it: the view's shift taken off)
        hfr_h_[hfr_cur_][ln] = (int16_t)(hscroll_for(ln) - bg_shift[ln]);
        hfr_v_[hfr_cur_][ln] = vscroll_latch;
        hfr_ok_[hfr_cur_][ln] = 1;
        const uint8_t status_out = status;
        for (int k = 1; k < subs; k++) {
            status = status_in;
            render_sub_line(ln, k, subs);
        }
        status = status_out;   // (the status bits stay the frame's own: the game reads them)
    }
}

// The line's layers composed into colours (sprites over the background unless its priority bit, the blanked column).
void SmsVdp::compose(uint8_t* out, const uint8_t* bg, const uint8_t* spr, int W) {
    for (int i = 0; i < W; i++) {
        const uint8_t b = bg[i], s = spr[i];
        if ((s & 0x0F) && !((b & 0x80) && (b & 0x0F)))
            out[i] = (uint8_t)(cram[16 + (s & 0x0F)] | (mark_sprites ? kSpritePixel : 0));
        else
            out[i] = (b & 0x20) && alt_cram ? alt_cram[b & 0x1F] : cram[b & 0x1F];
    }
    if ((reg[0] & 0x20) && !ignore_left_blank) memset(out + ext, cram[16 + (reg[7] & 0x0F)], 8);
}

// ---------------------------------------------------------------------------
// High frame rate (see sub_frames)
// ---------------------------------------------------------------------------
void SmsVdp::hfr_forget() {
    memset(hfr_ok_, 0, sizeof(hfr_ok_));
    hfr_rec_[0] = hfr_rec_[1] = ~0u;
    hfr_prev_ok_ = false;
    for (int i = 0; i < kSpr; i++) {
        spr_prev_[i].on = spr_prev2_[i].on = false;
        spr_dx_[i] = spr_dy_[i] = 0;
    }
}

// An in-between line (k / subs of the way from the last frame to this one): the background drawn again with its
// scroll part of the way back to the last frame's on this line (render_bg samples it sub_h_ px further, so bg_entry is
// asked for the extension at the place drawn), the sprites part of the way back; the line hook as for the frame's line.
void SmsVdp::render_sub_line(int ln, int k, int subs) {
    const int W = 256 + 2 * ext;
    const int cur = hfr_cur_, prev = cur ^ 1;
    uint8_t* out = sub_fb[k - 1][ln];
    const float back = hfr_prev_ok_ && hfr_ok_[prev][ln] ? 1.0f - (float)k / subs : 0.0f;
    constexpr int kMaxMove = 32;
    int dh = 0, dv = 0;
    if (back > 0) {
        dh = hfr_h_[cur][ln] - hfr_h_[prev][ln];
        dh = ((dh + 128) & 255) - 128;                                  // (the scroll is 8 bits)
        dv = ((hfr_v_[cur][ln] - hfr_v_[prev][ln] + 112 + 224) % 224) - 112;   // (the table's 224 lines)
        if (std::abs(dh) > kMaxMove || std::abs(dv) > kMaxMove) dh = dv = 0;   // (a cut)
    }
    sub_h_ = (int)std::lround(back * dh);   // (scrolled on by dh: the picture back that way)
    sub_v_ = -(int)std::lround(back * dv);
    bool moved = sub_h_ || sub_v_;
    for (int i = 0; i < kSpr; i++) {
        spr_sub_dx_[i] = (int16_t)-std::lround(back * spr_dx_[i]);
        spr_sub_dy_[i] = (int16_t)-std::lround(back * spr_dy_[i]);
        moved = moved || spr_sub_dx_[i] || spr_sub_dy_[i];
    }
    if (!moved) { memcpy(out, fb[ln], W); return; }
    uint8_t bg[kMaxWidth], spr[kMaxWidth];
    sub_ = true;
    sub_back_ = back;
    render_bg(bg, ln, -ext + bg_shift[ln], 256 + ext + bg_shift[ln]);
    memset(spr, 0, W);
    render_sprites(spr, ln);
    if (!(debug_layers & 1)) memset(bg, 0, W);
    if (!(debug_layers & 2)) memset(spr, 0, W);
    if (line_hook) line_hook(hook_host, ln, bg, spr);
    sub_ = false;
    compose(out, bg, spr, W);
}

// The sprites' moves since the last frame (hfr_match: matched by their look). Places as drawn: the module's true X
// (sprite_x) and the view's shift taken off; the table's entries up to its end mark, then the module's extra ones.
void SmsVdp::sprite_moves() {
    if (hfr_prev_ok_) {
        for (int i = 0; i < kSpr; i++) spr_prev2_[i] = spr_prev_[i];
    } else {
        for (int i = 0; i < kSpr; i++) spr_prev_[i].on = spr_prev2_[i].on = false;
    }
    HfrSprite cur[kSpr];
    const uint16_t sat = sat_base();
    const bool tall = (reg[1] & 0x02) != 0, zoom = (reg[1] & 0x01) != 0;
    const int h = (tall ? 16 : 8) * (zoom ? 2 : 1);
    bool end = false;
    for (int i = 0; i < kSpr; i++) {
        HfrSprite& s = cur[i];
        s.on = false;
        int y, x, pat;
        if (i < 64) {
            y = vram[(sat + i) & 0x3FFF];
            if (y == 0xD0) end = true;
            if (end) continue;
            x = vram[(sat + 0x80 + i * 2) & 0x3FFF];
            pat = vram[(sat + 0x81 + i * 2) & 0x3FFF];
        } else {
            if (i - 64 >= extra_count) continue;
            const ExtraSprite& e = extra[i - 64];
            y = e.y;
            x = e.x;
            pat = e.pattern;
        }
        if (reg[6] & 0x04) pat |= 0x100;
        if (tall) pat &= ~1;
        if (i < 64) {
            if (reg[0] & 0x08) x -= 8;
            if (sprite_x) x = sprite_x(hook_host, i, x, y, pat);
        }
        s.x = (int16_t)std::clamp(x - sprite_shift, -8192, 8191);
        s.y = (int16_t)((y + 1) & 0xFF);
        s.w = (uint8_t)(zoom ? 16 : 8);
        s.h = (uint8_t)h;
        s.key = (uint32_t)pat;
        s.look = 0;   // (the sprites share one palette and size)
        s.group = 0;
        s.on = !(s.y >= 192 && s.y + h <= 256) && x > -1024;   // (not parked below the picture, nor hidden)
    }
    hfr_match(cur, spr_prev_, spr_prev2_, kSpr, 256, spr_dx_, spr_dy_);
    for (int i = 0; i < kSpr; i++) spr_prev_[i] = cur[i];
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
    hfr_forget();
    update_irq();
}
