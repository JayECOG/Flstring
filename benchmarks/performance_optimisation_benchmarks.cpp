#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <random>
#include <numeric>

#include "../include/fl.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Simple timer utility
class Timer {
public:
    Timer() : start(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
    
    double elapsed_us() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(end - start).count();
    }
    
private:
    std::chrono::high_resolution_clock::time_point start;
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
    std::uniform_int_distribution<> distribution(0, sizeof(charset) - 2); // -2 for null terminator and last char
    for (size_t i = 0; i < length; ++i) {
        result[i] = charset[distribution(generator)];
    }
    return result;
}

int main(int argc, char** argv) {
    std::cout << "=== fl library Performance Optimisation Benchmarks ===\n" << std::endl;

    int SUBSTR_ITER = 50000;
    int BUFFER_ITER = 10000;
    int BUILDER_ITER = 10000;
    int COPY_ITER = 100000;

    int cpu_affinity = -1;
    int scale = 1;
    for (int ai = 1; ai < argc; ++ai) {
        std::string a(argv[ai]);
        if (a.rfind("--cpu=", 0) == 0) {
            try { cpu_affinity = std::stoi(a.substr(6)); } catch(...) { }
        } else if (a.rfind("--scale=", 0) == 0) {
            try { scale = std::stoi(a.substr(8)); if (scale < 1) scale = 1; } catch(...) { }
        }
    }

    if (scale > 1) {
        SUBSTR_ITER *= scale;
        BUFFER_ITER *= scale;
        BUILDER_ITER *= scale;
        COPY_ITER *= scale;
    }

    if (cpu_affinity >= 0) {
#ifdef _WIN32
        DWORD_PTR mask = (DWORD_PTR)1 << cpu_affinity;
        SetProcessAffinityMask(GetCurrentProcess(), mask);
#endif
    }

    // Benchmark 1: fl::string::substr vs std::string::substr and fl::substring_view
    {
        std::cout << "1. Substring Operations (" << SUBSTR_ITER << " iterations):\n";
        std::string long_std_string = generate_random_string(256);
        fl::string long_fl_string(long_std_string.c_str());

        // std::string::substr (creates new std::string)
        {
            Timer timer;
            for (int i = 0; i < SUBSTR_ITER; ++i) {
                std::string sub = long_std_string.substr(50, 100);
                volatile auto _ = sub.length();
                (void)_;
            }
            std::cout << "  std::string::substr (returns std::string):   " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n";
        }

        // fl::string::substr (creates new fl::string)
        {
            Timer timer;
            for (int i = 0; i < SUBSTR_ITER; ++i) {
                fl::string sub = long_fl_string.substr(50, 100);
                volatile auto _ = sub.length();
                (void)_;
            }
            std::cout << "  fl::string::substr (returns fl::string):     " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n";
        }

        // std::string_view from std::string
        {
            Timer timer;
            for (int i = 0; i < SUBSTR_ITER; ++i) {
                std::string_view sv = std::string_view(long_std_string).substr(50, 100);
                volatile auto _ = sv.length();
                (void)_;
            }
            std::cout << "  std::string_view from std::string:           " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n";
        }
        
        // fl::substring_view from fl::string
        {
            Timer timer;
            for (int i = 0; i < SUBSTR_ITER; ++i) {
                fl::substring_view fsv = long_fl_string.substr_view(50, 100);
                volatile auto _ = fsv.length();
                (void)_;
            }
            std::cout << "  fl::substring_view from fl::string:          " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n\n";
        }
    }

    // Benchmark 2: fl::temp_buffer with and without pooling
    {
        std::cout << "2. fl::temp_buffer Pooling (" << BUFFER_ITER << " iterations, 10 appends of 50 chars):\n";
        std::string append_data = generate_random_string(50);

        // Without pooling (direct arena_buffer construction)
        {
            Timer timer;
            for (int i = 0; i < BUFFER_ITER; ++i) {
                fl::arena_buffer<fl::detail::DEFAULT_ARENA_STACK_SIZE> buf; // Explicitly use default stack size
                for (int j = 0; j < 10; ++j) {
                    buf.append(append_data.c_str());
                }
                volatile auto _ = buf.to_string().size();
                (void)_;
            }
            std::cout << "  fl::arena_buffer (without pooling): " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n";
        }

        // With pooling (using get_pooled_temp_buffer)
        {
            Timer timer;
            for (int i = 0; i < BUFFER_ITER; ++i) {
                fl::temp_buffer buf = fl::get_pooled_temp_buffer();
                for (int j = 0; j < 10; ++j) {
                    buf->append(append_data.c_str());
                }
                volatile auto _ = buf->to_string().size();
                (void)_;
            }
            std::cout << "  fl::temp_buffer (with pooling):   " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n\n";
        }
    }

    // Benchmark 3: fl::string_builder growth strategy
    {
        std::cout << "3. fl::string_builder Growth (" << BUILDER_ITER << " iterations, building 1KB string):\n";
        std::string small_append_data = generate_random_string(10); // Append 10 bytes at a time

        // fl::string_builder (default aggressive exponential growth)
        {
            Timer timer;
            for (int i = 0; i < BUILDER_ITER; ++i) {
                fl::string_builder sb;
                for (int j = 0; j < 100; ++j) { // Append 100 times to reach 1KB
                    sb.append(small_append_data.c_str());
                }
                fl::string s = std::move(sb).build();
                volatile auto _ = s.length();
                (void)_;
            }
            std::cout << "  fl::string_builder (aggressive growth): " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n";
        }

        // std::string (repeated appends for comparison)
        {
            Timer timer;
            for (int i = 0; i < BUILDER_ITER; ++i) {
                std::string s;
                for (int j = 0; j < 100; ++j) {
                    s.append(small_append_data);
                }
                volatile auto _ = s.length();
                (void)_;
            }
            std::cout << "  std::string (repeated appends):         " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n\n";
        }
    }

    // Benchmark 4: fl::string copy construction (to validate H002)
    {
        std::cout << "4. String Copy Construction (" << COPY_ITER << " iterations, 100 char string):\n";
        std::string source_std_string = generate_random_string(100);
        fl::string source_fl_string(source_std_string.c_str());

        // std::string copy construction
        {
            Timer timer;
            for (int i = 0; i < COPY_ITER; ++i) {
                std::string s = source_std_string;
                volatile auto _ = s.length();
                (void)_;
            }
            std::cout << "  std::string copy construction: " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n";
        }

        // fl::string copy construction
        {
            Timer timer;
            for (int i = 0; i < COPY_ITER; ++i) {
                fl::string s = source_fl_string;
                volatile auto _ = s.length();
                (void)_;
            }
            std::cout << "  fl::string copy construction:  " << std::fixed << std::setprecision(3) << timer.elapsed_ms() << " ms\n\n";
        }
    }

    std::cout << "=== Benchmark Summary ===\n";
    std::cout << "Analysis of results should be performed to compare fl::string and std::string performance.\n";

    return 0;
}
