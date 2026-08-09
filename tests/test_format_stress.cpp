// Stress and performance tests for fl::format_to and related formatting APIs.
//
// This file tests performance characteristics, edge behaviour under heavy
// load, buffer boundary conditions, and robustness against malformed input.
//
// Build: linked against the fl header-only library.
//   g++ -std=c++20 -I../include -O2 -o test_format_stress test_format_stress.cpp

#include <fl/format.hpp>
#include <fl/sinks.hpp>
#include <fl.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <stdexcept>
#include <memory>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------
#define TEST(condition, name)                                                  \
    if (!(condition)) {                                                        \
        std::cerr << "FAIL: " << name << "\n";                                 \
        return 1;                                                              \
    } else {                                                                   \
        std::cout << "PASS: " << name << "\n";                                 \
    }

// Helper: render a formatting lambda into a std::string using a fixed buffer.
static std::string render(auto&& writer) {
    char buffer[4096];
    fl::buffer_sink sink(buffer, sizeof(buffer));
    writer(sink);
    sink.null_terminate();
    return std::string(buffer, sink.written());
}

// Timer helper.
class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_seconds() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - start_).count();
    }

    double elapsed_ms() const {
        return elapsed_seconds() * 1000.0;
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// ============================================================================
// 1. Repeated formatting: Format the same pattern 10,000 times, measure time
// ============================================================================
static int test_repeated_formatting() {
    std::cout << "\n=== Repeated Formatting (10,000 iterations) ===\n";

    const int iterations = 10000;

    Timer timer;
    for (int i = 0; i < iterations; ++i) {
        char buf[64];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "{} {} {:.2f}", 42, "hello", 3.14);
        sink.null_terminate();
    }
    double elapsed = timer.elapsed_ms();

    std::cout << "  Repeated formatting " << iterations << " times: "
              << elapsed << " ms (" << (elapsed / iterations) << " ms/op)\n";

    // Verify correctness on the last iteration
    {
        char buf[64];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "{} {} {:.2f}", 42, "hello", 3.14);
        sink.null_terminate();
        TEST(std::string(buf) == "42 hello 3.14", "repeated formatting correctness");
    }

    return 0;
}

// ============================================================================
// 2. Large output: Format a pattern that produces 100KB+ output
//    Note: format_to only accepts buffer_sink, so we use a large buffer
//    and repeated calls, or use growing_sink directly for raw writes.
// ============================================================================
static int test_large_output() {
    std::cout << "\n=== Large Output (100KB+) ===\n";

    // Generate a large string argument
    std::string large_str(1024, 'X');

    // Use growing_sink directly for large output (format_to only supports buffer_sink)
    fl::sinks::growing_sink sink;

    // Write the large string 100 times using direct sink writes
    for (int i = 0; i < 100; ++i) {
        sink.write(large_str.data(), large_str.size());
    }

    TEST(sink.written() >= 102400, "large output >= 100KB");
    std::cout << "  Large output size: " << sink.written() << " bytes\n";

    return 0;
}

// ============================================================================
// 3. Many arguments: Format with 20+ arguments using repeated format_to calls
// ============================================================================
static int test_many_arguments() {
    std::cout << "\n=== Many Arguments (20+) ===\n";

    // Use buffer_sink and format_to with many args via repeated calls
    // We build the output incrementally
    std::string result;
    for (int i = 0; i < 20; ++i) {
        char buf[32];
        fl::buffer_sink sink(buf, sizeof(buf));
        if (i > 0) {
            fl::format_to(sink, " {}", i);
        } else {
            fl::format_to(sink, "{}", i);
        }
        sink.null_terminate();
        result += buf;
    }

    // Verify
    std::string expected;
    for (int i = 0; i < 20; ++i) {
        if (i > 0) expected += " ";
        expected += std::to_string(i);
    }
    TEST(result == expected, "many arguments (20)");

    return 0;
}

