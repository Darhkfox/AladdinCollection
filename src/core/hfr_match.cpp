// High frame rate: matching the sprites of two frames (see hfr_match.h).
#include "hfr_match.h"
#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

void hfr_match(const HfrSprite* cur, const HfrSprite* pv, const HfrSprite* pv2, int n, int ywrap, int16_t* odx,
               int16_t* ody) {
    constexpr int kMatch = 20, kMax = 128;
    if (n > kMax) n = kMax;
    auto dyOf = [ywrap](int y, int py) {
        int d = y - py;
        if (ywrap) d = ((d % ywrap) + ywrap + ywrap / 2) % ywrap - ywrap / 2;
        return d;
    };
    int nearD[kMax], nearP[kMax];
    bool matched[kMax], taken[kMax], exact[kMax];
    for (int i = 0; i < n; i++) {
        odx[i] = ody[i] = 0;
        matched[i] = taken[i] = exact[i] = false;
    }
    // Last frame's sprite of its kind nearest its place (within half its width).
    for (int i = 0; i < n; i++) {
        nearD[i] = 1 << 30;
        nearP[i] = -1;
        if (!cur[i].on) continue;
        const int lim = cur[i].w / 2;
        for (int p = 0; p < n; p++) {
            if (!pv[p].on || pv[p].look != cur[i].look) continue;
            const int dx = cur[i].x - pv[p].x, dy = dyOf(cur[i].y, pv[p].y);
            if (std::abs(dx) >= lim || std::abs(dy) >= lim) continue;
            const int d = std::abs(dx) + std::abs(dy);
            if (d < nearD[i]) nearD[i] = d, nearP[i] = p;
        }
    }
    // The same look: pairs nearest first, each sprite once.
    struct Pair { int d, i, p; };
    thread_local std::vector<Pair> pairs;   // (launcher previews run on their own threads)
    pairs.clear();
    for (int i = 0; i < n; i++) {
        if (!cur[i].on) continue;
        for (int p = 0; p < n; p++) {
            if (!pv[p].on || pv[p].key != cur[i].key) continue;
            const int dx = cur[i].x - pv[p].x, dy = dyOf(cur[i].y, pv[p].y);
            if (std::abs(dx) > kMatch || std::abs(dy) > kMatch) continue;
            const int d = std::abs(dx) + std::abs(dy);
            if (nearP[i] >= 0 && d > nearD[i] + 2) continue;   // (something was right here: animated)
            pairs.push_back({d * 2 + (p != i), i, p});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.d < b.d; });
    for (const Pair& pr : pairs) {
        if (matched[pr.i] || taken[pr.p]) continue;
        odx[pr.i] = (int16_t)(cur[pr.i].x - pv[pr.p].x);
        ody[pr.i] = (int16_t)dyOf(cur[pr.i].y, pv[pr.p].y);
        matched[pr.i] = taken[pr.p] = exact[pr.i] = true;
    }
    // Animated in place.
    for (int i = 0; i < n; i++) {
        if (!cur[i].on || matched[i] || nearP[i] < 0) continue;
        odx[i] = (int16_t)(cur[i].x - pv[nearP[i]].x);
        ody[i] = (int16_t)dyOf(cur[i].y, pv[nearP[i]].y);
        matched[i] = true;
    }
    for (int i = 0; i < n; i++) {
        if (!cur[i].on || matched[i]) continue;
        // A matched neighbour of its kind.
        int best = -1, bestD = 16 + 1;
        for (int j = 0; j < n; j++) {
            if (!cur[j].on || !matched[j] || cur[j].look != cur[i].look) continue;
            const int d = std::max(std::abs(cur[j].x - cur[i].x), std::abs(dyOf(cur[j].y, cur[i].y)));
            if (d < bestD) bestD = d, best = j;
        }
        if (best >= 0) {
            odx[i] = odx[best];
            ody[i] = ody[best];
            continue;
        }
        // Not drawn near here last frame (flickered): the frame before's, half the way.
        bool drawnNear = false;
        for (int p = 0; p < n && !drawnNear; p++)
            drawnNear = pv[p].on && pv[p].look == cur[i].look && std::abs(cur[i].x - pv[p].x) <= kMatch &&
                        std::abs(dyOf(cur[i].y, pv[p].y)) <= kMatch;
        if (drawnNear) continue;
        best = -1;
        bestD = 1 << 30;
        for (int p = 0; p < n; p++) {
            if (!pv2[p].on || pv2[p].key != cur[i].key) continue;
            const int dx = cur[i].x - pv2[p].x, dy = dyOf(cur[i].y, pv2[p].y);
            if (std::abs(dx) > kMatch || std::abs(dy) > kMatch) continue;
            const int d = (std::abs(dx) + std::abs(dy)) * 2 + (p != i);
            if (d < bestD) bestD = d, best = p;
        }
        if (best >= 0) {
            odx[i] = (int16_t)((cur[i].x - pv2[best].x) / 2);
            ody[i] = (int16_t)(dyOf(cur[i].y, pv2[best].y) / 2);
        }
    }
    // One character moves as one. Touching sprites of a group are its pieces; each offers every move that puts a
    // sprite of exactly its look last frame where it came from, and a move clearly most of the pieces offer is the
    // character's, for the pieces whose own move was a guess. A piece matched by its exact look keeps its move, and so
    // does one with a sprite of its look where its guess says it came from (touching pieces can be things moving
    // apart).
    int group[kMax];
    for (int i = 0; i < n; i++) group[i] = i;
    auto root = [&](int i) {
        while (group[i] != i) i = group[i] = group[group[i]];
        return i;
    };
    for (int i = 0; i < n; i++) {
        if (!cur[i].on) continue;
        for (int j = i + 1; j < n; j++) {
            if (!cur[j].on || cur[j].group != cur[i].group) continue;
            const int dy = dyOf(cur[j].y, cur[i].y);
            if (cur[j].x > cur[i].x + cur[i].w || cur[i].x > cur[j].x + cur[j].w || dy > cur[i].h || -dy > cur[j].h) continue;
            group[root(j)] = root(i);
        }
    }
    for (int i = 0; i < n; i++) {
        if (!cur[i].on || exact[i]) continue;
        for (int p = 0; p < n && !exact[i]; p++)
            exact[i] = pv[p].on && pv[p].key == cur[i].key && cur[i].x - pv[p].x == odx[i] &&
                       dyOf(cur[i].y, pv[p].y) == ody[i];
    }
    thread_local std::vector<std::pair<int, int>> offers;   // (move, piece)
    for (int r = 0; r < n; r++) {
        if (!cur[r].on || root(r) != r) continue;
        offers.clear();
        int pieces = 0;
        for (int i = 0; i < n; i++) {
            if (!cur[i].on || root(i) != r) continue;
            pieces++;
            for (int p = 0; p < n; p++) {
                if (!pv[p].on || pv[p].key != cur[i].key) continue;
                const int dx = cur[i].x - pv[p].x, dy = dyOf(cur[i].y, pv[p].y);
                if (std::abs(dx) > kMatch || std::abs(dy) > kMatch) continue;
                offers.push_back({(dx + 64) << 8 | (dy + 64), i});
            }
        }
        if (pieces < 3 || offers.empty()) continue;
        std::sort(offers.begin(), offers.end());
        offers.erase(std::unique(offers.begin(), offers.end()), offers.end());   // (a piece offers a move once)
        int best = -1, bestN = 0, second = 0;
        for (size_t a = 0; a < offers.size();) {
            size_t b = a;
            while (b < offers.size() && offers[b].first == offers[a].first) b++;
            const int c = (int)(b - a);
            if (c > bestN) second = bestN, bestN = c, best = offers[a].first;
            else if (c > second) second = c;
            a = b;
        }
        if (bestN < 3 || bestN * 3 < pieces * 2 || bestN == second) continue;
        const int mdx = (best >> 8) - 64, mdy = (best & 255) - 64;
        for (int i = 0; i < n; i++)
            if (cur[i].on && !exact[i] && root(i) == r) odx[i] = (int16_t)mdx, ody[i] = (int16_t)mdy;
    }
}
