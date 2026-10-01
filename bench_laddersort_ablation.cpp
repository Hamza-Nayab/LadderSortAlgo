#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <fstream>

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

//--------------------------- DCE sink (avoid optimizer) -----------------------
static volatile uint64_t g_sink64 = 0;
template<typename Vec>
inline void consume(const Vec& v) {
    uint64_t s = 0;
    for (auto x : v) s += (uint64_t)x;
    g_sink64 ^= s;
}

//=========================== Item types =======================================
struct PlainItem {
    int key;
};

struct StableItem {
    int key;
    uint32_t pos;
};

template<bool Stable>
using ItemT = std::conditional_t<Stable, StableItem, PlainItem>;

template<bool Stable>
inline ItemT<Stable> make_item(int key, uint32_t pos) {
    if constexpr (Stable) return StableItem{key, pos};
    else return PlainItem{key};
}

template<bool Stable>
inline int get_key(const ItemT<Stable>& x) {
    return x.key;
}

template<bool Stable>
inline bool less_item(const ItemT<Stable>& a, const ItemT<Stable>& b) {
    if (a.key != b.key) return a.key < b.key;
    if constexpr (Stable) return a.pos < b.pos;
    else return false;
}

template<bool Stable>
inline bool less_eq_item(const ItemT<Stable>& a, const ItemT<Stable>& b) {
    if (a.key != b.key) return a.key < b.key;
    if constexpr (Stable) return a.pos <= b.pos;
    else return true;
}

//=========================== Config ===========================================
struct LadderConfig {
    bool use_hint = true;
    bool use_gallop = true;
    bool use_loser_tree = true;
    bool stable = false;
    int gallop_threshold = 8;
};

//=========================== Workspaces =======================================
template<typename Item>
struct LadderWorkspace {
    static std::vector<std::vector<Item>> ladders;
    static std::vector<int> tops;
};

template<typename Item>
std::vector<std::vector<Item>> LadderWorkspace<Item>::ladders;

template<typename Item>
std::vector<int> LadderWorkspace<Item>::tops;

static std::vector<int> g_ladder_out_ws;

