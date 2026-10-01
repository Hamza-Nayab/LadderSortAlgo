#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<gfx/timsort.hpp>)
#include <gfx/timsort.hpp>
#elif __has_include("third_party/cpp-TimSort/include/gfx/timsort.hpp")
#include "third_party/cpp-TimSort/include/gfx/timsort.hpp"
#elif __has_include("../third_party/cpp-TimSort/include/gfx/timsort.hpp")
#include "../third_party/cpp-TimSort/include/gfx/timsort.hpp"
#endif

using std::size_t;

namespace {

constexpr std::string_view kMemoryRawHeader =
    "dataset,n,algo,seed,round,time_sec,max_rss_bytes,k_final,k_over_n,"
    "used_fallback,sorted_ok,git_hash";
constexpr std::string_view kMemorySummaryHeader =
    "dataset,n,algo,count,mean_sec,median_sec,mean_max_rss_bytes,"
    "median_max_rss_bytes,min_max_rss_bytes,max_max_rss_bytes,mean_k,"
    "median_k,fallback_rate,sorted_ok";

constexpr std::string_view kMemoryRawPath = "results/raw/13_memory_raw.csv";
constexpr std::string_view kMemorySummaryPath =
    "results/summary/14_memory_summary.csv";

struct Config {
  std::string dataset = "random";
  std::string algo = "RawLadderSort";
  size_t n = 10000000;
  uint64_t seed = 1;
  int round = 1;
  std::string git_hash = "unknown";
};

struct LadderStats {
  long long k_final = 0;
  double k_over_n = 0.0;
  bool used_fallback = false;
};

struct RunResult {
  double time_sec = 0.0;
  long long k_final = 0;
  double k_over_n = 0.0;
  bool used_fallback = false;
  bool sorted_ok = false;
};

static volatile uint64_t g_sink64 = 0;
static std::vector<std::vector<int>> g_ladder_ws;
static std::vector<int> g_tops_ws;
static std::vector<int> g_ladder_out_ws;

void consume(const std::vector<int>& values) {
  uint64_t sum = 0;
  for (int value : values) sum += static_cast<uint64_t>(value);
  g_sink64 ^= sum;
}

int hinted_lower_bound_lad(const std::vector<int>& tops, int x, int hint) {
  const int n = static_cast<int>(tops.size());
  if (n == 0) return 0;

  int i = hint;
  if (i < 0) i = 0;
  if (i >= n) i = n - 1;

  if ((i == 0 || tops[i - 1] > x) && tops[i] <= x) return i;
  if (i + 1 < n && tops[i] > x && tops[i + 1] <= x) return i + 1;
  if (i > 0 && tops[i - 1] <= x && (i == 1 || tops[i - 2] > x)) {
    return i - 1;
  }

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
      if (tops[mid] > x) {
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    return lo;
  }

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
    if (tops[mid] > x) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

long long measure_k_only(const std::vector<int>& input) {
  if (input.empty()) return 0;

  std::vector<int> tops;
  tops.reserve(64);
  tops.push_back(input[0]);

  int last_idx = 0;
  for (size_t i = 1; i < input.size(); ++i) {
    const int x = input[i];
    const int idx = hinted_lower_bound_lad(tops, x, last_idx);
    if (idx == static_cast<int>(tops.size())) {
      tops.push_back(x);
    } else {
      tops[idx] = x;
    }
    last_idx = idx;
  }
  return static_cast<long long>(tops.size());
}

void merge_two_gallop(const std::vector<int>& a,
                      const std::vector<int>& b,
                      std::vector<int>& out) {
  out.clear();
  out.reserve(a.size() + b.size());

  size_t i = 0;
  size_t j = 0;
  int win_a = 0;
  int win_b = 0;
  constexpr int kGallop = 8;

  auto gallop_right = [](const std::vector<int>& values, size_t lo, int key) {
    const size_t n = values.size();
    size_t step = 1;
    size_t hi = lo;
    while (hi + step < n && values[hi + step] <= key) step <<= 1;

    size_t left = lo;
    size_t right = std::min(hi + step, n);
    while (left < right) {
      size_t mid = left + ((right - left) >> 1);
      if (values[mid] <= key) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }
    return left;
  };

  while (i < a.size() && j < b.size()) {
    if (a[i] <= b[j]) {
      out.push_back(a[i++]);
      if (++win_a >= kGallop && j < b.size()) {
        const size_t next_i = gallop_right(a, i, b[j]);
        out.insert(out.end(), a.begin() + i, a.begin() + next_i);
        i = next_i;
        win_a = win_b = 0;
      }
    } else {
      out.push_back(b[j++]);
      if (++win_b >= kGallop && i < a.size()) {
        size_t step = 1;
        size_t next_j = j;
        while (next_j + step < b.size() && b[next_j + step] < a[i]) {
          step <<= 1;
        }

        size_t left = j;
        size_t right = std::min(next_j + step, b.size());
        while (left < right) {
          size_t mid = left + ((right - left) >> 1);
          if (b[mid] < a[i]) {
            left = mid + 1;
          } else {
            right = mid;
          }
        }

        out.insert(out.end(), b.begin() + j, b.begin() + left);
        j = left;
        win_a = win_b = 0;
      }
    }
  }

  out.insert(out.end(), a.begin() + i, a.end());
  out.insert(out.end(), b.begin() + j, b.end());
}

struct LoserTree {
  int k = 0;
  std::vector<int> tree;
  std::vector<int> key;
  std::vector<const int*> cur;
  std::vector<const int*> end;
  std::vector<char> alive;

  explicit LoserTree(const std::vector<std::vector<int>>& runs) {
    k = static_cast<int>(runs.size());
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

  bool less_eq(int a, int b) const {
    if (!alive[a]) return false;
    if (!alive[b]) return true;
    if (key[a] != key[b]) return key[a] < key[b];
    return a < b;
  }

  void adjust(int s) {
    int t = s;
    for (int parent = (s + k) >> 1; parent > 0; parent >>= 1) {
      int& loser = tree[parent - 1];
      if (loser < 0) {
        loser = t;
      } else if (!less_eq(t, loser)) {
        std::swap(t, loser);
      }
    }
    tree[0] = t;
  }

  int pop_and_advance() {
    int s = tree[0];
    int value = key[s];
    if (++cur[s] < end[s]) {
      key[s] = *cur[s];
    } else {
      alive[s] = 0;
    }
    adjust(s);
    return value;
  }
};

void merge_k_loser_tree(const std::vector<std::vector<int>>& runs,
                        std::vector<int>& out) {
  out.clear();
  size_t total = 0;
  for (const auto& run : runs) total += run.size();
  out.reserve(total);

  if (runs.empty()) return;
  if (runs.size() == 1) {
    out = runs[0];
    return;
  }

  LoserTree tree(runs);
  for (size_t i = 0; i < total; ++i) out.push_back(tree.pop_and_advance());
}

LadderStats ladder_sort_into_core(const std::vector<int>& input,
                                  std::vector<int>& out,
                                  bool enable_hybrid,
                                  size_t hybrid_threshold) {
  LadderStats stats;
  if (input.empty()) {
    out.clear();
    return stats;
  }

  auto& ladders = g_ladder_ws;
  auto& tops = g_tops_ws;
  ladders.clear();
  tops.clear();
  ladders.reserve(64);
  tops.reserve(64);

  ladders.push_back({input[0]});
  tops.push_back(input[0]);

  int last_idx = 0;
  for (size_t i = 1; i < input.size(); ++i) {
    const int x = input[i];
    const int idx = hinted_lower_bound_lad(tops, x, last_idx);

    if (idx == static_cast<int>(ladders.size())) {
      ladders.emplace_back().push_back(x);
      tops.push_back(x);

      if (enable_hybrid && ladders.size() > hybrid_threshold) {
        out = input;
        std::sort(out.begin(), out.end());
        stats.k_final = static_cast<long long>(ladders.size());
        stats.k_over_n =
            static_cast<double>(stats.k_final) / static_cast<double>(input.size());
        stats.used_fallback = true;
        return stats;
      }
    } else {
      ladders[idx].push_back(x);
      tops[idx] = x;
    }

    last_idx = idx;
  }

  stats.k_final = static_cast<long long>(ladders.size());
  stats.k_over_n =
      static_cast<double>(stats.k_final) / static_cast<double>(input.size());

  if (ladders.size() == 1) {
    out = ladders[0];
  } else if (ladders.size() == 2) {
    merge_two_gallop(ladders[0], ladders[1], out);
  } else {
    merge_k_loser_tree(ladders, out);
  }
  return stats;
}

LadderStats raw_ladder_sort_into(const std::vector<int>& input,
                                 std::vector<int>& out) {
  return ladder_sort_into_core(input, out, false, 0);
}

LadderStats hybrid_ladder_sort_into(const std::vector<int>& input,
                                    std::vector<int>& out,
                                    size_t threshold) {
  return ladder_sort_into_core(input, out, true, threshold);
}

size_t default_hybrid_threshold(size_t n) {
  return std::max<size_t>(
      1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(n)))));
}

