#include "soundtrack.h"
#include "romfind.h"
#include "sys.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <functional>
#include <map>
#include <tuple>

namespace {
std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}
bool has_ext(const std::wstring& name, const wchar_t* ext) {
    const std::wstring n = lower(name), e = ext;
    return n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0;
}
std::wstring parent_dir(const std::wstring& path) {
    size_t s = path.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : path.substr(0, s);
}

// ---- cue sheets -----------------------------------------------------------------------------------------------
struct CueTrack {
    int number = 0;
    std::wstring file;
    bool raw = false;
    int64_t start = 0, loop = 0;
    int loops = 0;             // REM LOOP: 1, REM NOLOOP: -1 (MdPlus::Track::loops)
    std::string title;
};

std::vector<CueTrack> parse_cue(const std::wstring& path) {
    std::vector<CueTrack> out;
    std::string text;
    if (!read_text(path, text)) return out;
    const std::wstring dir = parent_dir(path);
    std::wstring file;
    bool raw = false;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        line = line.substr(b);
        auto starts = [&](const char* k) { return line.compare(0, strlen(k), k) == 0; };
        if (starts("FILE ")) {
            size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            std::string name, type;
            if (q2 != std::string::npos) { name = line.substr(q1 + 1, q2 - q1 - 1); type = line.substr(q2 + 1); }
            else { size_t s = line.find(' ', 5); name = line.substr(5, s == std::string::npos ? std::string::npos : s - 5);
                   type = s == std::string::npos ? "" : line.substr(s); }
            file = path_join(dir, from_utf8(name));
            raw = type.find("BINARY") != std::string::npos;
        } else if (starts("TRACK ")) {
            CueTrack t;
            t.number = atoi(line.c_str() + 6);
            t.file = file;
            t.raw = raw;
            if (line.find("AUDIO") != std::string::npos && t.number > 0) out.push_back(t);
            else out.push_back(CueTrack{t.number, L"", false, 0, 0, -1, ""});   // a data track keeps its number
        } else if (starts("TITLE ") && !out.empty()) {
            size_t q1 = line.find('"'), q2 = q1 == std::string::npos ? q1 : line.rfind('"');
            out.back().title = q2 != std::string::npos && q2 > q1 ? line.substr(q1 + 1, q2 - q1 - 1) : line.substr(6);
        } else if (starts("INDEX 01") && !out.empty()) {
            int mm = 0, ss = 0, ff = 0;
            if (sscanf(line.c_str() + 8, " %d:%d:%d", &mm, &ss, &ff) == 3) out.back().start = ((int64_t)(mm * 60 + ss) * 75 + ff) * 588;
        } else if (starts("REM LOOP") && !out.empty()) {
            out.back().loop = (int64_t)atoi(line.c_str() + 8) * 588;      // CD sectors from the track start (bare: 0)
            out.back().loops = 1;
        } else if (starts("REM NOLOOP") && !out.empty()) {
            out.back().loops = -1;
        }
    }
    return out;
}

