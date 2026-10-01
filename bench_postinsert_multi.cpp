#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
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

//--------------------------- DCE sink (avoid optimizer) -----------------------
static volatile uint64_t g_sink64 = 0;
template<typename Vec>
inline void consume(const Vec& v) {
    uint64_t s = 0; for (auto x : v) s += (uint64_t)x;
    g_sink64 ^= s;
}

//======================== LadderSort workspaces (reused) ======================
static std::vector<std::vector<int>> g_lad_ws;
static std::vector<int>              g_tops_ws;
static std::vector<int>              g_ladder_out_ws;

//------------------------ Hinted search over flat `tops` ----------------------
inline int hinted_lower_bound_lad(const std::vector<int>& tops, int x, int hint) {
    int n = (int)tops.size();
    if (n == 0) return 0;

    int i = hint;
    if (i < 0)   i = 0;
    if (i >= n)  i = n - 1;

    if ((i == 0 || tops[i - 1] > x) && tops[i] <= x) return i;
    if (i + 1 < n && tops[i] > x && tops[i + 1] <= x) return i + 1;
    if (i > 0   && tops[i - 1] <= x && (i == 1 || tops[i - 2] > x)) return i - 1;

    if (tops[i] > x) {
        int last = i, ofs = 1;
        while (i + ofs < n && tops[i + ofs] > x) { last = i + ofs; ofs = (ofs << 1) + 1; }
        int lo = last + 1;
        int hi = std::min(i + ofs, n - 1);
        while (lo <= hi) {
            int mid = lo + ((hi - lo) >> 1);
            if (tops[mid] > x) lo = mid + 1; else hi = mid - 1;
        }
        return lo;
    } else {
        int last = i, ofs = 1;
        while (i - ofs >= 0 && tops[i - ofs] <= x) { last = i - ofs; ofs <<= 1; }
        int lo = std::max(0, i - ofs);
        int hi = last;
        while (lo <= hi) {
            int mid = lo + ((hi - lo) >> 1);
            if (tops[mid] > x) lo = mid + 1; else hi = mid - 1;
        }
        return lo;
    }
}

//======================== Merge primitives ====================================
static void merge_two_gallop(const std::vector<int>& A, const std::vector<int>& B, std::vector<int>& out) {
    out.clear();
    out.reserve(A.size() + B.size());
    size_t i = 0, j = 0;
    int winA = 0, winB = 0, GALLOP = 8;

    auto gallop_right = [](const std::vector<int>& V, size_t lo, int key) {
        size_t n = V.size();
        size_t step = 1, hi = lo;
        while (hi + step < n && V[hi + step] <= key) step <<= 1;
        size_t L = lo, R = std::min(hi + step, n);
        while (L < R) { size_t m = (L + R) / 2; if (V[m] <= key) L = m + 1; else R = m; }
        return L;
    };

    while (i < A.size() && j < B.size()) {
        if (A[i] <= B[j]) {
            out.push_back(A[i++]);
            if (++winA >= GALLOP) {
                size_t ni = gallop_right(A, i, B[j]);
                out.insert(out.end(), A.begin()+i, A.begin()+ni);
                i = ni; winA = winB = 0;
            }
        } else {
            out.push_back(B[j++]);
            if (++winB >= GALLOP) {
                size_t step = 1, n = B.size();
                size_t nj = j;
                while (nj + step < n && B[nj + step] < A[i]) step <<= 1;
                size_t L = j, R = std::min(nj + step, n);
                while (L < R) { size_t m = (L + R) / 2; if (B[m] < A[i]) L = m + 1; else R = m; }
                out.insert(out.end(), B.begin()+j, B.begin()+L);
                j = L; winA = winB = 0;
            }
        }
    }
    if (i < A.size()) out.insert(out.end(), A.begin()+i, A.end());
    if (j < B.size()) out.insert(out.end(), B.begin()+j, B.end());
}

