// Tests of the app's update check and achievements export (console; built as app_tests by CMakeLists.txt).
//   app_tests                          the version comparison, the release JSON's tag and the badge table, the
//                                      controllers' read filter and the Controls settings saved and read (offline)
//   app_tests --fetch <url>            also one GET of a GitHub "latest release" URL: prints the tag it read
//   app_tests --export <dir> [ini]     writes the export (achievements.csv + icons) to <dir>, from an achievements.ini
//   app_tests --sheet <file.png>       every badge, earned and locked, on one picture
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "../src/app/achexport.h"
#include "../src/app/achievements.h"
#include "../src/app/png.h"
#include "../src/app/sys.h"
#include "../src/app/updatecheck.h"
#include "../src/app/settings.h"
#include "../src/platform/input.h"
#include "../src/platform/pad_filter.h"

namespace {
int g_bad = 0;
void check(bool ok, const char* what) {
    if (!ok) { g_bad++; printf("FAIL: %s\n", what); }
}

std::vector<AchievementGroup> groups() {
    std::unique_ptr<GameModule> m[3] = {std::unique_ptr<GameModule>(create_md_aladdin()),
                                        std::unique_ptr<GameModule>(create_snes_aladdin()),
                                        std::unique_ptr<GameModule>(create_sms_aladdin())};
    std::vector<AchievementGroup> g;
    for (auto& mod : m) g.push_back({std::string(mod->title()) + " (" + system_short_name(mod->system()) + ")", mod->achievements()});
    g.push_back({"Aladdin Collection", collection_achievements()});
    return g;
}

void test_versions() {
    struct { const char *tag, *cur; bool want; } t[] = {
        {"v1.1", "1.0", true},    {"v1.0", "1.0", false},     {"1.0", "1.0", false},   {"v1.0.1", "1.0", true},
        {"v0.9", "1.0", false},   {"v2", "1.0", true},        {"V1.10", "1.9", true},  {"v1.9", "1.10", false},
        {"", "1.0", false},       {"latest", "1.0", false},   {"v1.0.0", "1.0", false}, {"v10.0", "9.9.9", true},
        {"v1.1-beta", "1.0", true}, {"release-1.0", "1.0", false}};
    for (auto& c : t) {
        char what[96];
        snprintf(what, sizeof what, "is_newer(\"%s\", \"%s\") should be %d", c.tag, c.cur, c.want);
        check(UpdateCheck::is_newer(c.tag, c.cur) == c.want, what);
    }
    check(UpdateCheck::tag_from_json("{\"url\":\"x\",\"tag_name\": \"v1.2\",\"name\":\"y\"}") == "v1.2", "tag_from_json");
    check(UpdateCheck::tag_from_json("{\"tag_name\":\"1.0.3\"}") == "1.0.3", "tag_from_json (no spaces)");
    check(UpdateCheck::tag_from_json("{\"message\":\"Not Found\"}").empty(), "tag_from_json (no release)");
    check(UpdateCheck::tag_from_json("").empty(), "tag_from_json (empty)");
    check(UpdateCheck::api_url() == std::string("https://api.github.com/repos/") + kUpdateRepo + "/releases/latest", "api_url");
    check(UpdateCheck::page_for("v1.1") == std::string("https://github.com/") + kUpdateRepo + "/releases/tag/v1.1", "page_for");
}

// Every achievement has a badge of its own (not the fallback), the collection's are gold / platinum.
void test_badges() {
    int n = 0;
    for (const AchievementGroup& g : groups())
        for (const AchievementDef& d : g.defs) {
            n++;
            std::vector<uint32_t> a, b;
            draw_achievement_icon(d.id, false, 32, a);
            draw_achievement_icon("no_such_achievement", false, 32, b);
            // (the fallback is a bronze star on gold: a real one differs at least in colour or symbol)
            char what[96];
            snprintf(what, sizeof what, "%s has its own badge", d.id);
            check(a != b, what);
        }
    check(n == 54, "54 achievements");
    check(achievement_tier("all_complete") == 3 && achievement_points("all_complete") == 100, "Completionist is platinum");
}

// The DirectInput read filter (pad_filter.h): a controller's held buttons when it is opened or comes back are not
// presses; a device sending random presses is left out until it has been quiet for 3 s; Other Controllers off leaves
// them all out.
void test_pad_filter() {
    // what poll_dinput reports for a read: the buttons down, pressed, and whether it is ignored
    struct Pad { PadFilter f; uint64_t buttons = 0, pressed = 0; bool ignored = false; };
    auto read = [](Pad& p, uint64_t raw, uint64_t now, bool enabled = true) {
        uint64_t prev = p.buttons;
        if (!p.f.read(raw, now, enabled)) { p.buttons = p.pressed = 0; p.ignored = true; return; }
        p.ignored = false;
        if (p.f.take_settle()) prev = raw;
        p.buttons = raw;
        p.pressed = raw & ~prev;
    };
    {   // opened with B and Back held: no press until they're let go and pressed again
        Pad p;
        read(p, 0x12, 1000);
        check(p.buttons == 0x12 && p.pressed == 0 && !p.ignored, "held at open: not pressed");
        read(p, 0x12, 1016);
        check(p.pressed == 0, "still held: not pressed");
        read(p, 0x02, 1032);
        read(p, 0x12, 1048);
        check(p.pressed == 0x10, "let go and pressed again: pressed");
        read(p, 0x13, 1064);
        check(p.pressed == 0x01, "a new button: pressed");
    }
    {   // a failed read (dropped out): what it holds when it comes back is not a press
        Pad p;
        read(p, 0, 1000);
        read(p, 0, 1016);
        p.f.read_failed();
        read(p, 0x04, 1032);
        check(p.pressed == 0, "held when it comes back: not pressed");
        read(p, 0x00, 1048);
        read(p, 0x04, 1064);
        check(p.pressed == 0x04, "after it comes back: pressed");
    }
    {   // a player mashing: fast, but on a few buttons - never left out
        Pad p;
        uint64_t t = 1000;
        bool ever = false;
        for (int i = 0; i < 600; i++, t += 8) {   // (125 reads a second, A / B / Right alternating every read)
            read(p, (i & 1) ? 0x03 : 0x40, t);
            ever = ever || p.ignored;
        }
        check(!ever, "mashing three buttons: never ignored");
    }
    {   // random presses on many buttons (a Switch Pro Controller in full report mode)
        Pad p;
        uint64_t t = 1000, seed = 12345;
        for (int i = 0; i < 250; i++, t += 8) {   // 2 s of garbage
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            read(p, (seed >> 40) & 0xFFFF, t);
        }
        check(p.ignored && p.f.noisy, "random presses: ignored");
        check(p.buttons == 0 && p.pressed == 0, "ignored: reads as idle");
        // quiet (nothing held) for a while: back after 3 quiet seconds, and no presses from that first read
        bool back_early = false;
        uint64_t back_at = 0;
        for (int i = 0; i < 800 && !back_at; i++, t += 8) {
            read(p, 0x08, t);
            if (!p.ignored) back_at = t;
            if (!p.ignored && back_at < 4000) back_early = true;
        }
        check(back_at > 0 && !back_early, "back after 3 quiet seconds");
        check(p.pressed == 0, "back: what it holds is not a press");
        read(p, 0x00, t);
        read(p, 0x08, t + 8);
        check(p.pressed == 0x08, "back: presses count again");
    }
    {   // Other Controllers off: left out; on again: held buttons aren't presses
        Pad p;
        read(p, 0x00, 1000);
        read(p, 0x01, 1016, false);
        check(p.ignored && p.pressed == 0, "Other Controllers off: ignored");
        read(p, 0x01, 1032, true);
        check(!p.ignored && p.pressed == 0, "on again: held is not pressed");
    }
}

// Controls > Menu Buttons and Other Controllers are kept in settings.ini.
void test_input_settings() {
    const wchar_t* tmp = _wgetenv(L"TEMP");
    const std::wstring path = path_join(tmp ? tmp : L".", L"aladdin_app_test_settings.ini");
    Settings a;
    check(!a.menu_confirm_right && a.other_controllers, "defaults: bottom confirms, other controllers on");
    a.menu_confirm_right = true;
    a.other_controllers = false;
    check(a.save(path), "settings saved");
    Settings b;
    check(b.load(path), "settings read");
    check(b.menu_confirm_right && !b.other_controllers, "Menu Buttons and Other Controllers read back");
    a.menu_confirm_right = false;
    a.other_controllers = true;
    check(a.save(path), "settings saved again");
    check(b.load(path) && !b.menu_confirm_right && b.other_controllers, "and back to the defaults");
    _wremove(path.c_str());
}
}  // namespace

