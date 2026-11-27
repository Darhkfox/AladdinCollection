// The Master System half of a session: the core, its game module, colours, sound.
#include "session.h"
#include "palette.h"
#include "../core/sms.h"
#include <cstddef>
#include <algorithm>

namespace {

class SmsSession : public Session {
public:
    SmsSession() : sms_(new Sms) {
        sms_->vdp.mark_sprites = true;   // (dimmed scenery strips leave sprites alone)
        build_sms_palette(lut_, ColorProfile());
    }

    double frame_rate() const override { return sms_->frame_rate(); }
    int base_width() const override { return 256; }
    int base_lines() const override { return 192; }
    const uint8_t* ram() const override { return sms_->ram; }
    size_t ram_size() const override { return sizeof(sms_->ram); }
    void achievement_frame(AchievementCtx& ctx) override { mod()->achievement_frame(*sms_, ctx); }

    void jukebox_boot() override { mod()->jukebox_boot(*sms_); }
    void jukebox_frame() override { mod()->jukebox_frame(*sms_); }
    void play_sound(int id) override { mod()->play_sound(*sms_, id); }
    void stop_sound() override { mod()->stop_sound(*sms_); }
    int channel_count() const override { return 4; }
    const char* channel_name(int ch) const override {
        static const char* n[4] = {"SQ1", "SQ2", "SQ3", "NOI"};
        return ch >= 0 && ch < 4 ? n[ch] : "";
    }
    float channel_level(int ch) const override { return ch >= 0 && ch < 4 ? sms_->psg.channel_level[ch] / 2600.0f : 0.0f; }

protected:
    SmsModule* mod() const { return static_cast<SmsModule*>(module_); }

    GameModule* own_module(GameModule* proto) override {
        GameModule* m = create_module(proto->id());
        return m && m->system() == System::MasterSystem ? m : (delete m, nullptr);
    }
    bool core_load(const std::vector<uint8_t>& rom) override { return sms_->load_rom(rom); }
    void core_power_on() override { sms_->power_on(); }
    void core_soft_reset() override { sms_->soft_reset(); }
    void core_headless(bool on) override { sms_->skip_render = on; }
    void core_settings(const Settings& s) override {
        ColorProfile p{s.color, s.brightness, s.saturation, s.gamma};
        if (p != profile_) {
            profile_ = p;
            build_sms_palette(lut_, p);
        }
        sms_->vdp.sprite_limit = !s.no_sprite_limit;
    }
    void core_configure(const GameConfig& c) override {
        sms_->vdp.ext = c.ext;
        sms_->vdp.ignore_left_blank = c.ext > 0;
        if (mod()) mod()->configure(*sms_, c);
    }
    bool core_can_configure(const GameConfig& c) override { return !mod() || mod()->can_configure(*sms_, c); }
    bool core_start_level(const GameConfig& c, int index, int variant) override {
        return mod() && mod()->start_level(*sms_, c, index, variant);
    }
    void core_run_frame(uint16_t pad1, uint16_t pad2, FrameInfo& info) override {
        // (the console's reset button is held by soft_reset for the frame; the pad doesn't release it)
        sms_->set_pad(0, (uint16_t)(pad1 | (sms_->pad_buttons[0] & SMS_RESET)));
        sms_->set_pad(1, pad2);
        const GameConfig& c = config();
        if (mod()) mod()->pre_frame(*sms_, c);
        sms_->run_frame();
        if (mod()) mod()->post_frame(*sms_, c, info);
    }
    void core_sub_frames(int k) override { sms_->vdp.sub_frames = k; }
    bool core_convert(std::vector<uint32_t>& px, std::vector<uint8_t>& sprite, int& w, int& h, int sub) override {
        const SmsVdp& v = sms_->vdp;
        if (sub >= SmsVdp::kMaxSubFrames) sub = 0;
        w = v.fb_width;
        h = v.fb_lines;
        px.resize((size_t)w * h);
        sprite.resize((size_t)w * h);
        for (int y = 0; y < h; y++) {
            const uint8_t* src = sub > 0 ? v.sub_fb[sub - 1][y] : v.fb[y];
            uint32_t* dst = &px[(size_t)y * w];
            uint8_t* spr = &sprite[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                dst[x] = lut_[src[x] & 0x3F];
                spr[x] = (src[x] & SmsVdp::kSpritePixel) ? 1 : 0;
            }
        }
        return true;
    }
    const int32_t* core_audio(int& frames) override {
        frames = sms_->audio_frames;
        return sms_->audio.data();
    }
    double core_sample_rate() const override { return sms_->sample_rate(); }
    // Four PSG channels at full volume reach about 10k: lifted to the other consoles' loudness (measured: Aladdin's
    // stage music at this gain is a little under the Mega Drive version's, the square waves sounding brighter).
    float core_audio_gain() const override { return 2.4f; }
    std::vector<uint8_t> core_save() override { return sms_->save_state(); }
    bool core_load_state(const std::vector<uint8_t>& data) override { return sms_->load_state(data); }

private:
    std::unique_ptr<Sms> sms_;
    ColorProfile profile_;
    uint32_t lut_[64];
};

}  // namespace

std::unique_ptr<Session> make_sms_session() { return std::unique_ptr<Session>(new SmsSession); }