bool exists(const std::wstring& path) {
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

// The ROM CRC a BPS patch was made for (its second-to-last... the first of the three trailing CRC32s).
bool bps_source_crc(const std::wstring& path, uint32_t& crc) {
    FILE* f = open_file(path, "rb");
    if (!f) return false;
    uint8_t head[4] = {}, tail[12] = {};
    const uint64_t n = file_size(f);
    bool ok = n > 16 && fread(head, 1, 4, f) == 4 && memcmp(head, "BPS1", 4) == 0 && file_seek(f, n - 12) &&
              fread(tail, 1, 12, f) == 12;
    fclose(f);
    if (ok) crc = tail[0] | (tail[1] << 8) | (tail[2] << 16) | ((uint32_t)tail[3] << 24);
    return ok;
}

// An MSU-1 pack's tracks in `entries`: NAME-N.pcm, NAME from the .msu file if there is one, else the most common
// (as the All-Stars port's msupack.cpp). Empty base: none.
void msu_tracks(const std::vector<DirEntry>& entries, std::wstring& base, int& tracks) {
    base.clear();
    tracks = 0;
    std::wstring named;
    for (const DirEntry& e : entries)
        if (!e.is_dir && has_ext(e.name, L".msu")) named = e.name.substr(0, e.name.size() - 4);
    std::vector<std::pair<std::wstring, int>> counts;
    for (const DirEntry& e : entries) {
        const std::wstring& n = e.name;
        if (e.is_dir || !has_ext(n, L".pcm")) continue;
        const size_t dash = n.find_last_of(L'-');
        if (dash == std::wstring::npos || dash + 1 >= n.size() - 4) continue;
        bool digits = true;
        for (size_t i = dash + 1; i < n.size() - 4; i++) digits = digits && iswdigit(n[i]);
        if (!digits) continue;
        const std::wstring b = n.substr(0, dash);
        auto it = std::find_if(counts.begin(), counts.end(), [&](const std::pair<std::wstring, int>& c) { return lower(c.first) == lower(b); });
        if (it == counts.end()) counts.push_back({b, 1});
        else it->second++;
    }
    for (const auto& c : counts) {
        const bool pick = named.empty() ? c.second > tracks : lower(c.first) == lower(named);
        if (!pick) continue;
        tracks = c.second;
        base = c.first;
    }
}

// A ROM file without a copier header (512 bytes before a whole number of KB).
bool read_rom_file(const std::wstring& path, std::vector<uint8_t>& out) {
    if (!read_file(path, out)) return false;
    if ((out.size() & 0x3FF) == 0x200) out.erase(out.begin(), out.begin() + 0x200);
    return !out.empty();
}

// Checks that take a ROM's worth of work, kept per file (path, size, time) and game: a pre-patched ROM's, an IPS
// patch's.
struct CheckKey {
    std::wstring path;
    uint64_t size, mtime;
    uint32_t game_crc;
    bool operator<(const CheckKey& o) const {
        return std::tie(path, size, mtime, game_crc) < std::tie(o.path, o.size, o.mtime, o.game_crc);
    }
};
std::map<CheckKey, bool>& check_cache() {
    static std::map<CheckKey, bool> m;
    return m;
}

// Writes to the MSU-1's track and control registers ($2004-$2007: STA / STZ / STX / STY absolute, STA long to bank
// $00 / $80) in the bytes of `rom` that differ from `orig` (or lie past its end).
bool writes_msu(const std::vector<uint8_t>& orig, const std::vector<uint8_t>& rom) {
    for (size_t i = 0; i + 2 < rom.size(); i++) {
        if (i + 2 < orig.size() && rom[i] == orig[i] && rom[i + 1] == orig[i + 1] && rom[i + 2] == orig[i + 2]) continue;
        const uint8_t op = rom[i];
        const bool abs = op == 0x8D || op == 0x9C || op == 0x8E || op == 0x8C;
        if (abs && rom[i + 1] >= 0x04 && rom[i + 1] <= 0x07 && rom[i + 2] == 0x20) return true;
        if (op == 0x8F && i + 3 < rom.size() && rom[i + 1] >= 0x04 && rom[i + 1] <= 0x07 && rom[i + 2] == 0x20 &&
            (rom[i + 3] & 0x7F) == 0)
            return true;
    }
    return false;
}

const SoundtrackGame* game_by_crc(const std::vector<SoundtrackGame>& games, uint32_t crc, System sys) {
    for (const SoundtrackGame& g : games) if (g.crc == crc && g.system == sys) return &g;
    return nullptr;
}

// A check of a file against a game, done once per file version (check_cache).
bool cached_check(const std::wstring& path, const DirEntry& e, const SoundtrackGame& g, const std::function<bool()>& run) {
    const CheckKey key{path, e.size, e.mtime, g.crc};
    auto it = check_cache().find(key);
    if (it == check_cache().end()) it = check_cache().emplace(key, run()).first;
    return it->second;
}
}  // namespace

