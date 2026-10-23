#include "session.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

// (session_md.cpp, session_snes.cpp, session_sms.cpp)
std::unique_ptr<Session> make_md_session();
std::unique_ptr<Session> make_snes_session();
std::unique_ptr<Session> make_sms_session();

std::unique_ptr<Session> Session::create(GameModule* module) {
    if (!module) return nullptr;
    switch (module->system()) {
    case System::MegaDrive: return make_md_session();
    case System::Snes: return make_snes_session();
    case System::MasterSystem: return make_sms_session();
    default: return nullptr;
    }
}

Session::~Session() {}

bool Session::open(GameModule* module, const std::vector<uint8_t>& rom) {
    module_ = module ? own_module(module) : nullptr;
    own_.reset(module_);
    if (!core_load(rom)) return false;
    power_on();
    return true;
}

void Session::configure_mixer() {
    const System sys = system();
    mixer_.configure(core_sample_rate(), out_rate_, filter_cutoff(last_.audio_filter, sys),
                     core_audio_gain() * last_.volume / 100.0f * (module_ ? module_->audio_gain() : 1.0f));
}

void Session::apply_settings(const Settings& s) {
    last_ = s;
    GameConfig c;
    const System sys = system();
    const int max_ext = module_ ? module_->max_ext() : 0;
    c.ext = wide_ext(fit_wide_mode(s.widescreen, s.aspect, sys, max_ext), s.aspect, sys);
    c.hud_anchor = s.hud_anchor;
    if (module_ && !s.achievements) {   // achievements on: no cheats
        auto g = s.cheats.find(module_->id());
        if (g != s.cheats.end()) c.cheats = g->second;
    }
    want_ = c;
    core_settings(s);
    configure_mixer();
    if (!configured_) switch_config();
    else try_switch();
    configured_ = true;
}

void Session::switch_config() {
    cfg_ = want_;
    pending_ = false;
    core_configure(cfg_);
}

void Session::try_switch() {
    pending_ = false;
    if (want_.ext == cfg_.ext && want_.hud_anchor == cfg_.hud_anchor && want_.cheats == cfg_.cheats) return;
    if (!module_ || core_can_configure(want_)) switch_config();
    else pending_ = true;
}

void Session::power_on() {
    core_power_on();
    mixer_.reset();
    on_power_on();
    if (configured_) switch_config();          // the CPU starts from its reset vector: nothing to wait for
    mask_l_ = mask_r_ = (float)cfg_.ext;
}

void Session::soft_reset() { core_soft_reset(); }

void Session::warm_up() {
    if (!module_) return;
    const bool was_muted = muted;
    muted = true;
    core_headless(true);
    for (int f = 0; f < module_->preview_start() && !module_->preview_ready(ram()); f++)
        run_frame(module_->preview_pad(f, ram()), 0);
    core_headless(false);
    muted = was_muted;
}

bool Session::start_level(int index, int variant) {
    if (!module_) return false;
    on_power_on();   // (an MD+ soundtrack stops first: the level's music is asked for during the start-up frames)
    const bool ok = core_start_level(want_, index, variant);   // starts from power-on
    switch_config();
    mixer_.reset();
    return ok;
}

void Session::run_frame(uint16_t pad1, uint16_t pad2) {
    info_ = FrameInfo();
    core_run_frame(pad1, pad2, info_);
    if (cfg_.ext == 0) {
        info_.mask_left = info_.mask_right = 0;
    } else if (!info_.gameplay) {
        info_.mask_left = info_.mask_right = cfg_.ext;
        info_.mask_lines = 0;
        info_.mask_from = 0;
    }
    mask_lines_ = info_.mask_lines;
    mask_from_ = info_.gameplay ? info_.mask_from : 0;
    pillar_below_ = info_.pillar_below && info_.gameplay;
    const bool dims = info_.gameplay && cfg_.ext > 0;
    dim_l_ = dims ? std::clamp(info_.dim_left, 0, cfg_.ext) : 0;
    dim_r_ = dims ? std::clamp(info_.dim_right, 0, cfg_.ext) : 0;
    // Leaving gameplay closes the side masks at once (beside a menu the old picture is stale);
    // coming back they open gradually so it doesn't pop. A mask that has to grow (the view widened
    // next to a level edge, a state loaded there) closes at once too: what it covers is outside the level.
    if (!info_.gameplay) mask_l_ = mask_r_ = (float)cfg_.ext;
    const float speed = std::max(2.0f, cfg_.ext / 6.0f);
    auto ease = [&](float& cur, int target) {
        if (cur < target) cur = (float)target;
        else if (cur > target) cur = std::max<float>((float)target, cur - speed);
    };
    ease(mask_l_, std::clamp(info_.mask_left, 0, cfg_.ext));
    ease(mask_r_, std::clamp(info_.mask_right, 0, cfg_.ext));
    if (pending_) try_switch();
}

