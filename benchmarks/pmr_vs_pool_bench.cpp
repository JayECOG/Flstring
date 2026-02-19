// Benchmark: std::pmr::monotonic_buffer_resource vs fl's custom pool allocator.
//
// fl::string uses an aligned pool allocator for heap strings.  This benchmark
// compares three allocation strategies for the same workload:
//
//   A) fl::string with default fl pool allocator
//   B) fl::string with std::pmr::monotonic_buffer_resource (arena per iteration)
//   C) std::pmr::string with monotonic_buffer_resource
//   D) std::string with global malloc (baseline)
//
// Workloads:
//   1. Build-and-destroy: construct N heap strings, discard all (alloc pressure)
//   2. Repeated append: grow a single string to ~1 KB
//   3. Mixed: create 32 strings, append to each, then discard
//
// The PMR path for fl::string hooks fl::set_alloc_hooks() to forward calls
// into an upstream PMR resource.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory_resource>
#include <random>
#include <string>
#include <vector>

#include "fl/string.hpp"

// ---------------------------------------------------------------------------
struct Timer {
    std::chrono::high_resolution_clock::time_point t0;
    Timer() : t0(std::chrono::high_resolution_clock::now()) {}
    double elapsed_us() const {
        using namespace std::chrono;
        return duration<double, std::micro>(high_resolution_clock::now() - t0).count();
    }
};

static volatile std::size_t g_sink;
static void sink(std::size_t v) { g_sink = v; }

// ---------------------------------------------------------------------------
// PMR hook adapter for fl::string.
//
// We keep a thread_local pointer to the active resource; the hook functions
// read it. When benchmarking the PMR path we set the resource before the run.
// ---------------------------------------------------------------------------
namespace {

thread_local std::pmr::memory_resource* tl_pmr_resource = nullptr;

void* pmr_alloc(std::size_t n) {
    return tl_pmr_resource->allocate(n, alignof(std::max_align_t));
}
void pmr_dealloc(void* p, std::size_t n) {
    tl_pmr_resource->deallocate(p, n, alignof(std::max_align_t));
}

// Scoped RAII: installs/removes PMR hooks for fl::string.
struct PmrHookGuard {
    std::pmr::memory_resource* res;
    explicit PmrHookGuard(std::pmr::memory_resource* r) : res(r) {
        tl_pmr_resource = r;
        fl::set_alloc_hooks(pmr_alloc, pmr_dealloc);
    }
    ~PmrHookGuard() {
        fl::set_alloc_hooks(nullptr, nullptr); // restore defaults
        tl_pmr_resource = nullptr;
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Workload 1: Build-and-destroy N heap strings (each ~100 chars)
// ---------------------------------------------------------------------------
static double bench_build_destroy_fl(int n, int iters) {
    constexpr char src[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-+=!@";
    constexpr std::size_t src_len = sizeof(src) - 1;
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        Timer t;
        for (int i = 0; i < n; ++i) {
            fl::string s(src, src_len);
            sink(s.size());
        }
        total += t.elapsed_us();
    }
    return total / iters;
}

static double bench_build_destroy_fl_pmr(int n, int iters) {
    constexpr char src[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-+=!@";
    constexpr std::size_t src_len = sizeof(src) - 1;
    // Size the buffer generously
    const std::size_t buf_sz = static_cast<std::size_t>(n) * 128;
    std::vector<std::byte> buf(buf_sz);
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size(),
                                                std::pmr::null_memory_resource());
        PmrHookGuard guard(&mbr);
        Timer t;
        for (int i = 0; i < n; ++i) {
            fl::string s(src, src_len);
            sink(s.size());
        }
        total += t.elapsed_us();
        // mbr freed implicitly; destructor resets buffer
    }
    return total / iters;
}

static double bench_build_destroy_std(int n, int iters) {
    constexpr char src[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-+=!@";
    constexpr std::size_t src_len = sizeof(src) - 1;
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        Timer t;
        for (int i = 0; i < n; ++i) {
            std::string s(src, src_len);
            sink(s.size());
        }
        total += t.elapsed_us();
    }
    return total / iters;
}

static double bench_build_destroy_pmr_string(int n, int iters) {
    constexpr char src[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-+=!@";
    constexpr std::size_t src_len = sizeof(src) - 1;
    const std::size_t buf_sz = static_cast<std::size_t>(n) * 128;
    std::vector<std::byte> buf(buf_sz);
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size(),
                                                std::pmr::null_memory_resource());
        Timer t;
        for (int i = 0; i < n; ++i) {
            std::pmr::string s(src, src_len, &mbr);
            sink(s.size());
        }
        total += t.elapsed_us();
    }
    return total / iters;
}

// ---------------------------------------------------------------------------
// Workload 2: Grow a single string to ~1 KB via repeated appends of 4 chars
// ---------------------------------------------------------------------------
static double bench_append_fl(int iters) {
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        Timer t;
        fl::string s;
        for (int j = 0; j < 256; ++j) s.append("data");
        sink(s.size());
        total += t.elapsed_us();
    }
    return total / iters;
}

