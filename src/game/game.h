// Per-game enhancement modules: ROM identification, widescreen and HUD re-anchoring, cheats, level select,
// sound/music tables and achievements. The collection runs games on three consoles; each module is written
// for its console's machine (MdModule, SnesModule, SmsModule) and the app talks to it through GameModule.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

class MegaDrive;
class Snes;
class Sms;

enum class System : int { MegaDrive = 0, Snes, MasterSystem, Count };
constexpr int kSystemCount = (int)System::Count;
const char* system_name(System s);         // "Mega Drive / Genesis", "Super Nintendo", "Master System"
const char* system_short_name(System s);   // "Mega Drive", "SNES", "Master System"

struct LevelEntry {
    std::string group;      // e.g. "Agrabah"
    std::string name;       // e.g. "Agrabah Market"
    unsigned variants = 0;  // bit n = available in level_variants()[n]; 0 = all
};

struct SoundEntry {
    int id;                 // the game's own sound number / driver command
    std::string name;
    bool music;
    int  seconds = 0;       // approximate loop length / duration for the music player (0 = unknown)
};

struct CheatEntry {
    std::string key;        // stable id stored in settings.ini
    std::string name;
    std::string desc;
    std::vector<std::string> excludes;   // cheats switched off when this one is switched on (they don't work together)
};

struct GameConfig {
    int  ext = 0;                        // widescreen: extra pixels on each side of the console's picture
    bool hud_anchor = true;              // move HUD elements to the screen edges in widescreen
    int  cpu_turbo = 1;                  // SNES: CPU speed for the game's frame work (Snes::cpuTurbo); 1 = the original's
    std::map<std::string, bool> cheats;  // enabled cheats
    bool cheat(const char* k) const { auto it = cheats.find(k); return it != cheats.end() && it->second; }
};

// Reported by the module after every frame.
struct FrameInfo {
    bool gameplay = false;   // a screen where widescreen shows real content
    int  mask_left = 0;      // pixels of the left extension to black out (0..ext)
    int  mask_right = 0;     // pixels of the right extension to black out (0..ext)
    int  mask_from = 0;      // edge masks start at this line (a status bar above it stays whole)
    int  mask_lines = 0;     // edge masks cover lines [mask_from, mask_lines) only (0 = to the end), e.g. to spare a status bar
    bool pillar_below = false; // lines [mask_lines, end) are pillarboxed (HUD shown at its original width)
    // Scenery outside the play area (a boss arena the 4:3 camera never shows past): pixels of the extension whose
    // scenery is shown dimmed instead of black (sprites there are left as they are), on the same lines as the masks.
    int  dim_left = 0, dim_right = 0;
    int  view_left = -1;     // widescreen: the view's left extension for the camera at the frame's end (tools)
};

struct AchievementDef {
    const char* id;          // stable id stored in achievements.ini ("md_agrabah")
    const char* name;
    const char* desc;
    bool secret = false;     // shown as "???" until earned
    int target = 0;          // counted achievements: the count to reach (shown as progress), else 0
    bool set = false;        // the progress is a set of things found (bits; shown as how many), see AchievementCtx::marked
};

// What a module's achievement_frame() works with. The app owns it per session: `scratch` is reset when the
// run is interrupted (level select, a loaded save state); `run_bits` belong to the playthrough and travel with
// its save states.
struct AchievementCtx {
    bool run_counts = false;     // this game started (power-on) with achievements on and they stayed on: no cheats,
                                 // level select or rewind since (the app only runs the checks then)
    bool level_select = false;   // in: a level was just started with the port's level select (the module clears it)
    int  players = 1;
    uint32_t run_bits = 0;       // the module's facts about the playthrough (e.g. "a life was lost")
    int32_t scratch[32] = {};    // the module's frame-to-frame memory
    std::vector<uint8_t> prev;   // the previous frame's work RAM (empty on the first frame), kept by the app
    std::vector<int> unlocked;   // out: indices into achievements() earned this frame
    std::vector<std::pair<int, int>> progress;   // out: (index, count): the count reached now (the best is kept)
    std::vector<int> counted;    // out: indices whose saved count goes up by one (kept across sessions; earned at target)
    std::vector<std::pair<int, int>> marked;     // out: (index, bit) found, for a `set` achievement (earned at target)
    void reset_scratch() { for (int32_t& v : scratch) v = 0; prev.clear(); }
};

// Read access to a work-RAM image, the current frame's or the previous one's. Mega Drive: 64 KB of big-endian
// 68000 RAM ($FF0000-$FFFFFF); SNES: 128 KB WRAM ($7E0000-$7FFFFF), little-endian; Master System: 8 KB
// ($C000-$DFFF), little-endian.
struct RamViewBE {
    const uint8_t* r;
    uint32_t mask = 0xFFFF;
    uint8_t  b(uint32_t a) const { return r[a & mask]; }
    uint16_t w(uint32_t a) const { a &= mask; return (uint16_t)((r[a] << 8) | r[(a + 1) & mask]); }
    int16_t  sw(uint32_t a) const { return (int16_t)w(a); }
    uint32_t l(uint32_t a) const { return ((uint32_t)w(a) << 16) | w(a + 2); }
};
struct RamViewLE {
    const uint8_t* r;
    uint32_t mask;
    uint8_t  b(uint32_t a) const { return r[a & mask]; }
    uint16_t w(uint32_t a) const { a &= mask; return (uint16_t)(r[a] | (r[(a + 1) & mask] << 8)); }
    int16_t  sw(uint32_t a) const { return (int16_t)w(a); }
    uint32_t l(uint32_t a) const { return w(a) | ((uint32_t)w(a + 2) << 16); }
};

