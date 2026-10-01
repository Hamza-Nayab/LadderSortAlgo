#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <climits>
#include <deque>
#include <iterator>

#if __has_include(<gfx/timsort.hpp>)
#include <gfx/timsort.hpp>
#elif __has_include("third_party/cpp-TimSort/include/gfx/timsort.hpp")
#include "third_party/cpp-TimSort/include/gfx/timsort.hpp"
#elif __has_include("../third_party/cpp-TimSort/include/gfx/timsort.hpp")
#include "../third_party/cpp-TimSort/include/gfx/timsort.hpp"
#endif

#if __has_include("bench_csv.hpp")
#include "bench_csv.hpp"
#elif __has_include("include/bench_csv.hpp")
#include "include/bench_csv.hpp"
#endif

using std::size_t;

//--------------------------- DCE sink -----------------------------------------
static volatile uint64_t g_sink64 = 0;

template<typename Vec>
inline void consume(const Vec& v) {
    uint64_t s = 0;
    for (auto x : v) s += (uint64_t)x;
    g_sink64 ^= s;
}

//======================== LadderSort workspaces ===============================
static std::vector<std::vector<int>> g_lad_ws;
static std::vector<int>              g_tops_ws;
static std::vector<int>              g_ladder_out_ws;

//------------------------ Hinted search over flat tops ------------------------
inline int hinted_lower_bound_lad(const std::vector<int>& tops, int x, int hint) {
    int n = (int)tops.size();
    if (n == 0) return 0;

    int i = hint;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;

    if ((i == 0 || tops[i - 1] > x) && tops[i] <= x) return i;
    if (i + 1 < n && tops[i] > x && tops[i + 1] <= x) return i + 1;
    if (i > 0 && tops[i - 1] <= x && (i == 1 || tops[i - 2] > x)) return i - 1;

    if (tops[i] > x) {
        int last = i;
        int ofs = 1;

        while (i + ofs < n && tops[i + ofs] > x) {
            last = i + ofs;
            ofs = (ofs << 1) + 1;
        }

        int lo = last + 1;
        int hi = std::min(i + ofs, n - 1);

        while (lo <= hi) {
            int mid = lo + ((hi - lo) >> 1);
            if (tops[mid] > x) lo = mid + 1;
            else hi = mid - 1;
        }

        return lo;
    } else {
        int last = i;
        int ofs = 1;

        while (i - ofs >= 0 && tops[i - ofs] <= x) {
            last = i - ofs;
            ofs <<= 1;
        }

        int lo = std::max(0, i - ofs);
        int hi = last;

        while (lo <= hi) {
            int mid = lo + ((hi - lo) >> 1);
            if (tops[mid] > x) lo = mid + 1;
            else hi = mid - 1;
        }

        return lo;
    }
}

//======================== Merge primitives ====================================
static void merge_two_gallop(
    const std::vector<int>& A,
    const std::vector<int>& B,
    std::vector<int>& out
) {
    out.clear();
    out.reserve(A.size() + B.size());

    size_t i = 0;
    size_t j = 0;

    int winA = 0;
    int winB = 0;
    const int GALLOP = 8;

    auto gallop_right = [](const std::vector<int>& V, size_t lo, int key) {
        size_t n = V.size();
        size_t step = 1;
        size_t hi = lo;

        while (hi + step < n && V[hi + step] <= key) {
            step <<= 1;
        }

        size_t L = lo;
        size_t R = std::min(hi + step, n);

        while (L < R) {
            size_t m = L + ((R - L) >> 1);
            if (V[m] <= key) L = m + 1;
            else R = m;
        }

        return L;
    };

    while (i < A.size() && j < B.size()) {
        if (A[i] <= B[j]) {
            out.push_back(A[i++]);

            if (++winA >= GALLOP && j < B.size()) {
                size_t ni = gallop_right(A, i, B[j]);
                out.insert(out.end(), A.begin() + i, A.begin() + ni);
                i = ni;
                winA = winB = 0;
            }
        } else {
            out.push_back(B[j++]);

            if (++winB >= GALLOP && i < A.size()) {
                size_t step = 1;
                size_t n = B.size();
                size_t nj = j;

                while (nj + step < n && B[nj + step] < A[i]) {
                    step <<= 1;
                }

                size_t L = j;
                size_t R = std::min(nj + step, n);

                while (L < R) {
                    size_t m = L + ((R - L) >> 1);
                    if (B[m] < A[i]) L = m + 1;
                    else R = m;
                }

                out.insert(out.end(), B.begin() + j, B.begin() + L);
                j = L;
                winA = winB = 0;
            }
        }
    }

    if (i < A.size()) out.insert(out.end(), A.begin() + i, A.end());
    if (j < B.size()) out.insert(out.end(), B.begin() + j, B.end());
}

