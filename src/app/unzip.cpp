#include "unzip.h"
#include "romfind.h"
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Inflate (after Mark Adler's puff.c: canonical Huffman decoding a bit at a time, which is plenty
// for a few megabytes of ROM)
// ---------------------------------------------------------------------------
namespace {
const int kMaxBits = 15;

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[288];
};

struct Inflater {
    const uint8_t* in;
    size_t n, pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool err = false;
    std::vector<uint8_t>& out;
    size_t limit;

    Inflater(const uint8_t* i, size_t len, std::vector<uint8_t>& o, size_t lim) : in(i), n(len), out(o), limit(lim) {}

    int bits(int need) {
        uint32_t val = bitbuf;
        while (bitcnt < need) {
            if (pos >= n) { err = true; return 0; }
            val |= (uint32_t)in[pos++] << bitcnt;
            bitcnt += 8;
        }
        bitbuf = val >> need;
        bitcnt -= need;
        return (int)(val & ((1u << need) - 1));
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; len++) {
            code |= bits(1);
            if (err) return -1;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    bool stored() {
        bitbuf = 0;
        bitcnt = 0;
        if (pos + 4 > n) return false;
        unsigned len = in[pos] | (in[pos + 1] << 8);
        unsigned nlen = in[pos + 2] | (in[pos + 3] << 8);
        pos += 4;
        if (len != (~nlen & 0xFFFF) || pos + len > n || out.size() + len > limit) return false;
        out.insert(out.end(), in + pos, in + pos + len);
        pos += len;
        return true;
    }

    bool codes(const Huffman& lencode, const Huffman& distcode) {
        static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                        8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                       7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lencode);
            if (symbol < 0) return false;
            if (symbol < 256) {
                if (out.size() >= limit) return false;
                out.push_back((uint8_t)symbol);
            } else if (symbol == 256) {
                return true;
            } else {
                symbol -= 257;
                if (symbol >= 29) return false;
                size_t len = lens[symbol] + bits(lext[symbol]);
                int d = decode(distcode);
                if (d < 0 || d >= 30) return false;
                size_t dist = dists[d] + bits(dext[d]);
                if (err || dist > out.size() || out.size() + len > limit) return false;
                size_t from = out.size() - dist;
                for (size_t k = 0; k < len; k++) out.push_back(out[from + k]);
            }
        }
    }
};

// Returns 0 for a complete code, > 0 for an incomplete one, < 0 for an over-subscribed one.
int construct(Huffman& h, const short* length, int n) {
    for (int len = 0; len <= kMaxBits; len++) h.count[len] = 0;
    for (int s = 0; s < n; s++) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int s = 0; s < n; s++)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = (short)s;
    return left;
}

bool fixed_block(Inflater& s) {
    static Huffman lencode, distcode;
    static bool built = false;
    if (!built) {
        short lengths[288];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        construct(lencode, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        construct(distcode, lengths, 30);
        built = true;
    }
    return s.codes(lencode, distcode);
}

bool dynamic_block(Inflater& s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[286 + 30];
    Huffman lencode, distcode;
    int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
    if (s.err || nlen > 286 || ndist > 30) return false;
    int index = 0;
    for (; index < ncode; index++) lengths[order[index]] = (short)s.bits(3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s.err || construct(lencode, lengths, 19) != 0) return false;
    index = 0;
    while (index < nlen + ndist) {
        int symbol = s.decode(lencode);
        if (symbol < 0) return false;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            short len = 0;
            if (symbol == 16) {
                if (index == 0) return false;
                len = lengths[index - 1];
                symbol = 3 + s.bits(2);
            } else if (symbol == 17) {
                symbol = 3 + s.bits(3);
            } else {
                symbol = 11 + s.bits(7);
            }
            if (s.err || index + symbol > nlen + ndist) return false;
            while (symbol--) lengths[index++] = len;
        }
    }
    if (lengths[256] == 0) return false;
    int e = construct(lencode, lengths, nlen);
    if (e < 0 || (e > 0 && nlen != lencode.count[0] + lencode.count[1])) return false;
    e = construct(distcode, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist != distcode.count[0] + distcode.count[1])) return false;
    return s.codes(lencode, distcode);
}