// ---- finding packs --------------------------------------------------------------------------------------------
std::vector<SoundtrackPack> find_soundtracks(const std::vector<SoundtrackGame>& games) {
    std::vector<SoundtrackPack> out;
    std::vector<std::wstring> seen;
    auto check = [&](const std::wstring& dir) {
        for (const auto& s : seen) if (lower(s) == lower(dir)) return;
        seen.push_back(dir);
        std::vector<DirEntry> entries;
        if (!list_dir(dir, entries)) return;
        std::wstring cue, patch, rom;
        DirEntry rom_entry, patch_entry;
        for (const DirEntry& e : entries) {
            if (e.is_dir) continue;
            if (cue.empty() && has_ext(e.name, L".cue")) cue = path_join(dir, e.name);
            if (patch.empty() && (has_ext(e.name, L".bps") || has_ext(e.name, L".ips"))) {
                patch = path_join(dir, e.name);
                patch_entry = e;
            }
            if (rom.empty() && (has_ext(e.name, L".sfc") || has_ext(e.name, L".smc"))) {
                rom = path_join(dir, e.name);
                rom_entry = e;
            }
        }
        SoundtrackPack pk;
        pk.dir = dir;
        pk.name = to_utf8(file_name(dir));
        std::wstring base;
        int pcm = 0;
        msu_tracks(entries, base, pcm);
        if (pcm > 0 && (!patch.empty() || !rom.empty())) {
            // MSU-1: the SNES game whose ROM the BPS patch is for, the one an IPS patch adds MSU-1 code to, or the one
            // the pre-patched ROM is (with only MSU-1 code added: msu_rom_check)
            pk.kind = SoundtrackKind::Msu1;
            pk.msu_base = path_join(dir, base);
            pk.tracks = pcm;
            uint32_t crc = 0;
            if (!patch.empty() && has_ext(patch, L".bps")) {
                if (bps_source_crc(patch, crc))
                    if (const SoundtrackGame* g = game_by_crc(games, crc, System::Snes)) pk.game_id = g->id;
            } else if (!patch.empty()) {
                for (const SoundtrackGame& g : games) {
                    if (g.system != System::Snes || !g.rom || g.rom->empty() || !pk.game_id.empty()) continue;
                    if (cached_check(patch, patch_entry, g, [&]() {
                            std::vector<uint8_t> p;
                            std::string err;
                            return apply_rom_patch(*g.rom, patch, p, err) && writes_msu(*g.rom, p);
                        }))
                        pk.game_id = g.id;
                }
            }
            if (!pk.game_id.empty()) {
                pk.patch = patch;
            } else if (!rom.empty()) {
                for (const SoundtrackGame& g : games) {
                    if (g.system != System::Snes || !g.rom || g.rom->empty() || !pk.game_id.empty()) continue;
                    if (cached_check(rom, rom_entry, g, [&]() {
                            std::vector<uint8_t> r;
                            std::string why;
                            return read_rom_file(rom, r) && msu_rom_check(*g.rom, r, why);
                        })) {
                        pk.game_id = g.id;
                        pk.rom = rom;
                    }
                }
            }
            if (!pk.game_id.empty()) out.push_back(pk);
            return;
        }
        if (cue.empty() || patch.empty()) return;
        // MD+: the Mega Drive game whose ROM the BPS patch is for (an IPS patch carries no CRC: the Mega Drive game)
        pk.kind = SoundtrackKind::MdPlus;
        pk.cue = cue;
        pk.patch = patch;
        uint32_t crc = 0;
        if (has_ext(patch, L".bps")) {
            if (!bps_source_crc(patch, crc)) return;
            if (const SoundtrackGame* g = game_by_crc(games, crc, System::MegaDrive)) pk.game_id = g->id;
        } else {
            for (const SoundtrackGame& g : games) if (g.system == System::MegaDrive && pk.game_id.empty()) pk.game_id = g.id;
        }
        if (pk.game_id.empty()) return;
        for (const CueTrack& t : parse_cue(cue))
            if (!t.file.empty() && exists(t.file)) pk.tracks++;
        if (pk.tracks > 0) out.push_back(pk);
    };
    std::vector<DirEntry> entries, sub;
    std::vector<std::wstring> roots;
    for (const std::wstring& top : {exe_dir(), data_dir(), export_dir()})
        for (const wchar_t* name : {L"soundtracks", L"msu"}) {
            const std::wstring r = path_join(top, name);
            bool dup = false;
            for (const auto& o : roots) dup = dup || lower(o) == lower(r);
            if (!dup) roots.push_back(r);
        }
    for (const auto& root : roots) {            // soundtracks\<pack>, msu\<pack>, or either\<download>\<pack>
        if (!list_dir(root, entries)) continue;
        for (const DirEntry& e : entries) {
            if (!e.is_dir) continue;
            const std::wstring d = path_join(root, e.name);
            check(d);
            if (list_dir(d, sub))
                for (const DirEntry& s : sub) if (s.is_dir) check(path_join(d, s.name));
        }
    }
    for (const auto& root : rom_dirs()) {                   // a pack folder placed next to the ROMs
        if (!list_dir(root, entries)) continue;
        for (const DirEntry& e : entries)
            if (e.is_dir && lower(e.name) != L"soundtracks" && lower(e.name) != L"msu") check(path_join(root, e.name));
    }
    std::sort(out.begin(), out.end(), [](const SoundtrackPack& a, const SoundtrackPack& b) {
        return a.game_id != b.game_id ? a.game_id < b.game_id : a.name < b.name;
    });
    return out;
}

// "28-Boss_JackInTheBox.wav" -> "Boss - Jack In The Box"
static std::string name_from_file(const std::wstring& path) {
    std::string n = to_utf8(file_name(path));
    const size_t dot = n.rfind('.');
    if (dot != std::string::npos && dot > 0) n.resize(dot);
    size_t i = 0;
    while (i < n.size() && isdigit((unsigned char)n[i])) i++;
    if (i > 0 && i < n.size() && strchr("-_ .", n[i])) n = n.substr(i + 1);
    std::string out;
    for (size_t k = 0; k < n.size(); k++) {
        const unsigned char c = (unsigned char)n[k], prev = k ? (unsigned char)n[k - 1] : 0;
        if (c == '_') { out += " - "; continue; }
        if (islower(prev) && (isupper(c) || isdigit(c))) out += ' ';
        out += (char)c;
    }
    return out;
}