std::vector<int> generate_random(size_t n, uint64_t seed) {
  std::vector<int> out;
  out.reserve(n);

  uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;
  auto rnd = [&]() -> uint64_t {
    x ^= x << 7;
    x ^= x >> 9;
    x *= 0x2545F4914F6CDD1DULL;
    return x;
  };

  for (size_t i = 0; i < n; ++i) {
    out.push_back(static_cast<int>(rnd() & 0x7fffffffULL));
  }
  return out;
}

std::vector<int> generate_descending(size_t n) {
  std::vector<int> out(n);
  for (size_t i = 0; i < n; ++i) out[i] = static_cast<int>(n - 1 - i);
  return out;
}

std::vector<int> generate_social_feed(size_t n, uint64_t seed) {
  constexpr int kFeeds = 24;
  constexpr int kBurstMax = 24;
  constexpr int kStepMax = 2;
  constexpr int kSameProbabilityByte = 192;

  std::vector<int> out;
  out.reserve(n);

  std::vector<size_t> need(kFeeds);
  const size_t q = n / kFeeds;
  const size_t r = n % kFeeds;
  for (int k = 0; k < kFeeds; ++k) {
    need[k] = q + (k < static_cast<int>(r) ? 1 : 0);
  }

  std::vector<int> timestamp(kFeeds, 0);
  std::vector<size_t> emitted(kFeeds, 0);

  uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;
  auto rnd = [&]() -> uint64_t {
    x ^= x << 7;
    x ^= x >> 9;
    x *= 0x2545F4914F6CDD1DULL;
    return x;
  };

  auto has_more = [&](int feed) { return emitted[feed] < need[feed]; };

  int feed = static_cast<int>(rnd() % kFeeds);
  while (out.size() < n) {
    if (!has_more(feed)) {
      int tries = 0;
      while (tries < kFeeds && !has_more(feed)) {
        feed = (feed + 1) % kFeeds;
        ++tries;
      }
      if (tries == kFeeds) break;
    }

    int burst = 1 + static_cast<int>(rnd() % kBurstMax);
    while (burst-- > 0 && out.size() < n && has_more(feed)) {
      if (emitted[feed] > 0) {
        if ((rnd() & 0xFF) >= static_cast<uint64_t>(kSameProbabilityByte)) {
          timestamp[feed] += 1 + static_cast<int>(rnd() % kStepMax);
        }
      }
      out.push_back(timestamp[feed]);
      ++emitted[feed];
    }

    const uint64_t u = rnd() & 0xFF;
    if (u < 153) {
      // stay on the same feed
    } else if (u < 204) {
      feed = (feed + 1) % kFeeds;
    } else {
      feed = (feed + kFeeds - 1) % kFeeds;
    }
  }
  return out;
}