//=========================== Loser tree =======================================
struct LoserTree {
    int k;
    std::vector<int> tree;
    std::vector<int> key;
    std::vector<const int*> cur;
    std::vector<const int*> end;
    std::vector<char> alive;

    explicit LoserTree(const std::vector<std::vector<int>>& runs) {
        k = (int)runs.size();

        tree.assign(k, -1);
        key.resize(k);
        cur.resize(k);
        end.resize(k);
        alive.assign(k, 0);

        for (int i = 0; i < k; ++i) {
            cur[i] = runs[i].data();
            end[i] = runs[i].data() + runs[i].size();

            if (cur[i] < end[i]) {
                key[i] = *cur[i];
                alive[i] = 1;
            }
        }

        for (int i = 0; i < k; ++i) {
            if (alive[i]) adjust(i);
        }
    }

    inline bool less_eq(int a, int b) const {
        if (!alive[a]) return false;
        if (!alive[b]) return true;

        if (key[a] != key[b]) return key[a] < key[b];
        return a < b;
    }

    inline void adjust(int s) {
        int t = s;

        for (int parent = (s + k) >> 1; parent > 0; parent >>= 1) {
            int& los = tree[parent - 1];

            if (los < 0) {
                los = t;
            } else if (!less_eq(t, los)) {
                std::swap(t, los);
            }
        }

        tree[0] = t;
    }

    inline int pop_and_advance() {
        int s = tree[0];
        int v = key[s];

        if (++cur[s] < end[s]) {
            key[s] = *cur[s];
        } else {
            alive[s] = 0;
        }

        adjust(s);
        return v;
    }
};

static void merge_k_loser_tree(
    const std::vector<std::vector<int>>& runs,
    std::vector<int>& out
) {
    out.clear();

    size_t total = 0;
    for (const auto& r : runs) total += r.size();

    out.reserve(total);

    if (runs.empty()) return;

    if (runs.size() == 1) {
        out = runs[0];
        return;
    }

    LoserTree lt(runs);

    for (size_t t = 0; t < total; ++t) {
        out.push_back(lt.pop_and_advance());
    }
}

//============================= LadderSort =====================================
struct LadderStats {
    long long k_final = 0;
    bool used_fallback = false;
};

static LadderStats ladder_sort_into_core(
    const std::vector<int>& a,
    std::vector<int>& out,
    bool enable_hybrid,
    size_t hybrid_threshold
) {
    LadderStats stats;

    if (a.empty()) {
        out.clear();
        stats.k_final = 0;
        return stats;
    }

    auto& lad = g_lad_ws;
    auto& tops = g_tops_ws;

    lad.clear();
    tops.clear();

    lad.reserve(64);
    tops.reserve(64);

    lad.push_back({a[0]});
    tops.push_back(a[0]);

    int last_idx = 0;

    for (int i = 1, n = (int)a.size(); i < n; ++i) {
        int x = a[i];
        int idx = hinted_lower_bound_lad(tops, x, last_idx);

        if (idx == (int)lad.size()) {
            lad.emplace_back().emplace_back(x);
            tops.emplace_back(x);

            if (enable_hybrid && lad.size() > hybrid_threshold) {
                out = a;
                std::sort(out.begin(), out.end());

                stats.k_final = (long long)lad.size();
                stats.used_fallback = true;
                return stats;
            }
        } else {
            lad[idx].emplace_back(x);
            tops[idx] = x;
        }

        last_idx = idx;
    }

    stats.k_final = (long long)lad.size();

    if (lad.size() == 1) {
        out = lad[0];
        return stats;
    }

    if (lad.size() == 2) {
        merge_two_gallop(lad[0], lad[1], out);
        return stats;
    }

    merge_k_loser_tree(lad, out);
    return stats;
}

static LadderStats ladder_sort_into(const std::vector<int>& a, std::vector<int>& out) {
    return ladder_sort_into_core(a, out, false, 0);
}

static LadderStats hybrid_ladder_sort_into(
    const std::vector<int>& a,
    std::vector<int>& out,
    size_t threshold
) {
    return ladder_sort_into_core(a, out, true, threshold);
}

