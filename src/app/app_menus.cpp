// Pause menu, options pages, cheats, level select.
#include "app.h"
#include <cstdio>

namespace {
std::string pct(int v) { return std::to_string(v) + "%"; }
std::string signed_val(int v) { return (v > 0 ? "+" : "") + std::to_string(v); }
}

void App::open_pause_menu() {
    if (!session_) return;
    GameModule* m = session_->module();
    MenuPage p;
    p.title = m->title();
    p.subtitle = std::string(system_name(m->system())) + "  \xE2\x80\xA2  Paused";
    p.items.push_back(MenuItem::action("Resume", [this]() { menu_.close(); }));
    p.items.push_back(MenuItem::header("Save states"));
    p.items.push_back(MenuItem::choice("Slot", {"1", "2", "3", "4", "5"}, [this]() { return state_slot_ - 1; },
                                       [this](int v) { state_slot_ = v + 1; }, "Choose the save-state slot (F5 saves, F9 loads in game)."));
    p.items.push_back(MenuItem::action("Save State", [this]() { quick_save(); menu_.close(); }));
    p.items.push_back(MenuItem::action("Load State", [this]() {
        FILE* f = open_file(save_path(state_slot_), "rb");
        if (!f) { menu_.toast("Slot " + std::to_string(state_slot_) + " is empty"); return; }
        fclose(f);
        quick_load();
        menu_.close();
    }));
    p.items.push_back(MenuItem::header("Game"));
    // with achievements on, the level select and cheats are greyed out (Options -> Achievements switches them back)
    auto locked = [this]() { return std::string(settings_.achievements ? "Off (achievements on)" : ""); };
    if (!m->levels().empty()) {
        MenuItem it = MenuItem::action("Level Select", [this]() { menu_.push(level_page()); }, "Jump straight to any level.");
        it.enabled = [this]() { return !settings_.achievements; };
        it.value_text = locked;
        p.items.push_back(it);
    }
    if (!m->cheats().empty()) {
        MenuItem it = MenuItem::action("Cheats", [this]() { menu_.push(cheats_page()); }, "Toggle cheats. They apply immediately.");
        it.enabled = [this]() { return !settings_.achievements; };
        it.value_text = locked;
        p.items.push_back(it);
    }
    p.items.push_back(MenuItem::action("Achievements", [this]() { menu_.push(achievements_page(current_)); },
                                       "This game's achievements and the collection's."));
    if (!m->sounds().empty()) {
        p.items.push_back(MenuItem::action("Sound Test", [this]() { open_jukebox(current_, true); },
                                           "Play any music track or sound effect from the game."));
        p.items.push_back(MenuItem::action("Music Player", [this]() { open_jukebox(current_, false); },
                                           "Listen to the soundtrack."));
    }
    if (m->max_players() > 1)
        p.items.push_back(MenuItem::action("Player 2 Controls", [this]() { menu_.push(controls_page(session_->system(), 1)); },
                                           "Set up the second player's keyboard keys or gamepad."));
    p.items.push_back(MenuItem::action("Options", [this]() { open_options_menu(false); }));
    p.items.push_back(MenuItem::action("Credits", [this]() { open_credits(false); }));
    p.items.push_back(MenuItem::header("System"));
    p.items.push_back(MenuItem::action("Reset Game", [this]() {
        menu_.confirm("Reset the game?", [this]() {
            reset_game();
            menu_.close();
        }, "Unsaved progress will be lost.");
    }));
    p.items.push_back(MenuItem::action("Return to Launcher", [this]() {
        menu_.confirm("Return to the launcher?", [this]() { back_to_launcher(); }, "Unsaved progress will be lost.");
    }));
    p.items.push_back(MenuItem::action("Quit to Desktop", [this]() {
        menu_.confirm("Quit to desktop?", [this]() { running_ = false; }, "Unsaved progress will be lost.");
    }));
    menu_.open(std::move(p));
}

