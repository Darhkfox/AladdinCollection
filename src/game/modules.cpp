#include "game.h"

const char* system_name(System s) {
    switch (s) {
    case System::MegaDrive: return "Mega Drive / Genesis";
    case System::Snes: return "Super Nintendo";
    case System::MasterSystem: return "Master System";
    default: return "?";
    }
}

const char* system_short_name(System s) {
    switch (s) {
    case System::MegaDrive: return "Mega Drive";
    case System::Snes: return "SNES";
    case System::MasterSystem: return "Master System";
    default: return "?";
    }
}

GameModule* create_module(const std::string& id) {
    if (id == "md") return create_md_aladdin();
    if (id == "snes") return create_snes_aladdin();
    if (id == "sms") return create_sms_aladdin();
    return nullptr;
}
