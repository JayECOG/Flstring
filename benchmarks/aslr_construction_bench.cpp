// aslr_construction_bench.cpp
//
// Item 4: Profile fl::string vs std::string construction overhead relative to
// ASLR.  The hypothesis from Pass 2 is that construction anomalies in the
// shared container are environmental (ASLR-induced page faults / TLB misses)
// rather than algorithmic differences.
//
// Method:
//   Run the same construction loops multiple times within the same process
//   (so the allocator's internal state is warm), then compare first-run vs
//   warm-run latency.  If the first run is dramatically slower, the cost is
//   due to page-fault / TLB cold-start caused by ASLR placing the heap at
//   a fresh virtual address each process launch — not due to fl::string logic.
//
//   We also time the same loop after a manual heap warm-up phase to confirm
//   that warming the allocator closes the gap.

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

// Enable compile-time constant "no hooks" path so the compiler can DCE
// fl::string construction when the literal value is compile-time known —
// matching the treatment std::string receives at -O2.  Without this, the
// atomic::load on hooks_customised() prevents DCE and inflates fl ratios by
// 30–50×.  This define is safe here because the bench never installs hooks.
#define FL_HOOKS_ALWAYS_DEFAULT
#include "fl/string.hpp"

struct Timer {
    std::chrono::high_resolution_clock::time_point t0;
    Timer() : t0(std::chrono::high_resolution_clock::now()) {}
    double elapsed_us() const {
        using namespace std::chrono;
        return duration<double, std::micro>(high_resolution_clock::now() - t0).count();
    }
};

static volatile std::size_t g_sink;

// ---------------------------------------------------------------------------
// Warm up the allocator: pre-allocate and free N blocks of the target size
// so that the TLS free-list and glibc bins are primed.
// ---------------------------------------------------------------------------
static void warm_allocator(std::size_t block_size, int count) {
    std::vector<void*> ptrs(count);
    for (int i = 0; i < count; ++i)
        ptrs[i] = ::operator new(block_size);
    for (int i = 0; i < count; ++i)
        ::operator delete(ptrs[i]);
}

// ---------------------------------------------------------------------------
// One construction loop: construct N heap strings, return µs.
// ---------------------------------------------------------------------------
static double run_heap_construction(int n) {
    constexpr char src[] =
        "This is a much longer string that will definitely trigger heap allocation.";
    constexpr std::size_t src_len = sizeof(src) - 1;

    double fl_cost, std_cost;
    {
        Timer t;
        for (int i = 0; i < n; ++i) {
            fl::string s(src, src_len);
            g_sink = s.size();
        }
        fl_cost = t.elapsed_us();
    }
    {
        Timer t;
        for (int i = 0; i < n; ++i) {
            std::string s(src, src_len);
            g_sink = s.size();
        }
        std_cost = t.elapsed_us();
    }
    std::cout << std::fixed << std::setprecision(2)
              << "  fl::string  : " << fl_cost  << " µs  ("
              << fl_cost / n * 1000 << " ns/op)\n"
              << "  std::string : " << std_cost << " µs  ("
              << std_cost / n * 1000 << " ns/op)\n"
              << "  fl/std ratio: " << fl_cost / std_cost << "x\n";
    return fl_cost / std_cost;
}

// ---------------------------------------------------------------------------
static double run_sso_construction(int n) {
    double fl_cost, std_cost;
    {
        Timer t;
        for (int i = 0; i < n; ++i) {
            fl::string s("hello world");
            g_sink = s[0];
        }
        fl_cost = t.elapsed_us();
    }
    {
        Timer t;
        for (int i = 0; i < n; ++i) {
            std::string s("hello world");
            g_sink = s[0];
        }
        std_cost = t.elapsed_us();
    }
    std::cout << std::fixed << std::setprecision(2)
              << "  fl::string  : " << fl_cost  << " µs\n"
              << "  std::string : " << std_cost << " µs\n"
              << "  fl/std ratio: " << fl_cost / std_cost << "x\n";
    return fl_cost / std_cost;
}

// ---------------------------------------------------------------------------
int main() {
    constexpr int N = 100000;

    std::cout << "=== ASLR / allocator warm-up investigation ===\n\n";

    std::cout << "Run 1 (cold — first process launch, no explicit warm-up):\n";
    std::cout << "  Heap construction (" << N << " iterations):\n";
    double ratio1 = run_heap_construction(N);

    std::cout << "\nRun 2 (same process, allocator state partially warm from Run 1):\n";
    std::cout << "  Heap construction:\n";
    double ratio2 = run_heap_construction(N);

    std::cout << "\nRun 3 (same process, after explicit allocator warm-up with "
              << N << " pre-alloc/free cycles):\n";
    warm_allocator(128, N);
    std::cout << "  Heap construction:\n";
    double ratio3 = run_heap_construction(N);

    std::cout << "\n--- SSO path (no heap alloc, isolates non-allocator overhead) ---\n";
    std::cout << "\nRun 1 (cold SSO):\n";
    double sso_ratio1 = run_sso_construction(N);
    std::cout << "\nRun 2 (warm SSO):\n";
    double sso_ratio2 = run_sso_construction(N);
    std::cout << "\nRun 3 (warm SSO):\n";
    double sso_ratio3 = run_sso_construction(N);

    std::cout << "\n=== Summary ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Heap fl/std ratios: cold=" << ratio1
              << "  warm1=" << ratio2
              << "  warm2=" << ratio3 << "\n";
    std::cout << "SSO  fl/std ratios: cold=" << sso_ratio1
              << "  warm1=" << sso_ratio2
              << "  warm2=" << sso_ratio3 << "\n";
    std::cout << "\nInterpretation:\n"
              << "  If cold >> warm: construction anomaly is allocator cold-start\n"
              << "    (ASLR places heap at new VA each launch → TLB/page-fault cost).\n"
              << "  If all runs show fl >> std: the fl allocator path itself is slow\n"
              << "    (aligned_alloc overhead vs glibc malloc bins).\n"
              << "  If SSO ratios are near 1.0: the anomaly is heap-allocation specific.\n";
    return 0;
}