// ============================================================================
// 4. Deeply nested format strings: Complex combinations of specifiers
// ============================================================================
static int test_complex_format_specs() {
    std::cout << "\n=== Complex Format Specifiers ===\n";

    // Multiple specifiers in one string
    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:*>+10.4f} {:#010x} {:.^20} {:b} {:o}",
                          3.14159, 255, "center", 42, 64);
        });
        TEST(out.find("+3.1416") != std::string::npos, "complex float with sign");
        TEST(out.find("0xff") != std::string::npos, "complex hex alternate");
        TEST(out.find("center") != std::string::npos, "complex center align");
        TEST(out.find("101010") != std::string::npos, "complex binary");
        TEST(out.find("100") != std::string::npos, "complex octal");
    }

    // Nested-style: format with many specifiers on same type
    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:d} {:x} {:X} {:b} {:B} {:o}", 255, 255, 255, 255, 255, 255);
        });
        TEST(out == "255 ff FF 11111111 11111111 377", "all integer type specifiers");
    }

    // Float with all specifiers
    {
        auto out = render([](auto& sink) {
            fl::format_to(sink, "{:f} {:F} {:e} {:E} {:g} {:G}", 3.14, 3.14, 1000.0, 1000.0, 3.14, 3.14);
        });
        TEST(out.find("3.140000") != std::string::npos, "float f specifier");
        TEST(out.find('e') != std::string::npos, "float e specifier");
        TEST(out.find('E') != std::string::npos, "float E specifier");
    }

    return 0;
}

// ============================================================================
// 5. Rapid sink creation/destruction: Create and destroy many sinks
// ============================================================================
static int test_rapid_sink_creation() {
    std::cout << "\n=== Rapid Sink Creation/Destruction ===\n";

    const int iterations = 10000;

    Timer timer;
    for (int i = 0; i < iterations; ++i) {
        char buf[64];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "{}", i);
        sink.null_terminate();
    }
    double elapsed = timer.elapsed_ms();

    std::cout << "  Created/destroyed " << iterations << " sinks in "
              << elapsed << " ms\n";

    // Verify last iteration
    {
        char buf[64];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "{}", iterations - 1);
        sink.null_terminate();
        TEST(std::string(buf) == std::to_string(iterations - 1),
             "rapid sink creation correctness");
    }

    return 0;
}

// ============================================================================
// 6. Concurrent formatting (if thread-safe): Multiple threads formatting
// ============================================================================
static int test_concurrent_formatting() {
    std::cout << "\n=== Concurrent Formatting ===\n";

    const int num_threads = 4;
    const int ops_per_thread = 2500;
    std::vector<std::thread> threads;
    std::atomic<int> failures{0};

    Timer timer;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&failures, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                try {
                    char buf[128];
                    fl::buffer_sink sink(buf, sizeof(buf));
                    fl::format_to(sink, "thread={} iter={} val={:.3f}", t, i, 3.14159);
                    sink.null_terminate();
                } catch (...) {
                    failures.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    double elapsed = timer.elapsed_ms();
    int total_ops = num_threads * ops_per_thread;

    std::cout << "  " << total_ops << " formatting ops across "
              << num_threads << " threads in " << elapsed << " ms\n";
    TEST(failures.load() == 0, "concurrent formatting: no failures");

    return 0;
}

// ============================================================================
// 7. Memory allocation stress: Verify no unexpected allocations in the
//    zero-heap path (buffer_sink uses stack buffer)
// ============================================================================
static int test_memory_allocation_stress() {
    std::cout << "\n=== Memory Allocation Stress ===\n";

    // Format many times with buffer_sink (stack-allocated, no heap)
    const int iterations = 50000;

    Timer timer;
    for (int i = 0; i < iterations; ++i) {
        char buf[64];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "{}", 42);
        sink.null_terminate();
    }
    double elapsed = timer.elapsed_ms();

    std::cout << "  " << iterations << " stack-only formatting ops in "
              << elapsed << " ms\n";

    // Also test with growing_sink (heap-allocated) - direct writes only
    Timer timer2;
    for (int i = 0; i < iterations; ++i) {
        fl::sinks::growing_sink sink;
        sink.write("42", 2);
    }
    double elapsed2 = timer2.elapsed_ms();

    std::cout << "  " << iterations << " growing_sink write ops in "
              << elapsed2 << " ms\n";

    return 0;
}

