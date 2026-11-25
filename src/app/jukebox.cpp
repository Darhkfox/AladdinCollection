// Music player / sound test: a dedicated emulator instance runs the game's own sound driver. The Music Player can
// also play a soundtrack pack's arranged tracks (soundtrack.h) instead: an MD+ pack's directly, an MSU-1 pack's through
// the game's own requests on the ROM patched for it (its patch picks the track and mutes the console's music).
#include "app.h"
#include "../core/md.h"
#include "../core/msu1.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
// The CD track an MD+ pack plays for each of the game's tunes, read from its patched ROM: on a scratch machine the
// game's own music request runs for every music ID (GameModule::music_request), and the patch's commands to the CD
// overlay say which track it plays and whether it loops. Worked out once per pack.
const std::map<int, std::pair<int, bool>>& soundtrack_tracks(GameModule* module, const std::vector<uint8_t>& rom,
                                                             const SoundtrackPack& pack) {
    static std::map<std::wstring, std::map<int, std::pair<int, bool>>> cache;
    const std::wstring key = pack.dir + L"|" + pack.patch + L"|" + pack.rom;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::map<int, std::pair<int, bool>>& out = cache[key];
    std::vector<uint8_t> patched;
    std::string err;
    if (!soundtrack_rom(pack, rom, patched, err)) return out;
    if (pack.kind == SoundtrackKind::Msu1) {
        // MSU-1: each tune asked for through the music player's own request (the game's song upload) on the patched
        // ROM with the pack's chip; the track it loads (not missing) and whether it repeats, once it plays
        std::unique_ptr<Session> s = Session::create(module);
        if (!s || !s->open(module, patched) || !s->attach_soundtrack(pack, err)) return out;
        Settings js;
        js.widescreen = WideMode::Off;
        s->apply_settings(js);
        s->muted = true;
        s->set_headless(true);
        s->jukebox_boot();
        for (const SoundEntry& e : module->sounds()) {
            if (!e.music) continue;
            s->play_sound(e.id);
            Msu1* m = s->msu();
            for (int f = 0; f < 60; f++) {   // (the song played ahead of some, then the song's upload)
                s->jukebox_frame();
                s->run_frame(0, 0);
            }
            if (m && m->playing() && !m->missing()) out[e.id] = {m->track(), m->repeating()};
            s->stop_sound();
            if (m) m->stop();
            for (int f = 0; f < 4; f++) { s->jukebox_frame(); s->run_frame(0, 0); }
        }
        return out;
    }
    if (module->system() != System::MegaDrive) return out;   // (MD+ packs are for Mega Drive games)
    MdModule* mm = static_cast<MdModule*>(module);
    std::unique_ptr<MegaDrive> md(new MegaDrive);
    if (!md->load_rom(patched)) return out;
    mm->jukebox_boot(*md);
    bool unlocked = false;
    int cmd = 0;
    md->on_cart_write = [&](MegaDrive&, uint32_t a, uint16_t v, int size) {
        if (size != 2) return;
        if (a == 0x3F7FA) unlocked = v == 0xCD54;
        else if (a == 0x3F7FE && unlocked && ((v >> 8) == 0x11 || (v >> 8) == 0x12)) cmd = v;   // play once / looped
    };
    for (const SoundEntry& e : module->sounds()) {
        if (!e.music) continue;
        cmd = 0;
        if (!mm->music_request(*md, e.id)) break;
        if (cmd & 0xFF) out[e.id] = {cmd & 0xFF, (cmd >> 8) == 0x12};
    }
    return out;
}

std::string clock_text(double s) {
    const int t = std::max(0, (int)s);
    char b[16];
    snprintf(b, sizeof(b), "%d:%02d", t / 60, t % 60);
    return b;
}
}  // namespace

