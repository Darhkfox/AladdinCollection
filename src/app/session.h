// One running game: a console core + its game module + frame/audio conversion. The console-specific half is a
// subclass per system (session_md.cpp, session_snes.cpp, session_sms.cpp); Session::create picks it.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../game/game.h"
#include "settings.h"
#include "audio_mixer.h"
#include "soundtrack.h"

class Session {
public:
    // A session for the module's console (nullptr for an unknown system).
    static std::unique_ptr<Session> create(GameModule* module);
    virtual ~Session();

    // Every session runs its own instance of the game's module: the modules keep per-machine state (widescreen
    // bookkeeping, video hooks, cheats), and the launcher preview, the game and the music player run side by side.
    bool open(GameModule* module, const std::vector<uint8_t>& rom);
    GameModule* module() const { return module_; }
    System system() const { return module_ ? module_->system() : System::MegaDrive; }

    // An MD+ soundtrack for the game (Mega Drive only; open() was given the ROM with the pack's patch applied).
    virtual bool attach_soundtrack(const SoundtrackPack& pack, std::string& error) {
        (void)pack;
        error = "soundtracks are for Mega Drive games";
        return false;
    }
    virtual bool has_soundtrack() const { return false; }
    virtual MdPlus* soundtrack() { return nullptr; }   // (the Music Player plays its tracks directly)

    // Applies video/audio/cheat settings. Safe to call at any time between frames.
    void apply_settings(const Settings& s);
    void power_on();
    void soft_reset();
    bool start_level(int index, int variant = 0);

    // Runs one emulated frame with the given pad states (the console's own button bits: see action_bits()).
    void run_frame(uint16_t pad1, uint16_t pad2);
    // Converts the last frame to BGRA using the current colour profile; applies widescreen masks.
    const uint32_t* frame(int& w, int& h);
    // Resamples the last frame's audio to `out` (48 kHz stereo int16), appending.
    void mix_audio(std::vector<int16_t>& out, double adjust);
    void set_audio_rate(double out_rate);

    // `extra`: the app's own note stored with the state (the achievement run, see App::ach_state_tag).
    std::vector<uint8_t> save_state(const std::string& extra = std::string());
    bool load_state(const std::vector<uint8_t>& data, std::string* extra = nullptr);

    const FrameInfo& info() const { return info_; }
    const GameConfig& config() const { return cfg_; }
    int  ext() const { return cfg_.ext; }
    bool muted = false;

    // ---- the console ------------------------------------------------------------------
    virtual double frame_rate() const = 0;
    virtual int    base_width() const = 0;    // the original picture: 320 x 224, 256 x 224, 256 x 192
    virtual int    base_lines() const = 0;
    // Work RAM (achievements, tests).
    virtual const uint8_t* ram() const = 0;
    virtual size_t ram_size() const = 0;
    virtual void   achievement_frame(AchievementCtx& ctx) = 0;

    // ---- music player -----------------------------------------------------------------
    virtual void jukebox_boot() = 0;
    virtual void jukebox_frame() = 0;
    virtual void play_sound(int id) = 0;
    virtual void stop_sound() = 0;
    // Channel meters: how many channels the sound chips have, each one's name and its peak in the last frame (0..1).
    virtual int  channel_count() const = 0;
    virtual const char* channel_name(int ch) const = 0;
    virtual float channel_level(int ch) const = 0;

protected:
    Session() = default;
    // ---- what each console implements -----------------------------------------------
    virtual GameModule* own_module(GameModule* proto) = 0;   // a fresh instance of the module, typed for the console
    virtual bool core_load(const std::vector<uint8_t>& rom) = 0;
    virtual void core_power_on() = 0;
    virtual void core_soft_reset() = 0;
    virtual void core_settings(const Settings& s) = 0;       // sprite limit, palette, chip options
    virtual void core_configure(const GameConfig& c) = 0;    // video width + the module's configure()
    virtual bool core_can_configure(const GameConfig& c) = 0;
    virtual bool core_start_level(const GameConfig& c, int index, int variant) = 0;
    // pre_frame, the frame, post_frame
    virtual void core_run_frame(uint16_t pad1, uint16_t pad2, FrameInfo& info) = 0;
    // The last frame as BGRA (w x h) and which pixels are sprites (for dimmed scenery strips). False: keep showing
    // the previous picture (a module's redraw is holding the screen).
    virtual bool core_convert(std::vector<uint32_t>& px, std::vector<uint8_t>& sprite, int& w, int& h) = 0;
    virtual const int32_t* core_audio(int& frames) = 0;      // the last frame's audio, int32 stereo
    virtual double core_sample_rate() const = 0;
    virtual float  core_audio_gain() const = 0;              // the console's mix to the output's scale
    virtual std::vector<uint8_t> core_save() = 0;
    virtual bool core_load_state(const std::vector<uint8_t>& data) = 0;
    // The soundtrack's playing position travels with save states (Mega Drive)
    virtual std::string soundtrack_state() { return std::string(); }
    virtual void set_soundtrack_state(const std::string& s) { (void)s; }
    virtual void on_power_on() {}
    // Adds to `frames` stereo frames of the output just mixed (an MD+ soundtrack plays over the game's sound).
    virtual void mix_extra(int16_t* out, int frames) { (void)out; (void)frames; }

    std::unique_ptr<GameModule> own_;
    GameModule* module_ = nullptr;
    Settings last_;
    double out_rate_ = 48000.0;

private:
    // cfg_: what the ROM patches, video width and module hooks run with; want_: what the settings ask for.
    // A change waits (pending_) for a frame end where the CPU is clear of the code it changes.
    GameConfig cfg_, want_;
    bool pending_ = false;
    void switch_config();
    void try_switch();
    void configure_mixer();
    FrameInfo info_;
    std::vector<uint32_t> pixels_;
    std::vector<uint8_t> sprite_;
    int frame_w_ = 0, frame_h_ = 0;
    AudioMixer mixer_;
    float  mask_l_ = 0, mask_r_ = 0;     // animated masks (pixels)
    int    mask_lines_ = 0, mask_from_ = 0;
    int    dim_l_ = 0, dim_r_ = 0;       // dimmed scenery strips (FrameInfo::dim_left / dim_right)
    bool   pillar_below_ = false;
    bool   configured_ = false;
};
