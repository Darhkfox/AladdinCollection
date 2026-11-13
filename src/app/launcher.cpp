// Launcher: game cards with live attract-mode previews (or the player's covers), plus Options / Music Player / Credits / Quit.
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "imageload.h"

namespace {
std::string narrow(const std::wstring& w) { return to_utf8(w); }
const int kButtonCount = 5;
#ifdef __ANDROID__
// Android apps don't quit from a menu (Back on the launcher still offers it); ROMs come in through the picker.
const char* kButtons[kButtonCount] = {"Options", "Achievements", "Music Player", "Credits", "Add ROMs"};
#else
const char* kButtons[kButtonCount] = {"Options", "Achievements", "Music Player", "Credits", "Quit"};
#endif

// Shortens a file name to `max_w` by cutting the end of its stem ("Disney's Aladdin (Eur<...>.sms").
std::string elide_name(Video& v, FontId font, const std::string& name, float max_w, float scale) {
    if (v.measure_text(font, name, scale) <= max_w) return name;
    size_t dot = name.find_last_of('.');
    std::string ext = dot != std::string::npos && name.size() - dot <= 5 ? name.substr(dot) : "";
    std::string stem = name.substr(0, name.size() - ext.size());
    while (stem.size() > 4) {
        while (!stem.empty() && ((unsigned char)stem.back() & 0xC0) == 0x80) stem.pop_back();   // a UTF-8 character's tail
        if (!stem.empty()) stem.pop_back();                                                       // and its first byte
        while (!stem.empty() && stem.back() == ' ') stem.pop_back();
        if (v.measure_text(font, stem + "\xE2\x80\xA6" + ext, scale) <= max_w) break;
    }
    return stem + "\xE2\x80\xA6" + ext;
}

// Agrabah by night: an indigo sky warming towards the horizon, twinkling stars, a crescent moon and the palace's
// domes and minarets in silhouette along the bottom. `magic` (the Arabian Nights style): a glow round the moon, glints
// flashing among the stars and gold dust drifting up from the palace, as if from the lamp.
void draw_night(Video& v, float W, float H, float t, bool magic) {
    v.fill_rect_gradient({0, 0, W, H * 0.62f}, 0xFF07081C, 0xFF1A1036);
    v.fill_rect_gradient({0, H * 0.62f, W, H * 0.38f + 1}, 0xFF1A1036, 0xFF3A1A44);
    // stars: fixed pseudo-random places, each with its own twinkle
    uint32_t seed = 0x2F6E2B1u;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    const float s = std::max(1.0f, std::round(H / 540.0f));
    for (int i = 0; i < 140; i++) {
        const float x = rnd() * W, y = rnd() * H * 0.7f, ph = rnd() * 6.2832f, sp = 0.6f + rnd() * 1.6f, big = rnd();
        const float tw = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(t * sp + ph));
        const float a = tw * (1.0f - y / (H * 0.8f));
        const float sz = big > 0.93f ? s * 2 : s;
        v.fill_rect({std::floor(x), std::floor(y), sz, sz}, with_alpha(big > 0.8f ? 0xFFFFF2C8 : 0xFFDDE4FF, a));
    }
    // crescent moon (a gold disc with the sky's colour over most of it)
    const float mr = H * 0.055f, mx = W * 0.88f, my = H * 0.13f;
    v.fill_round_rect({mx - mr, my - mr, mr * 2, mr * 2}, mr, 0xFFF2D27A);
    v.fill_round_rect({mx - mr * 0.62f, my - mr * 1.12f, mr * 2, mr * 2}, mr, 0xFF0A0A21);
    if (magic) {
        // a soft glow round the moon (over it: the dark side catches a little of it, like earthshine)
        draw_ornament(v, Ornament::Glow, mx, my, mr * 6.0f, 0.22f);
        // glints: now and then one of them flashes, turning slowly
        for (int i = 0; i < 14; i++) {
            const float x = rnd() * W, y = (0.04f + rnd() * 0.5f) * H, ph = rnd() * 6.2832f, sp = 0.35f + rnd() * 0.5f;
            const float f = std::pow(std::max(0.0f, std::sin(t * sp + ph)), 6.0f);
            if (f > 0.02f && std::fabs(x - mx) + std::fabs(y - my) > mr * 2.5f)
                draw_ornament(v, Ornament::Sparkle, x, y, H * (0.018f + 0.022f * rnd()) * (0.4f + 0.6f * f), f);
            else
                rnd();
        }
    }
    // the palace: domes on towers, minarets, a long wall
    const uint32_t sil = 0xFF120A20;
    const float base = H;
    auto dome = [&](float cx, float w, float top) {
        v.fill_rect({cx - w * 0.5f, top + w * 0.45f, w, base - top}, sil);
        v.fill_round_rect({cx - w * 0.5f, top, w, w * 0.9f}, w * 0.45f, sil);
        v.fill_rect({cx - w * 0.04f, top - w * 0.28f, w * 0.08f, w * 0.3f}, sil);
    };
    auto minaret = [&](float cx, float w, float top) {
        v.fill_rect({cx - w * 0.5f, top, w, base - top}, sil);
        v.fill_round_rect({cx - w * 0.75f, top - w * 1.3f, w * 1.5f, w * 1.5f}, w * 0.75f, sil);
        v.fill_rect({cx - w * 0.05f, top - w * 2.0f, w * 0.1f, w * 0.8f}, sil);
    };
    v.fill_rect({0, H * 0.94f, W, H * 0.06f + 1}, sil);
    dome(W * 0.5f, H * 0.16f, H * 0.80f);
    dome(W * 0.40f, H * 0.09f, H * 0.86f);
    dome(W * 0.60f, H * 0.09f, H * 0.86f);
    minaret(W * 0.31f, H * 0.022f, H * 0.80f);
    minaret(W * 0.69f, H * 0.022f, H * 0.80f);
    dome(W * 0.12f, H * 0.07f, H * 0.89f);
    dome(W * 0.86f, H * 0.075f, H * 0.885f);
    minaret(W * 0.05f, H * 0.018f, H * 0.86f);
    minaret(W * 0.95f, H * 0.018f, H * 0.85f);
    if (magic) {
        // gold dust drifting up from the palace, swaying, fading as it rises
        for (int i = 0; i < 36; i++) {
            const float x0 = rnd() * W, sp = 0.025f + rnd() * 0.035f, ph = rnd(), sway = rnd() * 6.2832f, sz = rnd();
            const float k = std::fmod(t * sp + ph, 1.0f);   // 0 at the palace .. 1 high in the sky
            const float y = H * (0.93f - 0.62f * k);
            const float x = x0 + std::sin(t * 0.7f + sway + k * 5.0f) * H * 0.03f;
            const float a = std::min(1.0f, k * 6.0f) * (1.0f - k) * (0.55f + 0.45f * std::sin(t * 3.0f + sway));
            if (sz > 0.85f) {
                draw_ornament(v, Ornament::Sparkle, x, y, H * 0.022f, a);
            } else {
                const float r = H * (0.0014f + 0.0022f * sz);
                v.fill_round_rect({x - r, y - r, r * 2, r * 2}, r, with_alpha(0xFFFFD877, a));
            }
        }
    }
}
}