//=========================== Quicksort ========================================
static inline int median3(int a, int b, int c) {
    if (a < b) {
        if (b < c) return b;
        return (a < c) ? c : a;
    } else {
        if (a < c) return a;
        return (b < c) ? c : b;
    }
}

static inline int tukeys_ninther(const std::vector<int>& A, int l, int r) {
    int n = r - l + 1;
    int step = n / 8;

    int a1 = A[l];
    int a2 = A[l + step];
    int a3 = A[l + 2 * step];

    int b1 = A[l + n / 2 - step];
    int b2 = A[l + n / 2];
    int b3 = A[l + n / 2 + step];

    int c1 = A[r - 2 * step];
    int c2 = A[r - step];
    int c3 = A[r];

    int m1 = median3(a1, a2, a3);
    int m2 = median3(b1, b2, b3);
    int m3 = median3(c1, c2, c3);

    return median3(m1, m2, m3);
}

static inline void insertion_sort_range(std::vector<int>& a, int l, int r) {
    for (int i = l + 1; i <= r; ++i) {
        int x = a[i];
        int j = i - 1;

        while (j >= l && a[j] > x) {
            a[j + 1] = a[j];
            --j;
        }

        a[j + 1] = x;
    }
}

static void quicksort3(std::vector<int>& a) {
    if (a.empty()) return;

    const int SMALL = 32;

    struct Range {
        int l;
        int r;
    };

    std::vector<Range> st;
    st.reserve(64);
    st.push_back({0, (int)a.size() - 1});

    while (!st.empty()) {
        auto [l, r] = st.back();
        st.pop_back();

        while (l < r) {
            if (r - l + 1 <= SMALL) {
                insertion_sort_range(a, l, r);
                break;
            }

            int n = r - l + 1;

            int pivot = (n >= 128)
                ? tukeys_ninther(a, l, r)
                : median3(a[l], a[l + (n >> 1)], a[r]);

            int i = l;
            int j = r;
            int k = l;

            while (k <= j) {
                int v = a[k];

                if (v < pivot) {
                    std::swap(a[i++], a[k++]);
                } else if (v > pivot) {
                    std::swap(a[k], a[j--]);
                } else {
                    ++k;
                }
            }

            if (i - l < r - j) {
                if (i - 1 > l) st.push_back({l, i - 1});
                l = j + 1;
            } else {
                if (j + 1 < r) st.push_back({j + 1, r});
                r = i - 1;
            }
        }
    }
}

//=========================== Merge sort =======================================
static void mergesort_rec(std::vector<int>& a, std::vector<int>& buf, int l, int r) {
    if (r - l <= 32) {
        for (int i = l + 1; i <= r; ++i) {
            int x = a[i];
            int j = i - 1;

            while (j >= l && a[j] > x) {
                a[j + 1] = a[j];
                --j;
            }

            a[j + 1] = x;
        }

        return;
    }

    int m = l + (r - l) / 2;

    mergesort_rec(a, buf, l, m);
    mergesort_rec(a, buf, m + 1, r);

    int i = l;
    int j = m + 1;
    int k = l;

    while (i <= m && j <= r) {
        buf[k++] = (a[i] <= a[j]) ? a[i++] : a[j++];
    }

    while (i <= m) buf[k++] = a[i++];
    while (j <= r) buf[k++] = a[j++];

    std::copy(buf.begin() + l, buf.begin() + r + 1, a.begin() + l);
}

static void mergesort_with_buf(std::vector<int>& a, std::vector<int>& buf) {
    if (a.empty()) return;

    if ((int)buf.size() != (int)a.size()) {
        buf.assign(a.size(), 0);
    }

    mergesort_rec(a, buf, 0, (int)a.size() - 1);
}

//=========================== Timsort wrapper ==================================
namespace timsort {
inline void timsort(std::vector<int>& a) {
    gfx::timsort(a.begin(), a.end());
}
}

//=========================== PatienceSort Baseline ============================
// Classical patience sorting baseline (Aldous-Diaconis 1999):
// 1. Offline greedy decomposition into piles using standard unhinted binary search
// 2. Plain K-way merge using std::priority_queue (naive min-heap merge)
static std::vector<std::vector<int>> g_patience_piles_ws;
static std::vector<int>              g_patience_tops_ws;