int main(int argc, char** argv) {
    test_versions();
    test_badges();
    test_pad_filter();
    test_input_settings();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fetch") && i + 1 < argc) {
            std::string body;
            const bool ok = http_get(argv[++i], body);
            const std::string tag = UpdateCheck::tag_from_json(body);
            printf("GET %s: %s, %zu bytes, tag_name '%s', newer than 1.0: %s\n", argv[i], ok ? "ok" : "failed", body.size(),
                   tag.c_str(), UpdateCheck::is_newer(tag, "1.0") ? "yes" : "no");
            check(ok && !tag.empty(), "fetch");
        } else if (!strcmp(argv[i], "--export") && i + 1 < argc) {
            const std::wstring dir = from_utf8(argv[++i]);
            AchievementStore store;
            if (i + 1 < argc && argv[i + 1][0] != '-') store.load(from_utf8(argv[++i]));
            std::string err;
            check(export_achievements(dir, groups(), store, &err), ("export: " + err).c_str());
            std::string csv;
            check(read_text(path_join(dir, L"achievements.csv"), csv), "achievements.csv written");
            int rows = 0;
            for (char c : csv) rows += c == '\n';
            check(rows == 55, "a header and 54 rows");
            printf("exported to %s (%d lines)\n", to_utf8(dir).c_str(), rows);
        } else if (!strcmp(argv[i], "--sheet") && i + 1 < argc) {
            const int s = 96, cols = 18;
            std::vector<AchievementDef> all;
            for (const AchievementGroup& g : groups()) all.insert(all.end(), g.defs.begin(), g.defs.end());
            const int rows = ((int)all.size() + cols - 1) / cols * 2;
            std::vector<uint32_t> sheet((size_t)cols * s * rows * s, 0xFF202020), px;
            for (size_t k = 0; k < all.size(); k++)
                for (int locked = 0; locked < 2; locked++) {
                    draw_achievement_icon(all[k].id, locked != 0, s, px);
                    const int cx = (int)(k % cols) * s, cy = ((int)(k / cols) * 2 + locked) * s;
                    for (int y = 0; y < s; y++)
                        for (int x = 0; x < s; x++) sheet[(size_t)(cy + y) * cols * s + cx + x] = 0xFF000000 | px[(size_t)y * s + x];
                }
            check(write_png(from_utf8(argv[++i]), sheet.data(), cols * s, rows * s), "sheet written");
        }
    }
    printf(g_bad ? "%d FAILED\n" : "all passed\n", g_bad);
    return g_bad ? 1 : 0;
}