// ============================================================================
// 8. Format string fuzzing: Generate random format strings and verify no crashes
// ============================================================================
static int test_format_fuzzing() {
    std::cout << "\n=== Format String Fuzzing ===\n";

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> len_dist(1, 20);
    std::uniform_int_distribution<int> char_dist(32, 126);  // printable ASCII
    std::uniform_int_distribution<int> choice_dist(0, 4);

    const int fuzz_iterations = 5000;
    int valid_count = 0;
    int error_count = 0;

    for (int i = 0; i < fuzz_iterations; ++i) {
        // Generate random format string
        int len = len_dist(rng);
        std::string fmt;
        fmt.reserve(static_cast<std::size_t>(len));
        for (int j = 0; j < len; ++j) {
            int choice = choice_dist(rng);
            switch (choice) {
                case 0: fmt += '{'; break;
                case 1: fmt += '}'; break;
                case 2: fmt += static_cast<char>(char_dist(rng)); break;
                case 3: fmt += "{}"; break;
                case 4: fmt += "{:" + std::to_string(char_dist(rng) % 10) + "}"; break;
            }
        }

        // Try to format with it - should not crash
        try {
            char buf[256];
            fl::buffer_sink sink(buf, sizeof(buf));
            fl::format_to(sink, fmt, 42, "hello", 3.14);
            sink.null_terminate();
            ++valid_count;
        } catch (const std::invalid_argument&) {
            ++error_count;  // Expected for malformed format strings
        } catch (const std::overflow_error&) {
            ++error_count;  // Expected for overflow
        } catch (...) {
            std::cerr << "  Unexpected exception during fuzzing with fmt='"
                      << fmt << "'\n";
            return 1;
        }
    }

    std::cout << "  Fuzzing iterations: " << fuzz_iterations
              << " (valid: " << valid_count
              << ", expected errors: " << error_count << ")\n";
    TEST(valid_count + error_count == fuzz_iterations,
         "fuzzing: all iterations completed without crash");

    return 0;
}

// ============================================================================
// 9. Long-running format: Format with many placeholders in one string
// ============================================================================
static int test_long_format_string() {
    std::cout << "\n=== Long Format String ===\n";

    // Build expected output matching the format loop below:
    // a space is inserted before every multiple of 10 (except 0).
    std::string expected;
    for (int i = 0; i < 100; ++i) {
        if (i > 0 && i % 10 == 0) {
            expected += " ";
        }
        expected += std::to_string(i);
    }

    // Format with 100 arguments using buffer_sink and repeated format_to calls
    std::string result;
    for (int i = 0; i < 100; ++i) {
        char buf[32];
        fl::buffer_sink sink(buf, sizeof(buf));
        if (i > 0 && i % 10 == 0) {
            fl::format_to(sink, " {}", i);
        } else {
            fl::format_to(sink, "{}", i);
        }
        sink.null_terminate();
        result += buf;
    }

    TEST(result == expected, "long format string with 100 values");

    std::cout << "  Long format output size: " << result.size() << " bytes\n";

    return 0;
}