bool Jukebox::open(GameModule* module, const std::vector<uint8_t>& rom, const Settings& s, bool sfx,
                   const std::vector<SoundtrackPack>& packs) {
    close();
    module_ = module;
    sfx_ = sfx;
    session_ = Session::create(module);
    if (!session_) return false;
    session_->set_audio_rate(48000);
    if (!session_->open(module, rom)) { session_.reset(); return false; }
    rom_ = &rom;
    js_ = s;
    js_.widescreen = WideMode::Off;
    js_.cheats.clear();
    session_->apply_settings(js_);
    session_->jukebox_boot();
    session_rom_ = 0;
    tunes_.clear();
    for (const SoundEntry& e : module->sounds())
        if (sfx || e.music) tunes_.push_back(e);
    list_ = tunes_;
    sel_ = 0;
    playing_ = -1;
    elapsed_ = 0;
    scroll_ = 0;
    free_scroll_ = false;
    paused_ = false;
    close_requested_ = false;
    switch_dir_ = 0;
    versions_.assign(1, Version());
    ver_ = 0;
    cd_ = false;
    if (!sfx) {
        const auto chosen = s.soundtrack.find(module->id());
        for (const SoundtrackPack& p : packs) {
            if (p.game_id != module->id()) continue;
            Version v;
            v.pack = p;
            v.tracks = soundtrack_tracks(module, rom, p);
            if (v.tracks.empty()) continue;
            for (const auto& t : soundtrack_track_names(p)) {
                bool named = false;
                for (const auto& kv : v.tracks) named |= kv.second.first == t.first;
                if (!named) v.more.push_back({-t.first, t.second, true});
            }
            versions_.push_back(v);
            if (chosen != s.soundtrack.end() && chosen->second == p.name) set_version((int)versions_.size() - 1);
        }
    }
    active_ = true;
    return true;
}

bool Jukebox::has_version(int i) const {
    return ver_ > 0 && i >= 0 && i < (int)list_.size() && (list_[i].id < 0 || versions_[ver_].tracks.count(list_[i].id) != 0);
}

void Jukebox::start(int i) {
    if (Msu1* m = session_->msu()) {
        // MSU-1: a tune through the game's own request (the patch plays its track, or the original if it has none);
        // a track no tune names, directly (the driver's music stopped)
        if (playing_ >= 0) session_->stop_sound();
        m->stop();
        if (list_[i].id < 0) cd_ = m->play(-list_[i].id, true);
        else session_->play_sound(list_[i].id);
        if (list_[i].id >= 0) cd_ = has_version(i) && list_[i].music;
        cd_len_ = cd_ ? m->trackSeconds(list_[i].id < 0 ? -list_[i].id : versions_[ver_].tracks.at(list_[i].id).first) : 0.0;
        cd_heard_ = list_[i].id < 0;
        playing_ = i;
        elapsed_ = 0;
        silent_frames_ = 0;
        heard_ = false;
        return;
    }
    MdPlus* cd = session_->soundtrack();
    const bool arranged = has_version(i) && list_[i].music;
    const std::pair<int, bool> t = !arranged ? std::pair<int, bool>(0, false)
                                   : list_[i].id < 0 ? std::pair<int, bool>(-list_[i].id, true)
                                   : versions_[ver_].tracks.at(list_[i].id);
    if (arranged && cd && cd->play_track(t.first, t.second)) {
        if (playing_ >= 0 && !cd_) session_->stop_sound();   // (the driver's tune would play on)
        cd_ = true;
        cd_len_ = cd->track_seconds(t.first);
    } else {
        if (cd) cd->stop();
        cd_ = false;
        session_->play_sound(list_[i].id);   // a new track replaces the old one
    }
    playing_ = i;
    elapsed_ = 0;
    silent_frames_ = 0;
    heard_ = false;
}

void Jukebox::stop_playing() {
    if (Msu1* m = session_->msu()) {
        session_->stop_sound();
        m->stop();
    } else if (!cd_) session_->stop_sound();
    else if (MdPlus* cd = session_->soundtrack()) cd->stop();
    cd_ = false;
    playing_ = -1;
}

