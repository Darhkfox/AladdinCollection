// MSU-1: the SD2SNES / higan "media enhancement chip" that MSU-1 patches use for CD-quality music (and a data file).
// Ported from the Super Mario All-Stars PC port (src/core/msu1.cpp there, by the same author). Registers at
// $2000-$2007 (banks $00-$3F, $80-$BF):
//   read  $2000 status: 7 data busy, 6 audio busy, 5 repeating, 4 playing, 3 track missing, 2-0 revision
//         $2001 the data file's next byte
//         $2002-$2007 "S-MSU1"
//   write $2000-$2003 the data file's seek offset ($2003 seeks)
//         $2004-$2005 the track ($2005 loads it)
//         $2006 volume, $2007 control (bit 0 play, bit 1 repeat)
// Tracks are <base>-<n>.pcm: "MSU1", a 32-bit loop point (in samples), then 44.1 kHz 16-bit stereo; the data file
// <base>.msu. The chip's music is added to the console's own sound (32040 Hz) frame by frame (mix), so it keeps the
// game's time: fast forward, rewind and save states move it with the game. Nothing here is the console's: no
// SNES state depends on it apart from what the game reads back from the registers.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class Msu1 {
public:
    ~Msu1();
    // `base`: the pack's files' path without their endings (folder/Aladdin_(USA)_(MSU1)).
    void open(const std::wstring& base);
    void reset();
    uint8_t read(uint16_t adr);
    void write(uint16_t adr, uint8_t v);
    // Adds the music for `frames` stereo samples at `rate` to `out` (int32, unclamped), times `gain`.
    void mix(int32_t* out, size_t frames, double rate, float gain);
    bool playing() const { return playing_ && track_ != nullptr; }
    int track() const { return trackNum_; }
    bool repeating() const { return repeat_; }
    bool missing() const { return missing_; }
    // The Music Player: plays track `n` directly (repeating from its loop point if `repeat`); false if it's missing.
    bool play(int n, bool repeat);
    void stop() { playing_ = false; }
    double position() const { return pos_ / 44100.0; }
    double trackSeconds(int n) const;   // (0: missing)
    bool hasTrack(int n) const;
    // Save states: the registers, the track and where it is ("m<track>,<sample>,<flags>,<volume>,<data>,<latches>";
    // empty or unreadable: the chip as after reset, nothing playing). Loading the state of the track already open
    // (rewind) only moves within it.
    std::string state() const;
    void setState(const std::string& s);

private:
    std::wstring base_;
    FILE* data_ = nullptr;
    uint32_t dataPos_ = 0, seekLatch_ = 0;
    FILE* track_ = nullptr;
    uint16_t trackLatch_ = 0, trackNum_ = 0;
    bool missing_ = false, playing_ = false, repeat_ = false;
    uint8_t volume_ = 255;
    uint32_t loop_ = 0;          // (the loop point, in samples)
    uint64_t trackFrames_ = 0;   // (its length, in samples)
    uint64_t pos_ = 0;           // (the next sample to read)
    // (44.1 kHz to the console's rate, cubic: the four samples around the read position, and how far between [1] and [2])
    float win_[4][2] = {};
    double frac_ = 0.0;
    std::vector<int16_t> buf_;
    size_t bufAt_ = 0;
    void loadTrack(uint16_t n, uint64_t at, bool keepPlaying);
    bool nextSample(float* lr);
    void seek(uint64_t at);
};