static double bench_append_fl_pmr(int iters) {
    // One buffer per outer iteration; resets each time
    const std::size_t buf_sz = 4096;
    std::vector<std::byte> buf(buf_sz);
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size(),
                                                std::pmr::new_delete_resource());
        PmrHookGuard guard(&mbr);
        Timer t;
        fl::string s;
        for (int j = 0; j < 256; ++j) s.append("data");
        sink(s.size());
        total += t.elapsed_us();
    }
    return total / iters;
}

static double bench_append_std(int iters) {
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        Timer t;
        std::string s;
        for (int j = 0; j < 256; ++j) s.append("data");
        sink(s.size());
        total += t.elapsed_us();
    }
    return total / iters;
}

static double bench_append_pmr_string(int iters) {
    const std::size_t buf_sz = 4096;
    std::vector<std::byte> buf(buf_sz);
    double total = 0;
    for (int it = 0; it < iters; ++it) {
        std::pmr::monotonic_buffer_resource mbr(buf.data(), buf.size(),
                                                std::pmr::new_delete_resource());
        Timer t;
        std::pmr::string s(&mbr);
        for (int j = 0; j < 256; ++j) s.append("data");
        sink(s.size());
        total += t.elapsed_us();
    }
    return total / iters;
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "\n=== Allocator comparison: fl pool vs std::pmr::monotonic_buffer_resource ===\n\n";

    // Workload 1: build-and-destroy
    {
        constexpr int N = 1000;
        constexpr int ITERS = 500;
        double fl_pool  = bench_build_destroy_fl(N, ITERS);
        double fl_pmr   = bench_build_destroy_fl_pmr(N, ITERS);
        double std_heap = bench_build_destroy_std(N, ITERS);
        double pmr_str  = bench_build_destroy_pmr_string(N, ITERS);

        std::cout << "Workload 1: Build-and-destroy " << N
                  << " heap strings (~70 chars each), " << ITERS << " runs\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  fl::string + fl pool        : " << fl_pool  << " µs/run\n";
        std::cout << "  fl::string + pmr monotonic  : " << fl_pmr   << " µs/run\n";
        std::cout << "  std::string (global malloc) : " << std_heap << " µs/run\n";
        std::cout << "  std::pmr::string + monotonic: " << pmr_str  << " µs/run\n";
        std::cout << "  Ratios (vs fl pool):  fl_pmr="
                  << fl_pmr / fl_pool << "x  std=" << std_heap / fl_pool
                  << "x  pmr_str=" << pmr_str / fl_pool << "x\n\n";
    }

    // Workload 2: repeated append to ~1 KB
    {
        constexpr int ITERS = 100000;
        double fl_pool  = bench_append_fl(ITERS);
        double fl_pmr   = bench_append_fl_pmr(ITERS);
        double std_heap = bench_append_std(ITERS);
        double pmr_str  = bench_append_pmr_string(ITERS);

        std::cout << "Workload 2: Grow string to 1 KB via 256x append(\"data\"), "
                  << ITERS << " runs\n";
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "  fl::string + fl pool        : " << fl_pool  << " µs/run\n";
        std::cout << "  fl::string + pmr monotonic  : " << fl_pmr   << " µs/run\n";
        std::cout << "  std::string (global malloc) : " << std_heap << " µs/run\n";
        std::cout << "  std::pmr::string + monotonic: " << pmr_str  << " µs/run\n";
        std::cout << "  Ratios (vs fl pool):  fl_pmr="
                  << fl_pmr / fl_pool << "x  std=" << std_heap / fl_pool
                  << "x  pmr_str=" << pmr_str / fl_pool << "x\n\n";
    }

    return 0;
}
