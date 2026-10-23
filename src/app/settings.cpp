#include "settings.h"
#include "../platform/input.h"
#include "../platform/keys.h"
#include "../core/md.h"
#include "../core/snes.h"
#include "../core/sms.h"
#include "sys.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace {
// Per console: the action's ini name and menu name, its pad bit, and the default keyboard keys (player 1 / 2) and
// Xbox pad button (by position: the bottom face button is the console's main one).
struct ActionDef { const char* key; const char* name; uint16_t bit; int key1, key2; uint32_t pad; };
const ActionDef kMd[] = {
    {"up", "Up", MD_UP, VK_UP, 'I', PAD_UP}, {"down", "Down", MD_DOWN, VK_DOWN, 'K', PAD_DOWN},
    {"left", "Left", MD_LEFT, VK_LEFT, 'J', PAD_LEFT}, {"right", "Right", MD_RIGHT, VK_RIGHT, 'L', PAD_RIGHT},
    // Genesis-classics style layout: X = A, A = B, B = C.
    {"a", "A", MD_A, 'Z', 'U', PAD_X}, {"b", "B", MD_B, 'X', 'O', PAD_A}, {"c", "C", MD_C, 'C', 'P', PAD_B},
    {"start", "Start", MD_START, VK_RETURN, VK_BACK, PAD_START},
};
const ActionDef kSnes[] = {
    {"up", "Up", kPadUp, VK_UP, 'I', PAD_UP}, {"down", "Down", kPadDown, VK_DOWN, 'K', PAD_DOWN},
    {"left", "Left", kPadLeft, VK_LEFT, 'J', PAD_LEFT}, {"right", "Right", kPadRight, VK_RIGHT, 'L', PAD_RIGHT},
    {"b", "B", kPadB, 'Z', 'U', PAD_A}, {"a", "A", kPadA, 'X', 'O', PAD_B}, {"y", "Y", kPadY, 'A', 'Y', PAD_X},
    {"x", "X", kPadX, 'S', 'H', PAD_Y}, {"l", "L", kPadL, 'Q', '7', PAD_LB}, {"r", "R", kPadR, 'W', '9', PAD_RB},
    {"start", "Start", kPadStart, VK_RETURN, VK_BACK, PAD_START}, {"select", "Select", kPadSelect, VK_SPACE, 'N', PAD_LS},
};
const ActionDef kSms[] = {
    {"up", "Up", SMS_UP, VK_UP, 'I', PAD_UP}, {"down", "Down", SMS_DOWN, VK_DOWN, 'K', PAD_DOWN},
    {"left", "Left", SMS_LEFT, VK_LEFT, 'J', PAD_LEFT}, {"right", "Right", SMS_RIGHT, VK_RIGHT, 'L', PAD_RIGHT},
    {"1", "Button 1", SMS_1, 'Z', 'U', PAD_A}, {"2", "Button 2", SMS_2, 'X', 'O', PAD_B},
    {"pause", "Pause", SMS_PAUSE, VK_RETURN, VK_BACK, PAD_START},
};
const ActionDef* actions(System s, int& n) {
    switch (s) {
    case System::Snes: n = (int)(sizeof(kSnes) / sizeof(kSnes[0])); return kSnes;
    case System::MasterSystem: n = (int)(sizeof(kSms) / sizeof(kSms[0])); return kSms;
    default: n = (int)(sizeof(kMd) / sizeof(kMd[0])); return kMd;
    }
}
const char* kSystemKeys[kSystemCount] = {"md", "snes", "sms"};
const char* kHotkeyKeys[HK_COUNT] = {"fast_forward", "rewind", "menu", "quick_save", "quick_load", "screenshot"};
}  // namespace

int action_count(System s) { int n; actions(s, n); return n; }
const char* action_name(System s, int act) {
    int n;
    const ActionDef* d = actions(s, n);
    return act >= 0 && act < n ? d[act].name : "?";
}
uint16_t action_bit(System s, int act) {
    int n;
    const ActionDef* d = actions(s, n);
    return act >= 0 && act < n ? d[act].bit : 0;
}

Settings::Settings() { reset_keys(); }

