#ifndef LADDERSORT_HPP
#define LADDERSORT_HPP

/**
 * LadderSort: Adaptive Sorting via Interleaved Nondecreasing Subsequences
 *
 * Reference:
 *   "LadderSort: Adaptive Sorting via Interleaved Nondecreasing Subsequences"
 *   IEEE Access (2026)
 *   GitHub: https://github.com/Hamza-Nayab/LadderSortAlgo
 *
 * LadderSort partitions an input of size N into K interleaved nondecreasing
 * subsequences (ladders) using an online greedy rule that achieves Dilworth's
 * optimal decomposition K = LDS(A) without offline preprocessing. It then
 * routes the merge based on K:
 *   - K = 1: Already sorted (direct return)
 *   - K = 2: Two-way galloping merge (O(N) time)
 *   - K > 2: Multiway Loser Tree merge (O(N log(K + 1)) time)
 *
 * Space complexity: O(N) auxiliary space.
 * Time complexity: O(N (1 + log(K + 1))).
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

namespace laddersort {

struct LadderStats {
    long long k_final = 0;
    double k_over_n = 0.0;
    bool used_fallback = false;
};

namespace detail {

// Exponential galloping lower bound with local hint on tops array
template <typename T>
inline int hinted_lower_bound(const std::vector<T>& tops, const T& x, int hint) {
    int n = static_cast<int>(tops.size());
    if (n == 0) return 0;

    int i = hint;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;

    // Fast local probe
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

// Two-way galloping merge for K = 2
template <typename T>
inline void merge_two_gallop(const std::vector<T>& a,
                            const std::vector<T>& b,
                            std::vector<T>& out) {
    out.clear();
    out.reserve(a.size() + b.size());

    size_t i = 0;
    size_t j = 0;
    int win_a = 0;
    int win_b = 0;
    constexpr int kGallop = 8;

    auto gallop_right = [](const std::vector<T>& values, size_t lo, const T& key) {
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

// Multiway Loser Tree merge for K > 2
template <typename T>
struct LoserTree {
    int k = 0;
    std::vector<int> tree;
    std::vector<T> key;
    std::vector<const T*> cur;
    std::vector<const T*> end;
    std::vector<char> alive;

    explicit LoserTree(const std::vector<std::vector<T>>& runs) {
        k = static_cast<int>(runs.size());
        tree.assign(k, -1);
        key.resize(k);
        cur.resize(k);
        end.resize(k);
        alive.assign(k, 0);

        for (int i = 0; i < k; ++i) {
            if (!runs[i].empty()) {
                cur[i] = runs[i].data();
                end[i] = runs[i].data() + runs[i].size();
                key[i] = *cur[i];
                alive[i] = 1;
            }
        }

        for (int i = k - 1; i >= 0; --i) {
            adjust(i);
        }
    }

    inline bool competitor_wins(int a, int b) const {
        if (a < 0) return false;
        if (b < 0) return true;
        if (!alive[a]) return false;
        if (!alive[b]) return true;
        return key[a] <= key[b];
    }

    inline void adjust(int leaf) {
        int winner = leaf;
        int node = (k + leaf) / 2;
        while (node > 0) {
            int current_loser = tree[node];
            if (current_loser == -1) {
                tree[node] = winner;
                winner = -1;
                break;
            }
            if (competitor_wins(current_loser, winner)) {
                tree[node] = winner;
                winner = current_loser;
            }
            node /= 2;
        }
        tree[0] = winner;
    }

    inline int top() const { return tree[0]; }

    inline void pop() {
        int winner = tree[0];
        if (winner < 0 || !alive[winner]) return;

        ++cur[winner];
        if (cur[winner] == end[winner]) {
            alive[winner] = 0;
        } else {
            key[winner] = *cur[winner];
        }
        adjust(winner);
    }
};

template <typename T>
inline void merge_k_loser_tree(const std::vector<std::vector<T>>& runs,
                               std::vector<T>& out) {
    out.clear();
    size_t total = 0;
    for (const auto& r : runs) total += r.size();
    out.reserve(total);

    LoserTree<T> lt(runs);
    while (true) {
        int winner = lt.top();
        if (winner < 0 || !lt.alive[winner]) break;
        out.push_back(lt.key[winner]);
        lt.pop();
    }
}

} // namespace detail

/**
 * Core LadderSort routine.
 * Sorts @p input into @p out.
 * If @p enable_hybrid is true and K exceeds @p hybrid_threshold,
 * falls back to std::sort for guaranteed O(N log N) worst case.
 */
template <typename T>
inline LadderStats sort_into(const std::vector<T>& input,
                            std::vector<T>& out,
                            bool enable_hybrid = false,
                            size_t hybrid_threshold = 0) {
    LadderStats stats;
    if (input.empty()) {
        out.clear();
        return stats;
    }

    std::vector<std::vector<T>> ladders;
    std::vector<T> tops;
    ladders.reserve(64);
    tops.reserve(64);

    ladders.push_back({input[0]});
    tops.push_back(input[0]);

    int last_idx = 0;
    for (size_t i = 1; i < input.size(); ++i) {
        const T& x = input[i];
        const int idx = detail::hinted_lower_bound(tops, x, last_idx);

        if (idx == static_cast<int>(ladders.size())) {
            ladders.emplace_back().push_back(x);
            tops.push_back(x);

            if (enable_hybrid && ladders.size() > hybrid_threshold) {
                out = input;
                std::sort(out.begin(), out.end());
                stats.k_final = static_cast<long long>(ladders.size());
                stats.k_over_n = static_cast<double>(stats.k_final) / static_cast<double>(input.size());
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
    stats.k_over_n = static_cast<double>(stats.k_final) / static_cast<double>(input.size());

    // Merge router
    if (ladders.size() == 1) {
        out = ladders[0];
    } else if (ladders.size() == 2) {
        detail::merge_two_gallop(ladders[0], ladders[1], out);
    } else {
        detail::merge_k_loser_tree(ladders, out);
    }
    return stats;
}

/**
 * Standard LadderSort in-place interface.
 */
template <typename T>
inline void sort(std::vector<T>& arr) {
    std::vector<T> out;
    sort_into(arr, out, false, 0);
    arr.swap(out);
}

/**
 * Hybrid LadderSort with fallback threshold (default: ceil(sqrt(N))).
 */
template <typename T>
inline void hybrid_sort(std::vector<T>& arr, size_t threshold = 0) {
    if (threshold == 0) {
        threshold = std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(arr.size())))));
    }
    std::vector<T> out;
    sort_into(arr, out, true, threshold);
    arr.swap(out);
}

} // namespace laddersort

#endif // LADDERSORT_HPP