void App::update_launcher(const UiInput& ui, double dt) {
    const int N = kGameCount;          // cards 0..N-1, buttons N..N+4
    poll_roms(dt);
    int& s = launcher_sel_;
    if (s < N) launcher_last_card_ = s;
    if (ui.left && s != 0 && s != N) s--;
    if (ui.right && s != N - 1 && s != N + kButtonCount - 1) s++;
    if (ui.down && s < N) s = N + 2;
    if (ui.up && s >= N) s = launcher_last_card_;
    bool go = ui.confirm;
    if (ui.tap && over_update_badge(ui.tap_x, ui.tap_y)) open_update_page();
    else if (ui.tap)   // touch: a card or button is chosen and used at once
        for (int i = 0; i < N + kButtonCount; i++) {
            const Rect& r = launcher_hit_[i];
            if (ui.tap_x >= r.x && ui.tap_x < r.x + r.w && ui.tap_y >= r.y && ui.tap_y < r.y + r.h) { s = i; go = true; }
        }
    if (s < N) launcher_last_card_ = s;
    if (go) {
        if (s < N) launch(s);
        else if (s == N) open_options_menu(true);
        else if (s == N + 1) menu_.open(achievements_page(-1));
        else if (s == N + 2) {
            int g = -1;
            for (int k = 0; k < N && g < 0; k++) {
                int c = (launcher_last_card_ + k) % N;
                if (games_[c].rom.found) g = c;
            }
            if (g >= 0) open_jukebox(g, false);
            else menu_.toast("No ROMs found");
        } else if (s == N + 3) open_credits(true);
#ifdef __ANDROID__
        else pick_roms();
#else
        else running_ = false;
#endif
    }
    if (ui.menu || ui.back) menu_.confirm("Quit?", [this]() { running_ = false; });
    for (int i = 0; i < N + kButtonCount; i++) {
        float target = i == s ? 1.0f : 0.0f;
        launcher_anim_[i] += (target - launcher_anim_[i]) * (float)std::min(1.0, dt * 12.0);
    }
}