void Settings::reset_keys(System s, int p) {
    int n;
    const ActionDef* d = actions(s, n);
    for (int a = 0; a < kMaxActions; a++) {
        keys[(int)s][p][a] = a < n ? (p == 0 ? d[a].key1 : d[a].key2) : 0;
        pad_buttons[(int)s][p][a] = a < n ? d[a].pad : 0;
    }
}

void Settings::reset_keys() {
    for (int s = 0; s < kSystemCount; s++)
        for (int p = 0; p < 2; p++) reset_keys((System)s, p);
    hotkey_keys[HK_FAST_FORWARD] = VK_TAB;
    hotkey_keys[HK_REWIND] = 'R';
    hotkey_pads[HK_FAST_FORWARD] = PAD_RT;
    hotkey_pads[HK_REWIND] = PAD_LT;
    hotkey_keys[HK_MENU] = VK_ESCAPE;
    hotkey_pads[HK_MENU] = PAD_BACK;
    hotkey_keys[HK_SAVE_STATE] = VK_F5;
    hotkey_pads[HK_SAVE_STATE] = 0;
    hotkey_keys[HK_LOAD_STATE] = VK_F9;
    hotkey_pads[HK_LOAD_STATE] = 0;
    hotkey_keys[HK_SCREENSHOT] = VK_F12;
    hotkey_pads[HK_SCREENSHOT] = 0;
}

int resolve_player_pad(const int pad_index[2], const bool* connected, int slots, int player) {
    if (pad_index[player] >= 0) return pad_index[player];
    // Player 1's Auto has priority: player 2's Auto skips whatever player 1 resolves to.
    int taken = pad_index[player ^ 1];
    if (player == 1 && taken < 0) taken = resolve_player_pad(pad_index, connected, slots, 0);
    for (int i = 0; i < slots; i++)
        if (connected[i] && i != taken) return i;
    return -1;
}

int base_width(System s) { return s == System::MegaDrive ? 320 : 256; }
int base_lines(System s) { return s == System::MasterSystem ? 192 : 224; }

double pixel_aspect(System s, AspectMode a) {
    if (a != AspectMode::Crt) return 1.0;
    // A 4:3 TV: the Mega Drive's 320 pixels fill its width; the SNES's and Master System's 256 pixels (the same
    // 5.37 MHz dot clock) are 8:7 wide (the Master System's 192 lines leave a border above and below).
    return s == System::MegaDrive ? (4.0 / 3.0) * 224.0 / 320.0 : 8.0 / 7.0;
}

int wide_ext(WideMode m, AspectMode a, System s) {
    double ratio;
    switch (m) {
    case WideMode::W16x10: ratio = 16.0 / 10.0; break;
    case WideMode::W16x9:  ratio = 16.0 / 9.0; break;
    case WideMode::W2x1:   ratio = 2.0; break;
    case WideMode::W21x9:  ratio = 43.0 / 18.0; break;   // 3440x1440, 2560x1080 screens
    default: return 0;
    }
    const double w = base_lines(s) * ratio / pixel_aspect(s, a);
    const int ext = (int)((w - base_width(s)) / 2.0 + 0.5);
    // Mega Drive     CRT 32 54 80 128, square 20 40 64 108
    // SNES           CRT 30 46 68 106, square 52 72 96 140
    // Master System  CRT  6 22 40  74, square 26 44 64 102
    return std::max(0, (ext + 1) & ~1);
}

WideMode fit_wide_mode(WideMode m, AspectMode a, System s, int max_ext) {
    int t = std::clamp((int)m, 0, (int)WideMode::Count - 1);
    while (t > 0 && wide_ext((WideMode)t, a, s) > max_ext) t--;
    return (WideMode)t;
}

double filter_cutoff(AudioFilter f, System s) {
    if (f == AudioFilter::Off) return 0.0;
    const bool warm = f == AudioFilter::Warm;
    switch (s) {
    case System::MegaDrive: return warm ? 2400.0 : 3390.0;    // Console: the Model 1's output filter
    case System::Snes: return warm ? 4200.0 : 9000.0;         // Console: a TV's audio
    default: return warm ? 3400.0 : 8000.0;
    }
}

