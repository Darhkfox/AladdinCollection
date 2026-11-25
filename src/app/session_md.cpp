// The Mega Drive half of a session: the core, its game module, colours, sound and MD+ soundtracks.
#include "session.h"
#include "palette.h"
#include "../core/md.h"
#include <algorithm>

namespace {

class MdSession : public Session {
public:
    MdSession() : md_(new MegaDrive) {
        md_->vdp.mark_sprites = true;   // (dimmed scenery strips leave sprites alone)
        build_md_palette(lut_, ColorProfile());
    }

    double frame_rate() const override { return md_->frame_rate(); }
    int base_width() const override { return 320; }
    int base_lines() const override { return 224; }
    const uint8_t* ram() const override { return md_->ram; }
    size_t ram_size() const override { return sizeof(md_->ram); }
    void achievement_frame(AchievementCtx& ctx) override { mod()->achievement_frame(*md_, ctx); }

    void jukebox_boot() override { mod()->jukebox_boot(*md_); }
    void jukebox_frame() override { mod()->jukebox_frame(*md_); }
    void play_sound(int id) override { mod()->play_sound(*md_, id); }
    void stop_sound() override { mod()->stop_sound(*md_); }
    int channel_count() const override { return 10; }
    const char* channel_name(int ch) const override {
        static const char* n[10] = {"FM1", "FM2", "FM3", "FM4", "FM5", "FM6", "SQ1", "SQ2", "SQ3", "NOI"};
        return ch >= 0 && ch < 10 ? n[ch] : "";
    }
    float channel_level(int ch) const override {
        return ch < 6 ? md_->ym.channel_level[ch] / 6000.0f : md_->psg.channel_level[ch - 6] / 2600.0f;
    }

    bool attach_soundtrack(const SoundtrackPack& pack, std::string& error) override {
        if (pack.kind != SoundtrackKind::MdPlus) { error = "it isn't an MD+ pack"; return false; }
        std::unique_ptr<MdPlus> p(new MdPlus);
        if (!p->load(pack, error)) return false;
        mdplus_ = std::move(p);
        md_->on_cart_write = [this](MegaDrive&, uint32_t a, uint16_t v, int size) { mdplus_->write(a, v, size); };
        return true;
    }
    bool has_soundtrack() const override { return mdplus_ != nullptr; }
    MdPlus* soundtrack() override { return mdplus_.get(); }

protected:
    MdModule* mod() const { return static_cast<MdModule*>(module_); }

    GameModule* own_module(GameModule* proto) override {
        GameModule* m = create_module(proto->id());
        return m && m->system() == System::MegaDrive ? m : (delete m, nullptr);
    }
    bool core_load(const std::vector<uint8_t>& rom) override {
        mdplus_.reset();
        md_->on_cart_write = nullptr;
        return md_->load_rom(rom);
    }
    void core_power_on() override { md_->power_on(); }
    void core_soft_reset() override { md_->soft_reset(); }
    void core_headless(bool on) override { md_->skip_render = on; }
    void on_power_on() override { if (mdplus_) mdplus_->stop(); }
    void core_settings(const Settings& s) override {
        ColorProfile p{s.color, s.brightness, s.saturation, s.gamma};
        if (p != profile_) {
            profile_ = p;
            build_md_palette(lut_, p);
        }
        md_->vdp.sprite_limit = !s.no_sprite_limit;
        md_->ym.ladder_effect = s.ladder_effect;
        soundtrack_gain_ = s.volume / 100.0f * s.soundtrack_volume / 100.0f;
    }
    void core_configure(const GameConfig& c) override {
        md_->vdp.ext = c.ext;
        md_->vdp.ignore_left_blank = c.ext > 0;
        if (mod()) mod()->configure(*md_, c);
    }
    bool core_can_configure(const GameConfig& c) override { return !mod() || mod()->can_configure(*md_, c); }
    bool core_start_level(const GameConfig& c, int index, int variant) override {
        return mod() && mod()->start_level(*md_, c, index, variant);
    }
    void core_run_frame(uint16_t pad1, uint16_t pad2, FrameInfo& info) override {
        md_->set_pad(0, pad1);
        md_->set_pad(1, pad2);
        const GameConfig& c = config();
        if (mod()) mod()->pre_frame(*md_, c);
        md_->run_frame();
        if (mod()) mod()->post_frame(*md_, c, info);
    }
    void core_sub_frames(int k) override { md_->vdp.sub_frames = k; }
    bool core_convert(std::vector<uint32_t>& px, std::vector<uint8_t>& sprite, int& w, int& h, int sub) override {
        const VDP& v = md_->vdp;
        if (sub >= VDP::kMaxSubFrames) sub = 0;
        w = v.fb_width;
        h = v.fb_lines;
        px.resize((size_t)w * h);
        sprite.resize((size_t)w * h);
        for (int y = 0; y < h; y++) {
            const uint16_t* src = sub > 0 ? v.sub_fb[sub - 1][y] : v.fb[y];
            uint32_t* dst = &px[(size_t)y * w];
            uint8_t* spr = &sprite[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                dst[x] = VDP::dimmed(lut_[src[x] & 0x7FF], src[x] >> VDP::kDimShift);   // (a dimmed boss arena's sides)
                spr[x] = (src[x] & VDP::kSpritePixel) ? 1 : 0;
            }
        }
        return true;
    }
    const int32_t* core_audio(int& frames) override {
        frames = md_->audio_frames;
        return md_->audio.data();
    }
    double core_sample_rate() const override { return md_->sample_rate(); }
    // Full-scale FM (6 loud channels) is ~49k, but loud music peaks near 24k, so at the default volume music peaks
    // around 14k and leaves room for effects.
    float core_audio_gain() const override { return 0.9f; }
    std::vector<uint8_t> core_save() override { return md_->save_state(); }
    bool core_load_state(const std::vector<uint8_t>& data) override { return md_->load_state(data); }
    std::string soundtrack_state() override { return mdplus_ ? mdplus_->state() : std::string(); }
    void set_soundtrack_state(const std::string& s) override {
        if (!mdplus_) return;
        mdplus_->set_state(s);
        md_->set_mdplus_overlay(mdplus_->unlocked());   // (the cartridge's overlay as the state left it)
    }
    void mix_extra(int16_t* out, int frames) override {
        if (mdplus_) mdplus_->mix(out, frames, out_rate_, soundtrack_gain_);
    }

private:
    std::unique_ptr<MegaDrive> md_;
    std::unique_ptr<MdPlus> mdplus_;
    float soundtrack_gain_ = 0.7f;
    ColorProfile profile_;
    uint32_t lut_[2048];
};

}  // namespace

std::unique_ptr<Session> make_md_session() { return std::unique_ptr<Session>(new MdSession); }