// Another version of the music; what was playing carries on in it, from the start (a track of the old version's
// own stops).
void Jukebox::set_version(int v) {
    if (v < 0 || v >= (int)versions_.size()) return;
    const int was = playing_ >= 0 ? list_[playing_].id : 0, sel = list_.empty() ? 0 : list_[sel_].id;
    if (playing_ >= 0) stop_playing();
    ver_ = v;
    std::string err;
    // An MSU-1 version runs the ROM patched for its pack; the others the game's own (MD+: the pack plays directly)
    const int want = ver_ > 0 && versions_[ver_].pack.kind == SoundtrackKind::Msu1 ? ver_ : 0;
    if (want != session_rom_ && rom_) {
        std::vector<uint8_t> patched;
        const bool msu = want > 0 && soundtrack_rom(versions_[want].pack, *rom_, patched, err);
        if (!session_->open(module_, msu ? patched : *rom_)) { close(); return; }
        session_->apply_settings(js_);
        if (msu && !session_->attach_soundtrack(versions_[want].pack, err)) {
            session_->open(module_, *rom_);
            session_->apply_settings(js_);
            ver_ = 0;
        }
        session_->jukebox_boot();
        session_rom_ = session_->has_soundtrack() ? want : 0;
    } else if (ver_ > 0 && !session_->attach_soundtrack(versions_[ver_].pack, err)) {
        ver_ = 0;
    }
    list_ = tunes_;
    list_.insert(list_.end(), versions_[ver_].more.begin(), versions_[ver_].more.end());
    sel_ = std::min(sel_, std::max(0, (int)list_.size() - 1));
    for (int i = 0; i < (int)list_.size(); i++) {
        if (list_[i].id == sel) sel_ = i;
        if (was && list_[i].id == was) start(i);
    }
}

void Jukebox::close() {
    active_ = false;
    session_.reset();
    close_requested_ = false;
}

void Jukebox::update(const UiInput& in) {
    if (!active_) return;
    int n = (int)list_.size();
    if (in.menu || in.back) { close_requested_ = true; return; }
    if (n == 0) return;
    // touch: dragging scrolls the list with the finger, a tap on a track plays it (or stops it, if it is
    // playing), a tap on the game switch moves to the next game, a tap on the title area goes back
    UiInput t = in;
    if (in.drag != 0 && row_ > 0) {
        scroll_ -= in.drag / row_;
        free_scroll_ = true;
    }
    if (in.wheel != 0) {   // (the mouse wheel: three rows a notch)
        scroll_ += in.wheel * 3;
        free_scroll_ = true;
    }
    if (in.up || in.down || in.page_left || in.page_right) free_scroll_ = false;
    if (in.tap) {
        auto inside = [&](const Rect& r) { return in.tap_x >= r.x && in.tap_x < r.x + r.w && in.tap_y >= r.y && in.tap_y < r.y + r.h; };
        if (!sfx_ && inside(switch_rect_)) { switch_dir_ = 1; return; }
        if (versions_.size() > 1 && inside(version_rect_)) { set_version((ver_ + 1) % (int)versions_.size()); return; }
        if (in.tap_y < list_rect_.y) { close_requested_ = true; return; }
        if (inside(list_rect_) && row_ > 0) {
            int i = (int)std::floor((in.tap_y - list_y0_) / row_ + scroll_);
            if (i >= 0 && i < n) { sel_ = i; t.confirm = true; }
        }
    }
    const UiInput& inp = t;
    if (inp.up) sel_ = (sel_ - 1 + n) % n;
    if (inp.down) sel_ = (sel_ + 1) % n;
    if (inp.page_left) sel_ = std::max(0, sel_ - 10);
    if (inp.page_right) sel_ = std::min(n - 1, sel_ + 10);
    if (!sfx_ && (inp.left || inp.right)) switch_dir_ = inp.left ? -1 : 1;
    if (versions_.size() > 1 && (inp.key_pressed == VK_TAB || (inp.pad_pressed & PAD_Y)))
        set_version((ver_ + 1) % (int)versions_.size());
    if (inp.confirm) {
        // Enter on the entry that is still playing stops it (music, and looping effects); a finished
        // effect has already gone back to "stopped", so Enter plays it again.
        if (playing_ == sel_) stop_playing();
        else start(sel_);
    }
}

