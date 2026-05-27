// File: benchmarks/multi_standard_bench.cpp
// Multi-standard benchmark for arena, rope, and builder
// Measures throughput (ops/sec) and allocation counts

#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cstring>
#include "fl/string.hpp"
#include "fl/arena.hpp"
#include "fl/rope.hpp"
#include "fl/builder.hpp"
#include "fl/config.hpp"

namespace {

// Simple allocation counter hook
static size_t g_alloc_count = 0;
static size_t g_dealloc_count = 0;
static size_t g_total_allocated = 0;

// Volatile sink to prevent compiler optimization (MSVC compatible)
static volatile void* g_sink = nullptr;
inline void do_not_optimize(void* p) {
    g_sink = p;
}

// Benchmark: Arena allocator
void benchmark_arena() {
    std::cout << "\n=== Arena Allocator Benchmark ===\n";
    
    const int iterations = 100000;
    const size_t alloc_size = 256;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        fl::arena_allocator<4096> arena;
        void* p = arena.allocate(alloc_size);
        do_not_optimize(p);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double ops_per_sec = (double)iterations * 1e6 / duration.count();
    
    std::cout << "Arena alloc/dealloc throughput: " 
              << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec\n"
              << "Time per iteration: " << std::fixed << std::setprecision(3) 
              << (double)duration.count() / iterations << " µs\n";
}

// Benchmark: Rope string concatenation
void benchmark_rope() {
    std::cout << "\n=== Rope String Benchmark ===\n";
    
    const int iterations = 10000;
    fl::rope base("test");
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        fl::rope r = base + "string";
        do_not_optimize(&r);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double ops_per_sec = (double)iterations * 1e6 / duration.count();
    
    std::cout << "Rope concatenation throughput: " 
              << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec\n"
              << "Time per concat: " << std::fixed << std::setprecision(3) 
              << (double)duration.count() / iterations << " µs\n";
}

// Benchmark: String builder
void benchmark_builder() {
    std::cout << "\n=== String Builder Benchmark ===\n";
    
    const int iterations = 100000;
    const std::string test_str = "hello";
    const std::string_view test_view(test_str);
    const std::string_view world_view("world");
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        fl::string_builder builder;
        builder.append(test_view);
        builder.append(' ');
        builder.append(world_view);
        do_not_optimize(builder.data());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double ops_per_sec = (double)iterations * 1e6 / duration.count();
    
    std::cout << "Builder append throughput: " 
              << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec\n"
              << "Time per build: " << std::fixed << std::setprecision(3) 
              << (double)duration.count() / iterations << " µs\n";
}

// Benchmark: Builder with iterators (tests C++11 compatibility)
void benchmark_builder_iterators() {
    std::cout << "\n=== String Builder Iterator Benchmark ===\n";
    
    const int iterations = 50000;
    const char* str = "test";
    const size_t len = std::strlen(str);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        fl::string_builder builder;
        builder.append(std::string_view(str, len));  // View-based append
        do_not_optimize(builder.data());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double ops_per_sec = (double)iterations * 1e6 / duration.count();
    
    std::cout << "Builder iterator append throughput: " 
              << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec\n"
              << "Time per append: " << std::fixed << std::setprecision(3) 
              << (double)duration.count() / iterations << " µs\n";
}

// Benchmark: Builder append_formatted (tests type dispatch)
void benchmark_builder_formatted() {
    std::cout << "\n=== String Builder append_formatted Benchmark ===\n";
    
    const int iterations = 50000;
    const std::string_view int_fmt("int: {}");
    const std::string_view float_fmt("float: {}");
    const std::string_view str_fmt("str: {}");
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        fl::string_builder builder;
        builder.append_formatted(int_fmt, 42);
        builder.append_formatted(float_fmt, 3.14);
        builder.append_formatted(str_fmt, std::string_view("test"));
        do_not_optimize(builder.data());
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    double ops_per_sec = (double)iterations * 1e6 / duration.count();
    
    std::cout << "Builder append_formatted throughput: " 
              << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec\n"
              << "Time per format call (3 formats): " << std::fixed << std::setprecision(3) 
              << (double)duration.count() / iterations << " µs\n";
}

}  // anonymous namespace

int main() {
    std::cout << "========================================\n";
    std::cout << "Multi-Standard C++ Benchmarks\n";
    std::cout << "========================================\n";
    
    try {
        benchmark_arena();
        benchmark_rope();
        benchmark_builder();
        benchmark_builder_iterators();
        benchmark_builder_formatted();
        
        std::cout << "\n========================================\n";
        std::cout << "✓ BENCHMARKS COMPLETED\n";
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ BENCHMARK FAILED: " << e.what() << "\n";
        return 1;
    }
}