std::vector<std::pair<int, std::string>> soundtrack_track_names(const SoundtrackPack& pack) {
    std::vector<std::pair<int, std::string>> out;
    if (pack.kind == SoundtrackKind::Msu1) {   // (NAME-N.pcm: numbers only)
        std::vector<DirEntry> entries;
        const std::wstring stem = lower(file_name(pack.msu_base)) + L"-";
        if (list_dir(pack.dir, entries))
            for (const DirEntry& e : entries) {
                const std::wstring n = lower(e.name);
                if (e.is_dir || !has_ext(n, L".pcm") || n.compare(0, stem.size(), stem) != 0) continue;
                const int t = (int)wcstol(n.c_str() + stem.size(), nullptr, 10);
                if (t > 0) out.push_back({t, "Track " + std::to_string(t)});
            }
        std::sort(out.begin(), out.end());
        return out;
    }
    const std::vector<CueTrack> cue = parse_cue(pack.cue);
    for (const CueTrack& t : cue) {
        if (t.file.empty() || !exists(t.file)) continue;
        const bool shared = std::count_if(cue.begin(), cue.end(), [&](const CueTrack& o) { return o.file == t.file; }) > 1;
        std::string name = !t.title.empty() ? t.title : shared ? std::string() : name_from_file(t.file);
        if (name.empty()) name = "Track " + std::to_string(t.number);
        out.push_back({t.number, name});
    }
    return out;
}

// The name's parts in parentheses that say nothing about the arrangement: regions, years, consoles, "MSU1", genres.
static bool generic_part(std::string p) {
    for (auto& c : p) c = (char)tolower((unsigned char)c);
    static const char* words[] = {"usa", "u", "us", "europe", "e", "eu", "japan", "j", "jp", "world", "w", "msu1", "msu-1",
                                  "msu", "md+", "mdplus", "super nes", "snes", "sfc", "super famicom", "genesis",
                                  "mega drive", "megadrive", "md", "action platform", "platform", "action", "!"};
    for (const char* w : words) if (p == w) return true;
    if (p.size() == 4 && std::all_of(p.begin(), p.end(), [](char c) { return isdigit((unsigned char)c) != 0; })) return true;
    return p.compare(0, 4, "rev ") == 0 || p.compare(0, 2, "v1") == 0;
}

std::string soundtrack_label(const std::string& name) {
    std::string n = name;
    if (n.compare(0, 4, "MD+ ") == 0) n = n.substr(4);
    // the parts in parentheses: an "Audio ..." one (MSU-1 packs' naming: "(Audio Orchestral)"), else the last one that
    // isn't a region, a year or the like
    std::vector<std::string> parts;
    for (size_t a = n.find('('); a != std::string::npos; a = n.find('(', a + 1)) {
        const size_t b = n.find(')', a);
        if (b == std::string::npos) break;
        if (b > a + 1) parts.push_back(n.substr(a + 1, b - a - 1));
    }
    std::string pick;
    for (const std::string& p : parts)
        if (p.compare(0, 6, "Audio ") == 0 && p.size() > 6) pick = p.substr(6);
    for (size_t i = parts.size(); pick.empty() && i-- > 0;)
        if (!generic_part(parts[i])) pick = parts[i];
    const size_t by = n.rfind(" by ");
    if (!pick.empty()) n = pick;
    else if (by != std::string::npos) n = "by " + n.substr(by + 4);
    else if (!parts.empty()) n = n.substr(0, n.find('(')), n.erase(n.find_last_not_of(' ') + 1);
    if (n.size() > 34) n = n.substr(0, 32) + "\xE2\x80\xA6";
    return n;
}

std::string soundtrack_label(const SoundtrackPack& pack) {
    const std::string l = soundtrack_label(pack.name);
    if (pack.kind == SoundtrackKind::Msu1)
        return l.find("MSU") == std::string::npos ? "MSU-1 " + l : l;
    return l.find("MD+") == std::string::npos ? "MD+ " + l : l;
}

