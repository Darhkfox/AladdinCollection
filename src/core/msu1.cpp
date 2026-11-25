// MSU-1: see msu1.h. Ported from the Super Mario All-Stars PC port's core/msu1.cpp (same author, MIT); here the music
// is resampled with a cubic (instead of linear) interpolation, mixed into the session's int32 stream, and its state is
// kept as text with the session's save-state tag.
#include "msu1.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr size_t kChunk = 4096;   // (samples read at a time)

// (the core's own: the app's open_file isn't linked into the tools' DLLs)
FILE* open_file(const std::wstring& path, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + std::strlen(mode));
    return _wfopen(path.c_str(), m.c_str());
#else
    std::string u;
    for (wchar_t wc : path) {
        const uint32_t c = (uint32_t)wc;
        if (c < 0x80) u += (char)c;
        else if (c < 0x800) u += (char)(0xC0 | c >> 6), u += (char)(0x80 | (c & 0x3F));
        else if (c < 0x10000) u += (char)(0xE0 | c >> 12), u += (char)(0x80 | (c >> 6 & 0x3F)), u += (char)(0x80 | (c & 0x3F));
        else
            u += (char)(0xF0 | c >> 18), u += (char)(0x80 | (c >> 12 & 0x3F)), u += (char)(0x80 | (c >> 6 & 0x3F)),
                u += (char)(0x80 | (c & 0x3F));
    }
    return fopen(u.c_str(), mode);
#endif
}

bool seek_file(FILE* f, uint64_t at) {
#ifdef _WIN32
    return _fseeki64(f, (long long)at, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)at, SEEK_SET) == 0;
#endif
}

uint64_t file_length(FILE* f) {
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return 0;
    const long long n = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return 0;
    const long long n = (long long)ftello(f);
#endif
    return n > 0 ? (uint64_t)n : 0;
}
}  // namespace

Msu1::~Msu1() {
    if (data_) fclose(data_);
    if (track_) fclose(track_);
}

void Msu1::open(const std::wstring& base) {
    base_ = base;
    if (data_) fclose(data_);
    data_ = open_file(base + L".msu", "rb");
    reset();
}

void Msu1::reset() {
    if (track_) fclose(track_);
    track_ = nullptr;
    dataPos_ = seekLatch_ = 0;
    trackLatch_ = trackNum_ = 0;
    missing_ = playing_ = repeat_ = false;
    volume_ = 255;
    loop_ = 0;
    trackFrames_ = pos_ = 0;
    std::memset(win_, 0, sizeof(win_));
    frac_ = 0.0;
    buf_.clear();
    bufAt_ = 0;
}

uint8_t Msu1::read(uint16_t adr) {
    switch (adr & 7) {
    case 0:   // (never busy: tracks and data are read at once)
        return (uint8_t)((repeat_ ? 0x20 : 0) | (playing_ ? 0x10 : 0) | (missing_ ? 0x08 : 0) | 2);
    case 1: {
        uint8_t v = 0;
        if (data_ && seek_file(data_, dataPos_)) {
            const int c = fgetc(data_);
            if (c != EOF) v = (uint8_t)c;
        }
        dataPos_++;
        return v;
    }
    default: return (uint8_t)"S-MSU1"[(adr & 7) - 2];
    }
}

void Msu1::write(uint16_t adr, uint8_t v) {
    switch (adr & 7) {
    case 0:
    case 1:
    case 2: seekLatch_ = (seekLatch_ & ~(0xFFu << (8 * (adr & 7)))) | ((uint32_t)v << (8 * (adr & 7))); break;
    case 3:
        seekLatch_ = (seekLatch_ & 0x00FFFFFF) | ((uint32_t)v << 24);
        dataPos_ = seekLatch_;
        break;
    case 4: trackLatch_ = (uint16_t)((trackLatch_ & 0xFF00) | v); break;
    case 5:
        trackLatch_ = (uint16_t)((trackLatch_ & 0x00FF) | (v << 8));
        loadTrack(trackLatch_, 0, false);
        break;
    case 6: volume_ = v; break;
    case 7:
        if (missing_) break;
        playing_ = (v & 1) != 0;
        repeat_ = (v & 2) != 0;
        break;
    }
}

// A track opened (missing: the status says so, and the patch has the game play its own music).
void Msu1::loadTrack(uint16_t n, uint64_t at, bool keepPlaying) {
    if (track_) fclose(track_);
    track_ = nullptr;
    trackNum_ = n;
    playing_ = keepPlaying;
    if (!keepPlaying) repeat_ = false;
    buf_.clear();
    bufAt_ = 0;
    std::memset(win_, 0, sizeof(win_));
    frac_ = 0.0;
    missing_ = true;
    loop_ = 0;
    trackFrames_ = pos_ = 0;
    if (base_.empty()) {
        playing_ = false;
        return;
    }
    track_ = open_file(base_ + L"-" + std::to_wstring(n) + L".pcm", "rb");
    uint8_t head[8];
    if (!track_ || fread(head, 1, 8, track_) != 8 || std::memcmp(head, "MSU1", 4) != 0) {
        if (track_) fclose(track_);
        track_ = nullptr;
        playing_ = false;
        return;
    }
    loop_ = head[4] | head[5] << 8 | head[6] << 16 | (uint32_t)head[7] << 24;
    const uint64_t size = file_length(track_);
    trackFrames_ = size > 8 ? (size - 8) / 4 : 0;
    missing_ = false;
    seek(at);
}

void Msu1::seek(uint64_t at) {
    pos_ = std::min<uint64_t>(at, trackFrames_);
    if (track_) seek_file(track_, 8 + pos_ * 4);
    buf_.clear();
    bufAt_ = 0;
}