const uint32_t* Session::frame(int& w, int& h) {
    if (!core_convert(pixels_, sprite_, w, h) && !pixels_.empty()) {
        w = frame_w_;
        h = frame_h_;
        return pixels_.data();
    }
    frame_w_ = w;
    frame_h_ = h;
    const int ext = std::min(cfg_.ext, w / 2);
    const int ml = std::min((int)(mask_l_ + 0.5f), ext), mr = std::min((int)(mask_r_ + 0.5f), ext);
    // A screen that isn't widened (title, menus, a story or in-between screen) is pillarboxed. Where its picture's own
    // edge is one plain colour, the bar on that side takes it, so the screen fills the width instead of sitting
    // between black bars; pictures that run to the edge keep the black.
    const uint32_t black = 0xFF000000u;
    uint32_t bar_l = black, bar_r = black;
    if (!info_.gameplay && ext > 0) {
        bar_l = plain_bar(ext, ext + kEdgeStrip, +1, w, h);
        bar_r = plain_bar(w - ext - kEdgeStrip, w - ext, -1, w, h);
    }
    // dimmed scenery: about 40% brightness
    auto dim = [](uint32_t c) {
        return 0xFF000000u | ((((c >> 16) & 0xFF) * 102 >> 8) << 16) | ((((c >> 8) & 0xFF) * 102 >> 8) << 8) | ((c & 0xFF) * 102 >> 8);
    };
    for (int y = 0; y < h; y++) {
        uint32_t* dst = &pixels_[(size_t)y * w];
        const uint8_t* spr = sprite_.size() >= (size_t)w * h ? &sprite_[(size_t)y * w] : nullptr;
        const bool in_masks = y >= mask_from_ && (mask_lines_ == 0 || y < mask_lines_);
        if ((dim_l_ || dim_r_) && in_masks) {
            // the scenery only: sprites there (a boss, its shots) stay as they are
            for (int x = 0; x < dim_l_ && x < w; x++) if (!spr || !spr[x]) dst[x] = dim(dst[x]);
            for (int x = std::max(0, w - dim_r_); x < w; x++) if (!spr || !spr[x]) dst[x] = dim(dst[x]);
        }
        if (mask_lines_ > 0 && y >= mask_lines_) {
            if (pillar_below_) {
                for (int x = 0; x < ext; x++) dst[x] = 0xFF000000u;
                for (int x = std::max(0, w - ext); x < w; x++) dst[x] = 0xFF000000u;
            }
            continue;
        }
        if (y < mask_from_) continue;
        for (int x = 0; x < ml; x++) dst[x] = bar_l;
        for (int x = std::max(0, w - mr); x < w; x++) dst[x] = bar_r;
    }
    return pixels_.data();
}

// A pillarboxed picture's bar colour from its edge strip [x0, x1): the strip's plain colour, or black. `inward` is +1 for
// the left edge (the picture lies to the right) and -1 for the right edge. A plain edge strip in another colour than a
// plain strip beside it, further in, is a blanked border column (the Master System blanks its leftmost 8 pixels to its
// border colour): the colour inside is taken and the column painted with it too, so it doesn't stand as a stripe.
uint32_t Session::plain_bar(int x0, int x1, int inward, int w, int h) {
    const uint32_t black = 0xFF000000u, none = 0;
    const uint32_t outer = plain_edge(x0, x1, w, h, none);
    if (outer == none) return black;
    const int n = x1 - x0;
    const uint32_t in = inward > 0 ? plain_edge(x1, x1 + n, w, h, none) : plain_edge(x0 - n, x0, w, h, none);
    if (in == none || in == outer) return outer;
    for (int y = 0; y < h; y++)
        for (int x = std::max(0, x0); x < std::min(w, x1); x++) {
            const size_t i = (size_t)y * w + x;
            if (pixels_[i] == outer && !(sprite_.size() > i && sprite_[i])) pixels_[i] = in;
        }
    return in;
}

// The colour of a strip of columns [x0, x1) of the picture when it is plain: one colour in nearly all of its scenery
// pixels (sprites and a stray letter or two crossing it don't count), else `otherwise`.
uint32_t Session::plain_edge(int x0, int x1, int w, int h, uint32_t otherwise) const {
    x0 = std::max(0, x0);
    x1 = std::min(w, x1);
    if (x1 <= x0 || h <= 0) return otherwise;
    const bool marks = sprite_.size() >= (size_t)w * h;
    // the most common colour (a short list: a plain strip has one or two)
    uint32_t colour[8] = {};
    int count[8] = {}, kinds = 0, total = 0;
    for (int y = 0; y < h; y++) {
        const uint32_t* row = &pixels_[(size_t)y * w];
        for (int x = x0; x < x1; x++) {
            if (marks && sprite_[(size_t)y * w + x]) continue;
            total++;
            int k = 0;
            while (k < kinds && colour[k] != row[x]) k++;
            if (k == kinds) {
                if (kinds == 8) continue;   // (busy: can't be plain)
                colour[kinds] = row[x];
                count[kinds++] = 0;
            }
            count[k]++;
        }
    }
    int best = 0;
    for (int k = 1; k < kinds; k++) if (count[k] > count[best]) best = k;
    return kinds > 0 && total > 0 && count[best] * 100 >= total * 97 ? colour[best] : otherwise;
}