// ---- ROM patches ----------------------------------------------------------------------------------------------
bool apply_rom_patch(const std::vector<uint8_t>& rom, const std::wstring& patch_path, std::vector<uint8_t>& out,
                     std::string& error) {
    std::vector<uint8_t> p;
    if (!read_file(patch_path, p)) { error = "can't read the patch"; return false; }
    if (p.size() >= 4 && memcmp(p.data(), "BPS1", 4) == 0) {
        if (p.size() < 16) { error = "the patch is cut short"; return false; }
        size_t pos = 4;
        const size_t end = p.size() - 12;
        bool bad = false;
        auto num = [&]() -> uint64_t {
            uint64_t data = 0, shift = 1;
            while (pos < end) {
                const uint8_t x = p[pos++];
                data += (x & 0x7F) * shift;
                if (x & 0x80) return data;
                shift <<= 7;
                data += shift;
            }
            bad = true;
            return 0;
        };
        auto crc_at = [&](size_t o) { return p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24); };
        const uint64_t src_size = num(), dst_size = num(), meta = num();
        pos += (size_t)meta;
        if (bad || src_size != rom.size() || dst_size > (16u << 20)) { error = "the patch is for another ROM"; return false; }
        if (crc32_of(rom.data(), rom.size()) != crc_at(p.size() - 12)) { error = "the patch is for another ROM"; return false; }
        out.assign((size_t)dst_size, 0);
        size_t o = 0, src_rel = 0, dst_rel = 0;
        while (pos < end && !bad) {
            const uint64_t d = num();
            const size_t len = (size_t)(d >> 2) + 1;
            if (o + len > out.size()) { bad = true; break; }
            switch (d & 3) {
            case 0:                                                    // SourceRead
                if (o + len > rom.size()) { bad = true; break; }
                memcpy(&out[o], &rom[o], len); o += len; break;
            case 1:                                                    // TargetRead
                if (pos + len > end) { bad = true; break; }
                memcpy(&out[o], &p[pos], len); pos += len; o += len; break;
            default: {
                const uint64_t r = num();
                const int64_t off = (r & 1 ? -1 : 1) * (int64_t)(r >> 1);
                if ((d & 3) == 2) {                                    // SourceCopy
                    src_rel = (size_t)((int64_t)src_rel + off);
                    if (src_rel + len > rom.size()) { bad = true; break; }
                    memcpy(&out[o], &rom[src_rel], len); src_rel += len; o += len;
                } else {                                               // TargetCopy (may overlap)
                    dst_rel = (size_t)((int64_t)dst_rel + off);
                    if (dst_rel >= o) { bad = true; break; }
                    for (size_t i = 0; i < len; i++) out[o++] = out[dst_rel++];
                }
            }
            }
        }
        if (bad || o != out.size()) { error = "the patch is damaged"; return false; }
        if (crc32_of(out.data(), out.size()) != crc_at(p.size() - 8)) { error = "the patched ROM doesn't check out"; return false; }
        return true;
    }
    if (p.size() >= 8 && memcmp(p.data(), "PATCH", 5) == 0) {         // IPS
        out = rom;
        size_t pos = 5;
        while (pos + 3 <= p.size()) {
            if (memcmp(&p[pos], "EOF", 3) == 0) {
                if (pos + 6 <= p.size()) out.resize((p[pos + 3] << 16) | (p[pos + 4] << 8) | p[pos + 5]);   // truncation
                return true;
            }
            if (pos + 5 > p.size()) break;
            const size_t at = (p[pos] << 16) | (p[pos + 1] << 8) | p[pos + 2];
            size_t len = (p[pos + 3] << 8) | p[pos + 4];
            pos += 5;
            if (len == 0) {                                            // run of one byte
                if (pos + 3 > p.size()) break;
                len = (p[pos] << 8) | p[pos + 1];
                if (out.size() < at + len) out.resize(at + len, 0);
                memset(&out[at], p[pos + 2], len);
                pos += 3;
            } else {
                if (pos + len > p.size()) break;
                if (out.size() < at + len) out.resize(at + len, 0);
                memcpy(&out[at], &p[pos], len);
                pos += len;
            }
        }
        error = "the patch is damaged";
        return false;
    }
    error = "not a BPS or IPS patch";
    return false;
}

// ---- the ROM a pack runs on ------------------------------------------------------------------------------------
namespace {
// Known mistakes in packs' patches, corrected in the patched ROM (identified by its CRC32): bytes at ROM offsets, each
// with what the patch wrote and what it should be.
struct PackFix {
    uint32_t patched_crc;
    const char* what;
    struct Byte { uint32_t at; uint8_t was, now; };
    std::vector<Byte> bytes;
};
const PackFix kPackFixes[] = {
    // Aladdin CD - MD+ (ArcadeTV, 2023-04-12): its table of the game's tunes swaps the boss tune ($32, which the game
    // starts as a boss appears, a loop) and the bonus jingle ($12, about 4 s): a boss fight played the 5.5 s jingle
    // 13-BONUS_nl.wav, which ends in silence, and the jingle the 35 s 05-BOSSTUNE.wav. The comparisons' immediates at
    // $2050FE (cmp.b #$12 -> track 5) and $20513E (cmp.b #$32 -> track 13) are exchanged.
    {0x4A7C033C, "the boss tune and the bonus jingle", {{0x205101, 0x12, 0x32}, {0x205141, 0x32, 0x12}}},
};

uint32_t be32(const std::vector<uint8_t>& r, size_t a) {
    return a + 4 <= r.size() ? (uint32_t)r[a] << 24 | r[a + 1] << 16 | r[a + 2] << 8 | r[a + 3] : 0;
}

// The Mega Drive header's checksum ($18E): the words from $200 to the ROM end the header gives ($1A4).
void md_fix_checksum(std::vector<uint8_t>& r) {
    if (r.size() < 0x200) return;
    uint32_t end = be32(r, 0x1A4) + 1;
    if (end <= 0x200 || end > r.size()) end = (uint32_t)r.size();
    uint16_t sum = 0;
    for (uint32_t a = 0x200; a + 1 < end; a += 2) sum = (uint16_t)(sum + (r[a] << 8 | r[a + 1]));
    r[0x18E] = (uint8_t)(sum >> 8);
    r[0x18F] = (uint8_t)sum;
}
}  // namespace

