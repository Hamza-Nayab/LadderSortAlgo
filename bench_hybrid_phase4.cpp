#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

constexpr std::string_view kHybridRawHeader =
    "dataset,n,algo,seed,round,time_sec,k_final,k_over_n,used_fallback,sorted_ok,git_hash";
constexpr std::string_view kHybridSummaryHeader =
    "dataset,n,algo,count,mean_sec,median_sec,std_sec,min_sec,max_sec,mean_k,"
    "median_k,fallback_rate,speedup_vs_raw_ladder_median,"
    "speedup_vs_timsort_median,sorted_ok";

constexpr std::string_view kHybridRawPath = "results/raw/11_hybrid_raw.csv";
constexpr std::string_view kHybridSummaryPath =
    "results/summary/12_hybrid_summary.csv";

constexpr std::string_view kAlgorithms[] = {
    "RawLadderSort",
    "HybridLadderSort",
    "TimSort",
    "StdSort",
};

static volatile uint64_t g_sink64 = 0;

void consume(const std::vector<int>& values) {
  uint64_t sum = 0;
  for (int value : values) sum += static_cast<uint64_t>(value);
  g_sink64 ^= sum;
}

struct LadderStats {
  long long k_final = 0;
  double k_over_n = 0.0;
  bool used_fallback = false;
};

struct HybridRow {
  std::string dataset;
  size_t n = 0;
  std::string_view algo;
  uint64_t seed = 0;
  int round = 0;
  double time_sec = 0.0;
  long long k_final = 0;
  double k_over_n = 0.0;
  bool used_fallback = false;
  bool sorted_ok = false;
  std::string git_hash;
};

struct Config {
  std::vector<std::string> datasets = {
      "descending",
      "random",
      "block_cyclic",
      "social_feed",
  };
  std::vector<size_t> sizes = {1000000, 10000000, 100000000};
  int rounds = 10;
  std::string seeds_path = "results/seeds.txt";
  std::string out_path = std::string(kHybridRawPath);
  std::string git_hash = "unknown";
  size_t hybrid_threshold = 0;
};

static std::vector<std::vector<int>> g_ladder_ws;
static std::vector<int> g_tops_ws;
static std::vector<int> g_ladder_out_ws;

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
  return std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(
                                 static_cast<double>(n)))));
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

std::vector<int> generate_descending(size_t n, uint64_t seed) {
  (void)seed;
  std::vector<int> out(n);
  for (size_t i = 0; i < n; ++i) out[i] = static_cast<int>(n - 1 - i);
  return out;
}

std::vector<int> generate_block_cyclic(size_t n, uint64_t seed) {
  (void)seed;
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
  if (dataset == "descending") return generate_descending(n, seed);
  if (dataset == "random") return generate_random(n, seed);
  if (dataset == "block_cyclic") return generate_block_cyclic(n, seed);
  if (dataset == "social_feed") return generate_social_feed(n, seed);
  throw std::runtime_error("unknown dataset: " + dataset);
}

std::vector<std::string> parse_csv_strings(const std::string& text) {
  std::vector<std::string> values;
  std::stringstream input(text);
  std::string token;
  while (std::getline(input, token, ',')) {
    if (!token.empty()) values.push_back(token);
  }
  if (values.empty()) throw std::runtime_error("CSV list must not be empty");
  return values;
}

std::vector<size_t> parse_sizes(const std::string& text) {
  std::vector<size_t> sizes;
  std::stringstream input(text);
  std::string token;
  while (std::getline(input, token, ',')) {
    if (!token.empty()) sizes.push_back(static_cast<size_t>(std::stoull(token)));
  }
  if (sizes.empty()) throw std::runtime_error("--sizes must not be empty");
  return sizes;
}

std::vector<uint64_t> load_seeds(const std::string& path) {
  std::ifstream in(path);
  std::vector<uint64_t> seeds;
  if (in) {
    uint64_t seed = 0;
    while (in >> seed) seeds.push_back(seed);
  }
  if (!seeds.empty()) return seeds;

  seeds.reserve(10);
  for (uint64_t seed = 1; seed <= 10; ++seed) seeds.push_back(seed);
  return seeds;
}

void print_usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n"
      << "Options:\n"
      << "  --datasets CSV          default descending,random,block_cyclic,social_feed\n"
      << "  --sizes CSV             default 1000000,10000000,100000000\n"
      << "  --rounds N              default 10\n"
      << "  --seeds PATH            default results/seeds.txt; falls back to 1..10\n"
      << "  --out PATH              default results/raw/11_hybrid_raw.csv\n"
      << "  --git-hash STRING       default unknown\n"
      << "  --hybrid-threshold N    default ceil(sqrt(n)) per size\n";
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

    if (arg == "--datasets") {
      cfg.datasets = parse_csv_strings(require_value("--datasets"));
    } else if (arg == "--sizes") {
      cfg.sizes = parse_sizes(require_value("--sizes"));
    } else if (arg == "--rounds") {
      cfg.rounds = std::stoi(require_value("--rounds"));
      if (cfg.rounds <= 0) throw std::runtime_error("--rounds must be > 0");
    } else if (arg == "--seeds") {
      cfg.seeds_path = require_value("--seeds");
    } else if (arg == "--out") {
      cfg.out_path = require_value("--out");
    } else if (arg == "--git-hash") {
      cfg.git_hash = require_value("--git-hash");
    } else if (arg == "--hybrid-threshold") {
      cfg.hybrid_threshold = std::stoull(require_value("--hybrid-threshold"));
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  for (const std::string& dataset : cfg.datasets) {
    if (dataset != "descending" && dataset != "random" &&
        dataset != "block_cyclic" && dataset != "social_feed") {
      throw std::runtime_error("unsupported dataset: " + dataset);
    }
  }
  return cfg;
}

