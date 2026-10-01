#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

constexpr std::string_view kComparisonsRawHeader =
    "dataset,n,algo,seed,round,time_sec,total_comparisons,hint_comparisons,"
    "merge_comparisons,k_final,k_over_n,used_fallback,sorted_ok,git_hash";
constexpr std::string_view kComparisonsSummaryHeader =
    "dataset,n,algo,count,mean_sec,median_sec,mean_comparisons,"
    "median_comparisons,comparisons_per_n,comparisons_per_n_log2_n,"
    "mean_hint_comparisons,mean_merge_comparisons,mean_k,median_k,"
    "fallback_rate,sorted_ok";

constexpr std::string_view kComparisonsRawPath =
    "results/raw/15_comparisons_raw.csv";
constexpr std::string_view kComparisonsSummaryPath =
    "results/summary/16_comparisons_summary.csv";

struct Config {
  std::string dataset = "random";
  std::string algo = "RawLadderSort";
  size_t n = 100000;
  uint64_t seed = 1;
  int round = 1;
  std::string git_hash = "unknown";
};

struct ComparisonStats {
  uint64_t total = 0;
  uint64_t hint = 0;
  uint64_t merge = 0;
};

struct LadderStats {
  long long k_final = 0;
  double k_over_n = 0.0;
  bool used_fallback = false;
};

struct RunResult {
  double time_sec = 0.0;
  ComparisonStats comparisons;
  long long k_final = 0;
  double k_over_n = 0.0;
  bool used_fallback = false;
  bool sorted_ok = false;
};

struct CountedLess {
  uint64_t* total = nullptr;