const char* color_mode_name(ColorMode m) {
    static const char* n[] = {"Accurate", "Linear", "Vivid", "Warm CRT", "Cool", "Grayscale", "Sepia", "Handheld LCD"};
    int i = (int)m;
    return i >= 0 && i < (int)ColorMode::Count ? n[i] : "?";
}
const char* wide_mode_name(WideMode m) {
    static const char* n[] = {"Off (original)", "16:10", "16:9", "2:1", "21:9 (ultrawide)"};
    int i = (int)m;
    return i >= 0 && i < (int)WideMode::Count ? n[i] : "?";
}
const char* aspect_mode_name(AspectMode m) {
    static const char* n[] = {"CRT (4:3 TV)", "Square pixels", "Stretch to window"};
    int i = (int)m;
    return i >= 0 && i < (int)AspectMode::Count ? n[i] : "?";
}
const char* audio_filter_name(AudioFilter f) {
    static const char* n[] = {"Off (raw)", "Console", "Warm"};
    int i = (int)f;
    return i >= 0 && i < (int)AudioFilter::Count ? n[i] : "?";
}

bool Settings::cheat(const std::string& game, const std::string& key) const {
    auto g = cheats.find(game);
    if (g == cheats.end()) return false;
    auto c = g->second.find(key);
    return c != g->second.end() && c->second;
}
void Settings::set_cheat(const std::string& game, const std::string& key, bool on) { cheats[game][key] = on; }

// ---------------------------------------------------------------------------
// INI I/O
// ---------------------------------------------------------------------------
namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}

bool Settings::load(const std::wstring& path) {
    std::string text;
    if (!read_text(path, text)) return false;
    std::istringstream f(text);
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') { section = trim(line.substr(1, line.find(']') - 1)); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        int iv = atoi(v.c_str());
        if (section == "video") {
            if (k == "fullscreen") fullscreen = iv != 0;
            else if (k == "monitor") monitor = v;
            else if (k == "window_scale") window_scale = clampi(iv, 1, 8);
            else if (k == "vsync") vsync = iv != 0;
            else if (k == "filter") filter = clampi(iv, 0, 4);
            else if (k == "widescreen") widescreen = (WideMode)clampi(iv, 0, (int)WideMode::Count - 1);
            else if (k == "aspect") aspect = (AspectMode)clampi(iv, 0, (int)AspectMode::Count - 1);
            else if (k == "integer_scale") integer_scale = iv != 0;
            else if (k == "color") color = (ColorMode)clampi(iv, 0, (int)ColorMode::Count - 1);
            else if (k == "brightness") brightness = clampi(iv, -50, 50);
            else if (k == "saturation") saturation = clampi(iv, -100, 100);
            else if (k == "gamma") gamma = clampi(iv, -50, 50);
            else if (k == "no_sprite_limit") no_sprite_limit = iv != 0;
            else if (k == "hud_anchor") hud_anchor = iv != 0;
            else if (k == "show_fps") show_fps = iv != 0;
            else if (k == "black_frames") black_frames = iv != 0;
        } else if (section == "audio") {
            if (k == "volume") volume = clampi(iv, 0, 100);
            else if (k == "filter") audio_filter = (AudioFilter)clampi(iv, 0, (int)AudioFilter::Count - 1);
            else if (k == "ladder_effect") ladder_effect = iv != 0;
            else if (k == "interpolation") interpolation = clampi(iv, 0, 1);
            else if (k == "soundtrack_volume") soundtrack_volume = clampi(iv, 0, 100);
            else if (k.rfind("soundtrack_", 0) == 0 && k.size() > 11) soundtrack[k.substr(11)] = v;   // soundtrack_md=<pack>
        } else if (section == "input1" || section == "input2") {
            int p = section == "input1" ? 0 : 1;
            for (int s = 0; s < kSystemCount; s++) {   // md_key_a=90, snes_pad_y=0x40
                int n;
                const ActionDef* d = actions((System)s, n);
                const std::string base = std::string(kSystemKeys[s]) + "_";
                for (int a = 0; a < n; a++) {
                    if (k == base + "key_" + d[a].key) keys[s][p][a] = iv;
                    else if (k == base + "pad_" + d[a].key) pad_buttons[s][p][a] = (uint32_t)strtoul(v.c_str(), nullptr, 0);
                }
            }
            for (int h = 0; h < HK_COUNT && p == 0; h++) {
                if (k == std::string("key_") + kHotkeyKeys[h]) hotkey_keys[h] = iv;
                else if (k == std::string("pad_") + kHotkeyKeys[h]) hotkey_pads[h] = (uint32_t)strtoul(v.c_str(), nullptr, 0);
            }
            if (k == "pad_index") pad_index[p] = clampi(iv, -1, Input::kMaxPads - 1);
            else if (k == "stick_as_dpad" && p == 0) stick_as_dpad = iv != 0;
        } else if (section.rfind("cheats.", 0) == 0) {
            cheats[section.substr(7)][k] = iv != 0;
        } else if (section == "touch") {
            if (k == "mode") touch_mode = clampi(iv, 0, 2);
            else if (k == "opacity") touch_opacity = clampi(iv, 10, 100);
        } else if (section == "general") {
            if (k == "last_game") last_game = v;
            else if (k == "fast_forward") fast_forward = iv != 0;
            else if (k == "rewind") rewind = iv != 0;
            else if (k == "achievements") achievements = iv != 0;
            else if (k == "no_slowdown") no_slowdown = iv != 0;
        }
    }
    return true;
}

