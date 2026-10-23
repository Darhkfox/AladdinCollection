// Aladdin (Super Nintendo, Capcom 1993): widescreen, HUD, cheats, level select, sound, achievements.
#include "game.h"
#include "snes_common.h"
#include "../core/snes.h"

using namespace snes_game;

namespace {

class SnesAladdin : public SnesModule {
public:
    const char* id() const override { return "snes"; }
    const char* title() const override { return "Aladdin"; }
    const char* subtitle() const override { return "Capcom"; }
    const char* year() const override { return "1993"; }
    uint32_t rom_crc32() const override { return 0x124D8E4D; }
    uint32_t rom_size() const override { return 1310720; }
    const char* rom_hint() const override { return "Aladdin (USA).sfc"; }
    const char* rom_names() const override { return "ALADDIN"; }
    uint32_t accent_color() const override { return 0xFFB394F7; }
    int max_ext() const override { return 0; }

    void configure(Snes& s, const GameConfig& cfg) override { (void)s; (void)cfg; }
};

}  // namespace

GameModule* create_snes_aladdin() { return new SnesAladdin; }
