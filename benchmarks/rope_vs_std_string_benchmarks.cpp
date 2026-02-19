#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <random>
#include <fstream>
#include <ctime>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

#include "fl.hpp"

// Simple timer utility
class Timer {
public:
    Timer() : start_wall(std::chrono::steady_clock::now()), start_cpu(std::clock()) {}
    
    double elapsed_ms() const {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_wall).count();
    }
    
    double elapsed_us() const {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::micro>(end - start_wall).count();
    }

    double elapsed_cpu_ms() const {
        return (std::clock() - start_cpu) / (CLOCKS_PER_SEC / 1000.0);
    }
    
private:
    std::chrono::steady_clock::time_point start_wall;
    std::clock_t start_cpu;
};

// Helper to generate a random string of a given length
std::string generate_random_string(size_t length) {
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::string result;
    result.resize(length);
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<> distribution(0, sizeof(charset) - 2);
    for (size_t i = 0; i < length; ++i) {
        result[i] = charset[distribution(generator)];
    }
    return result;
}

// Get current memory usage in MB
double get_memory_usage_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

int main() {
    std::ofstream out("benchmark_output.txt");
    if (!out) {
        std::cerr << "Failed to open output file\n";
        return 1;
    }
    out << "=== fl::rope vs std::string Benchmarks ===\n" << std::endl;

    const int CONCAT_ITER = 10000;  // Number of concatenations
    const int ACCESS_ITER = 100000; // Number of character accesses
    const int SUBSTR_ITER = 10000;  // Number of substring operations
    const int FLATTEN_ITER = 100;   // Number of flatten operations for large ropes

    // Benchmark 1: Concatenation of small strings
    {
        out << "1. Concatenation of Small Strings (" << CONCAT_ITER << " concatenations of 10-char strings):\n";
        std::vector<std::string> small_strings;
        std::vector<fl::rope> small_ropes;
        for (int i = 0; i < CONCAT_ITER; ++i) {
            std::string s = generate_random_string(10);
            small_strings.push_back(s);
            small_ropes.push_back(fl::rope(s.c_str()));
        }

        // std::string concatenation
        {
            Timer timer;
            std::string result;
            for (const auto& s : small_strings) {
                result += s;
            }
            volatile auto _ = result.length();
            (void)_;
            out << "  std::string concatenation: " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n";
        }

        // fl::rope concatenation
        {
            Timer timer;
            fl::rope result;
            for (const auto& r : small_ropes) {
                result += r;
            }
            result.rebalance();
            volatile auto _ = result.length();
            (void)_;
            out << "  fl::rope concatenation:    " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n\n";
        }
    }

    // Benchmark 2: Character Access
    {
        out << "2. Character Access (" << ACCESS_ITER << " random accesses in a 1MB string):\n";
        std::string large_std = generate_random_string(1000000); // 1MB
        fl::rope large_rope(large_std.c_str(), large_std.length());

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 999999);

        // std::string access
        {
            Timer timer;
            volatile char c;
            for (int i = 0; i < ACCESS_ITER; ++i) {
                size_t pos = dis(gen);
                c = large_std[pos];
            }
            (void)c;
            out << "  std::string access: " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n";
        }

        // fl::rope access
        {
            Timer timer;
            volatile char c;
            for (int i = 0; i < ACCESS_ITER; ++i) {
                size_t pos = dis(gen);
                c = large_rope[pos];
            }
            (void)c;
            out << "  fl::rope access:    " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n\n";
        }
    }

    // Benchmark 3: Substring Operations
    {
        out << "3. Substring Operations (" << SUBSTR_ITER << " substrings of 100 chars from 10KB string):\n";
        std::string medium_std = generate_random_string(10000); // 10KB
        fl::rope medium_rope(medium_std.c_str(), medium_std.length());

        // std::string substr
        {
            Timer timer;
            for (int i = 0; i < SUBSTR_ITER; ++i) {
                std::string sub = medium_std.substr(100, 100);
                volatile auto _ = sub.length();
                (void)_;
            }
            out << "  std::string::substr: " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n";
        }

        // fl::rope substr (returns substring_view)
        {
            Timer timer;
            for (int i = 0; i < SUBSTR_ITER; ++i) {
                fl::substring_view sub = medium_rope.substr(100, 100);
                volatile auto _ = sub.length();
                (void)_;
            }
            out << "  fl::rope::substr:    " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n\n";
        }
    }

    // Benchmark 4: Flattening/Linerization
    {
        out << "4. Flattening/Linerization (" << FLATTEN_ITER << " times for a rope built from 1000 small strings):\n";
        
        // Build a large rope
        fl::rope large_rope;
        for (int i = 0; i < 1000; ++i) {
            large_rope += fl::rope(generate_random_string(100).c_str());
        }
        large_rope.rebalance();

        // Build equivalent std::string
        std::string large_std;
        for (int i = 0; i < 1000; ++i) {
            large_std += generate_random_string(100);
        }

        // std::string is already linear, so "flattening" is just copying
        {
            Timer timer;
            for (int i = 0; i < FLATTEN_ITER; ++i) {
                std::string copy = large_std; // Simulate flattening
                volatile auto _ = copy.length();
                (void)_;
            }
            out << "  std::string copy: " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n";
        }

        // fl::rope flatten
        {
            Timer timer;
            for (int i = 0; i < FLATTEN_ITER; ++i) {
                fl::string flattened = large_rope.flatten();
                volatile auto _ = flattened.length();
                (void)_;
            }
            out << "  fl::rope::flatten: " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n\n";
        }
    }

    // Benchmark 5: Equality Comparison
    {
        out << "5. Equality Comparison (comparing two 100KB strings):\n";
        std::string str1 = generate_random_string(100000);
        std::string str2 = str1; // Identical
        fl::rope rope1(str1.c_str(), str1.length());
        fl::rope rope2(str2.c_str(), str2.length());

        const int COMP_ITER = 1000;

        // std::string comparison
        {
            Timer timer;
            volatile bool eq;
            for (int i = 0; i < COMP_ITER; ++i) {
                eq = (str1 == str2);
            }
            (void)eq;
            out << "  std::string ==: " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n";
        }

        // fl::rope comparison
        {
            Timer timer;
            volatile bool eq;
            for (int i = 0; i < COMP_ITER; ++i) {
                eq = (rope1 == rope2);
            }
            (void)eq;
            out << "  fl::rope ==:    " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms (CPU: " << timer.elapsed_cpu_ms() << " ms)\n";
            out << "  Memory usage: " << get_memory_usage_mb() << " MB\n\n";
        }
    }

    out << "Benchmarks completed.\n";
    return 0;
}