void Jukebox::step() {
    if (!active_ || !session_) return;
    session_->jukebox_frame();
    session_->run_frame(0, 0);
    if (playing_ >= 0) elapsed_ += 1.0 / session_->frame_rate();
    if (cd_ && session_->msu()) {   // (an MSU-1 track starts once the game's upload is done: ends after it was heard)
        const bool on = session_->msu()->playing();
        if (on) cd_heard_ = true;
        else if (cd_heard_ || elapsed_ > 3.0) { cd_ = false; playing_ = -1; }
    } else if (cd_ && (!session_->soundtrack() || !session_->soundtrack()->playing())) { cd_ = false; playing_ = -1; }   // played once
    const int chans = std::min(kMaxMeters, session_->channel_count());
    for (int i = 0; i < kMaxMeters; i++) {
        // a soundtrack track: two meters, its left and right channels
        float lv = cd_ ? (i < 2 ? cd_level_[i] : 0.0f) : i < chans ? session_->channel_level(i) : 0.0f;
        lv = std::min(1.0f, std::sqrt(std::max(0.0f, lv)));
        vu_[i] = std::max(lv, vu_[i] * 0.86f);
        peak_[i] = std::max(vu_[i], peak_[i] - 0.012f);
    }
    cd_level_[0] = cd_level_[1] = 0;
    // An effect ends on a moment of silence. Music can open with seconds of silence, so a tune ends only once it has
    // been heard and then stays silent for three seconds (the jingles and tunes that play once).
    if (playing_ >= 0 && !cd_) {
        const bool quiet = std::all_of(std::begin(vu_), std::end(vu_), [](float l) { return l < 0.02f; });
        if (!quiet) heard_ = true;
        silent_frames_ = quiet ? silent_frames_ + 1 : 0;
        if (!list_[playing_].music ? silent_frames_ > 20 : heard_ && silent_frames_ > 180) playing_ = -1;
    }
}

void Jukebox::mix_audio(std::vector<int16_t>& out, double adjust) {
    if (!session_) return;
    const size_t before = out.size();
    session_->mix_audio(out, adjust);
    if (cd_)
        for (size_t i = before; i + 1 < out.size(); i += 2)
            for (int c = 0; c < 2; c++) cd_level_[c] = std::max(cd_level_[c], std::abs(out[i + c]) / 32768.0f);
}

