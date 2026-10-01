#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using std::size_t;

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

static std::vector<int> generate_controlled_k_exact(size_t N, int K) {
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

static void expect_k(const std::string& name, const std::vector<int>& a, long long expected) {
    long long got = measure_k_only(a);

    std::cout << name << ": measured K = " << got
              << ", expected K = " << expected << "\n";

    if (got != expected) {
        throw std::runtime_error(name + " failed");
    }
}

int main() {
    expect_k("ascending", {1, 2, 3, 4, 5}, 1);
    expect_k("descending", {5, 4, 3, 2, 1}, 5);
    expect_k("two_run_riffle", {0, 5, 1, 6, 2, 7, 3, 8, 4, 9}, 2);
    expect_k("controlled_k4", {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8}, 4);

    const size_t N = 1'000'000;

    for (int K : {1, 2, 4, 8, 16, 32, 64, 128}) {
        auto a = generate_controlled_k_exact(N, K);
        long long got = measure_k_only(a);

        std::cout << "target_K=" << K
                  << ", measured_K=" << got << "\n";

        if (got != K) {
            throw std::runtime_error("controlled generator failed");
        }
    }

    std::cout << "All Phase 3 K smoke tests passed.\n";
    return 0;
}