void App::open_options_menu(bool from_launcher) {
    MenuPage p;
    p.title = "Options";
    p.items.push_back(MenuItem::action("Video", [this]() { menu_.push(video_page()); }, "Display mode, widescreen, scaling and filters."));
    p.items.push_back(MenuItem::action("Colour", [this]() { menu_.push(color_page()); }, "Colour profiles and picture adjustments."));
    p.items.push_back(MenuItem::action("Audio", [this]() { menu_.push(audio_page()); }, "Volume and sound-chip options."));
    p.items.push_back(MenuItem::action("Launcher & Menus", [this]() { menu_.push(launcher_page()); },
                                       "The menus' look, and your own cover pictures and background for the launcher."));
    // Each console has its own buttons: in a game its console's bindings, from the launcher all three.
    if (session_ && !from_launcher) {
        const System sys = session_->system();
        p.items.push_back(MenuItem::action("Controls", [this, sys]() { menu_.push(controls_page(sys, 0)); },
                                           std::string("Keyboard and gamepad bindings for ") + system_name(sys) + " games."));
    } else {
        for (int sy = 0; sy < kSystemCount; sy++) {
            const System sys = (System)sy;
            p.items.push_back(MenuItem::action(std::string("Controls - ") + system_name(sys),
                                               [this, sys]() { menu_.push(controls_page(sys, 0)); },
                                               std::string("Keyboard and gamepad bindings for the ") + system_name(sys) + " game."));
        }
    }
    p.items.push_back(MenuItem::header("Speed"));
    // (beside each switch: what to hold, or why it is off)
    MenuItem ff = MenuItem::toggle("Fast Forward", [this]() { return settings_.fast_forward && !settings_.achievements; },
                                   [this](bool v) { settings_.fast_forward = v; },
                                   "Hold the fast forward key or button for 4x speed (they can be changed under Controls).");
    ff.enabled = [this]() { return !settings_.achievements; };
    ff.value_text = [this]() { return settings_.achievements ? std::string("Off (achievements on)") : hotkey_text(HK_FAST_FORWARD); };
    p.items.push_back(ff);
    MenuItem rw = MenuItem::toggle("Rewind", [this]() { return settings_.rewind && !settings_.achievements; },
                                   [this](bool v) { settings_.rewind = v; if (!v) rewind_.clear(); },
                                   "Hold the rewind key or button to step back up to a minute (they can be changed under Controls).");
    rw.enabled = [this]() { return !settings_.achievements; };
    rw.value_text = [this]() { return settings_.achievements ? std::string("Off (achievements on)") : hotkey_text(HK_REWIND); };
    p.items.push_back(rw);
    // (the SNES game only: offered in it and from the launcher)
    if (!session_ || session_->system() == System::Snes)
        p.items.push_back(MenuItem::toggle("Remove Slowdown", [this]() { return settings_.no_slowdown; },
                                           [this](bool v) { settings_.no_slowdown = v; apply_settings(); },
                                           "SNES: the console's CPU runs the game's work faster where the original slowed down. Off: the original's speed."));
    // (right under what it switches off)
    p.items.push_back(MenuItem::header("Achievements"));
    p.items.push_back(achievements_switch());
    add_update_items(p);
#ifdef __ANDROID__
    p.items.push_back(MenuItem::header("Touch screen"));
    p.items.push_back(MenuItem::choice("On-screen Controls", {"When touched", "Always", "Off"}, [this]() { return settings_.touch_mode; },
                                       [this](int v) { settings_.touch_mode = v; touch_.visible = touch_active_ ? v != 2 : v == 1; },
                                       "When touched: the pad shows while you use the screen and hides when a controller is used."));
    p.items.push_back(MenuItem::slider("Controls Opacity", 10, 100, 5, [this]() { return settings_.touch_opacity; },
                                       [this](int v) { settings_.touch_opacity = v; }, [](int v) { return std::to_string(v) + "%"; }));
#endif
#ifdef _WIN32   // (keyboard hotkeys: a phone rarely has a keyboard)
    p.items.push_back(MenuItem::header("Hotkeys (changed under Controls)"));
    p.items.push_back(MenuItem::info("Menu", [this]() { return hotkey_text(HK_MENU); }));
    p.items.push_back(MenuItem::info("Quick save", [this]() { return hotkey_text(HK_SAVE_STATE); }));
    p.items.push_back(MenuItem::info("Quick load", [this]() { return hotkey_text(HK_LOAD_STATE); }));
    p.items.push_back(MenuItem::info("Screenshot", [this]() { return hotkey_text(HK_SCREENSHOT); }));
    p.items.push_back(MenuItem::info("Fullscreen", []() { return std::string("F11 / Alt+Enter"); }));
#endif
    p.on_back = [this]() { save_settings(); };
    if (from_launcher) menu_.open(std::move(p));
    else menu_.push(std::move(p));
}