void write_header_if_new(const std::string& path) {
  std::ifstream existing(path);
  if (existing.good() && existing.peek() != std::ifstream::traits_type::eof()) {
    return;
  }

  std::ofstream out(path, std::ios::app);
  if (!out) throw std::runtime_error("cannot open output CSV: " + path);
  out << kHybridRawHeader << '\n';
}

void append_row(std::ofstream& out, const HybridRow& row) {
  out << row.dataset << ',' << row.n << ',' << row.algo << ',' << row.seed
      << ',' << row.round << ',' << std::fixed << std::setprecision(9)
      << row.time_sec << ',' << row.k_final << ',' << std::setprecision(12)
      << row.k_over_n << ',' << (row.used_fallback ? 1 : 0) << ','
      << (row.sorted_ok ? 1 : 0) << ',' << row.git_hash << '\n';
}

HybridRow run_algorithm(const std::vector<int>& base,
                        const std::string& dataset,
                        size_t n,
                        uint64_t seed,
                        int round,
                        std::string_view algo,
                        long long measured_k,
                        size_t hybrid_threshold,
                        const std::string& git_hash) {
  std::vector<int> values = base;
  LadderStats stats;
  stats.k_final = measured_k;
  stats.k_over_n =
      n == 0 ? 0.0 : static_cast<double>(measured_k) / static_cast<double>(n);

  const auto t0 = std::chrono::steady_clock::now();
  if (algo == "RawLadderSort") {
    stats = raw_ladder_sort_into(values, g_ladder_out_ws);
    values.swap(g_ladder_out_ws);
  } else if (algo == "HybridLadderSort") {
    stats = hybrid_ladder_sort_into(values, g_ladder_out_ws, hybrid_threshold);
    values.swap(g_ladder_out_ws);
  } else if (algo == "TimSort") {
    gfx::timsort(values.begin(), values.end());
  } else if (algo == "StdSort") {
    std::sort(values.begin(), values.end());
  } else {
    throw std::runtime_error("unknown algorithm: " + std::string(algo));
  }
  const auto t1 = std::chrono::steady_clock::now();

  consume(values);

  HybridRow row;
  row.dataset = dataset;
  row.n = n;
  row.algo = algo;
  row.seed = seed;
  row.round = round;
  row.time_sec = std::chrono::duration<double>(t1 - t0).count();
  row.k_final = stats.k_final;
  row.k_over_n = stats.k_over_n;
  row.used_fallback = stats.used_fallback;
  row.sorted_ok = std::is_sorted(values.begin(), values.end());
  row.git_hash = git_hash;
  return row;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config cfg = parse_args(argc, argv);
    const std::vector<uint64_t> seeds = load_seeds(cfg.seeds_path);

    write_header_if_new(cfg.out_path);
    std::ofstream out(cfg.out_path, std::ios::app);
    if (!out) throw std::runtime_error("cannot open output CSV: " + cfg.out_path);

    std::cout << "Phase 4 hybrid config:"
              << " datasets=" << cfg.datasets.size()
              << " sizes=" << cfg.sizes.size()
              << " rounds=" << cfg.rounds
              << " seeds=" << seeds.size()
              << " out=" << cfg.out_path << "\n";

    for (const std::string& dataset : cfg.datasets) {
      for (size_t n : cfg.sizes) {
        const size_t threshold =
            cfg.hybrid_threshold == 0 ? default_hybrid_threshold(n)
                                      : cfg.hybrid_threshold;

        for (uint64_t seed : seeds) {
          std::cout << "dataset=" << dataset << " n=" << n << " seed=" << seed
                    << " hybrid_threshold=" << threshold << "\n";
          const std::vector<int> base = generate_dataset(dataset, n, seed);
          const long long measured_k = measure_k_only(base);

          for (int round = 1; round <= cfg.rounds; ++round) {
            for (std::string_view algo : kAlgorithms) {
              const HybridRow row =
                  run_algorithm(base, dataset, n, seed, round, algo, measured_k,
                                threshold, cfg.git_hash);
              append_row(out, row);
            }
          }
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    print_usage(argv[0]);
    return 1;
  }

  (void)kHybridSummaryHeader;
  (void)kHybridSummaryPath;
  return 0;
}
