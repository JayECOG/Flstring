// Benchmark: find-substring throughput across haystack sizes 256 B – 4 MB.
//
// Two entropy regimes are tested:
//
//   High-entropy: uniform random lowercase ASCII.  glibc memmem's AVX2 window
//   skips ~256 bytes per comparison on average — it dominates in this regime.
//
//   Low-entropy: haystack is all 'a', needle is (m-1) 'a' + 'b'.  Every
//   position matches the first (m-1) characters of the needle, forcing memmem
//   into O(n·m) worst-case scan.  The Two-Way algorithm's critical factorization
//   uses the period to skip left-half rescans, giving true O(n+m).
//
// For each cell the needle is planted at three positions:
//   early (10%), mid (50%), late (90%).
//
// Small haystacks (≤ 4 KB): 500 000 iterations.
// Large haystacks (≥ 64 KB): 1 000 iterations (per-op cost is ≥ 10 000× higher).

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "fl/string.hpp"

// ---------------------------------------------------------------------------
struct Timer {
    std::chrono::high_resolution_clock::time_point t0;
    Timer() : t0(std::chrono::high_resolution_clock::now()) {}
    double elapsed_ns() const {
        using namespace std::chrono;
        return duration<double, std::nano>(high_resolution_clock::now() - t0).count();
    }
};

static volatile std::size_t sink_sz;
static void sink(std::size_t v) { sink_sz = v; }

// ---------------------------------------------------------------------------
// High-entropy haystack: uniform random lowercase, avoiding needle[0] to
// prevent accidental false matches outside the planted position.
// ---------------------------------------------------------------------------
static std::string make_haystack_random(std::size_t size,
                                         std::string_view needle,
                                         std::size_t plant_at) {
    std::mt19937 rng(0xBEEFCAFE);
    char avoid = needle.empty() ? 'x' : needle[0];
    std::string h(size, 'a');
    for (char& c : h) {
        char cand = 'a' + static_cast<char>(rng() % 26);
        c = (cand == avoid) ? (cand == 'z' ? 'a' : cand + 1) : cand;
    }
    if (!needle.empty() && plant_at + needle.size() <= size)
        std::memcpy(&h[plant_at], needle.data(), needle.size());
    return h;
}

// ---------------------------------------------------------------------------
// Low-entropy haystack: all 'a'.  Needle designed as (m-1) × 'a' + 'b' so
// memmem cannot use the 2-byte window shortcut — it re-scans from every 'a'.
// The planted needle is the actual (m-1 × 'a' + 'b') at plant_at.
// ---------------------------------------------------------------------------
static std::string make_haystack_periodic(std::size_t size,
                                           std::size_t needle_len,
                                           std::size_t plant_at) {
    std::string h(size, 'a');
    // Plant a single needle at plant_at: (needle_len-1) 'a' + 'b'
    if (needle_len > 0 && plant_at + needle_len <= size) {
        // all-'a' prefix already there; just write the terminating 'b'
        h[plant_at + needle_len - 1] = 'b';
    }
    return h;
}

// Build the low-entropy needle: (needle_len-1) × 'a' + 'b'
static std::string make_periodic_needle(std::size_t len) {
    if (len == 0) return {};
    std::string n(len, 'a');
    n.back() = 'b';
    return n;
}

// ---------------------------------------------------------------------------
// Run one cell.  Returns {std_ns_per_iter, fl_ns_per_iter}.
// ---------------------------------------------------------------------------
static std::pair<double, double> bench_find(const std::string& haystack_str,
                                             std::string_view needle_sv,
                                             int iters) {
    fl::string haystack_fl(haystack_str.data(), haystack_str.size());
    const std::string& haystack_std = haystack_str;
    const std::string needle_std(needle_sv);

    // Warm-up (capped to keep large-haystack bench from taking too long)
    const int warmup = std::min(iters / 10, 200);
    for (int i = 0; i < warmup; ++i) {
        sink(haystack_std.find(needle_std));
        sink(haystack_fl.find(needle_sv));
    }

    double std_ns;
    {
        Timer t;
        for (int i = 0; i < iters; ++i)
            sink(haystack_std.find(needle_std));
        std_ns = t.elapsed_ns() / iters;
    }

    double fl_ns;
    {
        Timer t;
        for (int i = 0; i < iters; ++i)
            sink(haystack_fl.find(needle_sv));
        fl_ns = t.elapsed_ns() / iters;
    }

    return {std_ns, fl_ns};
}

// ---------------------------------------------------------------------------
// Print header
// ---------------------------------------------------------------------------
static void print_header(const char* title) {
    std::cout << "\n=== " << title << " ===\n";
    std::cout << std::left
              << std::setw(10) << "Haystack"
              << std::setw(12) << "Needle"
              << std::setw(10) << "Position"
              << std::setw(14) << "std (ns/op)"
              << std::setw(14) << "fl  (ns/op)"
              << std::setw(12) << "fl/std"
              << "Winner\n";
    std::cout << std::string(72, '-') << '\n';
}