void App::open_credits(bool from_launcher) {
    MenuPage p;
    p.title = "Credits";
    p.subtitle = std::string("Aladdin Collection  \xE2\x80\xA2  v") + kAppVersion;
    p.wide = true;
    p.items.push_back(MenuItem::header("Find me over at:"));
    auto link = [this](const char* label, const char* shown, const char* url) {
        MenuItem it = MenuItem::action(label, [this, label, url]() {
            if (autotest_) return;   // scripted test runs never open a browser
            open_url(url);
            menu_.toast(std::string("Opened ") + label + " in your browser");
        }, "Enter / (A) opens it in your web browser.");
        it.value_text = [shown]() { return std::string(shown); };
        return it;
    };
    p.items.push_back(link("YouTube", "youtube.com/adhgaming", "https://www.youtube.com/adhgaming"));
    p.items.push_back(link("Reddit", "u/Darhkwing", "https://www.reddit.com/user/Darhkwing"));
    p.items.push_back(link("GitHub", "github.com/Darhkfox", "https://github.com/Darhkfox"));
    p.items.push_back(link("X", "x.com/ADH_Gaming", "https://x.com/ADH_Gaming"));
    p.items.push_back(link("Patreon", "patreon.com/c/TeamRedTail", "https://www.patreon.com/c/TeamRedTail"));
    p.items.push_back(MenuItem::header("The games"));
    for (const GameSlot& g : games_) {
        const GameModule* m = g.module.get();
        p.items.push_back(MenuItem::info(game_label(m), [m]() { return std::string(m->subtitle()) + ", " + m->year(); }));
    }
    p.items.push_back(MenuItem::info("Disney's Aladdin \xC2\xA9 Disney. An unofficial fan project; it contains no game data."));
    // (the third-party parts the package ships; licences in README.md / readme.txt and next to the exe)
    p.items.push_back(MenuItem::header("Made with"));
#ifdef _WIN32
    p.items.push_back(MenuItem::info("Slang shaders", []() { return std::string("librashader by Ronny Chan (MPL 2.0)"); }));
    p.items.push_back(MenuItem::info("CRT presets", []() { return std::string("libretro slang-shaders"); }));
#else
    p.items.push_back(MenuItem::info("SDL2", []() { return std::string("Sam Lantinga and contributors (zlib)"); }));
#endif
    p.items.push_back(MenuItem::info("Cover pictures", []() { return std::string("stb_image by Sean Barrett (public domain)"); }));
    if (from_launcher) menu_.open(std::move(p));
    else menu_.push(std::move(p));   // from the pause menu: Back returns to it
}

MenuPage App::launcher_page() {
    MenuPage p;
    p.title = "Launcher & Menus";
    p.wide = true;
    p.items.push_back(MenuItem::choice("Menu Style", {"Arabian Nights", "Classic"}, [this]() { return settings_.menu_style; },
                                       [this](int v) { settings_.menu_style = v; apply_ui_style(); },
                                       "Arabian Nights: deep indigo and gold arabesques under the stars. Classic: the original dark panels."));
    p.items.push_back(MenuItem::header("Your own pictures (none are included)"));
    // (beside each switch: how many of its pictures were found)
    MenuItem covers = MenuItem::toggle("Game Covers", [this]() { return settings_.launcher_covers; },
                                       [this](bool v) { settings_.launcher_covers = v; load_launcher_art(true); },
                                       "Your cover pictures on the game cards in place of the live previews.");
    covers.value_text = [this]() {
        if (!settings_.launcher_covers) return std::string();
        int n = 0;
        for (int g = 0; g < kGameCount; g++) n += has_cover(g) ? 1 : 0;
        return n ? std::to_string(n) + " of " + std::to_string(kGameCount) + " found" : std::string("none found");
    };
    p.items.push_back(covers);
    MenuItem bg = MenuItem::toggle("Custom Background", [this]() { return settings_.launcher_background; },
                                   [this](bool v) { settings_.launcher_background = v; load_launcher_art(true); },
                                   "Your picture behind the launcher (filling the screen; the middle is kept).");
    bg.value_text = [this]() {
        return settings_.launcher_background && !bg_art_.w ? std::string("not found") : std::string();
    };
    p.items.push_back(bg);
    p.items.push_back(MenuItem::choice("Cover Shape", {"Whole Cover", "Fill the Frame"}, [this]() { return settings_.cover_fill ? 1 : 0; },
                                       [this](int v) { settings_.cover_fill = v == 1; },
                                       "Whole Cover shows all of it (bars beside a tall box); Fill the Frame trims it to fill."));
#ifdef __ANDROID__
    for (int g = 0; g < kGameCount; g++) {
        const std::string id = games_[g].module->id();
        p.items.push_back(MenuItem::action(std::string("Choose Cover: ") + system_name(games_[g].module->system()),
                                           [this, id]() { pick_image(id); },
                                           "Pick a picture (PNG or JPEG) from your device for this game's card."));
    }
    p.items.push_back(MenuItem::action("Choose Background", [this]() { pick_image("background"); },
                                       "Pick a picture (PNG or JPEG) from your device for the launcher's background."));
#else
    p.items.push_back(MenuItem::action("Open Covers Folder", [this]() {
        make_dir(covers_dir());
        if (!autotest_) open_url(to_utf8(covers_dir()).c_str());
    }, "Put md, snes, sms (the covers) and background there as .png or .jpg; they're read when you leave this page."));
    p.items.push_back(MenuItem::info("Covers: md, snes, sms    Background: background    (.png or .jpg)"));
#endif
    p.on_back = [this]() { load_launcher_art(true); save_settings(); };
    return p;
}