struct PatiencePQNode {
    const int* cur;
    const int* end;
    bool operator>(const PatiencePQNode& other) const {
        return *cur > *other.cur;
    }
};

static void patience_sort(
    const std::vector<int>& a,
    std::vector<int>& out,
    long long& k_final
) {
    if (a.empty()) {
        out.clear();
        k_final = 0;
        return;
    }

    auto& piles = g_patience_piles_ws;
    auto& tops = g_patience_tops_ws;

    piles.clear();
    tops.clear();

    piles.reserve(64);
    tops.reserve(64);

    piles.push_back({a[0]});
    tops.push_back(a[0]);

    for (size_t i = 1; i < a.size(); ++i) {
        const int x = a[i];

        // Standard unhinted binary search over strictly decreasing tops vector:
        // tops[0] > tops[1] > ... > tops[K-1]
        // std::lower_bound with std::greater<int>() finds first pile j where tops[j] <= x
        auto it = std::lower_bound(tops.begin(), tops.end(), x, std::greater<int>());

        if (it == tops.end()) {
            piles.emplace_back().push_back(x);
            tops.push_back(x);
        } else {
            size_t idx = std::distance(tops.begin(), it);
            piles[idx].push_back(x);
            tops[idx] = x;
        }
    }

    k_final = static_cast<long long>(piles.size());

    out.clear();
    out.reserve(a.size());

    if (piles.size() == 1) {
        out = std::move(piles[0]);
        return;
    }

    std::priority_queue<PatiencePQNode, std::vector<PatiencePQNode>, std::greater<PatiencePQNode>> pq;
    for (const auto& p : piles) {
        if (!p.empty()) {
            pq.push(PatiencePQNode{p.data(), p.data() + p.size()});
        }
    }

    while (!pq.empty()) {
        PatiencePQNode node = pq.top();
        pq.pop();
        out.push_back(*node.cur);
        if (++node.cur < node.end) {
            pq.push(node);
        }
    }
}

//====================== EncroachingListsSort Baseline =========================
// Classical encroaching lists baseline (Skiena 1988):
// 1. Maintain dynamic collection of lists. For each incoming element x,
//    scan existing lists and place x at whichever end keeps that list monotonic:
//    try to append to the back of the first list whose back element is <= x,
//    else prepend to the front of the first list whose front element is >= x,
//    else start a new list. Plain double-ended greedy version, naive from literature.
// 2. Plain K-way merge using the same generic std::priority_queue-based merge.
struct EncroachingListEnds {
    int front;
    int back;
};

static std::vector<std::deque<int>>     g_encroaching_deques_ws;
static std::vector<EncroachingListEnds> g_encroaching_ends_ws;

static void encroaching_lists_sort(
    const std::vector<int>& a,
    std::vector<int>& out,
    long long& k_final
) {
    if (a.empty()) {
        out.clear();
        k_final = 0;
        return;
    }

    auto& deques = g_encroaching_deques_ws;
    auto& ends = g_encroaching_ends_ws;
    auto& piles = g_patience_piles_ws;

    deques.clear();
    ends.clear();
    piles.clear();

    deques.reserve(64);
    ends.reserve(64);

    // Initial list with a[0]
    deques.emplace_back();
    deques.back().push_back(a[0]);
    ends.push_back({a[0], a[0]});

    for (size_t i = 1; i < a.size(); ++i) {
        const int x = a[i];

        int target_list = -1;
        bool is_append = false;

        // Try to append to the back of the first list whose back element is <= x
        for (size_t j = 0; j < ends.size(); ++j) {
            if (ends[j].back <= x) {
                target_list = static_cast<int>(j);
                is_append = true;
                break;
            }
        }

        // Else prepend to the front of the first list whose front element is >= x
        if (target_list == -1) {
            for (size_t j = 0; j < ends.size(); ++j) {
                if (ends[j].front >= x) {
                    target_list = static_cast<int>(j);
                    is_append = false;
                    break;
                }
            }
        }

        // Else start a new list
        if (target_list == -1) {
            deques.emplace_back();
            deques.back().push_back(x);
            ends.push_back({x, x});
        } else if (is_append) {
            deques[target_list].push_back(x);
            ends[target_list].back = x;
        } else {
            deques[target_list].push_front(x);
            ends[target_list].front = x;
        }
    }

    k_final = static_cast<long long>(deques.size());

    out.clear();
    out.reserve(a.size());

    if (deques.size() == 1) {
        out.assign(deques[0].begin(), deques[0].end());
        return;
    }

    // Flatten deques to vector piles for the same generic priority_queue K-way merge
    piles.resize(deques.size());
    for (size_t j = 0; j < deques.size(); ++j) {
        piles[j].assign(deques[j].begin(), deques[j].end());
    }

    std::priority_queue<PatiencePQNode, std::vector<PatiencePQNode>, std::greater<PatiencePQNode>> pq;
    for (const auto& p : piles) {
        if (!p.empty()) {
            pq.push(PatiencePQNode{p.data(), p.data() + p.size()});
        }
    }

    while (!pq.empty()) {
        PatiencePQNode node = pq.top();
        pq.pop();
        out.push_back(*node.cur);
        if (++node.cur < node.end) {
            pq.push(node);
        }
    }
}