bool msu_rom_check(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& patched, std::string& why) {
    if (patched.size() != rom.size()) { why = "the ROM in the pack is another size than yours"; return false; }
    // free space in the original: runs of at least 32 of one byte
    std::vector<uint8_t> free_space(rom.size(), 0);
    for (size_t i = 0; i < rom.size();) {
        size_t j = i;
        while (j < rom.size() && rom[j] == rom[i]) j++;
        if (j - i >= 32) std::fill(free_space.begin() + i, free_space.begin() + j, 1);
        i = j;
    }
    auto header_sum = [&](size_t i) {   // (LoROM / HiROM header's checksum and its complement)
        return (i >= 0x7FDC && i < 0x7FE0) || (i >= 0xFFDC && i < 0xFFE0);
    };
    int hooks = 0;
    for (size_t i = 0; i < rom.size();) {
        if (rom[i] == patched[i] || header_sum(i) || free_space[i]) { i++; continue; }
        // a hook: changed bytes of used code / data, up to 8 together (gaps of up to 2 unchanged bytes inside)
        size_t last = i, j = i;
        while (j < rom.size() && j <= last + 2) {
            if (rom[j] != patched[j] && !free_space[j] && !header_sum(j)) last = j;
            j++;
        }
        if (last - i + 1 > 8) { why = "the ROM in the pack changes more of the game than an MSU-1 patch does"; return false; }
        if (++hooks > 16) { why = "the ROM in the pack changes more of the game than an MSU-1 patch does"; return false; }
        i = last + 1;
    }
    if (hooks == 0 || !writes_msu(rom, patched)) { why = "the ROM in the pack has no MSU-1 code"; return false; }
    return true;
}

bool soundtrack_rom(const SoundtrackPack& pack, const std::vector<uint8_t>& rom, std::vector<uint8_t>& out, std::string& error) {
    if (pack.kind == SoundtrackKind::Msu1) {
        if (!pack.patch.empty()) {
            if (!apply_rom_patch(rom, pack.patch, out, error)) return false;
            if (!writes_msu(rom, out)) { error = "the patch has no MSU-1 code"; return false; }
            return true;
        }
        std::vector<uint8_t> r;
        if (pack.rom.empty() || !read_rom_file(pack.rom, r)) { error = "its patched ROM can't be read"; return false; }
        if (!msu_rom_check(rom, r, error)) return false;
        out.swap(r);   // (equal to `rom` outside the checksum, its free space and the hooks)
        return true;
    }
    if (!apply_rom_patch(rom, pack.patch, out, error)) return false;
    bool changed = false;
    // A boot screen of the patch's own (its reset vector moved into what the patch appends): the game's own start instead
    if (out.size() > rom.size() && be32(out, 4) != be32(rom, 4) && (be32(out, 4) & 0xFFFFFF) >= rom.size()) {
        std::copy(rom.begin() + 4, rom.begin() + 8, out.begin() + 4);
        changed = true;
    }
    const uint32_t crc = crc32_of(out.data(), out.size());
    for (const PackFix& f : kPackFixes) {
        if (f.patched_crc != crc) continue;
        bool ok = true;
        for (const PackFix::Byte& b : f.bytes) ok = ok && b.at < out.size() && out[b.at] == b.was;
        if (!ok) continue;
        for (const PackFix::Byte& b : f.bytes) out[b.at] = b.now;
        changed = true;
    }
    if (changed) md_fix_checksum(out);
    return true;
}

// ---- MD+ player -----------------------------------------------------------------------------------------------
struct MdPlus::Stream {
    FILE* f = nullptr;
    std::wstring path;
    uint64_t data_off = 0;
    int64_t frames = 0;                  // in the file
    int channels = 2, rate = 44100;
    std::vector<int16_t> buf;            // decoded frames from buf_first on
    int64_t buf_first = 0;
    ~Stream() { if (f) fclose(f); }

