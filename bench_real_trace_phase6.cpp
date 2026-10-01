#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
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

constexpr std::string_view kDatasetName = "github_events";
constexpr std::string_view kRawCsvHeader =
    "dataset,n,algo,seed,round,time_sec,k_final,k_over_n,used_fallback,sorted_ok,git_hash";
constexpr std::string_view kSummaryCsvHeader =
    "dataset,n,algo,count,mean_sec,median_sec,std_sec,min_sec,max_sec,mean_k,median_k,"
    "fallback_rate,speedup_vs_timsort_median,sorted_ok";

constexpr std::string_view kDefaultInputPath =
    "data/processed/real_trace_github_events.csv";
constexpr std::string_view kRawCsvPath = "results/raw/19_real_trace_raw.csv";
constexpr std::string_view kSummaryCsvPath =
    "results/summary/20_real_trace_summary.csv";
constexpr std::string_view kNotesPath = "results/real_trace_notes.txt";

struct Config {
  std::string input_path = std::string(kDefaultInputPath);
  std::string algo = "RawLadderSort";
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
static std::vector<std::vector<long long>> g_ladder_ws;
static std::vector<long long> g_tops_ws;
static std::vector<long long> g_ladder_out_ws;

void consume(const std::vector<long long>& values) {
  uint64_t sum = 0;
  for (long long value : values) sum += static_cast<uint64_t>(value);
  g_sink64 ^= sum;
}

std::vector<std::string_view> split_csv_line(std::string_view line) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (start <= line.size()) {
    const size_t comma = line.find(',', start);
    if (comma == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, comma - start));
    start = comma + 1;
  }
  return fields;
}

std::vector<long long> load_trace_keys(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("could not open input CSV: " + path);

  std::string line;
  if (!std::getline(in, line)) {
    throw std::runtime_error("input CSV is empty: " + path);
  }
  const auto header = split_csv_line(line);
  int timestamp_col = -1;
  for (size_t i = 0; i < header.size(); ++i) {
    if (header[i] == "timestamp_ms") {
      timestamp_col = static_cast<int>(i);
      break;
    }
  }
  if (timestamp_col < 0) {
    throw std::runtime_error("input CSV is missing timestamp_ms column");
  }

  std::vector<long long> keys;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    const auto fields = split_csv_line(line);
    if (static_cast<int>(fields.size()) <= timestamp_col) {
      throw std::runtime_error("malformed input CSV row");
    }
    keys.push_back(std::stoll(std::string(fields[static_cast<size_t>(timestamp_col)])));
  }
  if (keys.empty()) throw std::runtime_error("input CSV has no data rows");
  return keys;
}

int hinted_lower_bound_lad(const std::vector<long long>& tops,
                           long long x,
                           int hint) {
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

long long measure_k_only(const std::vector<long long>& input) {
  if (input.empty()) return 0;

  std::vector<long long> tops;
  tops.reserve(64);
  tops.push_back(input[0]);

  int last_idx = 0;
  for (size_t i = 1; i < input.size(); ++i) {
    const long long x = input[i];
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

void merge_two_gallop(const std::vector<long long>& a,
                      const std::vector<long long>& b,
                      std::vector<long long>& out) {
  out.clear();
  out.reserve(a.size() + b.size());

  size_t i = 0;
  size_t j = 0;
  int win_a = 0;
  int win_b = 0;
  constexpr int kGallop = 8;

  auto gallop_right = [](const std::vector<long long>& values, size_t lo,
                         long long key) {
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
        out.insert(out.end(), a.begin() + static_cast<long long>(i),
                   a.begin() + static_cast<long long>(next_i));
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

        out.insert(out.end(), b.begin() + static_cast<long long>(j),
                   b.begin() + static_cast<long long>(left));
        j = left;
        win_a = win_b = 0;
      }
    }
  }

  out.insert(out.end(), a.begin() + static_cast<long long>(i), a.end());
  out.insert(out.end(), b.begin() + static_cast<long long>(j), b.end());
}

struct LoserTree {
  int k = 0;
  std::vector<int> tree;
  std::vector<long long> key;
  std::vector<const long long*> cur;
  std::vector<const long long*> end;
  std::vector<char> alive;

  explicit LoserTree(const std::vector<std::vector<long long>>& runs) {
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

  long long pop_and_advance() {
    int s = tree[0];
    long long value = key[s];
    if (++cur[s] < end[s]) {
      key[s] = *cur[s];
    } else {
      alive[s] = 0;
    }
    adjust(s);
    return value;
  }
};

void merge_k_loser_tree(const std::vector<std::vector<long long>>& runs,
                        std::vector<long long>& out) {
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

LadderStats ladder_sort_into_core(const std::vector<long long>& input,
                                  std::vector<long long>& out,
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
    const long long x = input[i];
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

LadderStats raw_ladder_sort_into(const std::vector<long long>& input,
                                 std::vector<long long>& out) {
  return ladder_sort_into_core(input, out, false, 0);
}

LadderStats hybrid_ladder_sort_into(const std::vector<long long>& input,
                                    std::vector<long long>& out,
                                    size_t threshold) {
  return ladder_sort_into_core(input, out, true, threshold);
}

size_t default_hybrid_threshold(size_t n) {
  return std::max<size_t>(
      1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(n)))));
}

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --input data/processed/real_trace_github_events.csv"
            << " --algo RawLadderSort|HybridLadderSort|TimSort|StdSort|StableSort"
            << " --seed S --round R --git-hash HASH\n";
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

    if (arg == "--input") {
      cfg.input_path = require_value("--input");
    } else if (arg == "--algo") {
      cfg.algo = require_value("--algo");
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

  if (cfg.algo != "RawLadderSort" && cfg.algo != "HybridLadderSort" &&
      cfg.algo != "TimSort" && cfg.algo != "StdSort" &&
      cfg.algo != "StableSort") {
    throw std::runtime_error("unsupported --algo: " + cfg.algo);
  }
  if (cfg.round <= 0) throw std::runtime_error("--round must be > 0");
  return cfg;
}

RunResult run_sort(const std::vector<long long>& base, const Config& cfg) {
  std::vector<long long> values = base;
  RunResult result;
  const size_t n = base.size();

  if (cfg.algo == "TimSort" || cfg.algo == "StdSort" ||
      cfg.algo == "StableSort") {
    result.k_final = measure_k_only(base);
    result.k_over_n =
        n == 0 ? 0.0
               : static_cast<double>(result.k_final) / static_cast<double>(n);
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
        hybrid_ladder_sort_into(values, g_ladder_out_ws, default_hybrid_threshold(n));
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
    const std::vector<long long> base = load_trace_keys(cfg.input_path);
    const RunResult result = run_sort(base, cfg);

    std::cout << kDatasetName << ',' << base.size() << ',' << cfg.algo << ','
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

  (void)kRawCsvHeader;
  (void)kSummaryCsvHeader;
  (void)kRawCsvPath;
  (void)kSummaryCsvPath;
  (void)kNotesPath;
  return 0;
}
