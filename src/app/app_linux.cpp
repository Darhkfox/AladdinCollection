// Linux (desktop and Steam Deck): SDL window and event loop, fullscreen / window size, the mouse and the touch screen
// on the launcher, and the hidden autotest. The drawing, sound and controllers are the Android build's SDL / OpenGL ES
// layer (src/platform/sdl).
//   AladdinCollection [--game md|snes|sms] [--autotest <out dir> <script> [--capture-size WxH]]
#include "app.h"
#include "../platform/sdl/sdl_keys.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<std::string> g_args;
const std::string kTitle = std::string("Aladdin Collection v") + kAppVersion;

// The first start's widescreen: the widest shape the display has room for, up to the default (16:9) - 16:10 on a
// Steam Deck's 1280 x 800 screen, the default on 16:9 and wider displays (as on Windows), Off on a 4:3 one.
WideMode first_widescreen(WideMode dflt) {
    SDL_DisplayMode d;
    if (SDL_GetDesktopDisplayMode(0, &d) != 0 || d.w <= 0 || d.h <= 0) return dflt;
    const float aspect = (float)std::max(d.w, d.h) / std::min(d.w, d.h);
    // (each mode is the picture's shape, on every console)
    static const float shape[(int)WideMode::Count] = {4.0f / 3.0f, 16.0f / 10.0f, 16.0f / 9.0f, 2.0f, 43.0f / 18.0f};
    WideMode best = WideMode::Off;
    for (int m = 0; m <= (int)dflt && m < (int)WideMode::Count; m++)
        if (shape[m] <= aspect + 0.01f) best = (WideMode)m;
    return best;
}

// The room a window has on its display: the usable area (without panels) less a title bar; 0 x 0 if unknown.
void usable_area(SDL_Window* win, int& mw, int& mh) {
    mw = mh = 0;
    const int d = win ? SDL_GetWindowDisplayIndex(win) : 0;
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(d < 0 ? 0 : d, &r) == 0 && r.w > 0 && r.h > 80) { mw = r.w; mh = r.h - 40; }
}
}  // namespace