    bool open(const std::wstring& p, bool raw) {
        path = p;
        f = open_file(p, "rb");
        if (!f) return false;
        const uint64_t size = file_size(f);
        if (raw) { data_off = 0; frames = (int64_t)(size / 4); return true; }
        uint8_t h[12];
        if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
        bool fmt_ok = false;
        uint64_t at = 12;
        while (at + 8 <= size) {
            uint8_t c[8];
            if (!file_seek(f, at) || fread(c, 1, 8, f) != 8) return false;
            const uint32_t len = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
            if (memcmp(c, "fmt ", 4) == 0) {
                uint8_t fm[40] = {};
                if (fread(fm, 1, std::min<uint32_t>(len, 40), f) < 16) return false;
                const int tag = fm[0] | (fm[1] << 8), bits = fm[14] | (fm[15] << 8);
                channels = fm[2] | (fm[3] << 8);
                rate = fm[4] | (fm[5] << 8) | (fm[6] << 16) | (fm[7] << 24);
                const bool pcm = tag == 1 || (tag == 0xFFFE && len >= 26 && (fm[24] | (fm[25] << 8)) == 1);   // extensible: subformat
                fmt_ok = pcm && bits == 16 && (channels == 1 || channels == 2) && rate >= 8000 && rate <= 192000;
            } else if (memcmp(c, "data", 4) == 0) {
                if (!fmt_ok) return false;
                data_off = at + 8;
                frames = (int64_t)(std::min<uint64_t>(len, size - data_off) / (2 * channels));
                return true;
            }
            at += 8 + len + (len & 1);
        }
        return false;
    }

    // frame `i` of the file (buffered reads of ~0.1 s)
    bool frame(int64_t i, float& l, float& r) {
        if (i < 0 || i >= frames) return false;
        const int64_t n = (int64_t)buf.size() / channels;
        if (i < buf_first || i >= buf_first + n) {
            const int64_t want = std::min<int64_t>(4096, frames - i);
            buf.resize((size_t)(want * channels));
            if (!file_seek(f, data_off + (uint64_t)i * 2 * channels)) { buf.clear(); return false; }
            const size_t got = fread(buf.data(), 2 * channels, (size_t)want, f);
            buf.resize(got * channels);
            buf_first = i;
            if (got == 0) return false;
        }
        const int16_t* s = &buf[(size_t)(i - buf_first) * channels];   // little-endian file, little-endian CPU
        l = s[0] / 32768.0f;
        r = channels == 2 ? s[1] / 32768.0f : l;
        return true;
    }
};

MdPlus::MdPlus() {}
MdPlus::~MdPlus() {}

bool MdPlus::load(const SoundtrackPack& pack, std::string& error) {
    tracks_.clear();
    std::vector<CueTrack> cue = parse_cue(pack.cue);
    if (cue.empty()) { error = "no tracks in the cue sheet"; return false; }
    for (size_t i = 0; i < cue.size(); i++) {
        const CueTrack& c = cue[i];
        if (c.number <= 0 || c.number > 99) continue;
        if ((int)tracks_.size() < c.number) tracks_.resize(c.number);
        Track& t = tracks_[c.number - 1];
        t.file = c.file;
        t.raw = c.raw;
        t.start = c.start;
        t.loop = c.loop;
        t.loops = c.loops;
        t.end = -1;
        if (i + 1 < cue.size() && cue[i + 1].file == c.file && cue[i + 1].start > c.start) t.end = cue[i + 1].start;
    }
    stop();
    return true;
}

void MdPlus::stop() {
    cur_ = 0;
    paused_ = false;
    unlocked_ = false;
    volume_ = 1.0f;
    fade_ = 1.0f;
    fade_step_ = 0;
}

bool MdPlus::playing() const { return cur_ > 0 && !paused_; }

double MdPlus::position() const {
    return cur_ && st_ ? std::max<int64_t>(0, pos_ - 3) / (double)st_->rate : 0.0;   // (pos_ runs 3 frames ahead)
}

double MdPlus::track_seconds(int track) const {
    if (track <= 0 || track > (int)tracks_.size() || tracks_[track - 1].file.empty()) return 0.0;
    const Track& t = tracks_[track - 1];
    Stream s;
    if (!s.open(t.file, t.raw)) return 0.0;
    const int64_t end = t.end >= 0 ? std::min(t.end, s.frames) : s.frames;
    return std::max<int64_t>(0, end - t.start) / (double)s.rate;
}

void MdPlus::write(uint32_t addr, uint16_t value, int size) {
    if (size != 2) return;
    if (addr == 0x3F7FA) unlocked_ = value == 0xCD54;
    else if (addr == 0x3F7FE && unlocked_) command(value >> 8, value & 0xFF);
}

void MdPlus::command(int cmd, int arg) {
    switch (cmd) {
    case 0x11: play(arg, false); break;                  // play once (unless the cue says REM LOOP)
    case 0x12: play(arg, true); break;                   // play, looping from the cue's REM LOOP point (unless NOLOOP)
    case 0x13:                                           // pause; arg = fade-out time in 1/75 s
        if (arg == 0 || !cur_) { paused_ = true; fade_ = 1.0f; fade_step_ = 0; }
        else fade_step_ = 75.0f / arg;                   // (per second; mix() turns it into per output frame)
        break;
    case 0x14: paused_ = false; fade_ = 1.0f; fade_step_ = 0; break;   // resume
    case 0x15: volume_ = arg / 255.0f; break;            // volume
    default: break;
    }
}

