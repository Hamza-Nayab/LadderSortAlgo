#pragma once

#include <fstream>
#include <string>

struct CsvRow {
    std::string dataset;
    long long n;
    unsigned long long seed;
    std::string algo;
    std::string variant;
    int round;
    double time_sec;
    long long k_final;
    long long comparisons;
    long long moves;
    long long peak_rss_bytes;
    int sorted_ok;
    std::string git_hash;
};

static inline bool csv_file_empty(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return true;
    return in.tellg() == 0;
}

static inline void csv_append_row(const std::string& path, const CsvRow& row) {
    bool need_header = csv_file_empty(path);

    std::ofstream out(path, std::ios::app);

    if (need_header) {
        out << "dataset,n,seed,algo,variant,round,time_sec,k_final,comparisons,moves,peak_rss_bytes,sorted_ok,git_hash\n";
    }

    out << row.dataset << ","
        << row.n << ","
        << row.seed << ","
        << row.algo << ","
        << row.variant << ","
        << row.round << ","
        << row.time_sec << ","
        << row.k_final << ","
        << row.comparisons << ","
        << row.moves << ","
        << row.peak_rss_bytes << ","
        << (row.sorted_ok ? "true" : "false") << ","
        << row.git_hash << "\n";
}