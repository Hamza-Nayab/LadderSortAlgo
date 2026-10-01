#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using std::size_t;

namespace {

constexpr std::string_view kAblationRawHeader =
    "dataset,n,variant,seed,round,time_sec,k_final,k_over_n,sorted_ok,git_hash";
constexpr std::string_view kAblationSummaryHeader =
    "dataset,n,variant,count,mean_sec,median_sec,std_sec,min_sec,max_sec,mean_k,"
    "median_k,relative_time_mean,relative_time_median,sorted_ok";

constexpr std::string_view kAblationSocialRawPath =
    "results/raw/07_ablation_social_raw.csv";
constexpr std::string_view kAblationSocialSummaryPath =
    "results/summary/08_ablation_social_summary.csv";
constexpr std::string_view kAblationRiffleRawPath =
    "results/raw/09_ablation_riffle_raw.csv";
constexpr std::string_view kAblationRiffleSummaryPath =
    "results/summary/10_ablation_riffle_summary.csv";

enum class AblationVariant {
  kLadderFull,
  kNoHint,
  kNoGallop,
  kHeapMerge,
  kStableMode,
};

constexpr AblationVariant kAblationVariants[] = {
    AblationVariant::kLadderFull,
    AblationVariant::kNoHint,
    AblationVariant::kNoGallop,
    AblationVariant::kHeapMerge,
    AblationVariant::kStableMode,
};

struct AblationResult {
  std::vector<int> output;
  long long k_final = 0;
  double k_over_n = 0.0;
  bool sorted_ok = true;
};

struct Config {
  std::string dataset = "social_feed";
  std::vector<size_t> sizes = {1000000, 10000000, 100000000};
  int rounds = 10;
  std::string seeds_path = "results/seeds.txt";
  std::string out_path;
  std::string git_hash = "unknown";
};

struct LadderConfig {
  bool use_hint = true;
  bool use_gallop = true;
  bool use_loser_tree = true;
  bool stable = false;
  int gallop_threshold = 8;
};

struct PlainItem {
  int key;
};

struct StableItem {
  int key;
  uint32_t pos;
};

template <bool Stable>
using ItemT = std::conditional_t<Stable, StableItem, PlainItem>;

template <bool Stable>
ItemT<Stable> make_item(int key, uint32_t pos) {
  if constexpr (Stable) {
    return StableItem{key, pos};
  } else {
    (void)pos;
    return PlainItem{key};
  }
}

template <bool Stable>
int get_key(const ItemT<Stable>& x) {
  return x.key;
}

template <bool Stable>
bool less_item(const ItemT<Stable>& a, const ItemT<Stable>& b) {
  if (a.key != b.key) return a.key < b.key;
  if constexpr (Stable) return a.pos < b.pos;
  return false;
}

template <bool Stable>
bool less_eq_item(const ItemT<Stable>& a, const ItemT<Stable>& b) {
  if (a.key != b.key) return a.key < b.key;
  if constexpr (Stable) return a.pos <= b.pos;
  return true;
}

template <typename Item>
struct LadderWorkspace {
  static std::vector<std::vector<Item>> ladders;
  static std::vector<int> tops;
  static std::vector<Item> item_output;
};

template <typename Item>
std::vector<std::vector<Item>> LadderWorkspace<Item>::ladders;

template <typename Item>
std::vector<int> LadderWorkspace<Item>::tops;

template <typename Item>
std::vector<Item> LadderWorkspace<Item>::item_output;

int plain_lower_bound_nonincreasing(const std::vector<int>& tops, int x) {
  int lo = 0;
  int hi = static_cast<int>(tops.size());
  while (lo < hi) {
    int mid = lo + ((hi - lo) >> 1);
    if (tops[mid] > x) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

int hinted_lower_bound_nonincreasing(const std::vector<int>& tops,
                                     int x,
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

template <bool Stable>
void merge_two_plain_items(const std::vector<ItemT<Stable>>& a,
                           const std::vector<ItemT<Stable>>& b,
                           std::vector<ItemT<Stable>>& out) {
  out.clear();
  out.reserve(a.size() + b.size());

  size_t i = 0;
  size_t j = 0;
  while (i < a.size() && j < b.size()) {
    if (less_eq_item<Stable>(a[i], b[j])) {
      out.push_back(a[i++]);
    } else {
      out.push_back(b[j++]);
    }
  }
  while (i < a.size()) out.push_back(a[i++]);
  while (j < b.size()) out.push_back(b[j++]);
}

template <bool Stable>
void merge_two_gallop_items(const std::vector<ItemT<Stable>>& a,
                            const std::vector<ItemT<Stable>>& b,
                            std::vector<ItemT<Stable>>& out,
                            int gallop_threshold) {
  out.clear();
  out.reserve(a.size() + b.size());

  size_t i = 0;
  size_t j = 0;
  int win_a = 0;
  int win_b = 0;

  auto gallop_right =
      [](const std::vector<ItemT<Stable>>& values, size_t lo,
         const ItemT<Stable>& key) {
        const size_t n = values.size();
        size_t step = 1;
        size_t hi = lo;
        while (hi + step < n && less_eq_item<Stable>(values[hi + step], key)) {
          step <<= 1;
        }

        size_t left = lo;
        size_t right = std::min(hi + step, n);
        while (left < right) {
          size_t mid = left + ((right - left) >> 1);
          if (less_eq_item<Stable>(values[mid], key)) {
            left = mid + 1;
          } else {
            right = mid;
          }
        }
        return left;
      };

  while (i < a.size() && j < b.size()) {
    if (less_eq_item<Stable>(a[i], b[j])) {
      out.push_back(a[i++]);
      if (++win_a >= gallop_threshold && j < b.size()) {
        size_t next_i = gallop_right(a, i, b[j]);
        out.insert(out.end(), a.begin() + i, a.begin() + next_i);
        i = next_i;
        win_a = win_b = 0;
      }
    } else {
      out.push_back(b[j++]);
      if (++win_b >= gallop_threshold && i < a.size()) {
        size_t step = 1;
        size_t next_j = j;
        while (next_j + step < b.size() &&
               less_item<Stable>(b[next_j + step], a[i])) {
          step <<= 1;
        }

        size_t left = j;
        size_t right = std::min(next_j + step, b.size());
        while (left < right) {
          size_t mid = left + ((right - left) >> 1);
          if (less_item<Stable>(b[mid], a[i])) {
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

template <bool Stable>
struct LoserTree {
  int k = 0;
  std::vector<int> tree;
  std::vector<ItemT<Stable>> key;
  std::vector<const ItemT<Stable>*> cur;
  std::vector<const ItemT<Stable>*> end;
  std::vector<char> alive;

  explicit LoserTree(const std::vector<std::vector<ItemT<Stable>>>& runs) {
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

  bool less_eq_run(int a, int b) const {
    if (!alive[a]) return false;
    if (!alive[b]) return true;
    return less_eq_item<Stable>(key[a], key[b]);
  }

  void adjust(int s) {
    int t = s;
    for (int parent = (s + k) >> 1; parent > 0; parent >>= 1) {
      int& loser = tree[parent - 1];
      if (loser < 0) {
        loser = t;
      } else if (!less_eq_run(t, loser)) {
        std::swap(t, loser);
      }
    }
    tree[0] = t;
  }

  ItemT<Stable> pop_and_advance() {
    int s = tree[0];
    ItemT<Stable> value = key[s];
    if (++cur[s] < end[s]) {
      key[s] = *cur[s];
    } else {
      alive[s] = 0;
    }
    adjust(s);
    return value;
  }
};

template <bool Stable>
void merge_k_loser_tree_items(const std::vector<std::vector<ItemT<Stable>>>& runs,
                              std::vector<ItemT<Stable>>& out) {
  out.clear();
  size_t total = 0;
  for (const auto& run : runs) total += run.size();
  out.reserve(total);

  if (runs.empty()) return;
  if (runs.size() == 1) {
    out = runs[0];
    return;
  }

  LoserTree<Stable> tree(runs);
  for (size_t i = 0; i < total; ++i) {
    out.push_back(tree.pop_and_advance());
  }
}

template <bool Stable>
void merge_k_heap_items(const std::vector<std::vector<ItemT<Stable>>>& runs,
                        std::vector<ItemT<Stable>>& out) {
  out.clear();
  size_t total = 0;
  for (const auto& run : runs) total += run.size();
  out.reserve(total);

  struct Node {
    ItemT<Stable> item;
    int run_id = 0;
    size_t index = 0;
  };

  struct GreaterNode {
    bool operator()(const Node& lhs, const Node& rhs) const {
      if (less_item<Stable>(rhs.item, lhs.item)) return true;
      if (less_item<Stable>(lhs.item, rhs.item)) return false;
      return rhs.run_id < lhs.run_id;
    }
  };

  std::priority_queue<Node, std::vector<Node>, GreaterNode> heap;
  for (int run_id = 0; run_id < static_cast<int>(runs.size()); ++run_id) {
    if (!runs[run_id].empty()) heap.push(Node{runs[run_id][0], run_id, 0});
  }

  while (!heap.empty()) {
    Node node = heap.top();
    heap.pop();
    out.push_back(node.item);

    size_t next_index = node.index + 1;
    if (next_index < runs[node.run_id].size()) {
      heap.push(Node{runs[node.run_id][next_index], node.run_id, next_index});
    }
  }
}

template <bool Stable>
bool is_sorted_items(const std::vector<ItemT<Stable>>& values) {
  for (size_t i = 1; i < values.size(); ++i) {
    if (less_item<Stable>(values[i], values[i - 1])) return false;
  }
  return true;
}

template <bool Stable>
AblationResult ladder_sort_variant_typed(const std::vector<int>& input,
                                         const LadderConfig& cfg) {
  using Item = ItemT<Stable>;
  auto& ladders = LadderWorkspace<Item>::ladders;
  auto& tops = LadderWorkspace<Item>::tops;
  auto& item_output = LadderWorkspace<Item>::item_output;

  AblationResult result;
  result.output.clear();
  result.output.reserve(input.size());

  if (input.empty()) {
    result.sorted_ok = true;
    return result;
  }

  ladders.clear();
  tops.clear();
  ladders.reserve(64);
  tops.reserve(64);

  ladders.push_back({make_item<Stable>(input[0], 0)});
  tops.push_back(input[0]);

  int last_idx = 0;
  for (size_t i = 1; i < input.size(); ++i) {
    const int x = input[i];
    const int idx = cfg.use_hint
                        ? hinted_lower_bound_nonincreasing(tops, x, last_idx)
                        : plain_lower_bound_nonincreasing(tops, x);

    if (idx == static_cast<int>(ladders.size())) {
      ladders.emplace_back().push_back(
          make_item<Stable>(x, static_cast<uint32_t>(i)));
      tops.push_back(x);
    } else {
      ladders[idx].push_back(make_item<Stable>(x, static_cast<uint32_t>(i)));
      tops[idx] = x;
    }

    last_idx = idx;
  }

  result.k_final = static_cast<long long>(ladders.size());
  result.k_over_n =
      static_cast<double>(result.k_final) / static_cast<double>(input.size());

  if (ladders.size() == 1) {
    item_output = ladders[0];
  } else if (ladders.size() == 2) {
    if (cfg.use_gallop) {
      merge_two_gallop_items<Stable>(ladders[0], ladders[1], item_output,
                                     cfg.gallop_threshold);
    } else {
      merge_two_plain_items<Stable>(ladders[0], ladders[1], item_output);
    }
  } else if (cfg.use_loser_tree) {
    merge_k_loser_tree_items<Stable>(ladders, item_output);
  } else {
    merge_k_heap_items<Stable>(ladders, item_output);
  }

  result.sorted_ok = is_sorted_items<Stable>(item_output);
  result.output.reserve(item_output.size());
  for (const auto& item : item_output) {
    result.output.push_back(get_key<Stable>(item));
  }
  return result;
}

LadderConfig config_for_variant(AblationVariant variant) {
  LadderConfig cfg;
  switch (variant) {
    case AblationVariant::kLadderFull:
      break;
    case AblationVariant::kNoHint:
      cfg.use_hint = false;
      break;
    case AblationVariant::kNoGallop:
      cfg.use_gallop = false;
      break;
    case AblationVariant::kHeapMerge:
      cfg.use_loser_tree = false;
      break;
    case AblationVariant::kStableMode:
      cfg.stable = true;
      break;
  }
  return cfg;
}

std::string_view variant_name(AblationVariant variant) {
  switch (variant) {
    case AblationVariant::kLadderFull:
      return "LadderFull";
    case AblationVariant::kNoHint:
      return "NoHint";
    case AblationVariant::kNoGallop:
      return "NoGallop";
    case AblationVariant::kHeapMerge:
      return "HeapMerge";
    case AblationVariant::kStableMode:
      return "StableMode";
  }
  return "Unknown";
}

AblationResult run_ablation_variant(const std::vector<int>& input,
                                    AblationVariant variant) {
  const LadderConfig cfg = config_for_variant(variant);
  if (cfg.stable) {
    return ladder_sort_variant_typed<true>(input, cfg);
  }
  return ladder_sort_variant_typed<false>(input, cfg);
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
      out.push_back(static_cast<int>(left_size + j));
      ++j;
    } else if (j == right_size) {
      out.push_back(static_cast<int>(i));
      ++i;
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
  if (dataset == "social_feed") return generate_social_feed(n, seed);
  if (dataset == "two_run_riffle") return generate_two_run_riffle(n, seed);
  throw std::runtime_error("unknown dataset: " + dataset);
}

std::vector<size_t> parse_sizes(const std::string& text) {
  std::vector<size_t> sizes;
  std::stringstream input(text);
  std::string token;
  while (std::getline(input, token, ',')) {
    if (token.empty()) continue;
    sizes.push_back(static_cast<size_t>(std::stoull(token)));
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

std::string default_out_path(const std::string& dataset) {
  if (dataset == "social_feed") return std::string(kAblationSocialRawPath);
  if (dataset == "two_run_riffle") return std::string(kAblationRiffleRawPath);
  throw std::runtime_error("unknown dataset: " + dataset);
}

void print_usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " --dataset social_feed|two_run_riffle [options]\n"
      << "Options:\n"
      << "  --sizes CSV        default 1000000,10000000,100000000\n"
      << "  --rounds N         default 10\n"
      << "  --seeds PATH       default results/seeds.txt; falls back to 1..10\n"
      << "  --out PATH         output raw CSV path\n"
      << "  --git-hash STRING  default unknown\n";
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
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  if (cfg.dataset != "social_feed" && cfg.dataset != "two_run_riffle") {
    throw std::runtime_error("--dataset must be social_feed or two_run_riffle");
  }
  if (cfg.out_path.empty()) cfg.out_path = default_out_path(cfg.dataset);
  return cfg;
}

void write_header_if_new(const std::string& path) {
  std::ifstream existing(path);
  if (existing.good() && existing.peek() != std::ifstream::traits_type::eof()) {
    return;
  }

  std::ofstream out(path, std::ios::app);
  if (!out) throw std::runtime_error("cannot open output CSV: " + path);
  out << kAblationRawHeader << '\n';
}

void append_raw_row(std::ofstream& out,
                    const std::string& dataset,
                    size_t n,
                    std::string_view variant,
                    uint64_t seed,
                    int round,
                    double time_sec,
                    const AblationResult& result,
                    const std::string& git_hash) {
  out << dataset << ',' << n << ',' << variant << ',' << seed << ',' << round
      << ',' << std::fixed << std::setprecision(9) << time_sec << ','
      << result.k_final << ',' << std::setprecision(12) << result.k_over_n
      << ',' << (result.sorted_ok ? 1 : 0) << ',' << git_hash << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Config cfg = parse_args(argc, argv);
    const std::vector<uint64_t> seeds = load_seeds(cfg.seeds_path);

    write_header_if_new(cfg.out_path);
    std::ofstream out(cfg.out_path, std::ios::app);
    if (!out) throw std::runtime_error("cannot open output CSV: " + cfg.out_path);

    std::cout << "dataset=" << cfg.dataset << " rounds=" << cfg.rounds
              << " out=" << cfg.out_path << "\n";

    for (size_t n : cfg.sizes) {
      for (uint64_t seed : seeds) {
        std::cout << "n=" << n << " seed=" << seed << "\n";
        const std::vector<int> base = generate_dataset(cfg.dataset, n, seed);

        for (int round = 1; round <= cfg.rounds; ++round) {
          for (AblationVariant variant : kAblationVariants) {
            const auto t0 = std::chrono::steady_clock::now();
            AblationResult result = run_ablation_variant(base, variant);
            const auto t1 = std::chrono::steady_clock::now();

            const double time_sec =
                std::chrono::duration<double>(t1 - t0).count();
            append_raw_row(out, cfg.dataset, n, variant_name(variant), seed,
                           round, time_sec, result, cfg.git_hash);
          }
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    print_usage(argv[0]);
    return 1;
  }

  (void)kAblationSummaryHeader;
  (void)kAblationSocialSummaryPath;
  (void)kAblationRiffleSummaryPath;
  return 0;
}
