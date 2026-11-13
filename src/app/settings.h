// Persistent user settings (settings.ini next to the executable).
#pragma once
#include <cstdint>
#include <string>
#include <map>
#include "../game/game.h"

enum class ColorMode : int {
    Accurate = 0,   // the console's measured output levels (SNES: through a CRT's gamma)
    Linear,         // evenly spaced levels (brighter midtones, common in emulators)
    Vivid,          // accurate + extra saturation/contrast
    Warm,           // soft warm CRT-like tint
    Cool,           // slightly cool, crisp
    Grayscale,
    Sepia,
    Handheld,       // 4-shade green LCD look
    Count
};

enum class WideMode : int { Off = 0, W16x10, W16x9, W2x1, W21x9, Count };   // the picture's shape (width: wide_ext)
enum class AspectMode : int { Crt = 0, Square, Stretch, Count };            // CRT: the console's own pixels (4:3 TV)
enum class AudioFilter : int { Off = 0, Console, Warm, Count };             // output low-pass: none, the console's, softer

// Pad actions (bindable), per console. The first four are the D-pad on every console.
enum : int { ACT_UP = 0, ACT_DOWN, ACT_LEFT, ACT_RIGHT };
constexpr int kMaxActions = 12;
int action_count(System s);                 // MD 8, SNES 12, Master System 7
const char* action_name(System s, int act); // "A", "Start", "Button 1", ...
uint16_t action_bit(System s, int act);     // the console's pad bit for an action

// For whoever presses them: fast forward and rewind are held, the others pressed.
enum Hotkey : int { HK_FAST_FORWARD = 0, HK_REWIND, HK_MENU, HK_SAVE_STATE, HK_LOAD_STATE, HK_SCREENSHOT, HK_COUNT };

struct Settings {
    // Video
    bool        fullscreen = true;          // first launch opens fullscreen (F11 / Alt+Enter for a window)
    std::string monitor;                    // GDI device name of the display to use (empty = primary)
    int         window_scale = 3;
    bool        vsync = true;
    int         filter = 1;                 // ScaleFilter
    std::string slang_preset;               // a slang shader preset (.slangp) in place of the filter: relative to the
                                            // exe's folder (or absolute), UTF-8; empty = off (Windows)
    WideMode    widescreen = WideMode::W16x9;
    AspectMode  aspect = AspectMode::Crt;
    bool        integer_scale = false;
    ColorMode   color = ColorMode::Accurate;
    int         brightness = 0;             // -50..50
    int         saturation = 0;             // -100..100
    int         gamma = 0;                  // -50..50
    bool        no_sprite_limit = true;     // removes flicker (sprites per line)
    bool        hud_anchor = true;          // re-anchor HUD to the screen edges in widescreen
    bool        show_fps = false;
    bool        black_frames = false;       // black frame insertion (120 / 180 / 240 Hz displays)

    // Audio
    int         volume = 80;                // 0..100
    int         soundtrack_volume = 70;     // MD+ soundtracks, relative to Volume
    std::map<std::string, std::string> soundtrack;   // game id -> soundtrack pack (folder name); none = the original
    AudioFilter audio_filter = AudioFilter::Console;
    bool        ladder_effect = true;       // Mega Drive: the YM2612's "ladder effect"
    int         interpolation = 0;          // SNES: DSP sample interpolation, 0 gaussian (original), 1 cubic (crisper)

    // Input: keyboard virtual-key codes and pad button bits (PadButton) per console, player 1/2 and action
    int         keys[kSystemCount][2][kMaxActions];
    uint32_t    pad_buttons[kSystemCount][2][kMaxActions];
    int         pad_index[2] = {-1, -1};    // -1 = first connected pad (player 1) / second (player 2)
    bool        stick_as_dpad = true;
    int         hotkey_keys[HK_COUNT];      // a key and a pad button for each Hotkey (0 = none)
    uint32_t    hotkey_pads[HK_COUNT];

    // Hold-to-use speed controls (by default keyboard Tab / R, pad right / left trigger: hotkey_keys / hotkey_pads)
    bool        fast_forward = true;
    bool        rewind = true;
    // Achievements on: they can be earned, and cheats, rewind and fast forward are off
    bool        achievements = true;
    // SNES: the CPU runs the game's frame work faster (GameConfig::cpu_turbo = kNoSlowdownTurbo): no slowdown
    bool        no_slowdown = true;

    // Update check: once a day the launcher reads the latest release's version on GitHub (nothing else is sent)
    bool        check_updates = true;
    std::string update_checked_on;          // "YYYY-MM-DD" of the last check that got an answer
    std::string update_latest;              // the latest release's tag it found ("v1.1")

    // Touch screens (Android): on-screen pad 0 = when the screen is touched, 1 = always, 2 = never; opacity %
    int         touch_mode = 0;
    int         touch_opacity = 55;

    // Cheats per game: game id -> cheat key -> enabled
    std::map<std::string, std::map<std::string, bool>> cheats;

    // Launcher and menus: the menus' look (UiStyle), and the player's own pictures on the launcher (none are shipped:
    // covers/<game id>.png or .jpg and covers/background.png in the data folder)
    int         menu_style = 0;             // 0 Arabian Nights, 1 Classic
    bool        launcher_covers = false;    // the game cards show covers/<id> in place of the live previews
    bool        launcher_background = false;   // the launcher's background is covers/background
    bool        cover_fill = false;         // covers fill their frames (trimmed); else shown whole

    std::string last_game;

    Settings();
    void reset_keys();
    void reset_keys(System s, int player);   // one console's bindings for one player
    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;

    bool cheat(const std::string& game, const std::string& key) const;
    void set_cheat(const std::string& game, const std::string& key, bool on);
};

// The controller slot a player (0/1) uses, or -1: their chosen slot, or (Auto, pad_index -1) the first
// connected pad the other player isn't using - so two players never share a pad unless both picked it.
int resolve_player_pad(const int pad_index[2], const bool* connected, int slots, int player);

// The console's picture: base_width x base_lines pixels, each `pixel_aspect` wide (as tall = 1) on a 4:3 TV.
int    base_width(System s);
int    base_lines(System s);
double pixel_aspect(System s, AspectMode a);
// Extra pixels per side for a widescreen mode on a console: the picture gets that shape.
int wide_ext(WideMode m, AspectMode a, System s);
// The widest mode up to `m` whose extension fits `max_ext` (a game's limit).
WideMode fit_wide_mode(WideMode m, AspectMode a, System s, int max_ext);
// The low-pass cutoff (Hz) of an output filter on a console (0 = none).
double filter_cutoff(AudioFilter f, System s);
constexpr int kNoSlowdownTurbo = 4;   // Remove Slowdown: the SNES CPU's speed for the game's frame work (Snes::cpuTurbo)

const char* color_mode_name(ColorMode m);
const char* wide_mode_name(WideMode m);
const char* aspect_mode_name(AspectMode m);
const char* audio_filter_name(AudioFilter f);
