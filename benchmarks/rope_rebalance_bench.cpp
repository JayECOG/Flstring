// Benchmark: fl::rope concatenation — with and without rebalance().
//
// Isolates the cost of rebalance() from raw concatenation by running three variants:
//   A) fl::rope concat  — no rebalance at all
//   B) fl::rope concat  — rebalance() once after all concat
//   C) fl::rope concat  — rebalance() every N concats (periodic)
//   D) std::string +=   — baseline linear append
//
// Run with strings of increasing count and size to expose the O(n log n) rebalance
// cost relative to the O(1)-per-node concat.

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "fl.hpp"

// ---------------------------------------------------------------------------
struct Timer {
    std::chrono::high_resolution_clock::time_point t0;
    Timer() : t0(std::chrono::high_resolution_clock::now()) {}
    double elapsed_ms() const {
        using namespace std::chrono;
        return duration<double, std::milli>(high_resolution_clock::now() - t0).count();
    }
};

static volatile std::size_t g_sink;
static void sink(std::size_t v) { g_sink = v; }

// ---------------------------------------------------------------------------
static std::string rand_string(std::mt19937& rng, std::size_t len) {
    std::string s(len, '\0');
    for (char& c : s)
        c = static_cast<char>('a' + rng() % 26);
    return s;
}

// ---------------------------------------------------------------------------
struct Result {
    double std_ms;
    double rope_no_rebalance_ms;
    double rope_rebalance_once_ms;
    double rope_rebalance_periodic_ms; // every 32 concats
};

static Result bench(int n_strings, std::size_t str_len, int repeats) {
    std::mt19937 rng(0xDEAD);

    // Pre-generate strings
    std::vector<std::string> strs;
    strs.reserve(n_strings);
    for (int i = 0; i < n_strings; ++i)
        strs.push_back(rand_string(rng, str_len));

    Result r{};

    // --- std::string baseline ---
    {
        double total = 0;
        for (int rep = 0; rep < repeats; ++rep) {
            Timer t;
            std::string acc;
            acc.reserve(n_strings * str_len);
            for (auto& s : strs) acc += s;
            sink(acc.size());
            total += t.elapsed_ms();
        }
        r.std_ms = total / repeats;
    }

    // --- fl::rope, no rebalance ---
    {
        double total = 0;
        for (int rep = 0; rep < repeats; ++rep) {
            Timer t;
            fl::rope acc;
            for (auto& s : strs) acc += fl::rope(s.c_str(), s.size());
            sink(acc.length());
            total += t.elapsed_ms();
        }
        r.rope_no_rebalance_ms = total / repeats;
    }

    // --- fl::rope, rebalance once at end ---
    {
        double total = 0;
        for (int rep = 0; rep < repeats; ++rep) {
            Timer t;
            fl::rope acc;
            for (auto& s : strs) acc += fl::rope(s.c_str(), s.size());
            acc.rebalance();
            sink(acc.length());
            total += t.elapsed_ms();
        }
        r.rope_rebalance_once_ms = total / repeats;
    }

    // --- fl::rope, rebalance every 32 concats ---
    {
        double total = 0;
        for (int rep = 0; rep < repeats; ++rep) {
            Timer t;
            fl::rope acc;
            for (int i = 0; i < n_strings; ++i) {
                acc += fl::rope(strs[i].c_str(), strs[i].size());
                if ((i & 31) == 31) acc.rebalance();
            }
            sink(acc.length());
            total += t.elapsed_ms();
        }
        r.rope_rebalance_periodic_ms = total / repeats;
    }

    return r;
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "\n=== fl::rope concat: effect of rebalance() ===\n";
    std::cout << "All timings are mean over repeated runs (ms).\n\n";

    // Column header
    std::cout << std::left
              << std::setw(12) << "N strings"
              << std::setw(10) << "str len"
              << std::setw(14) << "std::str (ms)"
              << std::setw(18) << "rope no-rebal (ms)"
              << std::setw(18) << "rope once  (ms)"
              << std::setw(18) << "rope per32 (ms)"
              << std::setw(14) << "no-rebal/std"
              << "once/std\n";
    std::cout << std::string(104, '-') << '\n';

    struct Case { int n; std::size_t len; int reps; };
    constexpr Case CASES[] = {
        {   100,    10, 200},
        {  1000,    10, 50},
        { 10000,    10, 10},
        {   100,   100, 200},
        {  1000,   100, 50},
        { 10000,   100, 10},
        {   100,  1000, 100},
        {  1000,  1000, 20},
        {  5000,  1000,  5},
    };

    for (auto& c : CASES) {
        Result r = bench(c.n, c.len, c.reps);
        std::cout << std::left << std::fixed << std::setprecision(3)
                  << std::setw(12) << c.n
                  << std::setw(10) << c.len
                  << std::setw(14) << r.std_ms
                  << std::setw(18) << r.rope_no_rebalance_ms
                  << std::setw(18) << r.rope_rebalance_once_ms
                  << std::setw(18) << r.rope_rebalance_periodic_ms
                  << std::setw(14) << std::setprecision(2)
                  << r.rope_no_rebalance_ms / r.std_ms
                  << std::setprecision(2)
                  << r.rope_rebalance_once_ms / r.std_ms
                  << '\n';
    }

    return 0;
}