// ---------------------------------------------------------------------------
// The player's pictures: covers (covers/<game id>) and the background (covers/background), PNG or JPEG
// ---------------------------------------------------------------------------
std::wstring App::covers_dir() const { return path_join(data_dir(), L"covers"); }

bool App::has_cover(int g) const {
    return art_covers_ && g >= 0 && g < kGameCount && cover_art_[g].w > 0;
}

// Reads the pictures the settings ask for (again if `reload`, or when the settings changed), and starts or
// stops the attract-mode previews to match: a card with a cover has none. A missing or unreadable file is simply
// not used (that card keeps its preview; the launcher its night sky).
void App::load_launcher_art(bool reload) {
    const bool covers = settings_.launcher_covers, bg = settings_.launcher_background;
    if (!reload && art_loaded_ && covers == art_covers_ && bg == art_bg_) return;
    art_loaded_ = true;
    auto drop = [this](Art& a) {
        if (a.image) video_.free_image(a.image);
        a = Art();
    };
    auto read = [&](Art& a, const std::string& name, int max_side) {
        drop(a);
        for (const wchar_t* ext : {L".png", L".jpg", L".jpeg", L".PNG", L".JPG", L".JPEG"}) {
            const std::wstring path = path_join(covers_dir(), from_utf8(name) + ext);
            if (load_image_file(path, max_side, a.px, &a.w, &a.h)) return;
        }
        a = Art();
    };
    art_covers_ = covers;
    art_bg_ = bg;
    for (int g = 0; g < kGameCount; g++) {
        if (covers) read(cover_art_[g], games_[g].module->id(), 1024);
        else drop(cover_art_[g]);
    }
    if (bg) read(bg_art_, "background", 2048);
    else drop(bg_art_);
    ensure_previews();
}

int App::art_image(Art& a) {
    if (!a.image && a.w > 0) {
        a.image = video_.create_image(a.px.data(), a.w, a.h);
        if (a.image) a.px = std::vector<uint32_t>();   // (the video layer keeps its own copy)
    }
    return a.image;
}