// The track's next sample (silence when stopped or done), -1..1.
bool Msu1::nextSample(float* lr) {
    lr[0] = lr[1] = 0;
    if (!playing_ || !track_) return false;
    if (bufAt_ >= buf_.size()) {
        if (pos_ >= trackFrames_) {
            if (!repeat_ || loop_ >= trackFrames_) {
                playing_ = false;   // (the status's "playing" bit clears: the game may wait for it)
                return false;
            }
            seek(loop_);
        }
        const size_t want = (size_t)std::min<uint64_t>(kChunk, trackFrames_ - pos_);
        buf_.resize(want * 2);
        const size_t got = fread(buf_.data(), 4, want, track_);
        buf_.resize(got * 2);
        bufAt_ = 0;
        if (got == 0) {
            playing_ = false;
            return false;
        }
    }
    // (little-endian files, read as host shorts: x86 and ARM are little-endian)
    lr[0] = buf_[bufAt_] / 32768.0f;
    lr[1] = buf_[bufAt_ + 1] / 32768.0f;
    bufAt_ += 2;
    pos_++;
    return true;
}

void Msu1::mix(int32_t* out, size_t frames, double rate, float gain) {
    if (!playing_ && win_[1][0] == 0 && win_[1][1] == 0 && win_[2][0] == 0 && win_[2][1] == 0 && win_[3][0] == 0 &&
        win_[3][1] == 0)
        return;
    const double step = 44100.0 / rate;
    const float g = gain * volume_ / 255.0f * 32767.0f;
    for (size_t i = 0; i < frames; i++) {
        const float t = (float)frac_, t2 = t * t, t3 = t2 * t;
        for (int c = 0; c < 2; c++) {   // cubic Hermite through win_[0..3], between [1] and [2]
            const float p0 = win_[0][c], p1 = win_[1][c], p2 = win_[2][c], p3 = win_[3][c];
            const float v = p1 + 0.5f * t * (p2 - p0) + t2 * (p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3) +
                            t3 * (1.5f * (p1 - p2) + 0.5f * (p3 - p0));
            out[i * 2 + c] += (int32_t)lrintf(v * g);
        }
        frac_ += step;
        while (frac_ >= 1.0) {
            frac_ -= 1.0;
            for (int k = 0; k < 3; k++) {
                win_[k][0] = win_[k + 1][0];
                win_[k][1] = win_[k + 1][1];
            }
            nextSample(win_[3]);
        }
    }
}

bool Msu1::hasTrack(int n) const {
    if (base_.empty() || n < 0 || n > 0xFFFF) return false;
    FILE* f = open_file(base_ + L"-" + std::to_wstring(n) + L".pcm", "rb");
    if (!f) return false;
    uint8_t head[4];
    const bool ok = fread(head, 1, 4, f) == 4 && std::memcmp(head, "MSU1", 4) == 0;
    fclose(f);
    return ok;
}

double Msu1::trackSeconds(int n) const {
    if (base_.empty() || n < 0 || n > 0xFFFF) return 0.0;
    FILE* f = open_file(base_ + L"-" + std::to_wstring(n) + L".pcm", "rb");
    if (!f) return 0.0;
    const uint64_t size = file_length(f);
    fclose(f);
    return size > 8 ? (double)((size - 8) / 4) / 44100.0 : 0.0;
}

bool Msu1::play(int n, bool repeat) {
    if (n < 0 || n > 0xFFFF) return false;
    trackLatch_ = (uint16_t)n;
    loadTrack((uint16_t)n, 0, true);
    if (missing_) return false;
    repeat_ = repeat;
    return true;
}

std::string Msu1::state() const {
    char b[128];
    snprintf(b, sizeof(b), "m%u,%llu,%u,%u,%u,%u,%u", (unsigned)trackNum_, (unsigned long long)pos_,
             (missing_ ? 1u : 0u) | (playing_ ? 2u : 0u) | (repeat_ ? 4u : 0u), (unsigned)volume_, (unsigned)dataPos_,
             (unsigned)trackLatch_, (unsigned)seekLatch_);
    return b;
}

void Msu1::setState(const std::string& s) {
    unsigned track = 0, flags = 0, volume = 255, data = 0, latch = 0, seekl = 0;
    unsigned long long at = 0;
    if (s.size() < 2 || s[0] != 'm' ||
        sscanf(s.c_str() + 1, "%u,%llu,%u,%u,%u,%u,%u", &track, &at, &flags, &volume, &data, &latch, &seekl) != 7) {
        reset();   // (a state without the chip's: silent until the game picks a track)
        return;
    }
    dataPos_ = data;
    seekLatch_ = seekl;
    trackLatch_ = (uint16_t)latch;
    volume_ = (uint8_t)std::min(volume, 255u);
    const bool wasMissing = (flags & 1) != 0;
    if (!wasMissing && track_ && !missing_ && track == trackNum_) {
        // (the same track, as rewinding steps back through it: a seek, not a reopen)
        playing_ = (flags & 2) != 0;
        repeat_ = (flags & 4) != 0;
        std::memset(win_, 0, sizeof(win_));
        frac_ = 0.0;
        seek(at);
        return;
    }
    if (track == 0 && !wasMissing) {   // (as after reset: no track asked for yet)
        if (track_) fclose(track_);
        track_ = nullptr;
        trackNum_ = 0;
        missing_ = playing_ = repeat_ = false;
        loop_ = 0;
        trackFrames_ = pos_ = 0;
        std::memset(win_, 0, sizeof(win_));
        frac_ = 0.0;
        buf_.clear();
        bufAt_ = 0;
        return;
    }
    if (wasMissing) {
        loadTrack((uint16_t)track, 0, false);
        missing_ = true;
        return;
    }
    loadTrack((uint16_t)track, at, (flags & 2) != 0);
    repeat_ = (flags & 4) != 0;
}