//=========================== Dataset generators ===============================
static std::vector<int> generate_dataset(
    const std::string& dataset,
    size_t N,
    uint64_t seed
) {
    if (dataset == "random") {
        std::vector<int> out;
        out.reserve(N);

        uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;

        auto rnd = [&]() -> uint64_t {
            x ^= x << 7;
            x ^= x >> 9;
            x *= 0x2545F4914F6CDD1DULL;
            return x;
        };

        for (size_t i = 0; i < N; ++i) {
            out.push_back((int)(rnd() & 0x7fffffffULL));
        }

        return out;
    }

    if (dataset == "ascending") {
        std::vector<int> out(N);

        for (size_t i = 0; i < N; ++i) {
            out[i] = (int)i;
        }

        return out;
    }

    if (dataset == "descending") {
        std::vector<int> out(N);

        for (size_t i = 0; i < N; ++i) {
            out[i] = (int)(N - 1 - i);
        }

        return out;
    }

    if (dataset == "band_limited") {
        const size_t W = 32;
        const size_t BLOCK = W + 1;

        std::vector<int> out(N);

        for (size_t i = 0; i < N; ++i) {
            out[i] = (int)(i + 1);
        }

        uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;

        auto rnd = [&]() -> uint64_t {
            x ^= x << 7;
            x ^= x >> 9;
            x *= 0x2545F4914F6CDD1DULL;
            return x;
        };

        size_t phase = (BLOCK > 1) ? (size_t)(rnd() % BLOCK) : 0;

        auto shuffle_block = [&](size_t b, size_t e) {
            for (size_t i = e; i > b + 1; ) {
                --i;
                size_t j = b + (size_t)(rnd() % (i - b + 1));
                std::swap(out[i], out[j]);
            }
        };

        if (phase && phase < N) {
            shuffle_block(0, phase);
        }

        for (size_t b = phase; b < N; b += BLOCK) {
            size_t e = std::min(N, b + BLOCK);
            shuffle_block(b, e);
        }

        return out;
    }

    if (dataset == "block_cyclic") {
        const int K = 8;
        const size_t B = 4;

        std::vector<int> out;
        out.reserve(N);

        std::vector<size_t> cur(K);
        std::vector<size_t> stop(K);

        size_t q = N / K;
        size_t r = N % K;
        size_t acc = 0;

        for (int k = 0; k < K; ++k) {
            size_t len = q + (k < (int)r ? 1 : 0);
            cur[k] = acc + 1;
            stop[k] = acc + len;
            acc += len;
        }

        size_t produced = 0;
        int k = 0;

        while (produced < N) {
            if (cur[k] <= stop[k]) {
                size_t take = std::min(B, stop[k] - cur[k] + 1);

                for (size_t t = 0; t < take; ++t) {
                    out.push_back((int)cur[k]++);
                }

                produced += take;
            }

            k = (k + 1) % K;
        }

        return out;
    }

    if (dataset == "two_run_riffle") {
        size_t L = N / 2;
        size_t R = N - L;

        std::vector<int> out;
        out.reserve(N);

        uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;

        auto u01 = [&]() {
            x ^= x << 7;
            x ^= x >> 9;
            x *= 0x2545F4914F6CDD1ULL;

            return (double)(((x >> 11) & ((1ull << 53) - 1)) *
                            (1.0 / (1ull << 53)));
        };

        size_t i = 0;
        size_t j = 0;

        while (i < L || j < R) {
            if (i == L) {
                out.push_back((int)(L + j));
                ++j;
            } else if (j == R) {
                out.push_back((int)i);
                ++i;
            } else if (u01() < 0.5) {
                out.push_back((int)i++);
            } else {
                out.push_back((int)(L + j++));
            }
        }

        return out;
    }

    if (dataset == "social_feed") {
        const int K = 24;
        const int BURST_MAX = 24;
        const int STEP_MAX = 2;
        const int P_SAME = 192;

        std::vector<int> out;
        out.reserve(N);

        std::vector<size_t> need(K);

        size_t q = N / K;
        size_t r = N % K;

        for (int k = 0; k < K; ++k) {
            need[k] = q + (k < (int)r ? 1 : 0);
        }

        std::vector<int> t(K, 0);
        std::vector<size_t> em(K, 0);

        uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;

        auto rnd = [&]() -> uint64_t {
            x ^= x << 7;
            x ^= x >> 9;
            x *= 0x2545F4914F6CDD1DULL;
            return x;
        };

        auto has_more = [&](int k) {
            return em[k] < need[k];
        };

        int f = (int)(rnd() % K);

        while (out.size() < N) {
            if (!has_more(f)) {
                int tries = 0;

                while (tries < K && !has_more(f)) {
                    f = (f + 1) % K;
                    ++tries;
                }

                if (tries == K) break;
            }

            int burst = 1 + (int)(rnd() % BURST_MAX);

            while (burst-- > 0 && out.size() < N && has_more(f)) {
                if (em[f] > 0) {
                    if ((rnd() & 0xFF) >= (uint64_t)P_SAME) {
                        t[f] += 1 + (int)(rnd() % STEP_MAX);
                    }
                }

                out.push_back(t[f]);
                ++em[f];
            }

            uint64_t u = rnd() & 0xFF;

            if (u < 153) {
                // stay
            } else if (u < 204) {
                f = (f + 1) % K;
            } else {
                f = (f + K - 1) % K;
            }
        }

        return out;
    }

    if (dataset == "partial_index") {
        const int K = 16;
        const double overlap = 0.5;
        const size_t U = std::max<size_t>(1, (size_t)(N * overlap));
        const int BURST_MAX = 32;
        const int STAY_PCT = 60;
        const int RIGHT_PCT = 20;

        std::vector<size_t> need(K);

        size_t q = N / K;
        size_t r = N % K;

        for (int k = 0; k < K; ++k) {
            need[k] = q + (k < (int)r ? 1 : 0);
        }

        uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;

        auto rnd = [&]() -> uint64_t {
            x ^= x << 7;
            x ^= x >> 9;
            x *= 0x2545F4914F6CDD1DULL;
            return x;
        };

        std::vector<std::vector<int>> runs(K);

        for (int k = 0; k < K; ++k) {
            runs[k].reserve(need[k]);

            size_t left = need[k];
            size_t pos = (size_t)(rnd() % std::max<size_t>(1, U / K));

            while (left--) {
                uint64_t rv = rnd();
                size_t gap = 1 + (size_t)(rv % 4);
                pos += gap;

                if (pos >= U) {
                    pos = (size_t)(rnd() % (U / 2 + 1));
                }

                runs[k].push_back((int)pos);
            }
        }

        std::vector<int> out;
        out.reserve(N);

        std::vector<size_t> cur(K, 0);

        auto has_more = [&](int s) {
            return cur[s] < runs[s].size();
        };

        int s = (int)(rnd() % K);

        while (out.size() < N) {
            if (!has_more(s)) {
                int tries = 0;

                while (tries < K && !has_more(s)) {
                    s = (s + 1) % K;
                    ++tries;
                }

                if (tries == K) break;
            }

            int burst = 1 + (int)(rnd() % BURST_MAX);

            while (burst-- > 0 && out.size() < N && has_more(s)) {
                out.push_back(runs[s][cur[s]++]);
            }

            int toss = (int)(rnd() % 100);

            if (toss < STAY_PCT) {
                // stay
            } else if (toss < STAY_PCT + RIGHT_PCT) {
                s = (s + 1) % K;
            } else {
                s = (s + K - 1) % K;
            }
        }

        return out;
    }

    throw std::runtime_error("unknown dataset: " + dataset);
}