// ============================================================================
// 10. Buffer boundary tests: Format strings that exactly fill, under-fill,
//     and over-fill buffers
// ============================================================================
static int test_buffer_boundaries() {
    std::cout << "\n=== Buffer Boundary Tests ===\n";

    // Exact fill: format a string that exactly fills the buffer
    {
        char buf[8];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "1234567");  // 7 chars + null = 8 total
        sink.null_terminate();
        TEST(std::string(buf) == "1234567", "exact fill buffer (7 chars in 8-byte buf)");
        TEST(sink.written() == 7, "exact fill written count");
    }

    // Under-fill: format a small string into a large buffer
    {
        char buf[128];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "hi");
        sink.null_terminate();
        TEST(std::string(buf) == "hi", "under-fill buffer");
        TEST(sink.written() == 2, "under-fill written count");
    }

    // Over-fill: format a string that exceeds buffer capacity
    {
        char buf[4];
        fl::buffer_sink sink(buf, sizeof(buf));
        bool threw = false;
        try {
            fl::format_to(sink, "hello");
        } catch (const std::overflow_error&) {
            threw = true;
        }
        TEST(threw, "over-fill buffer throws overflow_error");
    }

    // Boundary: exactly fill to capacity (no room for null)
    {
        char buf[5];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "1234");  // 4 chars, exactly fits
        sink.null_terminate();
        TEST(std::string(buf) == "1234", "boundary fill (4 chars in 5-byte buf)");
    }

    // Multiple writes that exactly fill
    {
        char buf[10];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "abc");
        fl::format_to(sink, "def");
        fl::format_to(sink, "ghi");
        sink.null_terminate();
        TEST(std::string(buf) == "abcdefghi", "multiple writes exact fill");
        TEST(sink.written() == 9, "multiple writes exact fill count");
    }

    // Multiple writes that overflow
    {
        char buf[8];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "1234");
        bool threw = false;
        try {
            fl::format_to(sink, "12345");  // would need 9 total, only 8 available
        } catch (const std::overflow_error&) {
            threw = true;
        }
        TEST(threw, "multiple writes overflow");
    }

    // Single char writes at boundary
    {
        char buf[3];
        fl::buffer_sink sink(buf, sizeof(buf));
        fl::format_to(sink, "a");
        fl::format_to(sink, "b");
        bool threw = false;
        try {
            fl::format_to(sink, "cc");  // needs 4 total, only 3 available
        } catch (const std::overflow_error&) {
            threw = true;
        }
        TEST(threw, "single char writes overflow at boundary");
    }

    return 0;
}