void Jukebox::draw(Video& v, float time, uint32_t accent) {
    const float W = (float)v.width(), H = (float)v.height();
    v.fill_rect_gradient({0, 0, W, H}, lerp_color(0xFF0B0E18, accent, 0.10f), 0xFF07080D);
    const float pad = H * 0.05f;
    v.draw_text(FontId::Title, pad, pad * 0.7f, sfx_ ? "Sound Test" : "Music Player", accent);
    v.draw_text(FontId::Body, pad, pad * 0.7f + v.line_height(FontId::Title), game_label(module_), 0xFFB9C1D2);
    if (!sfx_) {
        const char* sw = touch_ui ? "Tap here to switch game  \xE2\x96\xB6" : "\xE2\x97\x80 \xE2\x96\xB6  switch game";
        float tw = v.measure_text(FontId::Small, sw);
        v.draw_text(FontId::Small, W - pad, pad * 0.9f, sw, 0xFF7C8598, Align::Right);
        switch_rect_ = {W - pad - tw - pad * 0.5f, 0, tw + pad * 1.5f, pad * 0.9f + v.line_height(FontId::Small) * 1.6f};
    }
    if (versions_.size() > 1) {
        // which version of the music plays: the game's own, or a soundtrack pack
        const std::string vt = ver_ == 0 ? std::string("Original music") : soundtrack_label(versions_[ver_].pack);
        const char* key = touch_ui ? "Tap to change" : "Tab / (Y)";
        const float y = pad * 0.9f + v.line_height(FontId::Small) * 1.7f;
        const float vw = v.measure_text(FontId::Body, vt), kw = v.measure_text(FontId::Small, key);
        v.draw_text(FontId::Body, W - pad, y, vt, ver_ == 0 ? 0xFFC9D0DC : accent, Align::Right);
        v.draw_text(FontId::Small, W - pad - vw - pad * 0.4f, y + v.line_height(FontId::Body) * 0.12f, key, 0xFF7C8598, Align::Right);
        version_rect_ = {W - pad - vw - kw - pad * 0.9f, y - pad * 0.2f, vw + kw + pad * 1.4f, v.line_height(FontId::Body) + pad * 0.4f};
    }

    // Track list
    Rect list = {pad, H * 0.2f, W * 0.46f, H * 0.68f};
    draw_panel(v, list, accent);
    const float row = v.line_height(FontId::Body) * 1.25f;
    list_rect_ = list; row_ = row; list_y0_ = list.y + pad * 0.3f;
    int visible = std::max(1, (int)((list.h - pad * 0.6f) / row));
    const float most = (float)std::max(0, (int)list_.size() - visible);
    float target = std::clamp(free_scroll_ ? scroll_ : (float)sel_ - visible * 0.5f + 0.5f, 0.0f, most);
    if (free_scroll_) scroll_ = target;
    scroll_ += (target - scroll_) * 0.3f;
    int first = std::max(0, (int)scroll_);
    for (int i = first; i < (int)list_.size() && i <= first + visible; i++) {
        float y = list.y + pad * 0.3f + (i - scroll_) * row;
        if (y < list.y + pad * 0.1f || y + row > list.y + list.h) continue;
        bool sel = i == sel_;
        if (sel) v.fill_round_rect({list.x + pad * 0.2f, y, list.w - pad * 0.4f, row * 0.94f}, row * 0.2f, with_alpha(accent, 0.28f));
        char num[16];
        if (list_[i].id < 0) snprintf(num, sizeof(num), "CD");   // a soundtrack track no tune names
        else snprintf(num, sizeof(num), "%02X", list_[i].id);
        uint32_t col = sel ? 0xFFFFFFFF : 0xFFC9D0DC;
        float ty = y + (row - v.line_height(FontId::Body)) * 0.4f;
        v.draw_text(FontId::Small, list.x + pad * 0.5f, ty + v.line_height(FontId::Body) * 0.12f, num, 0xFF7C8598);
        std::string name = list_[i].name;
        if (sfx_) name = std::string(list_[i].music ? "\xE2\x99\xAA " : "") + name;
        v.draw_text(FontId::Body, list.x + pad * 1.4f, ty, name, col);
        if (ver_ > 0 && list_[i].music && !has_version(i))   // not in the soundtrack: the original plays
            v.draw_text(FontId::Small, list.x + list.w - pad * 1.5f, ty + v.line_height(FontId::Body) * 0.12f, "original",
                        0xFF7C8598, Align::Right);
        if (i == playing_) {
            float bounce = 0.5f + 0.5f * std::sin(time * 8.0f);
            v.draw_text(FontId::Body, list.x + list.w - pad * 0.5f, ty, "\xE2\x96\xB6", with_alpha(accent, 0.6f + 0.4f * bounce), Align::Right);
        }
    }

    // Now playing + meters
    Rect np = {W * 0.52f, H * 0.2f, W * 0.48f - pad, H * 0.68f};
    draw_panel(v, np, accent);
    float y = np.y + pad * 0.5f;
    v.draw_text(FontId::Small, np.x + pad * 0.6f, y, playing_ >= 0 ? "NOW PLAYING" : "STOPPED", 0xFF7C8598);
    y += v.line_height(FontId::Small) * 1.2f;
    std::string title = playing_ >= 0 ? list_[playing_].name : std::string("\xE2\x80\x94");
    float fit = std::min(1.0f, (np.w - pad * 1.2f) / std::max(1.0f, v.measure_text(FontId::Title, title)));
    v.draw_text(FontId::Title, np.x + pad * 0.6f, y, title, 0xFFFFFFFF, Align::Left, fit);
    y += v.line_height(FontId::Title) * 1.1f;
    const MdPlus* cd = cd_ && session_ ? session_->soundtrack() : nullptr;
    const Msu1* msu = cd_ && session_ ? session_->msu() : nullptr;
    const std::string clock = cd    ? clock_text(cd->position()) + " / " + clock_text(cd_len_)
                              : msu ? clock_text(msu->position()) + " / " + clock_text(cd_len_)
                                    : clock_text(playing_ >= 0 ? elapsed_ : 0.0);
    v.draw_text(FontId::Body, np.x + pad * 0.6f, y, clock, accent);
    if (playing_ >= 0 && versions_.size() > 1)
        v.draw_text(FontId::Small, np.x + pad * 0.6f, y + v.line_height(FontId::Body) * 1.05f,
                    cd || msu ? "Soundtrack: " + soundtrack_label(versions_[ver_].pack) : std::string("Original music"),
                    0xFF7C8598);
    y += v.line_height(FontId::Body) * 2.0f;

    // the sound chips' channels (Mega Drive 6 FM + 4 PSG, SNES 8 voices, Master System 4 PSG), or a soundtrack
    // track's two
    static const char* cd_labels[2] = {"LEFT", "RIGHT"};
    const int chans = std::min(kMaxMeters, session_->channel_count());
    const bool two = cd || msu;
    const int bars = two ? 2 : chans;
    float mh = np.y + np.h - y - pad * 1.4f;
    const float bw = (np.w - pad * 1.2f) / bars, bw10 = (np.w - pad * 1.2f) / 10;
    const float barw = two ? bw10 * 1.6f : bw10 * 0.7f, radius = bw10 * 0.12f;
    for (int i = 0; i < bars; i++) {
        float bx = np.x + pad * 0.6f + i * bw;
        Rect back = {bx + (bw - barw) * 0.5f, y, barw, mh};
        v.fill_round_rect(back, radius, 0xFF1A1F2B);
        float h = mh * vu_[i];
        // (the Mega Drive's PSG channels in a second colour beside its FM ones)
        uint32_t c = two || session_->system() != System::MegaDrive || i < 6 ? accent : lerp_color(accent, 0xFF6FCF8A, 0.7f);
        if (h > 1) v.fill_round_rect({back.x, back.y + mh - h, back.w, h}, radius, c);
        float py = back.y + mh - mh * peak_[i];
        v.fill_rect({back.x, py - 2, back.w, 3}, 0xE0FFFFFF);
        v.draw_text(FontId::Small, bx + bw * 0.5f, y + mh + pad * 0.2f, two ? cd_labels[i] : session_->channel_name(i), 0xFF7C8598,
                    Align::Center);
    }
    std::string help = sfx_ ? "\xE2\x86\x91\xE2\x86\x93 Select    Enter / (A) Play / Stop    PgUp/PgDn (LB/RB) Jump    Esc / (B) Back"
                            : std::string("\xE2\x86\x91\xE2\x86\x93 Select    Enter / (A) Play / Stop    \xE2\x86\x90\xE2\x86\x92 Switch game    ") +
                                  (versions_.size() > 1 ? "Tab / (Y) Version    " : "") + "Esc / (B) Back";
    if (touch_ui) help = "Tap a track to play or stop it    Drag to scroll    Back to return";
    const float hfit = std::min(1.0f, (W - pad * 2) / std::max(1.0f, v.measure_text(FontId::Small, help)));
    v.draw_text(FontId::Small, W * 0.5f, H - v.line_height(FontId::Small) * 1.8f, help, with_alpha(0xFF6C7488, 1.0f - hint_fade_),
                Align::Center, hfit);
}