std::vector<int> generate_dataset(const std::string& dataset,
                                  size_t n,
                                  uint64_t seed) {
  if (dataset == "random") return generate_random(n, seed);
  if (dataset == "descending") return generate_descending(n);
  if (dataset == "social_feed") return generate_social_feed(n, seed);
  throw std::runtime_error("unknown dataset: " + dataset);
}

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
  st.push_back({0, static_cast<int>(a.size()) - 1});

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

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --dataset random|descending|social_feed"
            << " --algo RawLadderSort|HybridLadderSort|TimSort|StdSort|StableSort|QuickSort"
            << " --n N --seed S --round R --git-hash HASH\n";
}

Config parse_args(int argc, char** argv) {
  Config cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](std::string_view option) -> char* {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string(option) + " requires a value");
      }
      return argv[++i];
    };

    if (arg == "--dataset") {
      cfg.dataset = require_value("--dataset");
    } else if (arg == "--algo") {
      cfg.algo = require_value("--algo");
    } else if (arg == "--n") {
      cfg.n = static_cast<size_t>(std::stoull(require_value("--n")));
    } else if (arg == "--seed") {
      cfg.seed = std::stoull(require_value("--seed"));
    } else if (arg == "--round") {
      cfg.round = std::stoi(require_value("--round"));
    } else if (arg == "--git-hash") {
      cfg.git_hash = require_value("--git-hash");
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  if (cfg.dataset != "random" && cfg.dataset != "descending" &&
      cfg.dataset != "social_feed") {
    throw std::runtime_error("--dataset must be random, descending, or social_feed");
  }
  if (cfg.algo != "RawLadderSort" && cfg.algo != "HybridLadderSort" &&
      cfg.algo != "TimSort" && cfg.algo != "StdSort" &&
      cfg.algo != "StableSort" && cfg.algo != "QuickSort") {
    throw std::runtime_error("unsupported --algo: " + cfg.algo);
  }
  if (cfg.round <= 0) throw std::runtime_error("--round must be > 0");
  return cfg;
}