  bool operator()(int lhs, int rhs) const {
    ++(*total);
    return lhs < rhs;
  }
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

bool hint_gt(const std::vector<int>& tops,
             int idx,
             int x,
             ComparisonStats& counts) {
  ++counts.hint;
  ++counts.total;
  return tops[idx] > x;
}

bool hint_le(const std::vector<int>& tops,
             int idx,
             int x,
             ComparisonStats& counts) {
  ++counts.hint;
  ++counts.total;
  return tops[idx] <= x;
}

bool merge_le(int lhs, int rhs, ComparisonStats& counts) {
  ++counts.merge;
  ++counts.total;
  return lhs <= rhs;
}

bool merge_lt(int lhs, int rhs, ComparisonStats& counts) {
  ++counts.merge;
  ++counts.total;
  return lhs < rhs;
}

int hinted_lower_bound_lad_counted(const std::vector<int>& tops,
                                   int x,
                                   int hint,
                                   ComparisonStats& counts) {
  const int n = static_cast<int>(tops.size());
  if (n == 0) return 0;

  int i = hint;
  if (i < 0) i = 0;
  if (i >= n) i = n - 1;

  if ((i == 0 || hint_gt(tops, i - 1, x, counts)) &&
      hint_le(tops, i, x, counts)) {
    return i;
  }
  if (i + 1 < n && hint_gt(tops, i, x, counts) &&
      hint_le(tops, i + 1, x, counts)) {
    return i + 1;
  }
  if (i > 0 && hint_le(tops, i - 1, x, counts) &&
      (i == 1 || hint_gt(tops, i - 2, x, counts))) {
    return i - 1;
  }

  if (hint_gt(tops, i, x, counts)) {
    int last = i;
    int ofs = 1;
    while (i + ofs < n && hint_gt(tops, i + ofs, x, counts)) {
      last = i + ofs;
      ofs = (ofs << 1) + 1;
    }

    int lo = last + 1;
    int hi = std::min(i + ofs, n - 1);
    while (lo <= hi) {
      int mid = lo + ((hi - lo) >> 1);
      if (hint_gt(tops, mid, x, counts)) {
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    return lo;
  }

  int last = i;
  int ofs = 1;
  while (i - ofs >= 0 && hint_le(tops, i - ofs, x, counts)) {
    last = i - ofs;
    ofs <<= 1;
  }

  int lo = std::max(0, i - ofs);
  int hi = last;
  while (lo <= hi) {
    int mid = lo + ((hi - lo) >> 1);
    if (hint_gt(tops, mid, x, counts)) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

int hinted_lower_bound_lad_plain(const std::vector<int>& tops, int x, int hint) {
  ComparisonStats ignored;
  return hinted_lower_bound_lad_counted(tops, x, hint, ignored);
}

long long measure_k_only(const std::vector<int>& input) {
  if (input.empty()) return 0;

  std::vector<int> tops;
  tops.reserve(64);
  tops.push_back(input[0]);

  int last_idx = 0;
  for (size_t i = 1; i < input.size(); ++i) {
    const int x = input[i];
    const int idx = hinted_lower_bound_lad_plain(tops, x, last_idx);
    if (idx == static_cast<int>(tops.size())) {
      tops.push_back(x);
    } else {
      tops[idx] = x;
    }
    last_idx = idx;
  }
  return static_cast<long long>(tops.size());
}

void merge_two_gallop_counted(const std::vector<int>& a,
                              const std::vector<int>& b,
                              std::vector<int>& out,
                              ComparisonStats& counts) {
  out.clear();
  out.reserve(a.size() + b.size());

  size_t i = 0;
  size_t j = 0;
  int win_a = 0;
  int win_b = 0;
  constexpr int kGallop = 8;

  auto gallop_right = [&](const std::vector<int>& values, size_t lo, int key) {
    const size_t n = values.size();
    size_t step = 1;
    size_t hi = lo;
    while (hi + step < n && merge_le(values[hi + step], key, counts)) {
      step <<= 1;
    }

    size_t left = lo;
    size_t right = std::min(hi + step, n);
    while (left < right) {
      size_t mid = left + ((right - left) >> 1);
      if (merge_le(values[mid], key, counts)) {
        left = mid + 1;
      } else {
        right = mid;
      }
    }
    return left;
  };

  while (i < a.size() && j < b.size()) {
    if (merge_le(a[i], b[j], counts)) {
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
        while (next_j + step < b.size() &&
               merge_lt(b[next_j + step], a[i], counts)) {
          step <<= 1;
        }

        size_t left = j;
        size_t right = std::min(next_j + step, b.size());
        while (left < right) {
          size_t mid = left + ((right - left) >> 1);
          if (merge_lt(b[mid], a[i], counts)) {
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
  ComparisonStats* counts = nullptr;

  LoserTree(const std::vector<std::vector<int>>& runs, ComparisonStats& c)
      : counts(&c) {
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
    if (merge_lt(key[a], key[b], *counts)) return true;
    if (merge_lt(key[b], key[a], *counts)) return false;
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

void merge_k_loser_tree_counted(const std::vector<std::vector<int>>& runs,
                                std::vector<int>& out,
                                ComparisonStats& counts) {
  out.clear();
  size_t total = 0;
  for (const auto& run : runs) total += run.size();
  out.reserve(total);

  if (runs.empty()) return;
  if (runs.size() == 1) {
    out = runs[0];
    return;
  }

  LoserTree tree(runs, counts);
  for (size_t i = 0; i < total; ++i) out.push_back(tree.pop_and_advance());
}

LadderStats ladder_sort_into_core(const std::vector<int>& input,
                                  std::vector<int>& out,
                                  bool enable_hybrid,
                                  size_t hybrid_threshold,
                                  ComparisonStats& counts) {
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
    const int idx = hinted_lower_bound_lad_counted(tops, x, last_idx, counts);

    if (idx == static_cast<int>(ladders.size())) {
      ladders.emplace_back().push_back(x);
      tops.push_back(x);

      if (enable_hybrid && ladders.size() > hybrid_threshold) {
        out = input;
        CountedLess less{&counts.total};
        std::sort(out.begin(), out.end(), less);
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
    merge_two_gallop_counted(ladders[0], ladders[1], out, counts);
  } else {
    merge_k_loser_tree_counted(ladders, out, counts);
  }
  return stats;
}

LadderStats raw_ladder_sort_into(const std::vector<int>& input,
                                 std::vector<int>& out,
                                 ComparisonStats& counts) {
  return ladder_sort_into_core(input, out, false, 0, counts);
}

LadderStats hybrid_ladder_sort_into(const std::vector<int>& input,
                                    std::vector<int>& out,
                                    size_t threshold,
                                    ComparisonStats& counts) {
  return ladder_sort_into_core(input, out, true, threshold, counts);
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

std::vector<int> generate_block_cyclic(size_t n) {
  constexpr int kRuns = 8;
  constexpr size_t kBlock = 4;

  std::vector<int> out;
  out.reserve(n);
  std::vector<size_t> cur(kRuns);
  std::vector<size_t> stop(kRuns);
  const size_t q = n / kRuns;
  const size_t r = n % kRuns;
  size_t acc = 0;

  for (int k = 0; k < kRuns; ++k) {
    const size_t len = q + (k < static_cast<int>(r) ? 1 : 0);
    cur[k] = acc + 1;
    stop[k] = acc + len;
    acc += len;
  }

  size_t produced = 0;
  int k = 0;
  while (produced < n) {
    if (cur[k] <= stop[k]) {
      const size_t take = std::min(kBlock, stop[k] - cur[k] + 1);
      for (size_t t = 0; t < take; ++t) {
        out.push_back(static_cast<int>(cur[k]++));
      }
      produced += take;
    }
    k = (k + 1) % kRuns;
  }
  return out;
}

std::vector<int> generate_two_run_riffle(size_t n, uint64_t seed) {
  const size_t left_size = n / 2;
  const size_t right_size = n - left_size;
  std::vector<int> out;
  out.reserve(n);

  uint64_t x = seed ? seed : 0x9E3779B97F4A7C15ULL;
  auto u01 = [&]() {
    x ^= x << 7;
    x ^= x >> 9;
    x *= 0x2545F4914F6CDD1ULL;
    return static_cast<double>(((x >> 11) & ((1ULL << 53) - 1)) *
                               (1.0 / (1ULL << 53)));
  };

  size_t i = 0;
  size_t j = 0;
  while (i < left_size || j < right_size) {
    if (i == left_size) {
      out.push_back(static_cast<int>(left_size + j++));
    } else if (j == right_size) {
      out.push_back(static_cast<int>(i++));
    } else if (u01() < 0.5) {
      out.push_back(static_cast<int>(i++));
    } else {
      out.push_back(static_cast<int>(left_size + j++));
    }
  }
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
  if (dataset == "block_cyclic") return generate_block_cyclic(n);
  if (dataset == "two_run_riffle") return generate_two_run_riffle(n, seed);
  if (dataset == "social_feed") return generate_social_feed(n, seed);
  throw std::runtime_error("unknown dataset: " + dataset);
}

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --dataset random|descending|block_cyclic|two_run_riffle|social_feed"
            << " --algo RawLadderSort|HybridLadderSort|TimSort|StdSort|StableSort"
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
      cfg.dataset != "block_cyclic" && cfg.dataset != "two_run_riffle" &&
      cfg.dataset != "social_feed") {
    throw std::runtime_error("unsupported --dataset: " + cfg.dataset);
  }
  if (cfg.algo != "RawLadderSort" && cfg.algo != "HybridLadderSort" &&
      cfg.algo != "TimSort" && cfg.algo != "StdSort" &&
      cfg.algo != "StableSort") {
    throw std::runtime_error("unsupported --algo: " + cfg.algo);
  }
  if (cfg.round <= 0) throw std::runtime_error("--round must be > 0");
  return cfg;
}

RunResult run_sort(const std::vector<int>& base, const Config& cfg) {
  std::vector<int> values = base;
  RunResult result;

  if (cfg.algo == "TimSort" || cfg.algo == "StdSort" ||
      cfg.algo == "StableSort") {
    result.k_final = measure_k_only(base);
    result.k_over_n =
        cfg.n == 0 ? 0.0 : static_cast<double>(result.k_final) /
                              static_cast<double>(cfg.n);
  }

  const auto t0 = std::chrono::steady_clock::now();
  if (cfg.algo == "RawLadderSort") {
    const LadderStats stats =
        raw_ladder_sort_into(values, g_ladder_out_ws, result.comparisons);
    values.swap(g_ladder_out_ws);
    result.k_final = stats.k_final;
    result.k_over_n = stats.k_over_n;
  } else if (cfg.algo == "HybridLadderSort") {
    const LadderStats stats =
        hybrid_ladder_sort_into(values, g_ladder_out_ws,
                                default_hybrid_threshold(cfg.n),
                                result.comparisons);
    values.swap(g_ladder_out_ws);
    result.k_final = stats.k_final;
    result.k_over_n = stats.k_over_n;
    result.used_fallback = stats.used_fallback;
  } else if (cfg.algo == "TimSort") {
    CountedLess less{&result.comparisons.total};
    gfx::timsort(values.begin(), values.end(), less);
  } else if (cfg.algo == "StdSort") {
    CountedLess less{&result.comparisons.total};
    std::sort(values.begin(), values.end(), less);
  } else if (cfg.algo == "StableSort") {
    CountedLess less{&result.comparisons.total};
    std::stable_sort(values.begin(), values.end(), less);
  }
  const auto t1 = std::chrono::steady_clock::now();

  result.time_sec = std::chrono::duration<double>(t1 - t0).count();
  result.sorted_ok = std::is_sorted(values.begin(), values.end());
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
              << result.comparisons.total << ',' << result.comparisons.hint
              << ',' << result.comparisons.merge << ',' << result.k_final
              << ',' << std::setprecision(12) << result.k_over_n << ','
              << (result.used_fallback ? 1 : 0) << ','
              << (result.sorted_ok ? 1 : 0) << ',' << cfg.git_hash << '\n';
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    print_usage(argv[0]);
    return 1;
  }

  (void)kComparisonsRawHeader;
  (void)kComparisonsSummaryHeader;
  (void)kComparisonsRawPath;
  (void)kComparisonsSummaryPath;
  return 0;
}