// ---------------------------------------------------------------------------
// LZMA (zip method 14, as 7-Zip can write it), after the LZMA specification's reference decoder: a range coder,
// adaptive bit probabilities, literals coded against the previous byte and matches against four recent distances
// ---------------------------------------------------------------------------
struct RangeDecoder {
    const uint8_t* in;
    size_t n, pos = 0;
    uint32_t range = 0xFFFFFFFFu, code = 0;
    bool err = false;

    RangeDecoder(const uint8_t* i, size_t len) : in(i), n(len) {}
    uint8_t byte() {
        if (pos >= n) { err = true; return 0; }
        return in[pos++];
    }
    bool init() {
        if (byte() != 0) return false;
        for (int i = 0; i < 4; i++) code = (code << 8) | byte();
        return !err && code != range;
    }
    void normalize() {
        if (range < (1u << 24)) { range <<= 8; code = (code << 8) | byte(); }
    }
    unsigned bit(uint16_t& p) {
        const uint32_t bound = (range >> 11) * p;
        unsigned b;
        if (code < bound) { p += (2048 - p) >> 5; range = bound; b = 0; }
        else { p -= p >> 5; code -= bound; range -= bound; b = 1; }
        normalize();
        return b;
    }
    uint32_t direct(int count) {
        uint32_t res = 0;
        while (count-- > 0) {
            range >>= 1;
            code -= range;
            const uint32_t t = 0u - (code >> 31);
            code += range & t;
            if (code == range) err = true;
            normalize();
            res = (res << 1) + (t + 1);
        }
        return res;
    }
    unsigned tree(uint16_t* p, int bits) {   // most significant bit first
        unsigned m = 1;
        for (int i = 0; i < bits; i++) m = (m << 1) + bit(p[m]);
        return m - (1u << bits);
    }
    unsigned tree_rev(uint16_t* p, int bits) {   // least significant bit first
        unsigned m = 1, sym = 0;
        for (int i = 0; i < bits; i++) {
            const unsigned b = bit(p[m]);
            m = (m << 1) + b;
            sym |= b << i;
        }
        return sym;
    }
};

struct LzmaLength {
    uint16_t choice = 1024, choice2 = 1024, low[16][8], mid[16][8], high[256];
    LzmaLength() {
        std::fill(&low[0][0], &low[0][0] + 16 * 8, (uint16_t)1024);
        std::fill(&mid[0][0], &mid[0][0] + 16 * 8, (uint16_t)1024);
        std::fill(high, high + 256, (uint16_t)1024);
    }
    unsigned decode(RangeDecoder& rc, unsigned pos_state) {
        if (!rc.bit(choice)) return rc.tree(low[pos_state], 3);
        if (!rc.bit(choice2)) return 8 + rc.tree(mid[pos_state], 3);
        return 16 + rc.tree(high, 8);
    }
};

