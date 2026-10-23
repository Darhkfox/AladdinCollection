// Aladdin (Mega Drive / Genesis, Virgin Games 1993): widescreen, HUD, cheats, level select, sound, achievements.
#include "game.h"
#include "md_common.h"
#include "../core/md.h"

using namespace md_game;

namespace {

class MdAladdin : public MdModule {
public:
    const char* id() const override { return "md"; }
    const char* title() const override { return "Aladdin"; }
    const char* subtitle() const override { return "Virgin Games"; }
    const char* year() const override { return "1993"; }
    uint32_t rom_crc32() const override { return 0xED427EA9; }
    uint32_t rom_size() const override { return 2u << 20; }
    const char* rom_hint() const override { return "Aladdin (U) [!].bin"; }
    const char* rom_names() const override { return "ALADDIN"; }
    uint32_t accent_color() const override { return 0xFF5AA8F2; }
    int max_ext() const override { return 0; }

    void configure(MegaDrive& md, const GameConfig& cfg) override { (void)md; (void)cfg; }
};

}  // namespace

GameModule* create_md_aladdin() { return new MdAladdin; }