MenuPage App::video_page() {
    MenuPage p;
    p.title = "Video";
    auto changed = [this]() { apply_settings(); };
#ifdef _WIN32
    p.items.push_back(MenuItem::choice("Display Mode", {"Windowed", "Fullscreen"}, [this]() { return settings_.fullscreen ? 1 : 0; },
                                       [this, changed](int v) { settings_.fullscreen = v == 1; changed(); }));
    {
        std::vector<std::string> labels;
        for (const MonitorInfo& m : enum_monitors()) labels.push_back(m.label);
        p.items.push_back(MenuItem::choice("Monitor", labels, [this]() {
            std::vector<MonitorInfo> l = enum_monitors();
            HMONITOR cur = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
            for (int i = 0; i < (int)l.size(); i++) if (l[i].handle == cur) return i;
            return 0;
        }, [this](int v) { move_to_monitor(v); save_settings(); },
        "Which display the game uses. You can also drag the window to another screen."));
    }
    MenuItem ws = MenuItem::slider("Window Size", 1, 6, 1, [this]() { return settings_.window_scale; },
                                   [this](int v) { settings_.window_scale = v; resize_window_to_scale(); },
                                   [](int v) { return std::to_string(v) + "x"; }, "Size of the window in windowed mode.");
    ws.enabled = [this]() { return !settings_.fullscreen; };
    p.items.push_back(ws);
#endif
    {
        // In a game only the widths it supports are offered (the setting is shared: a wider one chosen in
        // another game shows here as this game's widest).
        int top = (int)WideMode::Count - 1;
        if (session_)
            top = (int)fit_wide_mode(WideMode::W21x9, settings_.aspect, session_->system(), session_->module()->max_ext());
        std::vector<std::string> labels;
        for (int i = 0; i <= top; i++) labels.push_back(wide_mode_name((WideMode)i));
        p.items.push_back(MenuItem::choice("Widescreen", labels, [this, top]() { return std::min((int)settings_.widescreen, top); },
                                           [this, changed](int v) { settings_.widescreen = (WideMode)v; changed(); resize_window_to_scale(); },
                                           "Shows more of each level. Title screens, menus and cut-scenes keep their original shape."));
    }
    MenuItem hud = MenuItem::toggle("Re-anchor HUD", [this]() { return settings_.hud_anchor; },
                                    [this, changed](bool v) { settings_.hud_anchor = v; changed(); },
                                    "In widescreen, moves the status bar items out to the screen edges.");
    hud.enabled = [this]() { return settings_.widescreen != WideMode::Off; };
    p.items.push_back(hud);
    p.items.push_back(MenuItem::choice("Pixel Aspect", {aspect_mode_name(AspectMode::Crt), aspect_mode_name(AspectMode::Square),
                                                        aspect_mode_name(AspectMode::Stretch)},
                                       [this]() { return (int)settings_.aspect; },
                                       [this, changed](int v) { settings_.aspect = (AspectMode)v; changed(); resize_window_to_scale(); },
                                       "CRT shows each console's pixels as a 4:3 television did. Widescreen widths follow it."));
    p.items.push_back(MenuItem::toggle("Integer Scaling", [this]() { return settings_.integer_scale; },
                                       [this](bool v) { settings_.integer_scale = v; }, "Scale only by whole numbers for perfectly even pixels."));
    MenuItem filter = MenuItem::choice("Filter", {"Nearest (sharp)", "Sharp bilinear", "Bilinear (smooth)", "Scanlines", "CRT"},
                                       [this]() { return settings_.filter; }, [this](int v) { settings_.filter = v; });
#ifdef _WIN32
    filter.enabled = [this]() { return settings_.slang_preset.empty(); };   // (a slang shader draws instead)
    p.items.push_back(filter);
    MenuItem slang = MenuItem::action("Slang Shader", [this]() { menu_.push(slang_page()); },
                                      "RetroArch slang shader presets (.slangp) from the 'shaders' folder next to the game, "
                                      "in place of the filter above.");
    slang.value_text = [this]() {
        if (settings_.slang_preset.empty()) return std::string("Off");
        std::string n = settings_.slang_preset.substr(settings_.slang_preset.find_last_of("\\/") + 1);
        return n.size() > 7 ? n.substr(0, n.size() - 7) : n;
    };
    p.items.push_back(slang);
#else
    p.items.push_back(filter);
#endif
#ifdef _WIN32
    p.items.push_back(MenuItem::toggle("VSync", [this]() { return settings_.vsync; }, [this, changed](bool v) { settings_.vsync = v; changed(); }));
#endif
    MenuItem bfi = MenuItem::toggle("Black Frame Insertion", [this]() { return settings_.black_frames; }, [this](bool v) {
        settings_.black_frames = v;
#ifdef __ANDROID__
        set_frame_rate();   // (asks for 120 Hz)
#endif
    }, "Sharper motion: a black frame between the game's frames, like a CRT. Needs a 120 Hz screen; the picture gets darker.");
    bfi.value_text = [this]() { return black_frames_note(); };
    p.items.push_back(bfi);
#ifdef _WIN32
    MenuItem hfr = MenuItem::toggle("High Frame Rate", [this]() { return settings_.high_frame_rate; },
                                    [this](bool v) { settings_.high_frame_rate = v; },
                                    "Smoother motion on 100 Hz and faster screens: the refreshes between the game's 60 frames "
                                    "show in-between pictures (scrolling and sprites part of the way). The game still runs at "
                                    "60; the picture is one frame later. Not with Black Frame Insertion.");
    hfr.value_text = [this]() { return high_frame_rate_note(); };
    p.items.push_back(hfr);
#endif
    p.items.push_back(MenuItem::toggle("Remove Sprite Flicker", [this]() { return settings_.no_sprite_limit; },
                                       [this, changed](bool v) { settings_.no_sprite_limit = v; changed(); },
                                       "Lifts the hardware limit on sprites per line."));
    p.items.push_back(MenuItem::toggle("Show FPS", [this]() { return settings_.show_fps; }, [this](bool v) { settings_.show_fps = v; }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::color_page() {
    MenuPage p;
    p.title = "Colour";
    auto changed = [this]() { apply_settings(); };
    std::vector<std::string> modes;
    for (int i = 0; i < (int)ColorMode::Count; i++) modes.push_back(color_mode_name((ColorMode)i));
    p.items.push_back(MenuItem::choice("Colour Profile", modes, [this]() { return (int)settings_.color; },
                                       [this, changed](int v) { settings_.color = (ColorMode)v; changed(); },
                                       "Accurate uses each console's measured output levels."));
    p.items.push_back(MenuItem::slider("Brightness", -50, 50, 5, [this]() { return settings_.brightness; },
                                       [this, changed](int v) { settings_.brightness = v; changed(); }, signed_val));
    p.items.push_back(MenuItem::slider("Saturation", -100, 100, 10, [this]() { return settings_.saturation; },
                                       [this, changed](int v) { settings_.saturation = v; changed(); }, signed_val));
    p.items.push_back(MenuItem::slider("Gamma", -50, 50, 5, [this]() { return settings_.gamma; },
                                       [this, changed](int v) { settings_.gamma = v; changed(); }, signed_val));
    p.items.push_back(MenuItem::action("Reset Adjustments", [this, changed]() {
        settings_.brightness = settings_.saturation = settings_.gamma = 0;
        changed();
    }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::audio_page() {
    MenuPage p;
    p.title = "Audio";
    auto changed = [this]() { apply_settings(); };
    p.items.push_back(MenuItem::slider("Volume", 0, 100, 5, [this]() { return settings_.volume; },
                                       [this, changed](int v) { settings_.volume = v; changed(); }, pct));
    p.items.push_back(MenuItem::choice("Output Filter", {audio_filter_name(AudioFilter::Off), audio_filter_name(AudioFilter::Console),
                                                         audio_filter_name(AudioFilter::Warm)},
                                       [this]() { return (int)settings_.audio_filter; },
                                       [this, changed](int v) { settings_.audio_filter = (AudioFilter)v; changed(); },
                                       "Console: the low-pass filter of the original console's audio output (Mega Drive Model 1, a TV)."));
    const bool in_game = mode_ == Mode::Playing && session_;
    const System sys = in_game ? session_->system() : System::MegaDrive;
    if (!in_game || sys == System::MegaDrive)
        p.items.push_back(MenuItem::toggle("FM Chip Distortion", [this]() { return settings_.ladder_effect; },
                                           [this, changed](bool v) { settings_.ladder_effect = v; changed(); },
                                           "Mega Drive: the YM2612 'ladder effect' heard on original Model 1 consoles."));
    if (!in_game || sys == System::Snes)
        p.items.push_back(MenuItem::choice("Sample Interpolation", {"Original (gaussian)", "Crisper (cubic)"},
                                           [this]() { return settings_.interpolation; },
                                           [this, changed](int v) { settings_.interpolation = v; changed(); },
                                           "SNES: how the sound chip smooths its samples. Original is the console's own."));
    // Soundtracks found (MD+ packs in the soundtracks folder, MSU-1 packs in the msu folder): per game (in a game, only
    // its own)
    scan_soundtracks();
    bool any = false;
    for (int g = 0; g < kGameCount; g++) {
        if (in_game && g != current_) continue;
        const std::string id = games_[g].module->id();
        std::vector<std::string> labels = {"Original"}, names = {""};
        for (const SoundtrackPack& pk : soundtracks_)
            if (pk.game_id == id) {
                labels.push_back(soundtrack_label(pk));
                names.push_back(pk.name);
            }
        if (names.size() < 2) continue;
        if (!any) p.items.push_back(MenuItem::header("Soundtracks"));
        any = true;
        p.items.push_back(MenuItem::choice(in_game ? std::string("Soundtrack") : game_label(games_[g].module.get()), labels,
            [this, id, names]() {
                auto it = settings_.soundtrack.find(id);
                for (size_t i = 1; it != settings_.soundtrack.end() && i < names.size(); i++) if (names[i] == it->second) return (int)i;
                return 0;
            },
            [this, id, names, in_game](int v) {
                settings_.soundtrack[id] = names[v];
                if (in_game) switch_soundtrack();
            },
            games_[g].module->system() == System::Snes
                ? "CD-quality music from an MSU-1 pack in the msu folder (its tracks and its patch, or a ROM patched for "
                  "them). Changes at once in a game: the song playing starts again in the new music."
                : "CD-quality music from an MD+ pack in the soundtracks folder. Starts with the game (or Reset Game)."));
    }
    if (any)
        p.items.push_back(MenuItem::slider("Soundtrack Volume", 0, 100, 5, [this]() { return settings_.soundtrack_volume; },
                                           [this, changed](int v) { settings_.soundtrack_volume = v; changed(); }, pct));
#ifndef __ANDROID__
    if (!in_game) {
        if (!any) p.items.push_back(MenuItem::header("Soundtracks"));
        any = true;
        for (const wchar_t* folder : {L"soundtracks", L"msu"}) {
            const std::wstring dir = path_join(data_dir(), folder);
            const bool msu = folder[0] == L'm';
            p.items.push_back(MenuItem::action(msu ? "Open MSU-1 Folder" : "Open Soundtracks Folder", [this, dir]() {
                make_dir(dir);
                if (!autotest_) open_url(to_utf8(dir).c_str());
            }, msu ? "SNES: put each MSU-1 pack's folder here (its .pcm tracks with its .bps / .ips patch or its patched .sfc)."
                   : "Mega Drive: put each MD+ pack's folder here (its .cue sheet, .bps patch and .wav music)."));
        }
    }
#endif
#ifdef __ANDROID__
    if (!any) p.items.push_back(MenuItem::header("Soundtracks"));
    p.items.push_back(MenuItem::action("Add Soundtrack\xE2\x80\xA6", [this]() { pick_soundtrack(); },
                                       "Pick a pack's folder: MD+ (a .cue sheet, a .bps patch and the music) or MSU-1 (the "
                                       ".pcm tracks with their patch or patched ROM). It is copied in."));
#endif
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::controls_page(System sys, int player) {
    MenuPage p;
    const int si = (int)sys, n = action_count(sys);
    p.title = player == 1 ? "Controls - Player 2" : "Controls";
    p.subtitle = system_name(sys);
    p.wide = true;
    p.items.push_back(MenuItem::header("Keyboard"));
    for (int a = 0; a < n; a++) {
        MenuItem it;
        it.kind = MenuItem::Bind;
        it.label = std::string(action_name(sys, a));
        it.value_text = [this, si, player, a]() { int vk = settings_.keys[si][player][a]; return vk ? Input::key_name(vk) : std::string("-"); };
        it.on_bind = [this, si, player, a](int vk, uint32_t) {
            // A key can only do one thing on a console: if another action (either player, or a hotkey) has it, they swap.
            for (int q = 0; q < 2; q++)
                for (int b = 0; b < kMaxActions; b++)
                    if (settings_.keys[si][q][b] == vk && (q != player || b != a)) settings_.keys[si][q][b] = settings_.keys[si][player][a];
            for (int h = 0; h < HK_COUNT; h++)
                if (settings_.hotkey_keys[h] == vk) give_hotkey_key(h, settings_.keys[si][player][a], si, player, a);
            settings_.keys[si][player][a] = vk;
        };
        it.hint = "Press Enter, then the new key (a key already in use swaps). Esc cancels.";
        p.items.push_back(it);
    }
    static const char* kHotkeyNames[HK_COUNT] = {"Fast Forward (hold)", "Rewind (hold)", "Menu", "Quick Save", "Quick Load",
                                                 "Screenshot"};
    // (fast forward and rewind are off while achievements are on)
    auto speed = [](int h) { return h == HK_FAST_FORWARD || h == HK_REWIND; };
    if (player == 0)
        for (int h = 0; h < HK_COUNT; h++) {
            MenuItem it;
            it.kind = MenuItem::Bind;
            it.label = kHotkeyNames[h];
            it.value_text = [this, h, speed]() {
                if (speed(h) && settings_.achievements) return std::string("Off (achievements on)");
                int vk = settings_.hotkey_keys[h];
                return vk ? Input::key_name(vk) : std::string("-");
            };
            if (speed(h)) it.enabled = [this]() { return !settings_.achievements; };
            it.on_bind = [this, h](int vk, uint32_t) { bind_hotkey_key(h, vk); };
            it.hint = h == HK_MENU ? "Press Enter, then the new key (a key already in use swaps). Esc always opens the menu too."
                                   : "Press Enter, then the new key (a key already in use swaps). Esc cancels.";
            p.items.push_back(it);
        }
    p.items.push_back(MenuItem::header("Gamepad"));
    // Name the controllers connected now, so a player can tell the slots apart.
    std::vector<std::string> pads = {player == 0 ? "Auto (first free pad)" : "Auto (next free pad)"};
    for (int i = 0; i < Input::kMaxPads; i++) {
        const PadState& ps = input_.pad(i);
        pads.push_back("Slot " + std::to_string(i + 1) +
                       (ps.connected ? " - " + ps.name + (ps.ignored ? " (ignored)" : "") : std::string(" (empty)")));
    }
    if (last_input_pad_) p.sel = (int)p.items.size();   // (opened with a controller: start on its rows)
    p.items.push_back(MenuItem::choice("Device", pads, [this, player]() { return settings_.pad_index[player] + 1; },
                                       [this, player](int v) { settings_.pad_index[player] = v - 1; },
                                       "Auto takes the first connected pad the other player isn't using. Slots 1-4 are Xbox-style "
                                       "pads, 5-8 other controllers."));
    for (int a = 0; a < n; a++) {
        MenuItem it;
        it.kind = MenuItem::Bind;
        it.bind_pad = true;
        it.label = std::string(action_name(sys, a));
        it.value_text = [this, si, player, a]() {
            uint32_t b = settings_.pad_buttons[si][player][a];
            return b ? Input::pad_button_name(b) : std::string("-");
        };
        it.on_bind = [this, si, player, a](int, uint32_t b) {
            for (int c = 0; c < kMaxActions; c++)   // swap with the action (or hotkey) that had this button
                if (settings_.pad_buttons[si][player][c] == b && c != a) settings_.pad_buttons[si][player][c] = settings_.pad_buttons[si][player][a];
            for (int h = 0; h < HK_COUNT; h++)
                if (settings_.hotkey_pads[h] == b) give_hotkey_pad(h, settings_.pad_buttons[si][player][a], si, player, a);
            settings_.pad_buttons[si][player][a] = b;
        };
        it.hint = "Press Enter / (A), then the new pad button. Esc cancels.";
        p.items.push_back(it);
    }
    if (player == 0)
        for (int h = 0; h < HK_COUNT; h++) {
            MenuItem it;
            it.kind = MenuItem::Bind;
            it.bind_pad = true;
            it.label = kHotkeyNames[h];
            it.value_text = [this, h, speed]() {
                if (speed(h) && settings_.achievements) return std::string("Off (achievements on)");
                uint32_t b = settings_.hotkey_pads[h];
                return b ? Input::pad_button_name(b) : std::string("-");
            };
            if (speed(h)) it.enabled = [this]() { return !settings_.achievements; };
            it.on_bind = [this, h](int, uint32_t b) { bind_hotkey_pad(h, b); };
            it.hint = "Press Enter / (A), then the new pad button. Esc cancels.";
            p.items.push_back(it);
        }
    if (player == 0) {
        p.items.push_back(MenuItem::toggle("Left Stick Moves", [this]() { return settings_.stick_as_dpad; },
                                           [this](bool v) { settings_.stick_as_dpad = v; }));
        // (for every console: the menus are the collection's)
        p.items.push_back(MenuItem::choice("Menu Buttons", {"Bottom confirms (Xbox)", "Right confirms (Nintendo)"},
                                           [this]() { return settings_.menu_confirm_right ? 1 : 0; },
                                           [this](int v) { settings_.menu_confirm_right = v == 1; },
                                           "In the menus: which face button chooses; the other one goes back."));
#ifdef _WIN32
        p.items.push_back(MenuItem::toggle("Other Controllers", [this]() { return settings_.other_controllers; },
                                           [this](bool v) { settings_.other_controllers = v; input_.set_dinput_enabled(v); },
                                           "Controllers that aren't Xbox-style (DirectInput: e.g. a Switch Pro or PlayStation pad "
                                           "without Steam). Switch off if one sends presses you didn't make."));
#endif
    }
    p.items.push_back(MenuItem::action("Reset to Defaults", [this, sys, player]() {
        Settings d;
        settings_.reset_keys(sys, player);
        if (player == 0)
            for (int h = 0; h < HK_COUNT; h++) {
                settings_.hotkey_keys[h] = d.hotkey_keys[h];
                settings_.hotkey_pads[h] = d.hotkey_pads[h];
            }
        settings_.pad_index[player] = -1;
        menu_.toast("Controls reset");
    }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::cheats_page() {
    GameModule* m = session_->module();
    MenuPage p;
    p.title = "Cheats";
    p.subtitle = game_label(m);
    std::string gid = m->id();
    for (const CheatEntry& c : m->cheats()) {
        std::string key = c.key;
        std::vector<std::string> excludes = c.excludes;
        p.items.push_back(MenuItem::toggle(c.name, [this, gid, key]() { return settings_.cheat(gid, key); },
                                           [this, gid, key, excludes](bool v) {
                                               settings_.set_cheat(gid, key, v);
                                               if (v) for (const std::string& x : excludes) settings_.set_cheat(gid, x, false);
                                               apply_settings();
                                           }, c.desc));
    }
    p.items.push_back(MenuItem::action("Disable All", [this, gid]() {
        settings_.cheats[gid].clear();
        apply_settings();
    }));
    p.on_back = [this]() { save_settings(); };
    return p;
}

MenuPage App::level_page() {
    GameModule* m = session_->module();
    MenuPage p;
    p.title = "Level Select";
    p.subtitle = game_label(m);
    std::vector<std::string> variants = m->level_variants();
    if (!variants.empty()) {
        if (level_variant_ < 0 || level_variant_ >= (int)variants.size()) level_variant_ = m->level_variant_default();
        p.items.push_back(MenuItem::choice(m->level_variant_label(), variants, [this]() { return level_variant_; },
                                           [this](int v) { level_variant_ = v; },
                                           "Levels that can't be played this way are greyed out."));
    }
    std::string group;
    std::vector<LevelEntry> lv = m->levels();
    for (int i = 0; i < (int)lv.size(); i++) {
        if (lv[i].group != group) { group = lv[i].group; p.items.push_back(MenuItem::header(group)); }
        std::string name = lv[i].name;
        MenuItem it = MenuItem::action(name, [this, i, name, group]() {
            menu_.confirm("Start " + name + "?", [this, i]() {
                if (session_->start_level(i, level_variant_)) {
                    rewind_.clear();
                    ach_new_run(false);
                    ach_ctx_.level_select = true;
                    menu_.close();
                }
                else menu_.toast("Could not start that level");
                block_game_input_ = true;
            }, group + ". Current progress will be lost.", true);
        });
        unsigned mask = lv[i].variants;
        if (mask) it.enabled = [this, mask]() { return ((mask >> level_variant_) & 1) != 0; };
        p.items.push_back(it);
    }
    return p;
}

// A game button took a hotkey's key or pad button: the hotkey gets the button's old one, as a swap - unless another
// console's game button has that one (the hotkey would stop working in that console's game): then the hotkey is left
// without one, and the player is told.
static const char* const kHotkeyLabels[HK_COUNT] = {"Fast Forward", "Rewind", "Menu", "Quick Save", "Quick Load", "Screenshot"};

void App::give_hotkey_key(int h, int old_vk, int sys, int player, int act) {
    bool used = false;
    for (int s = 0; s < kSystemCount; s++)
        for (int p = 0; p < 2; p++)
            for (int a = 0; a < action_count((System)s); a++)
                if (settings_.keys[s][p][a] == old_vk && !(s == sys && p == player && a == act)) used = true;
    settings_.hotkey_keys[h] = used ? 0 : old_vk;
    if (used) menu_.toast(std::string(kHotkeyLabels[h]) + " has no key now: choose one under Controls", 4.0f);
}

void App::give_hotkey_pad(int h, uint32_t old_button, int sys, int player, int act) {
    bool used = false;
    for (int s = 0; s < kSystemCount; s++)
        for (int p = 0; p < 2; p++)
            for (int a = 0; a < action_count((System)s); a++)
                if (settings_.pad_buttons[s][p][a] == old_button && !(s == sys && p == player && a == act)) used = true;
    settings_.hotkey_pads[h] = used ? 0 : old_button;
    if (used) menu_.toast(std::string(kHotkeyLabels[h]) + " has no pad button now: choose one under Controls", 4.0f);
}

void App::open_jukebox(int game, bool sfx) {
    if (game < 0 || game >= kGameCount || !games_[game].rom.found) return;
    if (!sfx) scan_soundtracks();   // (the Music Player offers the game's MD+ soundtracks too)
    jukebox_.open(games_[game].module.get(), games_[game].rom.data, settings_, sfx, soundtracks_);
}