//=========================== Loser-tree =======================================
struct LoserTree {
    int k;
    std::vector<int> tree;
    std::vector<int> key;
    std::vector<const int*> cur, end;
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
        for (int i = 0; i < k; ++i) if (alive[i]) adjust(i);
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
            int &los = tree[parent - 1];
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

static void merge_k_loser_tree(const std::vector<std::vector<int>>& runs, std::vector<int>& out) {
    out.clear();
    size_t total = 0; for (const auto& r : runs) total += r.size();
    out.reserve(total);
    if (runs.empty()) return;
    if (runs.size() == 1) { out = runs[0]; return; }

    LoserTree lt(runs);
    for (size_t t = 0; t < total; ++t) out.push_back(lt.pop_and_advance());
}

//============================= LadderSort =====================================
static void ladder_sort_into(const std::vector<int>& a, std::vector<int>& out) {
    if (a.empty()) { out.clear(); return; }

    auto& lad  = g_lad_ws;   lad.clear();  lad.reserve(64);
    auto& tops = g_tops_ws;  tops.clear(); tops.reserve(64);

    lad.push_back({a[0]});
    tops.push_back(a[0]);

    int last_idx = 0;
    for (int i = 1, n = (int)a.size(); i < n; ++i) {
        int x = a[i];
        int idx = hinted_lower_bound_lad(tops, x, last_idx);
        if (idx == (int)lad.size()) {
            lad.emplace_back().emplace_back(x);
            tops.emplace_back(x);
        } else {
            lad[idx].emplace_back(x);
            tops[idx] = x;
        }
        last_idx = idx;
    }

    if (lad.size() == 1) { out = lad[0]; return; }
    if (lad.size() == 2) { merge_two_gallop(lad[0], lad[1], out); return; }
    merge_k_loser_tree(lad, out);
}

//=========================== Quicksort ========================================
static inline int median3(int a, int b, int c) {
    if (a < b) { if (b < c) return b; return (a < c) ? c : a; }
    else { if (a < c) return a; return (b < c) ? c : b; }
}

static inline int tukeys_ninther(const std::vector<int>& A, int l, int r) {
    int n = r - l + 1;
    int step = n / 8;
    int a1 = A[l],               a2 = A[l + step],          a3 = A[l + 2*step];
    int b1 = A[l + n/2 - step],  b2 = A[l + n/2],           b3 = A[l + n/2 + step];
    int c1 = A[r - 2*step],      c2 = A[r - step],          c3 = A[r];
    int m1 = median3(a1,a2,a3);
    int m2 = median3(b1,b2,b3);
    int m3 = median3(c1,c2,c3);
    return median3(m1,m2,m3);
}

static inline void insertion_sort_range(std::vector<int>& a, int l, int r) {
    for (int i = l + 1; i <= r; ++i) {
        int x = a[i], j = i - 1;
        while (j >= l && a[j] > x) { a[j+1] = a[j]; --j; }
        a[j+1] = x;
    }
}

static void quicksort3(std::vector<int>& a) {
    const int SMALL = 32;
    struct Range { int l, r; };
    std::vector<Range> st; st.reserve(64);
    st.push_back({0, (int)a.size()-1});
    while (!st.empty()) {
        auto [l,r] = st.back(); st.pop_back();
        while (l < r) {
            if (r - l + 1 <= SMALL) { insertion_sort_range(a, l, r); break; }

            int n = r - l + 1;
            int pivot = (n >= 128) ? tukeys_ninther(a, l, r)
                                   : median3(a[l], a[l + (n>>1)], a[r]);

            int i = l, j = r, k = l;
            while (k <= j) {
                int v = a[k];
                if (v < pivot) std::swap(a[i++], a[k++]);
                else if (v > pivot) std::swap(a[k], a[j--]);
                else ++k;
            }
            if (i - l < r - j) { if (i-1 > l) st.push_back({l, i-1}); l = j+1; }
            else               { if (j+1 < r) st.push_back({j+1, r}); r = i-1; }
        }
    }
}

//=========================== Merge sort =======================================
static void mergesort_rec(std::vector<int>& a, std::vector<int>& buf, int l, int r) {
    if (r - l <= 32) {
        for (int i = l + 1; i <= r; ++i) {
            int x = a[i], j = i - 1;
            while (j >= l && a[j] > x) { a[j + 1] = a[j]; --j; }
            a[j + 1] = x;
        }
        return;
    }
    int m = l + (r - l) / 2;
    mergesort_rec(a, buf, l, m);
    mergesort_rec(a, buf, m + 1, r);

    int i = l, j = m + 1, k = l;
    while (i <= m && j <= r) buf[k++] = (a[i] <= a[j]) ? a[i++] : a[j++];
    while (i <= m) buf[k++] = a[i++];
    while (j <= r) buf[k++] = a[j++];

    std::copy(buf.begin() + l, buf.begin() + r + 1, a.begin() + l);
}

static void mergesort_with_buf(std::vector<int>& a, std::vector<int>& buf) {
    if (a.empty()) return;
    if ((int)buf.size() != (int)a.size()) buf.assign(a.size(), 0);
    mergesort_rec(a, buf, 0, (int)a.size() - 1);
}

//=========================== Timsort wrapper ==================================
namespace timsort {
inline void timsort(std::vector<int>& a) { gfx::timsort(a.begin(), a.end()); }
}

//=========================== Dataset ==========================================
static std::vector<int> generate_dataset(size_t N, uint64_t seed) {
    const size_t W = 32;
    const size_t BLOCK = W + 1;
    std::vector<int> out(N);
    for (size_t i = 0; i < N; ++i) out[i] = (int)(i + 1);
    uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;
    auto rnd = [&]() -> uint64_t { x ^= x << 7;  x ^= x >> 9;  x *= 0x2545F4914F6CDD1DULL; return x; };
    size_t phase = (BLOCK > 1) ? (size_t)(rnd() % BLOCK) : 0;
    auto shuffle_block = [&](size_t b, size_t e) {
        for (size_t i = e; i > b + 1; ) {
            --i;
            size_t j = b + (size_t)(rnd() % (i - b + 1));
            std::swap(out[i], out[j]);
        }
    };
    if (phase && phase < N) shuffle_block(0, phase);
    for (size_t b = phase; b < N; b += BLOCK) {
        size_t e = std::min(N, b + BLOCK);
        shuffle_block(b, e);
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
        (void)fn(v);
        consume(v);
    }

    std::vector<double> times; 
    times.reserve(rounds);

    for (int r = 0; r < rounds; ++r) {
        std::vector<int> v = base;
        v.push_back(-1);

        auto t0 = std::chrono::steady_clock::now();
        long long k_final = fn(v);
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
        row.k_final = k_final;
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
        acc += d*d; 
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
struct CliConfig {
    std::string dataset = "random";
    std::string algo = "laddersort_raw";
    std::string out = "results/raw/01_main_runtime_raw.csv";
    std::string seeds_path = "results/seeds.txt";
    std::string git_hash = "unknown";
    size_t n = 1000000;
    int rounds = 1;
    int warmups = 1;
};

static void print_usage(const char* prog) {
    std::cout
        << "Usage:\n"
        << "  " << prog
        << " --dataset NAME --n N --algo NAME --rounds R --warmups W"
        << " --seeds PATH --out PATH --git-hash HASH\n\n"
        << "Datasets:\n"
        << "  random ascending descending band_limited block_cyclic\n"
        << "  two_run_riffle social_feed partial_index\n\n"
        << "Algorithms:\n"
        << "  laddersort_raw laddersort_hybrid timsort std_sort\n"
        << "  std_stable_sort quicksort mergesort\n";
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

    std::vector<uint64_t> seeds;
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

    std::cout << "Phase 2 config:\n";
    std::cout << "dataset=" << cfg.dataset << "\n";
    std::cout << "n=" << cfg.n << "\n";
    std::cout << "algo=" << cfg.algo << "\n";
    std::cout << "rounds=" << cfg.rounds << "\n";
    std::cout << "warmups=" << cfg.warmups << "\n";
    std::cout << "seeds=" << cfg.seeds_path << "\n";
    std::cout << "out=" << cfg.out << "\n";
    std::cout << "git_hash=" << cfg.git_hash << "\n";

    static std::vector<int> g_merge_buf;

    std::vector<int> base;
    try {
        base = generate_dataset(cfg.n, seeds[0]);
    } catch (const std::exception& e) {
        cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    auto run_selected_sort = [&](std::vector<int>& v) -> long long {
        if (cfg.algo == "laddersort_raw") {
            ladder_sort_into(v, g_ladder_out_ws);
            v.swap(g_ladder_out_ws);
            return (long long)g_lad_ws.size();
        } else if (cfg.algo == "laddersort_hybrid") {
            ladder_sort_into(v, g_ladder_out_ws);
            v.swap(g_ladder_out_ws);
            return (long long)g_lad_ws.size();
        } else if (cfg.algo == "timsort") {
            timsort::timsort(v);
            return -1;
        } else if (cfg.algo == "std_sort") {
            std::sort(v.begin(), v.end());
            return -1;
        } else if (cfg.algo == "std_stable_sort") {
            std::stable_sort(v.begin(), v.end());
            return -1;
        } else if (cfg.algo == "quicksort") {
            quicksort3(v);
            return -1;
        } else if (cfg.algo == "mergesort") {
            mergesort_with_buf(v, g_merge_buf);
            return -1;
        } else {
            throw std::runtime_error("unknown algorithm: " + cfg.algo);
        }
    };

    auto run_and_record = [&](int round_count) {
        if (cfg.algo == "laddersort_raw") {
            return bench_algo_postinsert_csv("laddersort_raw", "main", base, round_count, cfg.n, seeds[0], cfg.dataset, cfg.out, cfg.git_hash, run_selected_sort);
        } else if (cfg.algo == "laddersort_hybrid") {
            return bench_algo_postinsert_csv("laddersort_hybrid", "main", base, round_count, cfg.n, seeds[0], cfg.dataset, cfg.out, cfg.git_hash, run_selected_sort);
        } else if (cfg.algo == "timsort") {
            return bench_algo_postinsert_csv("timsort", "main", base, round_count, cfg.n, seeds[0], cfg.dataset, cfg.out, cfg.git_hash, run_selected_sort);
        } else if (cfg.algo == "std_sort") {
            return bench_algo_postinsert_csv("std_sort", "main", base, round_count, cfg.n, seeds[0], cfg.dataset, cfg.out, cfg.git_hash, run_selected_sort);
        } else if (cfg.algo == "std_stable_sort") {
            return bench_algo_postinsert_csv("std_stable_sort", "main", base, round_count, cfg.n, seeds[0], cfg.dataset, cfg.out, cfg.git_hash, run_selected_sort);
        } else if (cfg.algo == "quicksort") {
            return bench_algo_postinsert_csv("quicksort", "main", base, round_count, cfg.n, seeds[0], cfg.dataset, cfg.out, cfg.git_hash, run_selected_sort);
        } else if (cfg.algo == "mergesort") {
            return bench_algo_postinsert_csv("mergesort", "main", base, round_count, cfg.n, seeds[0], cfg.dataset, cfg.out, cfg.git_hash, run_selected_sort);
        }

        throw std::runtime_error("unknown algorithm: " + cfg.algo);
    };

    for (int i = 0; i < cfg.warmups; ++i) {
        std::vector<int> warmup = base;
        warmup.push_back(-1);
        run_selected_sort(warmup);
        consume(warmup);
    }

    try {
        Result r = run_and_record(cfg.rounds);
        print_result(r);
    } catch (const std::exception& e) {
        cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    if (g_sink64 == 0xdeadbeefULL) std::cerr << "";
    return 0;
}