int App::run() {
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");   // (a windowed game shouldn't turn compositing off)
#ifdef SDL_HINT_APP_NAME
    SDL_SetHint(SDL_HINT_APP_NAME, "Aladdin Collection");
#endif
    // A touch screen (the Steam Deck's) is the mouse: its taps reach the launcher as clicks (SDL's default, kept even
    // if the environment says otherwise); there are no on-screen controls in a game, as on the PC.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL could not start: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    settings_path_ = path_join(data_dir(), L"settings.ini");
    if (!settings_.load(settings_path_)) settings_.widescreen = first_widescreen(settings_.widescreen);
    if (data_dir() != exe_dir()) make_dir(path_join(data_dir(), L"roms"));   // (a place for them beside the settings)

    games_[0].module.reset(create_md_aladdin());
    games_[1].module.reset(create_snes_aladdin());
    games_[2].module.reset(create_sms_aladdin());

    for (size_t i = 0; i + 2 < g_args.size(); i++)
        if (g_args[i] == "--autotest") {
            autotest_dir_ = from_utf8(g_args[i + 1]);
            autotest_ = load_script(from_utf8(g_args[i + 2]));
            make_dir(autotest_dir_);
        }
    for (size_t i = 0; i + 1 < g_args.size(); i++)
        if (g_args[i] == "--capture-size" && sscanf(g_args[i + 1].c_str(), "%dx%d", &capture_w_, &capture_h_) != 2)
            capture_w_ = capture_h_ = 0;
    if (autotest_) { settings_.fullscreen = false; settings_.vsync = true; }
    else capture_w_ = capture_h_ = 0;

    // The window: the picture at Window Size (or the capture size), fitted to the display; fullscreen goes over it.
    int ww = capture_w_, wh = capture_h_;
    if (ww <= 0 || wh <= 0) {
        int mw, mh;
        usable_area(nullptr, mw, mh);
        window_size(ww, wh, mw, mh);
    }
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (autotest_ ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    window_ = SDL_CreateWindow(kTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!window_) {
        fprintf(stderr, "No window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window_, 480, 300);
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    if (!video_.init(window_, w, h)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle.c_str(),
                                 "OpenGL ES 3.0 could not be started (graphics drivers with OpenGL ES 3 support are needed).", window_);
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return 1;
    }
    audio_.init(48000);
    input_.init(window_);

    find_game_roms();
    scan_soundtracks();
    apply_settings();
    ach_startup();
    if (settings_.fullscreen) set_fullscreen(true);
    for (int g = 0; g < kGameCount; g++)
        if (settings_.last_game == games_[g].module->id()) launcher_sel_ = launcher_last_card_ = g;
    fix_launcher_selection();   // (a hidden game's card isn't there: the nearest shown one)
    // Command line: --game md|snes|sms starts a game directly.
    for (size_t i = 0; i + 1 < g_args.size(); i++)
        if (g_args[i] == "--game")
            for (int g = 0; g < kGameCount; g++)
                if (g_args[i + 1] == games_[g].module->id()) launch(g);

    static SDL_Cursor* hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    static SDL_Cursor* arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running_ = false; break;
            case SDL_WINDOWEVENT:
                switch (e.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_RESIZED:
                    SDL_GL_GetDrawableSize(window_, &w, &h);
                    video_.resize(w, h);
                    break;
                // Losing focus releases all keys (no stuck keys); the hidden autotest window never has focus.
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    active_window_ = false;
                    if (!autotest_) input_.on_focus_lost();
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED: active_window_ = true; break;
                default: break;
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (e.key.repeat) break;
                if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    settings_.fullscreen = !is_fullscreen_;   // Alt+Enter (F11: app.cpp)
                    set_fullscreen(settings_.fullscreen);
                    break;
                }
                const int vk = vk_from_scancode(e.key.keysym.scancode);
                if (vk) input_.on_key(vk, e.type == SDL_KEYDOWN);
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
            case SDL_JOYDEVICEADDED:     // (a controller SDL has no mapping for comes as a plain joystick: input_sdl.cpp)
            case SDL_JOYDEVICEREMOVED:
                input_.on_device_change();
                break;
            // The mouse (see App::mouse_in_): clicks, right clicks and the wheel for poll_ui; a hand over the launcher's
            // links (the logo / Patreon page, the update badge). A touch screen's taps come as clicks (SDL makes them
            // mouse events, marked SDL_TOUCH_MOUSEID) but don't bring the pointer up in fullscreen.
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN: {
                const bool touch = (e.type == SDL_MOUSEMOTION ? e.motion.which : e.button.which) == SDL_TOUCH_MOUSEID;
                int cw = 1, ch = 1;   // (window points -> drawable pixels: HiDPI)
                SDL_GetWindowSize(window_, &cw, &ch);
                const int mx = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
                const int my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
                const float x = mx * (float)w / std::max(1, cw), y = my * (float)h / std::max(1, ch);
                if (e.type == SDL_MOUSEMOTION) {
                    if (!touch && mode_ == Mode::Launcher && (e.motion.xrel || e.motion.yrel)) mouse_shown_ = true;
                    const bool link = mode_ == Mode::Launcher && !menu_.active() && !jukebox_.active() &&
                                      (over_launcher_link(x, y) || over_update_badge(x, y));
                    SDL_SetCursor(link ? hand : arrow);
                } else if (e.button.button == SDL_BUTTON_LEFT) {
                    mouse_in_.click = true;
                    mouse_in_.x = x;
                    mouse_in_.y = y;
                    if (!touch && mode_ == Mode::Launcher) mouse_shown_ = true;
                } else if (e.button.button == SDL_BUTTON_RIGHT) {
                    mouse_in_.right = true;
                }
                break;
            }
            case SDL_MOUSEWHEEL:
                mouse_in_.wheel -= e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -(float)e.wheel.y : (float)e.wheel.y;
                break;
            default: break;
            }
        }
        if (!running_) break;
        const Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freq;
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
            audio_.set_paused(true);
            SDL_Delay(20);
            continue;
        }
        audio_.set_paused(false);
        frame(dt);
        if (cursor_visible() != cursor_shown_) update_cursor();
        if (!settings_.vsync && !autotest_) {
            // Software pacing when not synchronised to the display (fast forward runs more game frames per picture,
            // not more pictures)
            const double target = 1.0 / (emu_fps() * hfr_subs_);
            for (;;) {
                const double el = (double)(SDL_GetPerformanceCounter() - last) / freq;
                if (el >= target) break;
                if (target - el > 0.002) SDL_Delay(1);
            }
        }
    }
    if (!autotest_) save_settings();
    stop_recording();
    jukebox_.close();
    session_.reset();
    for (auto& g : games_) g.preview.reset();
    input_.shutdown();
    audio_.shutdown();
    video_.shutdown();
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
    return 0;
}

