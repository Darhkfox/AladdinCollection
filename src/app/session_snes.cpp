// The SNES half of a session: the core, its game module (execution hooks), colours per master brightness, sound.
#include "session.h"
#include "palette.h"
#include "../core/snes.h"
#include <algorithm>

namespace {

class SnesSession : public Session {
public:
    SnesSession() : snes_(new Snes) {
        snes_->setExecHook([](void* ctx, uint32_t pc) {
            SnesSession* self = (SnesSession*)ctx;
            return self->mod() ? self->mod()->exec_hook(*self->snes_, pc) : false;
        }, this);
    }

    double frame_rate() const override { return Snes::kFrameRate; }
    int base_width() const override { return 256; }
    int base_lines() const override { return 224; }
    const uint8_t* ram() const override { return snes_->wram; }
    size_t ram_size() const override { return sizeof(snes_->wram); }
    void achievement_frame(AchievementCtx& ctx) override { mod()->achievement_frame(*snes_, ctx); }

    void jukebox_boot() override { mod()->jukebox_boot(*snes_); }
    void jukebox_frame() override { mod()->jukebox_frame(*snes_); }
    void play_sound(int id) override { mod()->play_sound(*snes_, id); }
    void stop_sound() override { mod()->stop_sound(*snes_); }
    int channel_count() const override { return 8; }
    const char* channel_name(int ch) const override {
        static const char* n[8] = {"V1", "V2", "V3", "V4", "V5", "V6", "V7", "V8"};
        return ch >= 0 && ch < 8 ? n[ch] : "";
    }
    float channel_level(int ch) const override { return ch >= 0 && ch < 8 ? levels_[ch] : 0.0f; }

protected:
    SnesModule* mod() const { return static_cast<SnesModule*>(module_); }

    GameModule* own_module(GameModule* proto) override {
        GameModule* m = create_module(proto->id());
        return m && m->system() == System::Snes ? m : (delete m, nullptr);
    }
    bool core_load(const std::vector<uint8_t>& rom) override { return snes_->loadRom(rom); }
    void core_power_on() override {
        snes_->reset(true);
        snes_->apu.samples.clear();
        freeze_ = 0;
    }
    void core_soft_reset() override { snes_->reset(false); }
    void core_headless(bool on) override { snes_->skipRender = on; }
    void core_turbo(int k) override {
        snes_->cpuTurbo = std::max(1, k);
        if (snes_->cpuTurbo <= 1) snes_->turboActive = false;
    }
    void core_settings(const Settings& s) override {
        ColorProfile p{s.color, s.brightness, s.saturation, s.gamma};
        if (p != profile_) {
            profile_ = p;
            for (auto& l : lut_) l.clear();
        }
        snes_->ppu.noSpriteLimit = s.no_sprite_limit;
        snes_->apu.dsp.interpMode = s.interpolation;
    }
    void core_configure(const GameConfig& c) override {
        snes_->ppu.setExt(c.ext, c.ext);
        snes_->cpuTurbo = std::max(1, c.cpu_turbo);
        if (c.cpu_turbo <= 1) snes_->turboActive = false;
        if (mod()) mod()->configure(*snes_, c);
    }
    bool core_can_configure(const GameConfig& c) override { return !mod() || mod()->can_configure(*snes_, c); }
    bool core_start_level(const GameConfig& c, int index, int variant) override {
        const bool ok = mod() && mod()->start_level(*snes_, c, index, variant);
        snes_->apu.samples.clear();
        return ok;
    }
    void core_run_frame(uint16_t pad1, uint16_t pad2, FrameInfo& info) override {
        snes_->input[0] = pad1;
        snes_->input[1] = pad2;
        const GameConfig& c = config();
        if (mod()) mod()->pre_frame(*snes_, c);
        snes_->runFrame();
        // A module's redraw holding the NMI, the frame it ends in, and the game's first two frames after it (its
        // sprites come one frame late): the picture shown stays the last finished one.
        thawed_ = freeze_ > 0;
        if (snes_->nmiHold || snes_->vblankHeld) freeze_ = 3;
        else if (freeze_ > 0) freeze_--;
        if (mod()) mod()->post_frame(*snes_, c, info);
        // the frame's audio, as the mixer takes it
        const std::vector<int16_t>& a = snes_->apu.samples;
        audio_.resize(a.size());
        for (size_t i = 0; i < a.size(); i++) audio_[i] = a[i];
        audio_frames_ = (int)a.size() / 2;
        // channel meters: each voice's peak
        for (int v = 0; v < 8; v++) levels_[v] = snes_->apu.dsp.takePeak(v) / 12000.0f;
        snes_->apu.samples.clear();
    }
    const uint32_t* lut(int bright) {
        std::vector<uint32_t>& l = lut_[bright & 15];
        if (l.empty()) {
            l.resize(32768);
            build_snes_palette(l.data(), profile_, bright & 15);
        }
        return l.data();
    }
    void core_sub_frames(int k) override { snes_->ppu.subFrames = k; }
    bool core_convert(std::vector<uint32_t>& px, std::vector<uint8_t>& sprite, int& w, int& h, int sub) override {
        // While a module's redraw holds the NMI (several frames after a width change), and on the frame it ends in,
        // the game updates neither its status bar nor its sprites and scroll: keep showing the last finished picture.
        if (freeze_ > 0) return false;
        // (the first frame shown after that: nothing in between with the frames that weren't)
        if (sub >= Ppu::kMaxSubFrames || thawed_) sub = 0;
        const Ppu& p = snes_->ppu;
        const uint32_t* frame = sub > 0 ? p.subFrame[sub - 1] : p.frame;
        const int el = p.extL, er = p.extR;
        w = 256 + el + er;
        h = p.frameHeight();
        px.resize((size_t)w * h);
        sprite.clear();
        int cur_b = -1;
        const uint32_t* table = nullptr;
        for (int y = 0; y < h; y++) {
            const uint32_t* src = frame + (size_t)y * kLineW + (kMaxExt - el);
            uint32_t* dst = &px[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                const uint32_t c = src[x];
                const int b = (int)(c >> 15) & 15;
                if (b != cur_b) { cur_b = b; table = lut(b); }
                dst[x] = table[c & 0x7FFF];
            }
        }
        return true;
    }
    const int32_t* core_audio(int& frames) override {
        frames = audio_frames_;
        return audio_.data();
    }
    double core_sample_rate() const override { return Snes::kSampleRate; }
    // The S-DSP mixes to 16 bits itself (the games keep their music well inside it).
    float core_audio_gain() const override { return 1.0f; }
    std::vector<uint8_t> core_save() override { return snes_->saveState(); }
    bool core_load_state(const std::vector<uint8_t>& data) override {
        if (!snes_->loadState(data)) return false;
        snes_->apu.samples.clear();
        freeze_ = 0;
        return true;
    }

private:
    std::unique_ptr<Snes> snes_;
    ColorProfile profile_;
    // Colour lookup per master brightness (0-15), built on first use after a profile change.
    std::vector<uint32_t> lut_[16];
    int freeze_ = 0;                  // frames left that keep showing the last picture (see core_run_frame)
    bool thawed_ = false;             // ...this frame is the first shown after them
    std::vector<int32_t> audio_;
    int audio_frames_ = 0;
    float levels_[8] = {};
};

}  // namespace

std::unique_ptr<Session> make_snes_session() { return std::unique_ptr<Session>(new SnesSession); }