// A track the pack doesn't have (or can't be read) leaves what plays as it is, as on the MegaSD (and in Genesis Plus GX).
// The cue's REM LOOP / REM NOLOOP overrides the command's looping, as there too.
bool MdPlus::play(int track, bool loop, int64_t at) {
    if (track <= 0 || track > (int)tracks_.size() || tracks_[track - 1].file.empty()) return false;
    const Track& t = tracks_[track - 1];
    if (!st_ || st_->path != t.file) {
        std::unique_ptr<Stream> s(new Stream);
        if (!s->open(t.file, t.raw)) return false;
        st_ = std::move(s);
    }
    cur_ = track;
    loop_ = t.loops != 0 ? t.loops > 0 : loop;
    paused_ = false;
    fade_ = 1.0f;
    fade_step_ = 0;
    pos_ = std::max<int64_t>(0, at);
    phase_ = 0;
    win_[0][0] = win_[0][1] = 0;
    for (int k = 1; k < 4; k++)
        if (!next_frame(win_[k][0], win_[k][1])) win_[k][0] = win_[k][1] = 0;
    return true;
}

// The next source frame of the playing track (looping back to the loop point); false at its end.
bool MdPlus::next_frame(float& l, float& r) {
    const Track& t = tracks_[cur_ - 1];
    const int64_t end = t.end >= 0 ? std::min(t.end, st_->frames) : st_->frames;
    if (t.start + pos_ >= end) {
        if (!loop_) return false;
        pos_ = t.loop < end - t.start ? t.loop : 0;
    }
    if (!st_->frame(t.start + pos_, l, r)) return false;
    pos_++;
    return true;
}

void MdPlus::mix(int16_t* out, int frames, double out_rate, float gain) {
    if (!cur_ || paused_ || !st_ || frames <= 0) return;
    const double step = st_->rate / out_rate;
    const float fade_per_frame = fade_step_ > 0 ? (float)(fade_step_ / out_rate) : 0.0f;
    int ended = 0;
    for (int i = 0; i < frames; i++) {
        const float t = (float)phase_, t2 = t * t, t3 = t2 * t;
        const float g = gain * volume_ * fade_;
        for (int c = 0; c < 2; c++) {                     // cubic Hermite through win_[0..3], between [1] and [2]
            const float p0 = win_[0][c], p1 = win_[1][c], p2 = win_[2][c], p3 = win_[3][c];
            const float v = p1 + 0.5f * t * (p2 - p0) + t2 * (p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3) +
                            t3 * (1.5f * (p1 - p2) + 0.5f * (p3 - p0));
            const int s = out[i * 2 + c] + (int)lrintf(v * g * 32767.0f);
            out[i * 2 + c] = (int16_t)std::clamp(s, -32768, 32767);
        }
        if (fade_per_frame > 0) {
            fade_ -= fade_per_frame;
            if (fade_ <= 0) { paused_ = true; fade_ = 1.0f; fade_step_ = 0; return; }
        }
        phase_ += step;
        while (phase_ >= 1.0) {
            phase_ -= 1.0;
            for (int k = 0; k < 3; k++) { win_[k][0] = win_[k + 1][0]; win_[k][1] = win_[k + 1][1]; }
            if (!next_frame(win_[3][0], win_[3][1])) {
                win_[3][0] = win_[3][1] = 0;
                if (++ended >= 3) { cur_ = 0; return; }   // the last frames have played out
            }
        }
    }
}

std::string MdPlus::state() const {
    if (!cur_ && !unlocked_) return "";
    char buf[64];
    // (the window holds the three frames before pos_: resume from the first of them)
    snprintf(buf, sizeof(buf), "t%d,%lld,%d,%d", cur_, cur_ ? (long long)std::max<int64_t>(0, pos_ - 3) : 0LL,
             (loop_ ? 1 : 0) | (paused_ ? 2 : 0) | (unlocked_ ? 4 : 0), (int)lrintf(volume_ * 255));
    return buf;
}

void MdPlus::set_state(const std::string& s) {
    stop();
    int track = 0, flags = 0, vol = 255;
    long long at = 0;
    if (s.size() < 2 || s[0] != 't' || sscanf(s.c_str() + 1, "%d,%lld,%d,%d", &track, &at, &flags, &vol) < 3) return;
    unlocked_ = (flags & 4) != 0;   // (a state saved between the overlay's opening and its command)
    volume_ = std::clamp(vol, 0, 255) / 255.0f;
    if (!track || !play(track, false, at)) return;
    loop_ = (flags & 1) != 0;
    paused_ = (flags & 2) != 0;
}