void App::set_fullscreen(bool fs) {
    if (!window_ || fs == is_fullscreen_) return;
    SDL_SetWindowFullscreen(window_, fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    is_fullscreen_ = fs;
    settings_.fullscreen = fs;
    update_cursor();   // (fullscreen hides the pointer until the mouse moves on the launcher)
}

// The pointer: shown or hidden as cursor_visible() says (each frame, and when the display mode changes).
void App::update_cursor() {
    cursor_shown_ = cursor_visible();
    SDL_ShowCursor(cursor_shown_ ? SDL_ENABLE : SDL_DISABLE);
}

// As on Windows (app_win.cpp): the running game's console's lines (the launcher: the Mega Drive's 224) and the width
// those lines' pixels make at the widescreen setting, at Window Size or the largest scale below it that fits.
void App::window_size(int& cw, int& ch, int max_w, int max_h) {
    const System sys = session_ ? session_->system() : System::MegaDrive;
    const int max_ext = session_ ? session_->module()->max_ext() : 1 << 12;
    const int ext = wide_ext(fit_wide_mode(settings_.widescreen, settings_.aspect, sys, max_ext), settings_.aspect, sys);
    const double par = pixel_aspect(sys, settings_.aspect);
    for (int scale = std::clamp(settings_.window_scale, 1, 6); scale >= 1; scale--) {
        cw = (int)((base_width(sys) + ext * 2) * par * scale + 0.5);
        ch = base_lines(sys) * scale;
        if (max_w <= 0 || max_h <= 0 || (cw <= max_w && ch <= max_h)) break;
    }
}

void App::resize_window_to_scale() {
    if (!window_ || is_fullscreen_ || capture_w_ > 0) return;
    int mw, mh, cw, ch;
    usable_area(window_, mw, mh);
    window_size(cw, ch, mw, mh);
    SDL_SetWindowSize(window_, cw, ch);
}

void App::move_to_monitor(int) {}   // (Windows only; a window manager moves windows between displays)

double App::refresh_rate() {
    if (test_refresh_ > 0) return test_refresh_;
    SDL_DisplayMode m;
    const int d = window_ ? SDL_GetWindowDisplayIndex(window_) : 0;
    if (SDL_GetCurrentDisplayMode(d < 0 ? 0 : d, &m) == 0 && m.refresh_rate > 1) return m.refresh_rate == 59 ? 59.94 : m.refresh_rate;
    return 60.0;
}

// Slang shader presets are Windows only (librashader's Direct3D 11 runtime); the Video menu doesn't offer them.
void App::apply_slang() {}
MenuPage App::slang_page() { return MenuPage(); }

// Trailer recording is a Windows test feature.
void App::start_recording(const std::string&, bool) {}
void App::record_frame() {}
void App::stop_recording() {}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) g_args.push_back(argv[i]);
    static App app;
    return app.run();
}