class GameModule {
public:
    virtual ~GameModule() = default;

    virtual System      system() const = 0;
    virtual const char* id() const = 0;          // "md" / "snes" / "sms": settings, save states, --game
    virtual const char* title() const = 0;       // "Aladdin"
    virtual const char* subtitle() const = 0;    // the developer: "Virgin Games"
    virtual const char* year() const = 0;
    virtual int         max_players() const { return 1; }   // simultaneous players supported
    virtual int         max_ext() const { return 128; }     // widest widescreen extension supported (px per side)
    virtual float       audio_gain() const { return 1.0f; } // output trim so the games play at similar loudness
    virtual uint32_t    rom_crc32() const = 0;
    virtual uint32_t    rom_size() const = 0;
    virtual const char* rom_hint() const = 0;    // expected file description for error messages
    // Header names (Mega Drive / SNES) or product codes (Master System) that mark any version of the game
    // (RomSpec::names), to tell a wrong version from none
    virtual const char* rom_names() const = 0;
    virtual uint32_t    accent_color() const = 0; // UI theme colour 0xAARRGGBB
    // Launcher preview: from power-on, frames are run (muted, not drawn) until the game's title screen is up, so the
    // preview opens there instead of on the logos: at most preview_start() frames, with preview_pad(frame, ram) held
    // (the console's pad bits, e.g. START through an intro), ending early once preview_ready(ram) says so. `ram` is
    // the work RAM (Session::ram()).
    virtual int         preview_start() const { return 0; }
    virtual uint16_t    preview_pad(int frame, const uint8_t* ram) const { (void)frame; (void)ram; return 0; }
    virtual bool        preview_ready(const uint8_t* ram) const { (void)ram; return false; }

    virtual std::vector<CheatEntry> cheats() const { return {}; }
    virtual std::vector<LevelEntry> levels() const { return {}; }
    // Optional start variants for the level select (e.g. difficulty), the menu label for them, and the one picked
    // when the game's level select first opens.
    virtual std::vector<std::string> level_variants() const { return {}; }
    virtual const char* level_variant_label() const { return "Difficulty"; }
    virtual int level_variant_default() const { return 0; }
    virtual std::vector<SoundEntry> sounds() const { return {}; }
    // Achievements: the list (order fixed: indices are what achievement_frame() reports).
    virtual std::vector<AchievementDef> achievements() const { return {}; }

    // The module's own state that save states must carry (widescreen bookkeeping); empty if none.
    virtual std::vector<uint8_t> save_ext() const { return {}; }
    virtual void load_ext(const uint8_t* data, size_t size) { (void)data; (void)size; }
};

// The console-specific half: what the session calls on the machine the game runs on.
template <class M>
class ModuleFor : public GameModule {
public:
    // Install ROM patches / video hooks for the configuration. Called after power-on and whenever the
    // configuration changes (between frames).
    virtual void configure(M& m, const GameConfig& cfg) = 0;
    // Whether configure(cfg) can be applied now without changing ROM code the CPU is running or will return into;
    // the session waits for a frame where it can.
    virtual bool can_configure(const M& m, const GameConfig& cfg) const { (void)m; (void)cfg; return true; }
    virtual void pre_frame(M& m, const GameConfig& cfg) { (void)m; (void)cfg; }
    virtual void post_frame(M& m, const GameConfig& cfg, FrameInfo& info) { (void)m; (void)cfg; (void)info; }
    // Power-cycles the machine and runs it (headless, fast) until the chosen level is starting.
    virtual bool start_level(M& m, const GameConfig& cfg, int index, int variant) {
        (void)m; (void)cfg; (void)index; (void)variant; return false;
    }
    // Music player: power-cycles the machine and runs it until the sound driver accepts commands, with no music
    // playing; then once per frame jukebox_frame (keeps the game quiet / in a stable state), and the requests.
    virtual void jukebox_boot(M& m) { (void)m; }
    virtual void jukebox_frame(M& m) { (void)m; }
    virtual void play_sound(M& m, int id) { (void)m; (void)id; }
    virtual void stop_sound(M& m) { (void)m; }
    // Asks for music `id` the way the game itself does (MD+ soundtracks: see the Mega Drive session). False if
    // the module can't.
    virtual bool music_request(M& m, int id) { (void)m; (void)id; return false; }
    // The check run after every emulated frame of a game being played with achievements on. Reads RAM only.
    virtual void achievement_frame(const M& m, AchievementCtx& ctx) const { (void)m; (void)ctx; }
};

class MdModule : public ModuleFor<MegaDrive> {
public:
    System system() const override { return System::MegaDrive; }
};
class SnesModule : public ModuleFor<Snes> {
public:
    System system() const override { return System::Snes; }
    // Called before the CPU executes an instruction at an address the module marked (Snes::markExec, in
    // configure). Return true if the hook moved the PC (the instruction at `pc` is then not executed).
    virtual bool exec_hook(Snes& s, uint32_t pc) { (void)s; (void)pc; return false; }
};
class SmsModule : public ModuleFor<Sms> {
public:
    System system() const override { return System::MasterSystem; }
};

GameModule* create_md_aladdin();
GameModule* create_snes_aladdin();
GameModule* create_sms_aladdin();
GameModule* create_module(const std::string& id);   // by id(), or nullptr
constexpr int kGameCount = 3;                        // launcher order: Mega Drive, SNES, Master System