//=========================== CLI ==============================================
struct CliConfig {
    std::string dataset = "random";
    std::string algo = "laddersort_raw";
    std::string out = "results/raw/01_main_runtime_raw.csv";
    std::string seeds_path = "results/seeds.txt";
    std::string git_hash = "unknown";

    size_t n = 1000000;
    int rounds = 10;
    int warmups = 1;

    size_t hybrid_threshold = 0;
};

static void print_usage(const char* prog) {
    std::cout
        << "Usage:\n"
        << "  " << prog
        << " --dataset NAME --n N --algo NAME --rounds R --warmups W"
        << " --seeds PATH --out PATH --git-hash HASH\n\n"
        << "Optional:\n"
        << "  --hybrid-threshold T   If 0, uses ceil(sqrt(N))\n\n"
        << "Datasets:\n"
        << "  random ascending descending band_limited block_cyclic\n"
        << "  two_run_riffle social_feed partial_index\n\n"
        << "Algorithms:\n"
        << "  laddersort_raw laddersort_hybrid timsort std_sort\n"
        << "  std_stable_sort quicksort mergesort patiencesort encroaching_lists_sort\n";
}

static std::vector<uint64_t> load_seeds(const std::string& path) {
    std::ifstream in(path);

    if (!in) {
        throw std::runtime_error("cannot open seeds file: " + path);
    }

    std::vector<uint64_t> seeds;
    uint64_t s;

    while (in >> s) {
        seeds.push_back(s);
    }

    if (seeds.empty()) {
        throw std::runtime_error("seeds file is empty: " + path);
    }

    return seeds;
}