RunResult run_sort(const std::vector<int>& base, const Config& cfg) {
  std::vector<int> values = base;
  RunResult result;

  if (cfg.algo == "TimSort" || cfg.algo == "StdSort" ||
      cfg.algo == "StableSort" || cfg.algo == "QuickSort") {
    result.k_final = measure_k_only(base);
    result.k_over_n =
        cfg.n == 0 ? 0.0 : static_cast<double>(result.k_final) /
                              static_cast<double>(cfg.n);
  }

  const auto t0 = std::chrono::steady_clock::now();
  if (cfg.algo == "RawLadderSort") {
    const LadderStats stats = raw_ladder_sort_into(values, g_ladder_out_ws);
    values.swap(g_ladder_out_ws);
    result.k_final = stats.k_final;
    result.k_over_n = stats.k_over_n;
    result.used_fallback = false;
  } else if (cfg.algo == "HybridLadderSort") {
    const LadderStats stats =
        hybrid_ladder_sort_into(values, g_ladder_out_ws,
                                default_hybrid_threshold(cfg.n));
    values.swap(g_ladder_out_ws);
    result.k_final = stats.k_final;
    result.k_over_n = stats.k_over_n;
    result.used_fallback = stats.used_fallback;
  } else if (cfg.algo == "TimSort") {
    gfx::timsort(values.begin(), values.end());
  } else if (cfg.algo == "StdSort") {
    std::sort(values.begin(), values.end());
  } else if (cfg.algo == "StableSort") {
    std::stable_sort(values.begin(), values.end());
  } else if (cfg.algo == "QuickSort") {
    quicksort3(values);
  }
  const auto t1 = std::chrono::steady_clock::now();

  result.time_sec = std::chrono::duration<double>(t1 - t0).count();
  bool ok = std::is_sorted(values.begin(), values.end()) && (values.size() == base.size());
  if (ok) {
    uint64_t sum_base = 0, sum_val = 0;
    uint64_t xor_base = 0, xor_val = 0;
    for (size_t i = 0; i < base.size(); ++i) {
      sum_base += static_cast<uint64_t>(base[i]);
      sum_val  += static_cast<uint64_t>(values[i]);
      xor_base ^= (static_cast<uint64_t>(base[i]) + 0x9e3779b97f4a7c15ULL);
      xor_val  ^= (static_cast<uint64_t>(values[i]) + 0x9e3779b97f4a7c15ULL);
    }
    if (sum_base != sum_val || xor_base != xor_val) {
      ok = false;
    }
  }
  result.sorted_ok = ok;
  consume(values);
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config cfg = parse_args(argc, argv);
    const std::vector<int> base = generate_dataset(cfg.dataset, cfg.n, cfg.seed);
    const RunResult result = run_sort(base, cfg);

    std::cout << cfg.dataset << ',' << cfg.n << ',' << cfg.algo << ','
              << cfg.seed << ',' << cfg.round << ',' << std::fixed
              << std::setprecision(9) << result.time_sec << ','
              << result.k_final << ',' << std::setprecision(12)
              << result.k_over_n << ',' << (result.used_fallback ? 1 : 0)
              << ',' << (result.sorted_ok ? 1 : 0) << ',' << cfg.git_hash
              << '\n';
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    print_usage(argv[0]);
    return 1;
  }

  (void)kMemoryRawHeader;
  (void)kMemorySummaryHeader;
  (void)kMemoryRawPath;
  (void)kMemorySummaryPath;
  return 0;
}