bool Settings::save(const std::wstring& path) const {
    std::ostringstream o;
    o << "; Aladdin Collection - settings\n\n[video]\n"
      << "fullscreen=" << fullscreen << "\nmonitor=" << monitor << "\nwindow_scale=" << window_scale << "\nvsync=" << vsync
      << "\nfilter=" << filter << "\nwidescreen=" << (int)widescreen << "\naspect=" << (int)aspect
      << "\ninteger_scale=" << integer_scale << "\ncolor=" << (int)color << "\nbrightness=" << brightness
      << "\nsaturation=" << saturation << "\ngamma=" << gamma << "\nno_sprite_limit=" << no_sprite_limit
      << "\nhud_anchor=" << hud_anchor << "\nshow_fps=" << show_fps << "\nblack_frames=" << black_frames << "\n\n[audio]\n"
      << "volume=" << volume << "\nfilter=" << (int)audio_filter << "\nladder_effect=" << ladder_effect
      << "\ninterpolation=" << interpolation << "\nsoundtrack_volume=" << soundtrack_volume << "\n";
    for (const auto& kv : soundtrack)
        if (!kv.second.empty()) o << "soundtrack_" << kv.first << "=" << kv.second << "\n";
    for (int p = 0; p < 2; p++) {
        o << "\n[input" << (p + 1) << "]\n";
        for (int s = 0; s < kSystemCount; s++) {
            int n;
            const ActionDef* d = actions((System)s, n);
            for (int a = 0; a < n; a++) o << kSystemKeys[s] << "_key_" << d[a].key << "=" << keys[s][p][a] << "\n";
            for (int a = 0; a < n; a++)
                o << kSystemKeys[s] << "_pad_" << d[a].key << "=0x" << std::hex << pad_buttons[s][p][a] << std::dec << "\n";
        }
        o << "pad_index=" << pad_index[p] << "\n";
        if (p == 0) {
            o << "stick_as_dpad=" << stick_as_dpad << "\n";
            for (int h = 0; h < HK_COUNT; h++)
                o << "key_" << kHotkeyKeys[h] << "=" << hotkey_keys[h] << "\npad_" << kHotkeyKeys[h] << "=0x" << std::hex
                  << hotkey_pads[h] << std::dec << "\n";
        }
    }
    for (auto& g : cheats) {
        o << "\n[cheats." << g.first << "]\n";
        for (auto& c : g.second) o << c.first << "=" << c.second << "\n";
    }
    o << "\n[general]\nlast_game=" << last_game << "\nfast_forward=" << fast_forward << "\nrewind=" << rewind
      << "\nachievements=" << achievements << "\nno_slowdown=" << no_slowdown << "\n";
#ifdef __ANDROID__
    o << "\n[touch]\nmode=" << touch_mode << "\nopacity=" << touch_opacity << "\n";
#endif
    const std::string s = o.str();
    return write_file(path, s.data(), s.size());
}
