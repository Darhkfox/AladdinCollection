// The SNES half of a session: the core, its game module (execution hooks), colours per master brightness, sound and
// MSU-1 soundtracks.
#include "session.h"
#include "palette.h"
#include "../core/snes.h"
#include "../game/snes_common.h"
#include <cstddef>
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

    // An MSU-1 pack: the chip, playing its tracks (the ROM was patched for it before open()).
    bool attach_soundtrack(const SoundtrackPack& pack, std::string& error) override {
        if (pack.kind != SoundtrackKind::Msu1) { error = "it isn't an MSU-1 pack"; return false; }
        if (pack.msu_base.empty()) { error = "it has no tracks"; return false; }
        snes_->msu.reset(new Msu1);
        snes_->msu->open(pack.msu_base);
        return true;
    }
    void detach_soundtrack() override { snes_->msu.reset(); }
    bool switch_soundtrack(const std::vector<uint8_t>& rom, const SoundtrackPack* pack, std::string& error) override {
        std::vector<uint8_t> r(rom.begin() + ((rom.size() & 0x3FF) == 0x200 ? 0x200 : 0), rom.end());
        if (r.size() != snes_->romSize()) { error = "its ROM is another size"; return false; }
        if (pack && (pack->kind != SoundtrackKind::Msu1 || pack->msu_base.empty())) { error = "it isn't an MSU-1 pack"; return false; }
        swap_rom_ = std::move(r);
        swap_base_ = pack ? pack->msu_base : std::wstring();
        swap_pending_ = true;
        try_swap();
        return true;
    }
    bool has_soundtrack() const override { return snes_->msu != nullptr; }
    Msu1* msu() override { return snes_->msu.get(); }

protected:
    SnesModule* mod() const { return static_cast<SnesModule*>(module_); }

    GameModule* own_module(GameModule* proto) override {
        GameModule* m = create_module(proto->id());
        return m && m->system() == System::Snes ? m : (delete m, nullptr);
    }
    bool core_load(const std::vector<uint8_t>& rom) override {
        snes_->msu.reset();
        return snes_->loadRom(rom);
    }
    void core_power_on() override {
        if (swap_pending_) try_swap(true);
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
        // An MSU-1 pack's music at the Soundtrack Volume of the output (as an MD+ pack's): its share of the console's
        // mix before the console's gain (core_audio_gain, the module's audio_gain) and the volume, which the mixer applies.
        msu_gain_ = s.soundtrack_volume / 100.0f / (core_audio_gain() * (module_ ? module_->audio_gain() : 1.0f));
    }
    void core_configure(const GameConfig& c) override {
        snes_->ppu.setExt(c.ext, c.ext);
        snes_->cpuTurbo = std::max(1, c.cpu_turbo);
        if (c.cpu_turbo <= 1) snes_->turboActive = false;
        if (mod()) mod()->configure(*snes_, c);
    }
    bool core_can_configure(const GameConfig& c) override { return !mod() || mod()->can_configure(*snes_, c); }
    bool core_start_level(const GameConfig& c, int index, int variant) override {
        if (swap_pending_) try_swap(true);   // (the level select starts from power-on)
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
        if (swap_pending_) try_swap();
        // the frame's audio, as the mixer takes it
        const std::vector<int16_t>& a = snes_->apu.samples;
        audio_.resize(a.size());
        for (size_t i = 0; i < a.size(); i++) audio_[i] = a[i];
        audio_frames_ = (int)a.size() / 2;
        // an MSU-1 pack's music, with the frame's own samples: it keeps the game's time (fast forward, rewind)
        if (snes_->msu) snes_->msu->mix(audio_.data(), (size_t)audio_frames_, Snes::kSampleRate, msu_gain_);
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
    // The MSU-1's registers, track and place travel with save states and rewind (in the state's tag)
    std::string soundtrack_state() override { return snes_->msu ? snes_->msu->state() : std::string(); }
    // A state saved with an MSU-1 pack loaded without it, or the other way round: the console's sound driver holds the
    // other ROM's song (the pack's patch uploads songs misplaced, to mute them), so it starts afresh and the song is asked
    // for again (in a level: replay_music), as for a pack switched there.
    void set_soundtrack_state(const std::string& s) override {
        if (snes_->msu) snes_->msu->setState(s);
        const bool saved_with = !s.empty() && s[0] == 'm';
        if (saved_with != (snes_->msu != nullptr) && mod()) mod()->replay_music(*snes_);
    }

private:
    // A soundtrack switched in a game (switch_soundtrack): the ROM swapped (its patch code kept: the module writes it)
    // and the chip attached or removed between frames, once no changed byte is at the PC or behind a return address;
    // then the module asks for the song again.
    // (`starting`: the console is about to start from its reset vector: always safe, and no song to ask for again)
    void try_swap(bool starting = false) {
        std::vector<long> changed;
        const std::vector<uint8_t>& now = snes_->romPristine;
        for (size_t i = 0; i < swap_rom_.size() && i < now.size(); i++)
            if (now[i] != swap_rom_[i]) changed.push_back((long)i);
        if (!starting && !snes_game::rom_change_safe(*snes_, changed)) return;
        const std::vector<uint8_t> patch = snes_->patchRom;
        snes_->loadRom(swap_rom_);
        snes_->patchRom = patch;
        if (swap_base_.empty()) snes_->msu.reset();
        else {
            snes_->msu.reset(new Msu1);
            snes_->msu->open(swap_base_);
        }
        if (!starting) {
            core_configure(config());
            if (mod()) mod()->replay_music(*snes_);
        }
        swap_pending_ = false;
        swap_rom_.clear();
    }
    bool swap_pending_ = false;
    std::vector<uint8_t> swap_rom_;
    std::wstring swap_base_;

    std::unique_ptr<Snes> snes_;
    ColorProfile profile_;
    // Colour lookup per master brightness (0-15), built on first use after a profile change.
    std::vector<uint32_t> lut_[16];
    int freeze_ = 0;                  // frames left that keep showing the last picture (see core_run_frame)
    bool thawed_ = false;             // ...this frame is the first shown after them
    std::vector<int32_t> audio_;
    int audio_frames_ = 0;
    float levels_[8] = {};
    float msu_gain_ = 0.4f;
};

}  // namespace

std::unique_ptr<Session> make_snes_session() { return std::unique_ptr<Session>(new SnesSession); }