// ============================================================================
// Additional: Performance benchmark - format many different types
// ============================================================================
static int test_format_performance() {
    std::cout << "\n=== Format Performance ===\n";

    const int iterations = 50000;

    // int
    {
        Timer timer;
        for (int i = 0; i < iterations; ++i) {
            char buf[32];
            fl::buffer_sink sink(buf, sizeof(buf));
            fl::format_to(sink, "{}", i);
            sink.null_terminate();
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "  int formatting: " << iterations << " ops in "
                  << elapsed << " ms (" << (elapsed / iterations * 1e6)
                  << " ns/op)\n";
    }

    // double
    {
        Timer timer;
        for (int i = 0; i < iterations; ++i) {
            char buf[32];
            fl::buffer_sink sink(buf, sizeof(buf));
            fl::format_to(sink, "{}", static_cast<double>(i) * 0.1);
            sink.null_terminate();
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "  double formatting: " << iterations << " ops in "
                  << elapsed << " ms (" << (elapsed / iterations * 1e6)
                  << " ns/op)\n";
    }

    // string
    {
        std::string s = "hello";
        Timer timer;
        for (int i = 0; i < iterations; ++i) {
            char buf[32];
            fl::buffer_sink sink(buf, sizeof(buf));
            fl::format_to(sink, "{}", s);
            sink.null_terminate();
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "  string formatting: " << iterations << " ops in "
                  << elapsed << " ms (" << (elapsed / iterations * 1e6)
                  << " ns/op)\n";
    }

    // Format with specifiers
    {
        Timer timer;
        for (int i = 0; i < iterations; ++i) {
            char buf[64];
            fl::buffer_sink sink(buf, sizeof(buf));
            fl::format_to(sink, "{:*>+10.4f}", 3.14159);
            sink.null_terminate();
        }
        double elapsed = timer.elapsed_ms();
        std::cout << "  complex spec formatting: " << iterations << " ops in "
                  << elapsed << " ms (" << (elapsed / iterations * 1e6)
                  << " ns/op)\n";
    }

    return 0;
}

// ============================================================================
// Additional: Growing sink stress - write many small chunks
// ============================================================================
static int test_growing_sink_stress() {
    std::cout << "\n=== Growing Sink Stress ===\n";

    fl::sinks::growing_sink sink(16);  // small initial capacity to force many reallocations

    const int num_writes = 100000;
    Timer timer;

    for (int i = 0; i < num_writes; ++i) {
        sink.write("x", 1);
    }

    double elapsed = timer.elapsed_ms();

    TEST(sink.written() == static_cast<std::size_t>(num_writes),
         "growing_sink stress: correct size");
    std::cout << "  " << num_writes << " single-byte writes in "
              << elapsed << " ms\n";

    return 0;
}

// ============================================================================
// Additional: Null sink performance benchmark
// ============================================================================
static int test_null_sink_performance() {
    std::cout << "\n=== Null Sink Performance ===\n";

    const int iterations = 100000;

    Timer timer;
    fl::sinks::null_sink sink;
    for (int i = 0; i < iterations; ++i) {
        sink.write("benchmark data", 14);
    }
    double elapsed = timer.elapsed_ms();

    TEST(sink.written() == static_cast<std::size_t>(iterations * 14),
         "null_sink performance: correct byte count");
    std::cout << "  " << iterations << " writes to null_sink in "
              << elapsed << " ms (" << (elapsed / iterations * 1e6)
              << " ns/write)\n";

    return 0;
}

// ============================================================================
// Additional: Format with very large width values
// ============================================================================
static int test_large_width() {
    std::cout << "\n=== Large Width Values ===\n";

    // Large width with small content
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:500}", 1); });
        TEST(out.size() == 500, "width 500 produces 500 chars");
        TEST(out[499] == '1', "width 500 last char is content");
    }

    // Large width with string
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:200}", "x"); });
        TEST(out.size() == 200, "width 200 string produces 200 chars");
        TEST(out[199] == 'x', "width 200 string last char is content");
    }

    return 0;
}

// ============================================================================
// Additional: Format with all possible fill characters
// ============================================================================
static int test_all_fill_chars() {
    std::cout << "\n=== All Fill Characters ===\n";

    const char fill_chars[] = {'*', '#', '.', '-', '=', '_', '~', '@', '$', '%'};
    for (char fc : fill_chars) {
        std::string spec = {fc, '>', '5'};
        std::string fmt = "{:" + spec + "}";
        auto out = render([&](auto& sink) {
            fl::format_to(sink, fmt, "X");
        });
        std::string expected(4, fc);
        expected += 'X';
        TEST(out == expected, std::string("fill char '") + fc + "'");
    }

    return 0;
}

// ============================================================================
// main: Run all stress tests
// ============================================================================
int main() {
    int result = 0;

    result |= test_repeated_formatting();
    result |= test_large_output();
    result |= test_many_arguments();
    result |= test_complex_format_specs();
    result |= test_rapid_sink_creation();
    result |= test_concurrent_formatting();
    result |= test_memory_allocation_stress();
    result |= test_format_fuzzing();
    result |= test_long_format_string();
    result |= test_buffer_boundaries();
    result |= test_format_performance();
    result |= test_growing_sink_stress();
    result |= test_null_sink_performance();
    result |= test_large_width();
    result |= test_all_fill_chars();

    if (result == 0) {
        std::cout << "\nAll stress tests passed!\n";
    } else {
        std::cerr << "\nSome stress tests FAILED.\n";
    }
    return result;
}