// `in`: the zip entry's data (a 4-byte LZMA SDK version / properties size, the 5 property bytes, the stream).
bool lzma_zip(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expected) {
    out.clear();
    if (n < 9 || (in[2] | (in[3] << 8)) != 5 || in[4] >= 9 * 5 * 5) return false;
    const unsigned lc = in[4] % 9, lp = in[4] / 9 % 5, pb = in[4] / 45;
    out.reserve(expected);
    RangeDecoder rc(in + 9, n - 9);
    if (!rc.init()) return false;
    std::vector<uint16_t> lit((size_t)0x300 << (lc + lp), 1024);
    uint16_t is_match[12 << 4], is_rep[12], is_rep_g0[12], is_rep_g1[12], is_rep_g2[12], is_rep0_long[12 << 4];
    uint16_t pos_slot[4][64], pos_special[115], align[16];
    for (uint16_t* a : {is_match, is_rep0_long}) std::fill(a, a + (12 << 4), (uint16_t)1024);
    for (uint16_t* a : {is_rep, is_rep_g0, is_rep_g1, is_rep_g2}) std::fill(a, a + 12, (uint16_t)1024);
    std::fill(&pos_slot[0][0], &pos_slot[0][0] + 4 * 64, (uint16_t)1024);
    std::fill(pos_special, pos_special + 115, (uint16_t)1024);
    std::fill(align, align + 16, (uint16_t)1024);
    LzmaLength len_dec, rep_len_dec;
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned state = 0;
    // The size is known (the zip says it), so decoding stops there; an end marker after it isn't needed.
    while (out.size() < expected) {
        if (rc.err) return false;
        const unsigned pos_state = (unsigned)out.size() & ((1u << pb) - 1);
        if (!rc.bit(is_match[(state << 4) + pos_state])) {
            const unsigned prev = out.empty() ? 0 : out.back();
            uint16_t* probs = &lit[(size_t)0x300 * ((((unsigned)out.size() & ((1u << lp) - 1)) << lc) + (prev >> (8 - lc)))];
            unsigned sym = 1;
            if (state >= 7) {   // after a match: the byte at rep0 steers the first bits
                if (rep0 >= out.size()) return false;
                unsigned match = out[out.size() - rep0 - 1];
                do {
                    const unsigned mb = (match >> 7) & 1;
                    match <<= 1;
                    const unsigned b = rc.bit(probs[((1 + mb) << 8) + sym]);
                    sym = (sym << 1) | b;
                    if (mb != b) break;
                } while (sym < 0x100);
            }
            while (sym < 0x100) sym = (sym << 1) | rc.bit(probs[sym]);
            out.push_back((uint8_t)sym);
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        unsigned len;
        if (rc.bit(is_rep[state])) {
            if (out.empty()) return false;
            if (!rc.bit(is_rep_g0[state])) {
                if (!rc.bit(is_rep0_long[(state << 4) + pos_state])) {   // one byte from rep0
                    if (rep0 >= out.size()) return false;
                    state = state < 7 ? 9 : 11;
                    out.push_back(out[out.size() - rep0 - 1]);
                    continue;
                }
            } else {
                uint32_t dist;
                if (!rc.bit(is_rep_g1[state])) {
                    dist = rep1;
                } else {
                    if (!rc.bit(is_rep_g2[state])) dist = rep2;
                    else { dist = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = dist;
            }
            len = rep_len_dec.decode(rc, pos_state);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = len_dec.decode(rc, pos_state);
            state = state < 7 ? 7 : 10;
            const unsigned slot = rc.tree(pos_slot[std::min(len, 3u)], 6);
            if (slot < 4) {
                rep0 = slot;
            } else {
                const int direct = (int)(slot >> 1) - 1;
                rep0 = (2 | (slot & 1)) << direct;
                if (slot < 14) rep0 += rc.tree_rev(pos_special + rep0 - slot, direct);
                else rep0 += (rc.direct(direct - 4) << 4) + rc.tree_rev(align, 4);
            }
            if (rep0 == 0xFFFFFFFFu) break;   // the end marker
        }
        len += 2;
        if (rep0 >= out.size() || out.size() + len > expected) return false;
        const size_t from = out.size() - rep0 - 1;
        for (size_t k = 0; k < len; k++) out.push_back(out[from + k]);
    }
    return !rc.err && out.size() == expected;
}

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

const uint32_t kMaxEntry = 64u << 20;   // nothing a Mega Drive ROM needs is bigger
}  // namespace

bool inflate_raw(const uint8_t* in, size_t n, std::vector<uint8_t>& out, size_t expected) {
    out.clear();
    out.reserve(expected);
    Inflater s(in, n, out, expected);
    int last;
    do {
        last = s.bits(1);
        int type = s.bits(2);
        if (s.err) return false;
        bool ok = type == 0 ? s.stored() : type == 1 ? fixed_block(s) : type == 2 ? dynamic_block(s) : false;
        if (!ok || s.err) return false;
    } while (!last);
    return out.size() == expected;
}

// ---------------------------------------------------------------------------
// Zip archives
// ---------------------------------------------------------------------------
bool zip_list(const std::wstring& path, std::vector<ZipEntry>& out) {
    out.clear();
    struct Closer { FILE* f; ~Closer() { if (f) fclose(f); } } file{open_file(path, "rb")};
    FILE* f = file.f;
    if (!f) return false;
    const uint64_t fsize = file_size(f);
    if (fsize < 22) return false;
    // End of central directory record: within the last 64 KB + 22 bytes (it can be followed by a comment).
    const size_t tail = (size_t)std::min<uint64_t>(fsize, 65557);
    std::vector<uint8_t> t(tail);
    if (!file_seek(f, fsize - tail) || fread(t.data(), 1, tail, f) != tail) return false;
    size_t eocd = SIZE_MAX;
    for (size_t i = tail - 22 + 1; i-- > 0;)
        if (rd32(&t[i]) == 0x06054B50) { eocd = i; break; }
    if (eocd == SIZE_MAX) return false;
    const uint16_t count = rd16(&t[eocd + 10]);
    const uint32_t cd_size = rd32(&t[eocd + 12]), cd_off = rd32(&t[eocd + 16]);
    if (cd_off == 0xFFFFFFFF || (uint64_t)cd_off + cd_size > fsize || cd_size > (16u << 20)) return false;
    std::vector<uint8_t> cd(cd_size);
    if (!file_seek(f, cd_off) || (cd_size && fread(cd.data(), 1, cd_size, f) != cd_size)) return false;
    size_t p = 0;
    for (unsigned k = 0; k < count && p + 46 <= cd.size(); k++) {
        if (rd32(&cd[p]) != 0x02014B50) return false;
        ZipEntry e;
        e.flags = rd16(&cd[p + 8]);
        e.method = rd16(&cd[p + 10]);
        e.crc = rd32(&cd[p + 16]);
        e.comp_size = rd32(&cd[p + 20]);
        e.size = rd32(&cd[p + 24]);
        const uint16_t nlen = rd16(&cd[p + 28]), xlen = rd16(&cd[p + 30]), clen = rd16(&cd[p + 32]);
        e.local_offset = rd32(&cd[p + 42]);
        if (p + 46 + nlen > cd.size()) return false;
        e.name.assign((const char*)&cd[p + 46], nlen);
        p += 46 + (size_t)nlen + xlen + clen;
        if (!e.name.empty() && e.name.back() != '/') out.push_back(std::move(e));
    }
    return true;
}

bool zip_supported(const ZipEntry& e) {
    return !(e.flags & 1) && (e.method == 0 || e.method == 8 || e.method == 14);   // not encrypted; stored, deflate, LZMA
}

bool zip_read(const std::wstring& path, const ZipEntry& e, std::vector<uint8_t>& out) {
    out.clear();
    if (!zip_supported(e) || e.size > kMaxEntry || e.comp_size > kMaxEntry) return false;
    struct Closer { FILE* f; ~Closer() { if (f) fclose(f); } } file{open_file(path, "rb")};
    FILE* f = file.f;
    if (!f) return false;
    uint8_t lh[30];
    if (!file_seek(f, e.local_offset) || fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034B50) return false;
    if (!file_seek(f, (uint64_t)e.local_offset + 30 + rd16(&lh[26]) + rd16(&lh[28]))) return false;
    std::vector<uint8_t> comp(e.comp_size);
    if (e.comp_size && fread(comp.data(), 1, e.comp_size, f) != e.comp_size) return false;
    if (e.method == 0) {
        if (e.comp_size != e.size) return false;
        out = std::move(comp);
    } else if (e.method == 14 ? !lzma_zip(comp.data(), comp.size(), out, e.size)
                              : !inflate_raw(comp.data(), comp.size(), out, e.size)) {
        return false;
    }
    return crc32_of(out.data(), out.size()) == e.crc;
}