void Session::set_audio_rate(double out_rate) {
    out_rate_ = out_rate;
    configure_mixer();
}

void Session::mix_audio(std::vector<int16_t>& out, double adjust) {
    int frames = 0;
    const int32_t* a = core_audio(frames);
    if (muted || !a) return;
    const size_t before = out.size();
    mixer_.process(a, frames, adjust, out);
    // a soundtrack plays in real time against the output: as many frames as the game's sound just took
    if (out.size() > before) mix_extra(&out[before], (int)((out.size() - before) / 2));
}

std::vector<uint8_t> Session::save_state(const std::string& extra) {
    std::vector<uint8_t> s = core_save();
    // The module's own state (widescreen bookkeeping), length-prefixed.
    const std::vector<uint8_t> mod = module_ ? module_->save_ext() : std::vector<uint8_t>();
    s.insert(s.end(), mod.begin(), mod.end());
    const uint32_t ml = (uint32_t)mod.size();
    for (int i = 0; i < 4; i++) s.push_back((uint8_t)(ml >> (i * 8)));
    // Then a tag: the module id (states from another game are rejected) and the configuration the ROM ran with
    // ("md|54|cheat,cheat"), so a load can restore exactly that ROM before moving to the current settings; then the
    // app's note, if any ("|a1.00000000"), and what a soundtrack plays, if any ("|t12,34567,1,255").
    const std::string cd = soundtrack_state();
    std::string tag = module_ ? module_->id() : "";
    tag += "|" + std::to_string(cfg_.ext) + "|";
    for (const auto& kv : cfg_.cheats)
        if (kv.second && tag.size() + kv.first.size() + extra.size() + cd.size() < 236) tag += kv.first + ",";
    if (!cd.empty()) tag += "|" + extra + "|" + cd;
    else if (!extra.empty()) tag += "|" + extra;
    s.insert(s.end(), tag.begin(), tag.end());
    s.push_back((uint8_t)tag.size());
    return s;
}

bool Session::load_state(const std::vector<uint8_t>& data, std::string* extra) {
    if (data.empty() || !module_) return false;
    const size_t idlen = data.back();
    if (data.size() < idlen + 1 + 4) return false;
    const std::string tag(data.end() - 1 - idlen, data.end() - 1);
    const size_t bar = tag.find('|');
    if (tag.substr(0, bar) != module_->id()) return false;
    size_t end = data.size() - 1 - idlen;
    const uint32_t ml = data[end - 4] | (data[end - 3] << 8) | (data[end - 2] << 16) | ((uint32_t)data[end - 1] << 24);
    end -= 4;
    if (ml > end) return false;
    const std::vector<uint8_t> mod(data.begin() + (end - ml), data.begin() + end);
    const std::vector<uint8_t> core(data.begin(), data.begin() + (end - ml));
    // The state runs on the ROM it was saved with; the current settings follow when it is safe (try_switch).
    GameConfig saved = want_;
    std::string cd;
    if (bar != std::string::npos) {
        const size_t bar2 = tag.find('|', bar + 1);
        const size_t bar3 = bar2 == std::string::npos ? std::string::npos : tag.find('|', bar2 + 1);
        const size_t bar4 = bar3 == std::string::npos ? std::string::npos : tag.find('|', bar3 + 1);
        saved.ext = std::clamp(atoi(tag.c_str() + bar + 1), 0, module_->max_ext());
        saved.cheats.clear();
        const std::string list = bar2 == std::string::npos ? "" : tag.substr(bar2 + 1, bar3 == std::string::npos ? std::string::npos : bar3 - bar2 - 1);
        if (extra) *extra = bar3 == std::string::npos ? "" : tag.substr(bar3 + 1, bar4 == std::string::npos ? std::string::npos : bar4 - bar3 - 1);
        cd = bar4 == std::string::npos ? "" : tag.substr(bar4 + 1);
        for (size_t a = 0; a < list.size();) {
            size_t b = list.find(',', a);
            if (b == std::string::npos) b = list.size();
            if (b > a) saved.cheats[list.substr(a, b - a)] = true;
            a = b + 1;
        }
    }
    // Configure for the saved width first (the module's bookkeeping is read for that width).
    const GameConfig want = want_, before = cfg_;
    want_ = saved;
    switch_config();
    if (!core_load_state(core)) {
        want_ = before;
        switch_config();
        want_ = want;
        return false;
    }
    module_->load_ext(mod.data(), mod.size());
    set_soundtrack_state(cd);
    want_ = want;
    try_switch();
    mixer_.reset();
    return true;
}