static size_t default_hybrid_threshold(size_t n) {
    return std::max<size_t>(1, (size_t)std::ceil(std::sqrt((double)n)));
}

//----------------------------------- Main -------------------------------------
int main(int argc, char** argv) {
    using namespace std;

    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    cout << std::unitbuf;

    CliConfig cfg;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--dataset" && i + 1 < argc) {
            cfg.dataset = argv[++i];
        } else if (arg == "--n" && i + 1 < argc) {
            cfg.n = std::stoull(argv[++i]);
        } else if (arg == "--algo" && i + 1 < argc) {
            cfg.algo = argv[++i];
        } else if (arg == "--rounds" && i + 1 < argc) {
            cfg.rounds = std::stoi(argv[++i]);
        } else if (arg == "--warmups" && i + 1 < argc) {
            cfg.warmups = std::stoi(argv[++i]);
        } else if (arg == "--seeds" && i + 1 < argc) {
            cfg.seeds_path = argv[++i];
        } else if ((arg == "--out" || arg == "--csv") && i + 1 < argc) {
            cfg.out = argv[++i];
        } else if (arg == "--git-hash" && i + 1 < argc) {
            cfg.git_hash = argv[++i];
        } else if (arg == "--hybrid-threshold" && i + 1 < argc) {
            cfg.hybrid_threshold = std::stoull(argv[++i]);
        } else {
            cerr << "Unknown or incomplete argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.rounds <= 0) {
        cerr << "ERROR: --rounds must be positive\n";
        return 1;
    }

    if (cfg.warmups < 0) {
        cerr << "ERROR: --warmups cannot be negative\n";
        return 1;
    }

    if (cfg.hybrid_threshold == 0) {
        cfg.hybrid_threshold = default_hybrid_threshold(cfg.n);
    }

    vector<uint64_t> seeds;

    try {
        seeds = load_seeds(cfg.seeds_path);
    } catch (const std::exception& e) {
        cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    if ((int)seeds.size() < cfg.rounds) {
        cerr << "ERROR: seeds file has fewer seeds than rounds\n";
        return 1;
    }

    cout << "Phase 2 config:\n";
    cout << "dataset=" << cfg.dataset << "\n";
    cout << "n=" << cfg.n << "\n";
    cout << "algo=" << cfg.algo << "\n";
    cout << "rounds=" << cfg.rounds << "\n";
    cout << "warmups=" << cfg.warmups << "\n";
    cout << "seeds=" << cfg.seeds_path << "\n";
    cout << "out=" << cfg.out << "\n";
    cout << "git_hash=" << cfg.git_hash << "\n";
    cout << "hybrid_threshold=" << cfg.hybrid_threshold << "\n";

    static std::vector<int> g_merge_buf;

    std::vector<double> times;
    times.reserve(cfg.rounds);

    bool all_ok = true;

    for (int r = -cfg.warmups; r < cfg.rounds; ++r) {
        const bool is_warmup = (r < 0);
        const int round_index = r + 1;

        uint64_t seed = seeds[is_warmup ? 0 : r];

        std::vector<int> base;

        try {
            base = generate_dataset(cfg.dataset, cfg.n, seed);
        } catch (const std::exception& e) {
            cerr << "ERROR: " << e.what() << "\n";
            return 1;
        }

        std::vector<int> v = base;

        long long k_final = -1;

        auto run_sort = [&]() {
            if (cfg.algo == "laddersort_raw") {
                LadderStats st = ladder_sort_into(v, g_ladder_out_ws);
                v.swap(g_ladder_out_ws);
                k_final = st.k_final;
            } else if (cfg.algo == "laddersort_hybrid") {
                LadderStats st = hybrid_ladder_sort_into(
                    v,
                    g_ladder_out_ws,
                    cfg.hybrid_threshold
                );

                v.swap(g_ladder_out_ws);
                k_final = st.k_final;
            } else if (cfg.algo == "timsort") {
                timsort::timsort(v);
            } else if (cfg.algo == "std_sort") {
                std::sort(v.begin(), v.end());
            } else if (cfg.algo == "std_stable_sort") {
                std::stable_sort(v.begin(), v.end());
            } else if (cfg.algo == "quicksort") {
                quicksort3(v);
            } else if (cfg.algo == "mergesort") {
                mergesort_with_buf(v, g_merge_buf);
            } else if (cfg.algo == "patiencesort" || cfg.algo == "PatienceSort") {
                patience_sort(v, g_ladder_out_ws, k_final);
                v.swap(g_ladder_out_ws);
            } else if (cfg.algo == "encroaching_lists_sort" || cfg.algo == "EncroachingListsSort" || cfg.algo == "encroachingsort") {
                encroaching_lists_sort(v, g_ladder_out_ws, k_final);
                v.swap(g_ladder_out_ws);
            } else {
                throw std::runtime_error("unknown algorithm: " + cfg.algo);
            }
        };

        auto t0 = std::chrono::steady_clock::now();

        try {
            run_sort();
        } catch (const std::exception& e) {
            cerr << "ERROR: " << e.what() << "\n";
            return 1;
        }

        auto t1 = std::chrono::steady_clock::now();

        consume(v);

        double dt = std::chrono::duration<double>(t1 - t0).count();
        bool ok = std::is_sorted(v.begin(), v.end()) && (v.size() == base.size());
        if (ok) {
            uint64_t sum_base = 0, sum_v = 0;
            uint64_t xor_base = 0, xor_v = 0;
            for (size_t i = 0; i < base.size(); ++i) {
                sum_base += (uint64_t)base[i];
                sum_v    += (uint64_t)v[i];
                xor_base ^= ((uint64_t)base[i] + 0x9e3779b97f4a7c15ULL);
                xor_v    ^= ((uint64_t)v[i] + 0x9e3779b97f4a7c15ULL);
            }
            if (sum_base != sum_v || xor_base != xor_v) {
                ok = false;
            }
        }

        all_ok = all_ok && ok;

        if (is_warmup) {
            cout << "warmup time=" << fixed << setprecision(6)
                 << dt << " ok=" << ok << "\n";
            continue;
        }

        times.push_back(dt);

        CsvRow row;
        row.dataset = cfg.dataset;
        row.n = (long long)cfg.n;
        row.seed = seed;
        row.algo = cfg.algo;
        row.variant = "main";
        row.round = round_index;
        row.time_sec = dt;
        row.k_final = k_final;
        row.comparisons = -1;
        row.moves = -1;
        row.peak_rss_bytes = -1;
        row.sorted_ok = ok ? 1 : 0;
        row.git_hash = cfg.git_hash;

        csv_append_row(cfg.out, row);

        cout << "round=" << round_index
             << " time=" << fixed << setprecision(6) << dt
             << " ok=" << ok
             << " k_final=" << k_final
             << "\n";
    }

    double sum = std::accumulate(times.begin(), times.end(), 0.0);
    double mean = sum / times.size();

    double acc = 0.0;

    for (double t : times) {
        double d = t - mean;
        acc += d * d;
    }

    double stddev = std::sqrt(acc / times.size());

    cout << "\nResult:\n";
    cout << cfg.algo
         << " avg: " << fixed << setprecision(6) << mean
         << " s  (+/-" << stddev << ")"
         << (all_ok ? "" : "  (! not sorted)")
         << "\n";

    if (g_sink64 == 0xdeadbeefULL) std::cerr << "";

    return all_ok ? 0 : 2;
}