void App::draw_launcher() {
    const float W = (float)video_.width(), H = (float)video_.height();
    const int N = kGameCount;
    uint32_t acc = accent();
    const bool ar = ui_style() == kStyleArabian;
    const bool custom_bg = art_bg_ && bg_art_.w > 0 && art_image(bg_art_);
    // (over the player's own picture, text gets a dark outline: it reads over anything)
    auto label = [&](FontId f, float x, float y, const std::string& s, uint32_t c, Align al, float sc = 1.0f) {
        if (custom_bg) draw_text_outlined(video_, f, x, y, s, c, 0xD0000008, al, sc);
        else video_.draw_text(f, x, y, s, c, al, sc);
    };

    // Background: the player's picture filling the window (its middle; not stretched), darkened a little and
    // shaded at the top and the bottom, where the title and the buttons are; else Agrabah at night.
    if (custom_bg) {
        const float s = std::max(W / bg_art_.w, H / bg_art_.h), vw = W / s, vh = H / s;
        video_.fill_rect({0, 0, W, H}, 0xFF000000);
        video_.draw_image(bg_art_.image, {0, 0, W, H}, {(bg_art_.w - vw) * 0.5f, (bg_art_.h - vh) * 0.5f, vw, vh});
        video_.fill_rect({0, 0, W, H}, 0x48000008);
        video_.fill_rect_gradient({0, 0, W, H * 0.25f}, 0xA0000008, 0x00000008);
        video_.fill_rect_gradient({0, H * 0.72f, W, H * 0.28f}, 0x00000008, 0xB0000008);
    } else {
        draw_night(video_, W, H, (float)time_, ar);
    }

    // Title
    float ty0 = H * 0.035f;
    const char* kTitle = "Aladdin Collection";
    const float title_w = std::max(1.0f, video_.measure_text(FontId::Huge, kTitle));
    float tscale = std::min(1.0f, W * 0.86f / title_w);
    tscale = std::min(tscale, 0.8f);
    if (ar) {   // gold, outlined in ink, with a shadow
        const float o = std::max(2.0f, H / 260.0f);
        video_.draw_text(FontId::Huge, W * 0.5f + o * 1.2f, ty0 + o * 2.2f, kTitle, 0xB0000000, Align::Center, tscale);
        draw_text_outlined(video_, FontId::Huge, W * 0.5f, ty0, kTitle, kGold, kInk, Align::Center, tscale, o);
    } else if (custom_bg) {
        label(FontId::Huge, W * 0.5f, ty0, kTitle, 0xFFF2C14E, Align::Center, tscale);
    } else {
        video_.draw_text(FontId::Huge, W * 0.5f + H * 0.004f, ty0 + H * 0.005f, kTitle, 0xC0000000, Align::Center, tscale);
        video_.draw_text(FontId::Huge, W * 0.5f, ty0, kTitle, 0xFFF2C14E, Align::Center, tscale);
    }
    draw_update_badge(W * 0.5f + title_w * tscale * 0.5f);   // (a newer release)
    ty0 += video_.line_height(FontId::Huge, tscale);
#ifdef __ANDROID__
    const char* edition = "Android Edition";
#else
    const char* edition = "PC Edition";
#endif
    const std::string subtitle = std::string("Mega Drive  \xE2\x80\xA2  Super Nintendo  \xE2\x80\xA2  Master System  \xE2\x80\xA2  ") +
                                 edition + "  \xE2\x80\xA2  v" + kAppVersion;
    const float sub_s = std::min(1.0f, W * 0.96f / std::max(1.0f, video_.measure_text(FontId::Body, subtitle)));
    label(FontId::Body, W * 0.5f, ty0, subtitle, ar ? 0xFFD3C7EC : 0xFFB4A9CC, Align::Center, sub_s);
    ty0 += video_.line_height(FontId::Body);
    if (ar) {
        // an arabesque rule: gold lines ending in beads, diamonds either side of an eight-pointed star
        const float cy = ty0 + H * 0.012f, half = std::min(W * 0.3f, title_w * tscale * 0.5f), ss = H * 0.03f, dh = H * 0.022f;
        const float lw = std::max(1.0f, H / 540.0f), bead = std::max(1.5f, H * 0.0035f);
        const float in = ss * 0.5f + dh * 1.4f;
        for (int side = -1; side <= 1; side += 2) {
            const float x0 = W * 0.5f + side * in, x1 = W * 0.5f + side * half;
            video_.fill_rect({std::min(x0, x1), cy - lw * 0.5f, std::fabs(x1 - x0), lw}, with_alpha(kGold, 0.8f));
            video_.fill_round_rect({x1 - bead, cy - bead, bead * 2, bead * 2}, bead, kGold);
            draw_ornament(video_, Ornament::Diamond, W * 0.5f + side * (ss * 0.5f + dh * 0.55f), cy, dh);
        }
        draw_ornament(video_, Ornament::Star8, W * 0.5f, cy, ss);
        ty0 += H * 0.026f;
    }

    // Cards: the frames are the console's screen's shape, or square when there are covers (box art fills them better)
    bool any_cover = false;
    for (int i = 0; i < N; i++) any_cover = any_cover || (has_cover(i) && games_[i].rom.found);
    const float aspect = any_cover ? 1.0f : 4.0f / 3.0f;
    const float top = ty0 + H * 0.045f;
    float gap = W * 0.03f;
    // The text under the preview is sized by the window height (fonts), the preview by the card width: give the
    // text the room its lines need and fit the card width to what is left (narrow 4:3 windows).
    const float rad0 = H * 0.02f;
    const float info_h = rad0 * 0.7f + video_.line_height(FontId::Title) * 0.95f +
                         video_.line_height(FontId::Small) * (1.05f + 1.1f + 1.0f) + rad0 * 0.6f;
    float cw = std::min((W * 0.94f - gap * (N - 1)) / N, (H * 0.82f - top - info_h - rad0 * 0.6f) * aspect + rad0 * 1.2f);
    float ph = rad0 * 0.6f + (cw - rad0 * 1.2f) / aspect;
    float ch = ph + info_h;
    float x0 = (W - (cw * N + gap * (N - 1))) * 0.5f;
    const float by = H * 0.855f;
    const float card_top = top + std::max(0.0f, (by - H * 0.03f - top - ch) * 0.5f);   // (centred above the buttons)
    // ROM file names share one text size (the one the longest name needs)
    std::string rom_label[kGameCount];
    float rom_fs = 1.0f;
    for (int i = 0; i < N; i++) {
        if (!games_[i].rom.found) continue;
        const std::wstring& path = games_[i].rom.path;
        rom_label[i] = "\xE2\x9C\x93 " + narrow(path.substr(path.find_last_of(L"\\/") + 1));
        rom_fs = std::min(rom_fs, cw * 0.92f / std::max(1.0f, video_.measure_text(FontId::Small, rom_label[i])));
    }
    const uint32_t c_frame_bg = ar ? 0xFF0A0620 : 0xFF0A0D14;
    for (int i = 0; i < N; i++) {
        GameSlot& g = games_[i];
        float an = launcher_anim_[i];
        float sc = 1.0f + 0.035f * an;
        float cx = x0 + i * (cw + gap) + cw * 0.5f, cy = card_top + ch * 0.5f;
        Rect r = {cx - cw * sc * 0.5f, cy - ch * sc * 0.5f, cw * sc, ch * sc};
        launcher_hit_[i] = r;
        uint32_t c = g.module->accent_color();
        float rad = H * 0.02f;
        if (an > 0.01f) {
            float glow = H * 0.012f * an;
            video_.fill_round_rect({r.x - glow, r.y - glow, r.w + glow * 2, r.h + glow * 2}, rad + glow, with_alpha(c, 0.25f * an));
        }
        video_.fill_round_rect(r, rad, ar ? kIndigo : 0xF0140F24);
        Rect pr = {r.x + rad * 0.6f, r.y + rad * 0.6f, r.w - rad * 1.2f, (r.w - rad * 1.2f) / aspect};
        if (has_cover(i) && g.rom.found && art_image(cover_art_[i])) {
            // (until its ROM is found, a card says what's missing instead)
            // The player's cover, whole (the dark backdrop either side) or filling its frame (trimmed; a tall
            // cover keeps more of its top, where box art has its logo); dimmed when not chosen.
            Art& a = cover_art_[i];
            video_.fill_round_rect(pr, rad * 0.5f, c_frame_bg);
            if (settings_.cover_fill) {
                const float s = std::max(pr.w / a.w, pr.h / a.h), vw = pr.w / s, vh = pr.h / s;
                video_.draw_image(a.image, pr, {(a.w - vw) * 0.5f, (a.h - vh) * 0.25f, vw, vh});
            } else {
                const float s = std::min(pr.w / a.w, pr.h / a.h), dw = a.w * s, dh = a.h * s;
                video_.draw_image(a.image, {pr.x + (pr.w - dw) * 0.5f, pr.y + (pr.h - dh) * 0.5f, dw, dh});
            }
            if (an < 0.99f) video_.fill_rect(pr, with_alpha(0xFF000000, 0.35f * (1.0f - an)));
        } else if (g.preview) {
            // (in a square frame beside covers: the picture its own shape, centred)
            Rect ip = pr;
            video_.fill_round_rect(pr, rad * 0.4f, any_cover ? c_frame_bg : 0xFF000000);
            if (any_cover) {
                ip.h = pr.w * 3.0f / 4.0f;
                ip.y = pr.y + (pr.h - ip.h) * 0.5f;
            }
            draw_game_view(1 + i, *g.preview, ip, 1.0f, 0.35f * (1.0f - an));
        } else {
            video_.fill_round_rect(pr, rad * 0.5f, c_frame_bg);
            // What is missing (or which file is the wrong version, or a zip with the ROM that can't be unpacked), shrunk
            // to fit the preview area.
            const bool other = !g.rom.other_file.empty();
            const std::string head = !other ? "ROM not found" : g.rom.other_zip ? "Can't unzip the ROM" : "Wrong ROM version";
            char crc[64];
            snprintf(crc, sizeof(crc), "(CRC32 %08X, %u KB%s).", g.module->rom_crc32(), g.module->rom_size() / 1024,
                     other ? "" : "; zipped is fine");
            std::vector<std::string> lines;
            if (other && g.rom.other_zip) {
                lines.push_back("holds the ROM, but it can't be unpacked");
                lines.push_back("(" + g.rom.other_desc + "). Unzip it, or zip it");
                lines.push_back("again with the usual Deflate compression");
            } else if (other) {
                lines.push_back("is " + g.rom.other_desc + ".");
                lines.push_back(std::string("Needs ") + g.module->rom_hint());
            } else {
#ifdef __ANDROID__
                lines.push_back("Tap Add ROMs and choose");
                lines.push_back(g.module->rom_hint());
#else
                lines.push_back(std::string("Place ") + g.module->rom_hint());
                lines.push_back("in the game folder or a \"roms\" folder");
#endif
            }
            lines.push_back(crc);
            float bw = 1.0f;
            for (const auto& l : lines) bw = std::max(bw, video_.measure_text(FontId::Small, l));
            if (other) {
                const std::wstring& f = g.rom.other_file;
                lines.insert(lines.begin(), elide_name(video_, FontId::Small, narrow(f.substr(f.find_last_of(L"\\/") + 1)), bw, 1.0f));
            }
            float hs = std::min(1.0f, pr.w * 0.9f / std::max(1.0f, video_.measure_text(FontId::Title, head)));
            float bs = std::min(1.0f, pr.w * 0.9f / bw);
            float th = video_.line_height(FontId::Title, hs) * 1.15f + video_.line_height(FontId::Small, bs) * lines.size();
            const float vs = std::min(1.0f, pr.h * 0.9f / th);
            hs *= vs;
            bs *= vs;
            float y = pr.y + (pr.h - th * vs) * 0.5f;
            video_.draw_text(FontId::Title, pr.x + pr.w * 0.5f, y, head, other ? 0xFFF0B060 : 0xFFE06A6A, Align::Center, hs);
            y += video_.line_height(FontId::Title, hs) * 1.15f;
            for (const auto& l : lines) {
                video_.draw_text(FontId::Small, pr.x + pr.w * 0.5f, y, l, ar ? 0xFFC4B8DC : 0xFFB0B8C8, Align::Center, bs);
                y += video_.line_height(FontId::Small, bs);
            }
        }
        float ty = pr.y + pr.h + rad * 0.7f;
        // the three cards are the same game: each is named by its console
        const std::string head = system_name(g.module->system());
        float tfs = std::min(1.0f, r.w * 0.94f / std::max(1.0f, video_.measure_text(FontId::Title, head)));
        video_.draw_text(FontId::Title, r.x + r.w * 0.5f, ty, head, an > 0.5f ? c : (ar ? kCream : 0xFFE6E9F0), Align::Center, tfs);
        ty += video_.line_height(FontId::Title, tfs) * 0.95f;
        std::string sub = std::string(g.module->title()) + "  \xE2\x80\xA2  " + g.module->subtitle();
        float sfs = std::min(1.0f, r.w * 0.94f / std::max(1.0f, video_.measure_text(FontId::Small, sub)));
        video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, sub, ar ? 0xFFCABDE6 : 0xFF9AA4BA, Align::Center, sfs);
        ty += video_.line_height(FontId::Small) * 1.05f;
        std::string meta = std::string(g.module->year()) + "  \xE2\x80\xA2  " +
                           (g.module->max_players() > 1 ? "1-2 Players" : "1 Player");
        video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, meta,
                         g.module->max_players() > 1 ? lerp_color(c, 0xFFFFFFFF, 0.3f) : (ar ? 0xFFA898C8 : 0xFF7C8598), Align::Center);
        ty += video_.line_height(FontId::Small) * 1.1f;
        if (g.rom.found) video_.draw_text(FontId::Small, r.x + r.w * 0.5f, ty, rom_label[i], 0xFF6FCF8A, Align::Center, rom_fs);
        if (ar)   // the gold arabesque frame: bright on the chosen card
            draw_arabesque_frame(video_, r, rad, 1.0f, an);
        else
            video_.outline_round_rect(r, rad, std::max(1.5f, H / 400.0f) * (0.5f + an), with_alpha(an > 0.01f ? c : 0xFF2A3040, 0.6f + 0.4f * an));
    }

    // Buttons
    float bh = video_.line_height(FontId::Body) * 1.6f;
    float bws[kButtonCount], total = 0;
    for (int i = 0; i < kButtonCount; i++) { bws[i] = video_.measure_text(FontId::Body, kButtons[i]) + bh * 1.4f; total += bws[i]; }
    float bgap = W * 0.02f;
    // the row shrinks to fit narrow (4:3, portrait) windows
    const float bs = std::min(1.0f, W * 0.96f / (total + bgap * (kButtonCount - 1)));
    for (float& w : bws) w *= bs;
    total *= bs;
    bgap *= bs;
    float bx = (W - total - bgap * (kButtonCount - 1)) * 0.5f;
    for (int i = 0; i < kButtonCount; i++) {
        float an = launcher_anim_[N + i];
        Rect br = {bx, by + bh * (1.0f - bs) * 0.5f, bws[i], bh * bs};
        launcher_hit_[N + i] = br;
        if (ar) {   // indigo plaques with gold trim; the chosen one lit gold
            video_.fill_round_rect({br.x, br.y + br.h * 0.08f, br.w, br.h}, br.h * 0.5f, 0x80000000);
            video_.fill_round_rect(br, br.h * 0.5f, lerp_color(0xF22A1A56, kGold, an));
            video_.outline_round_rect(br, br.h * 0.5f, std::max(1.0f, H / 540.0f), with_alpha(kGold, 0.55f + 0.45f * an));
        } else {
            video_.fill_round_rect(br, br.h * 0.5f, lerp_color(0xF0241A38, acc, an * 0.85f));
        }
        video_.draw_text(FontId::Body, br.x + br.w * 0.5f, br.y + (br.h - video_.line_height(FontId::Body, bs)) * 0.5f, kButtons[i],
                         an > 0.5f ? (ar ? kInk : 0xFF10131A) : (ar ? kCream : 0xFFD8DDE8), Align::Center, bs);
        bx += bws[i] + bgap;
    }
#ifdef __ANDROID__
    const char* hint = touch_active_ ? "Tap a game to play    Add ROMs to choose your ROM files"
                                     : "\xE2\x86\x90\xE2\x86\x92 Choose    (A) Play    Back Quit";
#else
    const char* hint = "\xE2\x86\x90\xE2\x86\x92 Choose    Enter / (A) Play    F11 Fullscreen    Esc Quit";
#endif
    label(FontId::Small, W * 0.5f, H - video_.line_height(FontId::Small) * 1.6f, hint,
          with_alpha(ar ? 0xFFD8C49A : 0xFF6C7488, 1.0f - menu_.toast_cover()), Align::Center);
}