//=========================== Search ===========================================
inline int plain_lower_bound_nonincreasing(const std::vector<int>& tops, int x) {
    int lo = 0, hi = (int)tops.size();
    while (lo < hi) {
        int mid = lo + ((hi - lo) >> 1);
        if (tops[mid] > x) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

inline int hinted_lower_bound_nonincreasing(const std::vector<int>& tops, int x, int hint) {
    int n = (int)tops.size();
    if (n == 0) return 0;

    int i = hint;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;

    if ((i == 0 || tops[i - 1] > x) && tops[i] <= x) return i;
    if (i + 1 < n && tops[i] > x && tops[i + 1] <= x) return i + 1;
    if (i > 0 && tops[i - 1] <= x && (i == 1 || tops[i - 2] > x)) return i - 1;

    if (tops[i] > x) {
        int last = i, ofs = 1;
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
        int last = i, ofs = 1;
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

//=========================== Merge: plain 2-way ===============================
template<bool Stable>
static void merge_two_plain(const std::vector<ItemT<Stable>>& A,
                            const std::vector<ItemT<Stable>>& B,
                            std::vector<int>& out) {
    out.clear();
    out.reserve(A.size() + B.size());

    size_t i = 0, j = 0;
    while (i < A.size() && j < B.size()) {
        if (less_eq_item<Stable>(A[i], B[j])) out.push_back(get_key<Stable>(A[i++]));
        else out.push_back(get_key<Stable>(B[j++]));
    }
    while (i < A.size()) out.push_back(get_key<Stable>(A[i++]));
    while (j < B.size()) out.push_back(get_key<Stable>(B[j++]));
}

//=========================== Merge: galloping 2-way ===========================
template<bool Stable>
static void merge_two_gallop(const std::vector<ItemT<Stable>>& A,
                             const std::vector<ItemT<Stable>>& B,
                             std::vector<int>& out,
                             int GALLOP) {
    out.clear();
    out.reserve(A.size() + B.size());

    size_t i = 0, j = 0;
    int winA = 0, winB = 0;

    auto gallop_right = [&](const std::vector<ItemT<Stable>>& V, size_t lo, const ItemT<Stable>& key) {
        size_t n = V.size();
        size_t step = 1, hi = lo;
        while (hi + step < n && less_eq_item<Stable>(V[hi + step], key)) step <<= 1;
        size_t L = lo, R = std::min(hi + step, n);
        while (L < R) {
            size_t m = L + ((R - L) >> 1);
            if (less_eq_item<Stable>(V[m], key)) L = m + 1;
            else R = m;
        }
        return L;
    };

    while (i < A.size() && j < B.size()) {
        if (less_eq_item<Stable>(A[i], B[j])) {
            out.push_back(get_key<Stable>(A[i++]));
            if (++winA >= GALLOP && j < B.size()) {
                size_t ni = gallop_right(A, i, B[j]);
                for (; i < ni; ++i) out.push_back(get_key<Stable>(A[i]));
                winA = winB = 0;
            }
        } else {
            out.push_back(get_key<Stable>(B[j++]));
            if (++winB >= GALLOP && i < A.size()) {
                size_t step = 1, n = B.size();
                size_t nj = j;
                while (nj + step < n && less_item<Stable>(B[nj + step], A[i])) step <<= 1;
                size_t L = j, R = std::min(nj + step, n);
                while (L < R) {
                    size_t m = L + ((R - L) >> 1);
                    if (less_item<Stable>(B[m], A[i])) L = m + 1;
                    else R = m;
                }
                for (; j < L; ++j) out.push_back(get_key<Stable>(B[j]));
                winA = winB = 0;
            }
        }
    }

    while (i < A.size()) out.push_back(get_key<Stable>(A[i++]));
    while (j < B.size()) out.push_back(get_key<Stable>(B[j++]));
}

//=========================== Merge: loser tree ================================
template<bool Stable>
struct LoserTree {
    int k;
    std::vector<int> tree;
    std::vector<ItemT<Stable>> key;
    std::vector<const ItemT<Stable>*> cur, end;
    std::vector<char> alive;

    explicit LoserTree(const std::vector<std::vector<ItemT<Stable>>>& runs) {
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
        for (int i = 0; i < k; ++i) if (alive[i]) adjust(i);
    }

    inline bool less_eq_run(int a, int b) const {
        if (!alive[a]) return false;
        if (!alive[b]) return true;
        return less_eq_item<Stable>(key[a], key[b]);
    }

    inline void adjust(int s) {
        int t = s;
        for (int parent = (s + k) >> 1; parent > 0; parent >>= 1) {
            int& los = tree[parent - 1];
            if (los < 0) {
                los = t;
            } else if (!less_eq_run(t, los)) {
                std::swap(t, los);
            }
        }
        tree[0] = t;
    }

    inline int pop_and_advance() {
        int s = tree[0];
        int v = get_key<Stable>(key[s]);
        if (++cur[s] < end[s]) key[s] = *cur[s];
        else alive[s] = 0;
        adjust(s);
        return v;
    }
};

template<bool Stable>
static void merge_k_loser_tree(const std::vector<std::vector<ItemT<Stable>>>& runs,
                               std::vector<int>& out) {
    out.clear();
    size_t total = 0;
    for (const auto& r : runs) total += r.size();
    out.reserve(total);

    if (runs.empty()) return;
    if (runs.size() == 1) {
        for (const auto& x : runs[0]) out.push_back(get_key<Stable>(x));
        return;
    }

    LoserTree<Stable> lt(runs);
    for (size_t t = 0; t < total; ++t) out.push_back(lt.pop_and_advance());
}

template<bool Stable>
static void merge_k_heap(const std::vector<std::vector<ItemT<Stable>>>& runs,
                         std::vector<int>& out) {
    out.clear();
    size_t total = 0;
    for (const auto& r : runs) total += r.size();
    out.reserve(total);

    struct Node {
        ItemT<Stable> item;
        int run_id;
        size_t idx;
    };

    struct Cmp {
        bool operator()(const Node& a, const Node& b) const {
            if (less_item<Stable>(a.item, b.item)) return false;
            if (less_item<Stable>(b.item, a.item)) return true;
            return false;
        }
    };

    std::priority_queue<Node, std::vector<Node>, Cmp> pq;
    for (int r = 0; r < (int)runs.size(); ++r) {
        if (!runs[r].empty()) pq.push(Node{runs[r][0], r, 0});
    }

    while (!pq.empty()) {
        Node cur = pq.top();
        pq.pop();
        out.push_back(get_key<Stable>(cur.item));

        size_t next_idx = cur.idx + 1;
        if (next_idx < runs[cur.run_id].size()) {
            pq.push(Node{runs[cur.run_id][next_idx], cur.run_id, next_idx});
        }
    }
}

//=========================== LadderSort core ==================================
template<bool Stable>
static void ladder_sort_into_cfg(const std::vector<int>& a,
                                 std::vector<int>& out,
                                 const LadderConfig& cfg) {
    if (a.empty()) {
        out.clear();
        return;
    }

    auto& ladders = LadderWorkspace<ItemT<Stable>>::ladders;
    auto& tops    = LadderWorkspace<ItemT<Stable>>::tops;

    ladders.clear();
    tops.clear();
    ladders.reserve(64);
    tops.reserve(64);

    ladders.push_back({make_item<Stable>(a[0], 0)});
    tops.push_back(a[0]);

    int last_idx = 0;

    for (int i = 1, n = (int)a.size(); i < n; ++i) {
        int x = a[i];
        int idx = cfg.use_hint
            ? hinted_lower_bound_nonincreasing(tops, x, last_idx)
            : plain_lower_bound_nonincreasing(tops, x);

        if (idx == (int)ladders.size()) {
            ladders.emplace_back().emplace_back(make_item<Stable>(x, (uint32_t)i));
            tops.emplace_back(x);
        } else {
            ladders[idx].emplace_back(make_item<Stable>(x, (uint32_t)i));
            tops[idx] = x;
        }

        last_idx = idx;
    }

    if (ladders.size() == 1) {
        out.clear();
        out.reserve(ladders[0].size());
        for (const auto& x : ladders[0]) out.push_back(get_key<Stable>(x));
        return;
    }

    if (ladders.size() == 2) {
        if (cfg.use_gallop) merge_two_gallop<Stable>(ladders[0], ladders[1], out, cfg.gallop_threshold);
        else merge_two_plain<Stable>(ladders[0], ladders[1], out);
        return;
    }

    if (cfg.use_loser_tree) merge_k_loser_tree<Stable>(ladders, out);
    else merge_k_heap<Stable>(ladders, out);
}

static void ladder_sort_into(const std::vector<int>& a,
                             std::vector<int>& out,
                             const LadderConfig& cfg) {
    if (cfg.stable) ladder_sort_into_cfg<true>(a, out, cfg);
    else ladder_sort_into_cfg<false>(a, out, cfg);
}

//=========================== Baselines ========================================
static inline int median3(int a, int b, int c) {
    if (a < b) { if (b < c) return b; return (a < c) ? c : a; }
    else { if (a < c) return a; return (b < c) ? c : b; }
}

static inline int tukeys_ninther(const std::vector<int>& A, int l, int r) {
    int n = r - l + 1;
    int step = n / 8;
    int a1 = A[l],               a2 = A[l + step],          a3 = A[l + 2 * step];
    int b1 = A[l + n / 2 - step], b2 = A[l + n / 2],        b3 = A[l + n / 2 + step];
    int c1 = A[r - 2 * step],    c2 = A[r - step],          c3 = A[r];
    int m1 = median3(a1, a2, a3);
    int m2 = median3(b1, b2, b3);
    int m3 = median3(c1, c2, c3);
    return median3(m1, m2, m3);
}

static inline void insertion_sort_range(std::vector<int>& a, int l, int r) {
    for (int i = l + 1; i <= r; ++i) {
        int x = a[i], j = i - 1;
        while (j >= l && a[j] > x) { a[j + 1] = a[j]; --j; }
        a[j + 1] = x;
    }
}

static void quicksort3(std::vector<int>& a) {
    const int SMALL = 32;
    struct Range { int l, r; };
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
            int pivot = (n >= 128) ? tukeys_ninther(a, l, r)
                                   : median3(a[l], a[l + (n >> 1)], a[r]);

            int i = l, j = r, k = l;
            while (k <= j) {
                int v = a[k];
                if (v < pivot) std::swap(a[i++], a[k++]);
                else if (v > pivot) std::swap(a[k], a[j--]);
                else ++k;
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

namespace timsort {
inline void timsort(std::vector<int>& a) { gfx::timsort(a.begin(), a.end()); }
}

//=========================== Dataset ==========================================
static std::vector<int> generate_dataset(size_t N, uint64_t seed) {
    const int K = 24;
    const int BURST_MAX = 24;
    const int STEP_MAX  = 2;
    const int P_SAME    = 192;

    std::vector<int> out;
    out.reserve(N);

    std::vector<size_t> need(K);
    size_t q = N / K, r = N % K;
    for (int k = 0; k < K; ++k) need[k] = q + (k < (int)r ? 1 : 0);

    std::vector<int> t(K, 0);
    std::vector<size_t> em(K, 0);

    uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;
    auto rnd = [&]() -> uint64_t {
        x ^= x << 7;
        x ^= x >> 9;
        x *= 0x2545F4914F6CDD1DULL;
        return x;
    };

    auto has_more = [&](int k) { return em[k] < need[k]; };

    int f = (int)(rnd() % K);
    while (out.size() < N) {
        if (!has_more(f)) {
            int tries = 0;
            while (tries < K && !has_more(f)) { f = (f + 1) % K; ++tries; }
            if (tries == K) break;
        }

        int burst = 1 + (int)(rnd() % BURST_MAX);
        while (burst-- > 0 && out.size() < N && has_more(f)) {
            if (em[f] > 0) {
                if ((rnd() & 0xFF) >= (uint64_t)P_SAME) t[f] += 1 + (int)(rnd() % STEP_MAX);
            }
            out.push_back(t[f]);
            ++em[f];
        }

        uint64_t u = rnd() & 0xFF;
        if      (u < 153) { }
        else if (u < 204) f = (f + 1) % K;
        else              f = (f + K - 1) % K;
    }

    return out;
}

//=========================== Benchmark harness =================================
struct Result {
    std::string name;
    double avg_seconds = 0.0;
    double std_seconds = 0.0;
    bool ok = true;
};

template<typename Fn>
static Result bench_algo_postinsert_csv(const std::string& algo_name,
                                        const std::string& variant_name,
                                        const std::vector<int>& base,
                                        int rounds,
                                        size_t n,
                                        uint64_t seed,
                                        const std::string& dataset_name,
                                        const std::string& csv_path,
                                        const std::string& git_hash,
                                        Fn fn) {
    Result res;
    res.name = algo_name;

    {
        std::vector<int> v = base;
        v.push_back(-1);
        fn(v);
        consume(v);
    }

    std::vector<double> times;
    times.reserve(rounds);

    for (int r = 0; r < rounds; ++r) {
        std::vector<int> v = base;
        v.push_back(-1);

        auto t0 = std::chrono::steady_clock::now();
        fn(v);
        auto t1 = std::chrono::steady_clock::now();

        consume(v);

        double dt = std::chrono::duration<double>(t1 - t0).count();
        bool ok = std::is_sorted(v.begin(), v.end());
        times.push_back(dt);
        res.ok = res.ok && ok;

        CsvRow row;
        row.dataset = dataset_name;
        row.n = (long long)n;
        row.seed = seed;
        row.algo = algo_name;
        row.variant = variant_name;
        row.round = r + 1;
        row.time_sec = dt;
        row.k_final = -1;
        row.comparisons = -1;
        row.moves = -1;
        row.peak_rss_bytes = -1;
        row.sorted_ok = ok ? 1 : 0;
        row.git_hash = git_hash;
        csv_append_row(csv_path, row);
    }

    double sum = std::accumulate(times.begin(), times.end(), 0.0);
    double mean = sum / times.size();
    double acc = 0.0;
    for (double t : times) {
        double d = t - mean;
        acc += d * d;
    }

    res.avg_seconds = mean;
    res.std_seconds = std::sqrt(acc / times.size());
    return res;
}

static void print_result(const Result& r) {
    std::cout << std::left << std::setw(14) << r.name
              << " avg: " << std::fixed << std::setprecision(6) << r.avg_seconds
              << " s  (±" << std::setprecision(6) << r.std_seconds << ")"
              << (r.ok ? "" : "  (! not sorted)") << "\n";
}

//----------------------------------- Main -------------------------------------
struct BenchCase { size_t n; int rounds; };

int main(int argc, char** argv) {
    using namespace std;

    string csv_path = "results/raw/ablation.csv";
    string git_hash = "unknown";
    string dataset_name = "social_feed";
    uint64_t seed = 0xC0FFEEULL;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--csv" && i + 1 < argc) csv_path = argv[++i];
        else if (arg == "--git-hash" && i + 1 < argc) git_hash = argv[++i];
        else if (arg == "--dataset" && i + 1 < argc) dataset_name = argv[++i];
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoull(argv[++i]);
    }

    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    cout << std::unitbuf;

    const vector<BenchCase> cases = {
        {1'000'000ULL,   10},
        {10'000'000ULL,  10},
        {100'000'000ULL, 10}
    };

    const LadderConfig cfg_full      {true,  true,  true,  false, 8};
    const LadderConfig cfg_no_hint   {false, true,  true,  false, 8};
    const LadderConfig cfg_no_gallop {true,  false, true,  false, 8};
    const LadderConfig cfg_heap      {true,  true,  false, false, 8};
    const LadderConfig cfg_stable    {true,  true,  true,  true,  8};

    for (const auto& C : cases) {
        const size_t n = C.n;
        const int rounds = C.rounds;

        std::vector<int> base = generate_dataset(n, seed);

        std::cout << "\n=== Ablation benchmark, n=" << n
                  << ", rounds=" << rounds << " ===\n\n";

        auto r_full = bench_algo_postinsert_csv("LadderSort", "LadderFull", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            ladder_sort_into(v, g_ladder_out_ws, cfg_full);
            v.swap(g_ladder_out_ws);
        });

        auto r_no_hint = bench_algo_postinsert_csv("LadderSort", "NoHint", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            ladder_sort_into(v, g_ladder_out_ws, cfg_no_hint);
            v.swap(g_ladder_out_ws);
        });

        auto r_no_gallop = bench_algo_postinsert_csv("LadderSort", "NoGallop", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            ladder_sort_into(v, g_ladder_out_ws, cfg_no_gallop);
            v.swap(g_ladder_out_ws);
        });

        auto r_heap = bench_algo_postinsert_csv("LadderSort", "HeapMerge", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            ladder_sort_into(v, g_ladder_out_ws, cfg_heap);
            v.swap(g_ladder_out_ws);
        });

        auto r_stable = bench_algo_postinsert_csv("LadderSort", "StableMode", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            ladder_sort_into(v, g_ladder_out_ws, cfg_stable);
            v.swap(g_ladder_out_ws);
        });

        auto r_tims = bench_algo_postinsert_csv("TimSort", "Baseline", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            timsort::timsort(v);
        });

        auto r_intro = bench_algo_postinsert_csv("StdSort", "Baseline", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            std::sort(v.begin(), v.end());
        });

        auto r_stdstable = bench_algo_postinsert_csv("StdStableSort", "Baseline", base, rounds, n, seed, dataset_name, csv_path, git_hash, [&](std::vector<int>& v) {
            std::stable_sort(v.begin(), v.end());
        });

        std::cout << "Results:\n";
        print_result(r_full);
        print_result(r_no_hint);
        print_result(r_no_gallop);
        print_result(r_heap);
        print_result(r_stable);
        print_result(r_tims);
        print_result(r_intro);
        print_result(r_stdstable);
    }

    if (g_sink64 == 0xdeadbeefULL) std::cerr << "";
    return 0;
}