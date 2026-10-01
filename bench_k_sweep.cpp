#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <climits>
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

//======================== LadderSort stats ====================================
struct LadderStats {
    long long k_final = 0;
    double k_over_n = 0.0;
    bool used_fallback = false;
};

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

//======================== K measurement only ==================================
static long long measure_k_only(const std::vector<int>& a) {
    if (a.empty()) return 0;

    std::vector<int> tops;
    tops.reserve(64);

    tops.push_back(a[0]);

    int last_idx = 0;

    for (int i = 1, n = (int)a.size(); i < n; ++i) {
        int x = a[i];
        int idx = hinted_lower_bound_lad(tops, x, last_idx);

        if (idx == (int)tops.size()) {
            tops.emplace_back(x);
        } else {
            tops[idx] = x;
        }

        last_idx = idx;
    }

    return (long long)tops.size();
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

//============================= LadderSort core =================================
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
        stats.k_over_n = 0.0;
        stats.used_fallback = false;
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
                stats.k_over_n = (double)stats.k_final / (double)a.size();
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
    stats.k_over_n = (double)stats.k_final / (double)a.size();
    stats.used_fallback = false;

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

namespace timsort {
inline void timsort(std::vector<int>& a) {
    gfx::timsort(a.begin(), a.end());
}
}

static inline uint64_t rng_next_u64(uint64_t& x) {
    x ^= x << 7;
    x ^= x >> 9;
    x *= 0x2545F4914F6CDD1DULL;
    return x;
}

static int pick_available_stream(
    const std::vector<size_t>& remaining,
    uint64_t& rng
) {
    const int K = (int)remaining.size();

    for (int tries = 0; tries < 32; ++tries) {
        int j = (int)(rng_next_u64(rng) % (uint64_t)K);
        if (remaining[j] > 0) return j;
    }

    for (int j = 0; j < K; ++j) {
        if (remaining[j] > 0) return j;
    }

    return -1;
}

// Randomized/sticky exact-K generator.
//
// Construction idea:
// 1. Emit one decreasing prefix of length K to force exactly K ladders.
// 2. Give each ladder a large separated numeric band.
// 3. Emit the remaining elements in randomized sticky bursts.
// 4. Each emitted value is guaranteed to be placed into its intended ladder by
//    the same leftmost-tail rule used by LadderSort.
// 5. This avoids the repeated long descending blocks that favor TimSort.
//
// For ladder j:
//   base[j] = (K - 1 - j) * stride
//   values for that ladder are base[j], base[j] + 1, base[j] + 2, ...
//
// Because stride > max ladder length, values from ladder j never cross the
// current band of ladder j-1, so greedy placement never creates more than K
// ladders and measured_K remains exactly K.
static std::vector<int> generate_controlled_k_randomized_exact(
    size_t N,
    int K,
    uint64_t seed = 0xC0FFEE123456789ULL
) {
    if (K <= 0) {
        throw std::runtime_error("K must be positive");
    }

    if (N < (size_t)K) {
        throw std::runtime_error("N must be at least K for exact-K generation");
    }

    std::vector<size_t> target_len(K);
    size_t q = N / (size_t)K;
    size_t r = N % (size_t)K;

    size_t max_len = 0;
    for (int j = 0; j < K; ++j) {
        target_len[j] = q + ((size_t)j < r ? 1 : 0);
        max_len = std::max(max_len, target_len[j]);
    }

    const int64_t stride = (int64_t)max_len + 1024;

    if ((int64_t)K * stride > (int64_t)std::numeric_limits<int>::max()) {
        throw std::runtime_error("controlled K generator exceeds int range");
    }

    std::vector<int64_t> base(K);
    for (int j = 0; j < K; ++j) {
        base[j] = (int64_t)(K - 1 - j) * stride;
    }

    std::vector<int> out;
    out.reserve(N);

    for (int j = 0; j < K; ++j) {
        out.push_back((int)base[j]);
    }

    std::vector<size_t> remaining(K);
    std::vector<size_t> next_offset(K, 1);

    size_t remaining_total = 0;
    for (int j = 0; j < K; ++j) {
        remaining[j] = target_len[j] - 1;
        remaining_total += remaining[j];
    }

    uint64_t rng = seed ? seed : 0xC0FFEE123456789ULL;
    int active = pick_available_stream(remaining, rng);

    while (remaining_total > 0) {
        if (active < 0 || remaining[active] == 0) {
            active = pick_available_stream(remaining, rng);
            if (active < 0) break;
        }

        size_t burst = 1 + (size_t)(rng_next_u64(rng) % 16ULL);

        while (burst-- > 0 && remaining_total > 0 && remaining[active] > 0) {
            int64_t value = base[active] + (int64_t)next_offset[active];

            if (value > (int64_t)std::numeric_limits<int>::max()) {
                throw std::runtime_error("generated value exceeds int range");
            }

            out.push_back((int)value);
            ++next_offset[active];
            --remaining[active];
            --remaining_total;
        }

        active = pick_available_stream(remaining, rng);
    }

    if (out.size() != N) {
        throw std::runtime_error("controlled K generator produced wrong N");
    }

    return out;
}

// Reference-only reverse-block exact-K generator retained for comparison.
static std::vector<int> generate_controlled_k_reverse_block_exact(size_t N, int K) {
    std::vector<int> out;
    out.reserve(N);

    std::vector<size_t> len(K);
    size_t q = N / K;
    size_t r = N % K;

    for (int s = 0; s < K; ++s) {
        len[s] = q + (s < (int)r ? 1 : 0);
    }

    std::vector<size_t> idx(K, 0);
    size_t produced = 0;

    while (produced < N) {
        for (int s = K - 1; s >= 0 && produced < N; --s) {
            if (idx[s] < len[s]) {
                int value = (int)(idx[s] * (size_t)K + (size_t)s);
                out.push_back(value);
                ++idx[s];
                ++produced;
            }
        }
    }

    return out;
}

static void validate_randomized_exact_k_generator() {
    const size_t test_n = 1'000'000;

    for (int K : {1, 2, 4, 8, 16, 32, 64, 128}) {
        auto a = generate_controlled_k_randomized_exact(
            test_n,
            K,
            0xC0FFEE123456789ULL + (uint64_t)K
        );

        long long measured = measure_k_only(a);

        std::cout << "randomized_exact target_K=" << K
                  << " measured_K=" << measured << "\n";

        if (measured != K) {
            throw std::runtime_error("randomized exact-K generator failed");
        }
    }

    std::cout << "Randomized exact-K generator validation passed.\n";
}

//======================== Controlled sweep schema ============================
static constexpr const char* kSweepOutPath = "results/raw/05_k_sweep_raw.csv";

static const std::vector<std::string> kSweepAlgorithms = {
    "LadderSort",
    "HybridLadderSort",
    "TimSort",
    "StdSort",
    "StableSort"
};

static constexpr const char* kSweepCsvHeader =
    "n,target_k,measured_k,seed,algo,variant,round,time_sec,k_over_n,used_fallback,sorted_ok,git_hash";

template<typename Fn>
static double time_one_run(
    const std::vector<int>& base,
    Fn fn,
    bool& sorted_ok
) {
    std::vector<int> v = base;

    auto t0 = std::chrono::steady_clock::now();
    fn(v);
    auto t1 = std::chrono::steady_clock::now();

    consume(v);
    sorted_ok = std::is_sorted(v.begin(), v.end());

    return std::chrono::duration<double>(t1 - t0).count();
}

static void write_k_sweep_row(std::ofstream& out,
                              size_t n,
                              int target_k,
                              long long measured_k,
                              uint64_t seed,
                              const std::string& algo,
                              const std::string& variant,
                              int round,
                              double time_sec,
                              double k_over_n,
                              bool used_fallback,
                              bool sorted_ok,
                              const std::string& git_hash) {
    out << n << ','
        << target_k << ','
        << measured_k << ','
        << seed << ','
        << algo << ','
        << variant << ','
        << round << ','
        << std::fixed << std::setprecision(12) << time_sec << ','
        << std::fixed << std::setprecision(12) << k_over_n << ','
        << (used_fallback ? 1 : 0) << ','
        << (sorted_ok ? 1 : 0) << ','
        << git_hash << '\n';
}

//=========================== Main ==============================================
int main(int argc, char** argv) {
    using namespace std;

    int rounds = 10;
    int warmups = 1;
    size_t n = 10'000'000ULL;
    std::string out_path = kSweepOutPath;
    std::string git_hash = "unknown";
    const uint64_t seed = 0;
    int only_k = -1;
    std::string only_algo = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            cout << "Usage:\n"
                 << "  " << argv[0]
                 << " --rounds 10 --warmups 1 --n 10000000 --out results/raw/05_k_sweep_raw.csv --git-hash HASH\n"
                 << "  Optional: --only-k K --only-algo ALGO (to run specific K and algo only)\n";
            return 0;
        } else if (arg == "--rounds" && i + 1 < argc) {
            rounds = std::stoi(argv[++i]);
        } else if (arg == "--warmups" && i + 1 < argc) {
            warmups = std::stoi(argv[++i]);
        } else if (arg == "--n" && i + 1 < argc) {
            n = std::stoull(argv[++i]);
        } else if ((arg == "--out" || arg == "--csv") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--git-hash" && i + 1 < argc) {
            git_hash = argv[++i];
        } else if (arg == "--only-k" && i + 1 < argc) {
            only_k = std::stoi(argv[++i]);
        } else if (arg == "--only-algo" && i + 1 < argc) {
            only_algo = argv[++i];
        } else {
            cerr << "Unknown or incomplete argument: " << arg << "\n";
            return 1;
        }
    }

    if (rounds <= 0) {
        cerr << "ERROR: --rounds must be positive\n";
        return 1;
    }

    if (warmups < 0) {
        cerr << "ERROR: --warmups cannot be negative\n";
        return 1;
    }

    const std::vector<int> target_ks = {1, 2, 4, 8, 16, 32, 64, 128};

    std::ofstream out(out_path, std::ios::trunc);
    if (!out) {
        cerr << "ERROR: cannot open output file: " << out_path << "\n";
        return 1;
    }

    out << kSweepCsvHeader << '\n';

    std::cout << "Phase 3 controlled k-sweep config:\n";
    std::cout << "n=" << n << "\n";
    std::cout << "rounds=" << rounds << "\n";
    std::cout << "warmups=" << warmups << "\n";
    std::cout << "out=" << out_path << "\n";
    std::cout << "git_hash=" << git_hash << "\n";

    validate_randomized_exact_k_generator();

    if (only_k >= 0 || !only_algo.empty()) {
        std::cout << "Running in filtered mode: only_k=" << only_k << " only_algo=" << only_algo << "\n";
    }

    for (int target_k : target_ks) {
        if (only_k >= 0 && target_k != only_k) {
            continue;
        }

        std::cout << "target_k=" << target_k << "\n";

        std::vector<int> base = generate_controlled_k_randomized_exact(n, target_k, 0xC0FFEE123456789ULL);
        long long measured_k = measure_k_only(base);
        double k_over_n = (double)measured_k / (double)n;

        for (const auto& algo : kSweepAlgorithms) {
            if (!only_algo.empty() && algo != only_algo) {
                continue;
            }

            const std::string variant = (algo == "HybridLadderSort") ? "hybrid" : "main";
            LadderStats st;

            for (int w = 0; w < warmups; ++w) {
                std::vector<int> warm = base;
                warm.push_back(-1);

                if (algo == "LadderSort") {
                    st = ladder_sort_into(warm, g_ladder_out_ws);
                    warm.swap(g_ladder_out_ws);
                } else if (algo == "HybridLadderSort") {
                    size_t hybrid_threshold = std::max<size_t>(1, (size_t)std::ceil(std::sqrt((double)n)));
                    st = hybrid_ladder_sort_into(warm, g_ladder_out_ws, hybrid_threshold);
                    warm.swap(g_ladder_out_ws);
                } else if (algo == "TimSort") {
                    timsort::timsort(warm);
                } else if (algo == "StdSort") {
                    std::sort(warm.begin(), warm.end());
                } else if (algo == "StableSort") {
                    std::stable_sort(warm.begin(), warm.end());
                } else {
                    throw std::runtime_error("unknown algorithm: " + algo);
                }

                consume(warm);
            }

            for (int r = 0; r < rounds; ++r) {
                bool ok = false;
                double sec = 0.0;

                if (algo == "LadderSort") {
                    sec = time_one_run(base, [&](std::vector<int>& v) {
                        st = ladder_sort_into(v, g_ladder_out_ws);
                        v.swap(g_ladder_out_ws);
                    }, ok);
                } else if (algo == "HybridLadderSort") {
                    size_t hybrid_threshold = std::max<size_t>(1, (size_t)std::ceil(std::sqrt((double)n)));
                    sec = time_one_run(base, [&](std::vector<int>& v) {
                        st = hybrid_ladder_sort_into(v, g_ladder_out_ws, hybrid_threshold);
                        v.swap(g_ladder_out_ws);
                    }, ok);
                } else if (algo == "TimSort") {
                    sec = time_one_run(base, [&](std::vector<int>& v) {
                        timsort::timsort(v);
                    }, ok);
                } else if (algo == "StdSort") {
                    sec = time_one_run(base, [&](std::vector<int>& v) {
                        std::sort(v.begin(), v.end());
                    }, ok);
                } else if (algo == "StableSort") {
                    sec = time_one_run(base, [&](std::vector<int>& v) {
                        std::stable_sort(v.begin(), v.end());
                    }, ok);
                } else {
                    throw std::runtime_error("unknown algorithm: " + algo);
                }

                write_k_sweep_row(
                    out,
                    n,
                    target_k,
                    measured_k,
                    seed,
                    algo,
                    variant,
                    r + 1,
                    sec,
                    k_over_n,
                    st.used_fallback,
                    ok,
                    git_hash
                );

                std::cout << "  algo=" << algo
                          << " round=" << (r + 1)
                          << " measured_k=" << measured_k
                          << " time_sec=" << std::fixed << std::setprecision(6) << sec
                          << " ok=" << ok
                          << "\n";
            }
        }
    }

    if (g_sink64 == 0xdeadbeefULL) std::cerr << "";
    return 0;
}
