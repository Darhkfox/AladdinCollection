// Updates: once a day (or on Check Now) the latest release's tag is looked up on GitHub (updatecheck.h); a newer one
// than kAppVersion gets a badge on the launcher (and Options > Updates a link) that opens its page. Nothing is
// downloaded or installed, nothing about the player is sent, a failed or offline check stays silent, and --autotest
// runs never touch the network.
#include "app.h"
#include <algorithm>
#include <cmath>

std::string App::update_version() const {
    return update_tag_.size() > 1 && (update_tag_[0] == 'v' || update_tag_[0] == 'V') ? update_tag_.substr(1) : update_tag_;
}

bool App::over_update_badge(float x, float y) const {
    const Rect& r = launcher_update_;
    return !update_tag_.empty() && r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void App::open_update_page() {
    if (update_tag_.empty() || autotest_) return;   // (scripted test runs never open a browser)
    open_url(UpdateCheck::page_for(update_tag_).c_str());
    menu_.toast("Opened the version " + update_version() + " page in your browser");
}

void App::check_for_update(bool manual) {
    if (autotest_) {   // (scripted runs: no network, only what the settings say an earlier check found)
        apply_update_tag(settings_.check_updates ? settings_.update_latest : std::string());
        return;
    }
    if (!manual && !settings_.check_updates) return;
    if (!manual && settings_.update_checked_on == today_text()) {
        apply_update_tag(settings_.update_latest);   // (checked today already)
        return;
    }
    update_manual_ = manual;
    updater_.start();
}

void App::apply_update_tag(const std::string& tag) {
    const bool newer = !tag.empty() && UpdateCheck::is_newer(tag, kAppVersion);
    update_tag_ = newer && (settings_.check_updates || update_manual_) ? tag : std::string();
    if (!update_tag_.empty() && !update_told_) {
        update_told_ = true;
        menu_.toast("Version " + update_version() + " is available - see the launcher's badge, or Options > Updates", 6.0f);
    }
}

void App::update_tick() {
    if (!update_started_) {
        update_started_ = true;
        check_for_update(false);
    }
    std::string tag;
    if (!updater_.poll(&tag)) return;
    const bool manual = update_manual_;   // (a manual check shows what it found even with the daily check off)
    if (!tag.empty()) {
        settings_.update_checked_on = today_text();
        settings_.update_latest = tag;
        save_settings();
        if (manual) update_told_ = false;
        apply_update_tag(tag);
        if (manual && update_tag_.empty()) menu_.toast(std::string("You have the latest version (") + kAppVersion + ")", 4.0f);
    } else if (manual) {   // (only a check the player asked for says it failed; the daily one stays quiet)
        menu_.toast("Couldn't reach GitHub to check for updates", 4.0f);
    }
    update_manual_ = false;
}

// A newer release: a gold pill at the top right, beside the title, that opens its page; where the title leaves no
// room there, a shorter one in the bottom right corner, beside the key hints.
void App::draw_update_badge(float title_right) {
    launcher_update_ = {};
    if (update_tag_.empty()) return;
    const float W = (float)video_.width(), H = (float)video_.height();
    const float bh = video_.line_height(FontId::Small) * 1.45f, m = H * 0.025f;
    std::string t = "Update available: v" + update_version();
    float bw = video_.measure_text(FontId::Small, t) + bh * 1.7f;
    Rect r;
    if (W - m - bw >= title_right + m) {
        r = {W - m - bw, H * 0.016f, bw, bh};   // (clear of the moon under it)
    } else {
        t = "v" + update_version() + " available";
        bw = video_.measure_text(FontId::Small, t) + bh * 1.7f;
        r = {W - m - bw, H - video_.line_height(FontId::Small) * 1.6f - (bh - video_.line_height(FontId::Small)) * 0.5f, bw, bh};
    }
    const uint32_t ink = 0xFF10131A;
    const float pulse = 0.5f + 0.5f * std::sin((float)time_ * 3.0f);
    video_.fill_round_rect({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, bh * 0.5f + 3, with_alpha(0xFFF2C14E, 0.2f + 0.25f * pulse));
    video_.fill_round_rect(r, bh * 0.5f, 0xFFF2C14E);
    video_.outline_round_rect(r, bh * 0.5f, std::max(1.5f, H / 400.0f), 0xFF7A5A1C);
    video_.fill_star(r.x + bh * 0.55f, r.y + bh * 0.5f, bh * 0.3f, ink);
    video_.fill_star(r.x + bh * 0.55f, r.y + bh * 0.5f, bh * 0.18f, 0xFFFFFFFF);
    video_.draw_text(FontId::Small, r.x + bh * 1.0f + (r.w - bh * 1.5f) * 0.5f, r.y + (bh - video_.line_height(FontId::Small)) * 0.5f,
                     t, ink, Align::Center);
    launcher_update_ = r;
}

void App::add_update_items(MenuPage& p) {
    p.items.push_back(MenuItem::header("Updates"));
    p.items.push_back(MenuItem::toggle("Check for Updates", [this]() { return settings_.check_updates; },
                                       [this](bool v) {
                                           settings_.check_updates = v;
                                           apply_update_tag(settings_.update_latest);
                                           if (v) check_for_update(false);
                                       },
                                       std::string("Once a day the launcher looks up the latest release on GitHub (only its version "
                                                   "number: nothing about you or your games is sent) and shows a badge when "
                                                   "there's a newer version than this one (") +
                                           kAppVersion + "). Nothing is downloaded or installed."));
    MenuItem latest = MenuItem::action("Latest Version", [this]() {
        if (!update_tag_.empty()) open_update_page();
        else if (!updater_.running()) {
            check_for_update(true);
            if (!autotest_) menu_.toast("Checking for updates\xE2\x80\xA6", 2.0f);
        }
    }, "Opens the new version's page on GitHub to download it yourself (saves and settings carry over), or checks now.");
    latest.value_text = [this]() {
        if (updater_.running()) return std::string("Checking\xE2\x80\xA6");
        if (!update_tag_.empty()) return "v" + update_version() + " available - open its page";
        return std::string("This is v") + kAppVersion + " - Check Now";
    };
    p.items.push_back(latest);
}
