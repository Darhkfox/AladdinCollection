// Aladdin (Master System, SIMS 1994): widescreen, HUD, cheats, level select, sound, achievements.
#include "game.h"
#include "sms_common.h"
#include "../core/sms.h"

using namespace sms_game;

namespace {

class SmsAladdin : public SmsModule {
public:
    const char* id() const override { return "sms"; }
    const char* title() const override { return "Aladdin"; }
    const char* subtitle() const override { return "SIMS"; }
    const char* year() const override { return "1994"; }
    uint32_t rom_crc32() const override { return 0xC8718D40; }
    uint32_t rom_size() const override { return 512u << 10; }
    const char* rom_hint() const override { return "Disney's Aladdin (Europe).sms"; }
    const char* rom_names() const override { return "9027"; }
    uint32_t accent_color() const override { return 0xFFF0705E; }
    int max_ext() const override { return 0; }

    void configure(Sms& s, const GameConfig& cfg) override { (void)s; (void)cfg; }
};

}  // namespace

GameModule* create_sms_aladdin() { return new SmsAladdin; }