static void print_row(std::size_t hsz, const char* needle_label,
                       const char* pos_label, double std_ns, double fl_ns) {
    double ratio = fl_ns / std_ns;
    const char* winner = (ratio < 0.93) ? "fl wins"
                       : (ratio > 1.07) ? "std wins"
                                        : "parity";
    std::cout << std::left
              << std::setw(10) << hsz
              << std::setw(12) << needle_label
              << std::setw(10) << pos_label
              << std::setw(14) << std::fixed << std::setprecision(2) << std_ns
              << std::setw(14) << fl_ns
              << std::setw(12) << std::setprecision(3) << ratio
              << winner << '\n';
}

// ---------------------------------------------------------------------------
int main() {
    // ---- Small haystacks: high-entropy, matches Pass 2 behaviour ----
    {
        constexpr std::size_t SMALL_SIZES[] = {256, 512, 1024, 2048, 4096};
        constexpr struct { const char* text; const char* label; } NEEDLES[] = {
            {"fox",          "3-char"},
            {"jumps over",   "10-char"},
            {"lazy dog end", "12-char"},
        };
        constexpr int ITERS = 500000;

        print_header("find() throughput — small haystacks 256–4096 B (high-entropy)");
        std::cout << "Iterations per cell: " << ITERS << "\n\n";

        for (std::size_t hsz : SMALL_SIZES) {
            for (auto& [needle_text, needle_label] : NEEDLES) {
                std::string_view needle(needle_text);
                std::size_t nlen = needle.size();
                for (auto [pos_label, frac] :
                         std::initializer_list<std::pair<const char*, double>>{
                             {"early", 0.10}, {"mid", 0.50}, {"late", 0.90}}) {
                    std::size_t plant =
                        std::min(static_cast<std::size_t>(hsz * frac),
                                 hsz > nlen ? hsz - nlen : 0u);
                    std::string h = make_haystack_random(hsz, needle, plant);
                    auto [s, f] = bench_find(h, needle, ITERS);
                    print_row(hsz, needle_label, pos_label, s, f);
                }
            }
            std::cout << '\n';
        }
    }

    // ---- Large haystacks: high-entropy ----
    {
        // 25-char needle — same length used in the Pass 3 two-way benchmarks.
        const std::string needle_text = "abcdefghijklmnopqrstuvwxy"; // 25 chars, all unique
        const char* needle_label = "25-char";
        constexpr std::size_t LARGE_SIZES[] = {65536, 131072, 524288, 1048576, 4194304};
        constexpr int ITERS = 1000;

        print_header("find() throughput — large haystacks 64 KB–4 MB (high-entropy)");
        std::cout << "Needle: 25-char unique-char string.  Iterations per cell: " << ITERS << "\n\n";

        for (std::size_t hsz : LARGE_SIZES) {
            for (auto [pos_label, frac] :
                     std::initializer_list<std::pair<const char*, double>>{
                         {"early", 0.10}, {"mid", 0.50}, {"late", 0.90}}) {
                std::size_t plant =
                    std::min(static_cast<std::size_t>(hsz * frac),
                             hsz > needle_text.size() ? hsz - needle_text.size() : 0u);
                std::string h = make_haystack_random(hsz, needle_text, plant);
                auto [s, f] = bench_find(h, needle_text, ITERS);
                print_row(hsz, needle_label, pos_label, s, f);
            }
            std::cout << '\n';
        }
    }

    // ---- Large haystacks: low-entropy (periodic worst-case for memmem) ----
    {
        // Needle length 25 chars: 24 × 'a' + 'b'.  Haystack: all 'a'.
        // Every haystack position partially matches, so memmem degrades to O(n·m);
        // the Two-Way algorithm's memory skips the left-half rescan every iteration.
        const std::size_t nlen = 25;
        const std::string needle_text = make_periodic_needle(nlen);
        const char* needle_label = "25-char";
        constexpr std::size_t LARGE_SIZES[] = {65536, 131072, 524288, 1048576};
        constexpr int ITERS = 500; // memmem is O(n·m) here; 1 MB × 25 ≈ 25 Mops each

        print_header("find() throughput — large haystacks 64 KB–1 MB (low-entropy / periodic)");
        std::cout << "Needle: 24×'a'+'b', haystack: all 'a'.  memmem is O(n*m) worst case.\n";
        std::cout << "Iterations per cell: " << ITERS << "\n\n";

        for (std::size_t hsz : LARGE_SIZES) {
            for (auto [pos_label, frac] :
                     std::initializer_list<std::pair<const char*, double>>{
                         {"early", 0.10}, {"mid", 0.50}, {"late", 0.90}}) {
                std::size_t plant =
                    std::min(static_cast<std::size_t>(hsz * frac),
                             hsz > nlen ? hsz - nlen : 0u);
                std::string h = make_haystack_periodic(hsz, nlen, plant);
                auto [s, f] = bench_find(h, needle_text, ITERS);
                print_row(hsz, needle_label, pos_label, s, f);
            }
            std::cout << '\n';
        }
    }

    return 0;
}
