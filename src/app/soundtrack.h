// CD-quality soundtracks for the games, which the player drops into a folder:
// - MD+ packs (Mega Drive): a ROM patch, a .cue sheet and the music as WAV files, in the "soundtracks" folder. The
//   patched game asks for music through the MD+ cartridge overlay (a write of $CD54 to $3F7FA, then a command word to
//   $3F7FE); MdPlus plays the track from the pack and mixes it with the game's own sound (its effects; the patch
//   silences the FM music).
// - MSU-1 packs (SNES): the tracks (NAME-1.pcm, NAME-2.pcm, ...) with an MSU-1 patch (.bps / .ips) or a ROM already
//   patched for them (.sfc), in the "msu" folder. The patched game plays them through the MSU-1 chip (core/msu1.h),
//   whose music the SNES session mixes with the console's sound (the patch keeps the effects and mutes the music).
#pragma once
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "../game/game.h"

class Msu1;

enum class SoundtrackKind { MdPlus, Msu1 };

struct SoundtrackPack {
    std::string name;          // the pack's folder name (what the settings store)
    std::string game_id;       // "md", "snes", ...
    SoundtrackKind kind = SoundtrackKind::MdPlus;
    std::wstring dir, cue, patch;
    std::wstring rom;          // MSU-1: a ROM already patched for the pack (used when there is no patch file)
    std::wstring msu_base;     // MSU-1: the tracks' path without "-N.pcm"
    int tracks = 0;            // tracks whose audio is there
};

// A game packs can be for: its id, console, the CRC of the ROM it expects and that ROM if it was found (a pre-patched
// MSU-1 ROM is checked against it).
struct SoundtrackGame {
    std::string id;
    System system = System::MegaDrive;
    uint32_t crc = 0;
    const std::vector<uint8_t>* rom = nullptr;
};

// The folders searched for packs: "soundtracks" and "msu" next to the executable and in the data folder (Android: also
// "msu" in the app's folder on the shared storage), and the ROM folders (a pack folder placed directly in one). A pack
// is a folder (or a folder inside one) with
// - MD+: a .cue and a .bps / .ips patch; its game is the one whose ROM CRC the BPS patch names;
// - MSU-1: NAME-N.pcm tracks and a .bps / .ips patch (BPS: for the game's ROM CRC) or a ROM patched for them, whose
//   changes from the game's own ROM are only MSU-1 code (msu_rom_check).
std::vector<SoundtrackPack> find_soundtracks(const std::vector<SoundtrackGame>& games);

// A short name for menus: "MD+ Castle Of Illusion (Grant Kirkhope 2013 Arranged)" -> "Grant Kirkhope 2013 Arranged",
// "... Patch and Soundpack by GMKalandar" -> "by GMKalandar", "Aladdin (USA) (1993) (MSU1) (Audio Orchestral)" ->
// "Orchestral". With `kind`: prefixed "MD+ " / "MSU-1 " unless it says so already.
std::string soundtrack_label(const std::string& pack_name);
std::string soundtrack_label(const SoundtrackPack& pack);

// The pack's tracks whose audio is there: number and a name (the cue's TITLE, else one made from the file name:
// "28-Boss_JackInTheBox.wav" -> "Boss - Jack In The Box"; "Track n" for tracks sharing one file). MSU-1: "Track n".
std::vector<std::pair<int, std::string>> soundtrack_track_names(const SoundtrackPack& pack);

// Applies a BPS or IPS patch to `rom`. BPS checks the source and result CRCs.
bool apply_rom_patch(const std::vector<uint8_t>& rom, const std::wstring& patch_path, std::vector<uint8_t>& out,
                     std::string& error);

// The ROM the game runs with the pack: `rom` (the verified original) with the pack's patch, or its pre-patched ROM once
// checked. MD+: an MD+ boot screen that the patch puts before the game (its reset vector moved into the appended code)
// is skipped (the original reset vector), so the game starts, and the level select works, as without the pack; and the
// known mistakes of a pack's patch are corrected (soundtrack.cpp, kPackFixes).
bool soundtrack_rom(const SoundtrackPack& pack, const std::vector<uint8_t>& rom, std::vector<uint8_t>& out, std::string& error);

// Whether `patched` is `rom` with MSU-1 support added and nothing else: the same size, and every difference either in
// the header's checksum, in what was free space in `rom` (a run of one fill byte), or a short hook (up to 16 hooks of up
// to 8 bytes each); and the new code writes the MSU-1's registers. `why` says what failed.
bool msu_rom_check(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& patched, std::string& why);

class MdPlus {
public:
    MdPlus();
    ~MdPlus();
    bool load(const SoundtrackPack& pack, std::string& error);
    // A 68000 write to cartridge space (MegaDrive::on_cart_write).
    void write(uint32_t addr, uint16_t value, int size);
    // Adds `frames` stereo frames at `out_rate` to `out` (the game's audio of the same moment), at `gain`.
    void mix(int16_t* out, int frames, double out_rate, float gain);
    void stop();               // power-on / reset
    bool playing() const;
    // The Music Player: plays a track directly (looping from the cue's loop point if `loop`); the playing track's
    // position and a track's length, in seconds.
    bool play_track(int track, bool loop) { return play(track, loop); }
    bool unlocked() const { return unlocked_; }   // (the overlay is open: a command may follow)
    double position() const;
    double track_seconds(int track) const;
    // Save states: what plays and where ("t<track>,<frame>,<flags>,<volume>"; flags: 1 looping, 2 paused, 4 the overlay
    // open; empty = nothing).
    std::string state() const;
    void set_state(const std::string& s);

private:
    struct Track {
        std::wstring file;
        bool raw = false;              // BINARY: raw CD audio (16-bit stereo, 44.1 kHz, little-endian)
        int64_t start = 0;             // first frame within the file (INDEX 01)
        int64_t end = -1;              // one past the last frame (-1: the end of the file / the next track)
        int64_t loop = 0;              // REM LOOP n: frames from the track start (n CD sectors of 588 frames)
        int loops = 0;                 // the cue's word on looping: REM LOOP 1 (always, as the MegaSD / Genesis Plus
                                       // GX do, even for a play-once command), REM NOLOOP -1 (never), 0 the command's
    };
    struct Stream;                     // an open track: file, format, read buffer
    std::vector<Track> tracks_;        // by track number - 1
    std::unique_ptr<Stream> st_;
    int cur_ = 0;                      // track playing (1-based; 0 none)
    bool loop_ = false, paused_ = false, unlocked_ = false;
    int64_t pos_ = 0;                  // next frame to read, relative to the track start
    double phase_ = 0;                 // resampler position between win_[1] and win_[2]
    float win_[4][2] = {};             // the four source frames around the read position (cubic)
    float volume_ = 1.0f;              // $15xx
    float fade_ = 1.0f, fade_step_ = 0; // $13xx: fading out, then pausing
    void command(int cmd, int arg);
    bool play(int track, bool loop, int64_t at = 0);
    bool next_frame(float& l, float& r);
};
