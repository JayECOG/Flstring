#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <random>
#include <numeric>
#include <algorithm>
#include <cmath>

#include "fl/string.hpp"

// Prevent compiler optimisation of results
template <typename T>
void do_not_optimize(T&& value) {
    using RawT = typename std::remove_cvref<T>::type;
    static volatile RawT sink;
    sink = std::forward<T>(value);
    (void)sink;  // Volatile read prevents -Wunused-but-set-variable
}

void do_not_optimize_char(char c) {
    static volatile char sink;
    sink = c;
    (void)sink;  // Volatile read prevents -Wunused-but-set-variable
}

struct Statistics {
    double median = 0.0;
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double q1 = 0.0;
    double q3 = 0.0;
    double iqr = 0.0;
    
    static Statistics compute(std::vector<double>& samples) {
        Statistics stats;
        if (samples.empty()) return stats;
        
        std::sort(samples.begin(), samples.end());
        
        stats.min = samples.front();
        stats.max = samples.back();
        stats.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        
        size_t n = samples.size();
        if (n % 2 == 0) {
            stats.median = (samples[n/2 - 1] + samples[n/2]) / 2.0;
        } else {
            stats.median = samples[n/2];
        }
        
        size_t q1_idx = n / 4;
        size_t q3_idx = 3 * n / 4;
        stats.q1 = samples[q1_idx];
        stats.q3 = samples[q3_idx];
        stats.iqr = stats.q3 - stats.q1;
        
        return stats;
    }
};

class BenchmarkRunner {
private:
    std::vector<double> samples;
    std::chrono::high_resolution_clock::time_point start;
    
public:
    void reset() {
        samples.clear();
    }
    
    void start_sample() {
        start = std::chrono::high_resolution_clock::now();
    }
    
    void end_sample() {
        auto end = std::chrono::high_resolution_clock::now();
        double us = std::chrono::duration<double, std::micro>(end - start).count();
        samples.push_back(us);
    }
    
    Statistics compute() {
        return Statistics::compute(samples);
    }
};

void print_csv_header() {
    std::cout << "Category,Operation,Iterations,StdMedian_us,FlMedian_us,StdMean_us,FlMean_us,"
              << "StdMin_us,FlMin_us,StdMax_us,FlMax_us,StdIQR,FlIQR,Ratio_Median\n";
}

void print_result(const std::string& category, const std::string& operation, 
                  int iterations, const Statistics& std_stats, const Statistics& fl_stats) {
    double ratio = (fl_stats.median > 0) ? (std_stats.median / fl_stats.median) : 0.0;
    
    std::cout << std::fixed << std::setprecision(3);
    std::cout << category << "," << operation << "," << iterations << ","
              << std_stats.median << "," << fl_stats.median << ","
              << std_stats.mean << "," << fl_stats.mean << ","
              << std_stats.min << "," << fl_stats.min << ","
              << std_stats.max << "," << fl_stats.max << ","
              << std_stats.iqr << "," << fl_stats.iqr << ","
              << ratio << "\n";
}

// Test data generators
std::vector<std::string> generate_random_strings(size_t count, size_t min_len, size_t max_len) {
    std::vector<std::string> result;
    result.reserve(count);
    std::mt19937 gen(42);
    std::uniform_int_distribution<> len_dist(min_len, max_len);
    std::uniform_int_distribution<> char_dist('a', 'z');
    
    for (size_t i = 0; i < count; ++i) {
        size_t len = len_dist(gen);
        std::string s;
        s.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            s.push_back(static_cast<char>(char_dist(gen)));
        }
        result.push_back(std::move(s));
    }
    return result;
}

void benchmark_construction() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    // 1. Default construction
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s;
            do_not_optimize_char(s.empty() ? 'x' : s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 10000; ++j) {
                std::string s;
                do_not_optimize_char(s.empty() ? 'x' : s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s;
            do_not_optimize_char(s.empty() ? 'x' : s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 10000; ++j) {
                fl::string s;
                do_not_optimize_char(s.empty() ? 'x' : s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "Default", 10000, std_stats, fl_stats);
    }
    
    // 2. From C-string (SSO size)
    {
        const char* cstr = "hello world";
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s(cstr);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s(cstr);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s(cstr);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s(cstr);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "FromCStr_SSO_11", 100000, std_stats, fl_stats);
    }
    
    // 3. From C-string + length (SSO size)
    {
        const char* cstr = "hello world";
        size_t len = 11;
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s(cstr, len);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s(cstr, len);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s(cstr, len);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s(cstr, len);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "FromCStrLen_SSO_11", 100000, std_stats, fl_stats);
    }
    
    // 4. From C-string (heap size)
    {
        const char* cstr = "This is a much longer string that will definitely trigger heap allocation in both implementations";
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s(cstr);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s(cstr);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s(cstr);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s(cstr);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "FromCStr_Heap_98", 50000, std_stats, fl_stats);
    }
    
    // 5. Copy construction (SSO)
    {
        std::string std_src("hello world");
        fl::string fl_src("hello world");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s(std_src);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s(std_src);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s(fl_src);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s(fl_src);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "Copy_SSO_11", 100000, std_stats, fl_stats);
    }
    
    // 6. Copy construction (heap)
    {
        std::string std_src("This is a much longer string that will definitely trigger heap allocation");
        fl::string fl_src("This is a much longer string that will definitely trigger heap allocation");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s(std_src);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s(std_src);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s(fl_src);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s(fl_src);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "Copy_Heap_75", 50000, std_stats, fl_stats);
    }
    
    // 7. Move construction
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string tmp("hello world");
            std::string s(std::move(tmp));
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string tmp("hello world");
                std::string s(std::move(tmp));
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string tmp("hello world");
            fl::string s(std::move(tmp));
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string tmp("hello world");
                fl::string s(std::move(tmp));
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "Move_SSO_11", 100000, std_stats, fl_stats);
    }
    
    // 8. Repeated character construction (SSO)
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s(15, 'A');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s(15, 'A');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s(15, 'A');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s(15, 'A');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "RepeatChar_SSO_15", 100000, std_stats, fl_stats);
    }
    
    // 9. Repeated character construction (heap)
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s(100, 'B');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s(100, 'B');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s(100, 'B');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s(100, 'B');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Construction", "RepeatChar_Heap_100", 50000, std_stats, fl_stats);
    }
}

void benchmark_assignment() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    // 1. Copy assignment (SSO to SSO)
    {
        std::string std_src("hello");
        fl::string fl_src("hello");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s;
            s = std_src;
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s;
                s = std_src;
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s;
            s = fl_src;
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s;
                s = fl_src;
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Assignment", "Copy_SSO_to_SSO", 100000, std_stats, fl_stats);
    }
    
    // 2. Copy assignment (heap to heap)
    {
        std::string std_src("This is a longer string that requires heap allocation for storage");
        fl::string fl_src("This is a longer string that requires heap allocation for storage");
        
        runner.reset();
        std::string std_dest("Another heap allocated string for initialisation");
        for (int i = 0; i < warmup; ++i) {
            std_dest = std_src;
            do_not_optimize_char(std_dest[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std_dest = std_src;
                do_not_optimize_char(std_dest[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        fl::string fl_dest("Another heap allocated string for initialisation");
        for (int i = 0; i < warmup; ++i) {
            fl_dest = fl_src;
            do_not_optimize_char(fl_dest[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl_dest = fl_src;
                do_not_optimize_char(fl_dest[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Assignment", "Copy_Heap_to_Heap", 50000, std_stats, fl_stats);
    }
    
    // 3. Move assignment
    {
        runner.reset();
        std::string std_dest;
        for (int i = 0; i < warmup; ++i) {
            std::string tmp("hello world");
            std_dest = std::move(tmp);
            do_not_optimize_char(std_dest[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string tmp("hello world");
                std_dest = std::move(tmp);
                do_not_optimize_char(std_dest[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        fl::string fl_dest;
        for (int i = 0; i < warmup; ++i) {
            fl::string tmp("hello world");
            fl_dest = std::move(tmp);
            do_not_optimize_char(fl_dest[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string tmp("hello world");
                fl_dest = std::move(tmp);
                do_not_optimize_char(fl_dest[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Assignment", "Move", 100000, std_stats, fl_stats);
    }
    
    // 4. C-string assignment
    {
        const char* cstr = "test string";
        
        runner.reset();
        std::string std_s;
        for (int i = 0; i < warmup; ++i) {
            std_s = cstr;
            do_not_optimize_char(std_s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std_s = cstr;
                do_not_optimize_char(std_s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        fl::string fl_s;
        for (int i = 0; i < warmup; ++i) {
            fl_s = cstr;
            do_not_optimize_char(fl_s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl_s = cstr;
                do_not_optimize_char(fl_s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Assignment", "CString", 100000, std_stats, fl_stats);
    }
}

void benchmark_append() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    // 1. Append C-string (SSO stays SSO)
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s;
            s.append("abc");
            s.append("def");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s;
                s.append("abc");
                s.append("def");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s;
            s.append("abc");
            s.append("def");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s;
                s.append("abc");
                s.append("def");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Append", "CStr_SSO_Stays_SSO", 100000, std_stats, fl_stats);
    }
    
    // 2. Append transitions from SSO to heap
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("initial");
            s.append("this will push us over the SSO boundary");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s("initial");
                s.append("this will push us over the SSO boundary");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("initial");
            s.append("this will push us over the SSO boundary");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s("initial");
                s.append("this will push us over the SSO boundary");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Append", "SSO_to_Heap_Transition", 50000, std_stats, fl_stats);
    }
    
    // 3. Append on heap (many small appends)
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s;
            for (int k = 0; k < 100; ++k) s.append("x");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 10000; ++j) {
                std::string s;
                for (int k = 0; k < 100; ++k) s.append("x");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s;
            for (int k = 0; k < 100; ++k) s.append("x");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 10000; ++j) {
                fl::string s;
                for (int k = 0; k < 100; ++k) s.append("x");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Append", "Many_Small_100x", 10000, std_stats, fl_stats);
    }
    
    // 4. Append single character
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s;
            s.append(1, 'x');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s;
                s.append(1, 'x');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s;
            s.append('x');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s;
                s.append('x');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Append", "SingleChar", 100000, std_stats, fl_stats);
    }
    
    // 5. operator+= with C-string
    {
        const char* suffix = " world";
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello");
            s += suffix;
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s("hello");
                s += suffix;
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello");
            s += suffix;
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s("hello");
                s += suffix;
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Append", "OperatorPlusEq_CStr", 100000, std_stats, fl_stats);
    }
    
    // 6. operator+= with char
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello");
            s += '!';
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s("hello");
                s += '!';
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello");
            s += '!';
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s("hello");
                s += '!';
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Append", "OperatorPlusEq_Char", 100000, std_stats, fl_stats);
    }
}

void benchmark_find() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    const char* text = "The quick brown fox jumps over the lazy dog";
    
    // 1. find() - substring found
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = std_s.find("fox");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = std_s.find("fox");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = fl_s.find("fox");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = fl_s.find("fox");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Find", "Substring_Found", 100000, std_stats, fl_stats);
    }
    
    // 2. find() - substring not found
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = std_s.find("elephant");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = std_s.find("elephant");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = fl_s.find("elephant");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = fl_s.find("elephant");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Find", "Substring_NotFound", 100000, std_stats, fl_stats);
    }
    
    // 3. find() - single character
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = std_s.find('f');
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = std_s.find('f');
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = fl_s.find('f');
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = fl_s.find('f');
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Find", "SingleChar", 100000, std_stats, fl_stats);
    }
    
    // 4. rfind() - reverse find
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = std_s.rfind("the");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = std_s.rfind("the");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = fl_s.rfind("the");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = fl_s.rfind("the");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Find", "Rfind_Substring", 100000, std_stats, fl_stats);
    }
    
    // 5. find_first_of()
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = std_s.find_first_of("aeiou");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = std_s.find_first_of("aeiou");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = fl_s.find_first_of("aeiou");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = fl_s.find_first_of("aeiou");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Find", "FindFirstOf", 100000, std_stats, fl_stats);
    }
    
    // 6. find_last_of()
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = std_s.find_last_of("aeiou");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = std_s.find_last_of("aeiou");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto pos = fl_s.find_last_of("aeiou");
            do_not_optimize(pos);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto pos = fl_s.find_last_of("aeiou");
                do_not_optimize(pos);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Find", "FindLastOf", 100000, std_stats, fl_stats);
    }
}

void benchmark_substr() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    const char* text = "A very long string that we want to extract parts from";
    
    // 1. substr() - small result (SSO)
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto sub = std_s.substr(2, 10);
            do_not_optimize_char(sub[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto sub = std_s.substr(2, 10);
                do_not_optimize_char(sub[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto sub = fl_s.substr(2, 10);
            do_not_optimize_char(sub[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                auto sub = fl_s.substr(2, 10);
                do_not_optimize_char(sub[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Substring", "Substr_Small_10", 100000, std_stats, fl_stats);
    }
    
    // 2. substr() - large result (heap)
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto sub = std_s.substr(5, 40);
            do_not_optimize_char(sub[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                auto sub = std_s.substr(5, 40);
                do_not_optimize_char(sub[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            auto sub = fl_s.substr(5, 40);
            do_not_optimize_char(sub[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                auto sub = fl_s.substr(5, 40);
                do_not_optimize_char(sub[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Substring", "Substr_Large_40", 50000, std_stats, fl_stats);
    }
    
    // 3. compare()
    {
        std::string std_s1("hello world");
        std::string std_s2("hello world");
        fl::string fl_s1("hello world");
        fl::string fl_s2("hello world");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            int cmp = std_s1.compare(std_s2);
            do_not_optimize(cmp);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                int cmp = std_s1.compare(std_s2);
                do_not_optimize(cmp);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            int cmp = fl_s1.compare(fl_s2);
            do_not_optimize(cmp);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                int cmp = fl_s1.compare(fl_s2);
                do_not_optimize(cmp);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Substring", "Compare_Equal", 100000, std_stats, fl_stats);
    }
}

void benchmark_modification() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    // 1. insert() - at beginning
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("world");
            s.insert(0, "hello ");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s("world");
                s.insert(0, "hello ");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("world");
            s.insert(0, "hello ");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s("world");
                s.insert(0, "hello ");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Modification", "Insert_Beginning", 50000, std_stats, fl_stats);
    }
    
    // 2. insert() - in middle
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello world");
            s.insert(5, " cruel");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s("hello world");
                s.insert(5, " cruel");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello world");
            s.insert(5, " cruel");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s("hello world");
                s.insert(5, " cruel");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Modification", "Insert_Middle", 50000, std_stats, fl_stats);
    }
    
    // 3. erase() - from middle
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello cruel world");
            s.erase(5, 6);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s("hello cruel world");
                s.erase(5, 6);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello cruel world");
            s.erase(5, 6);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s("hello cruel world");
                s.erase(5, 6);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Modification", "Erase_Middle", 50000, std_stats, fl_stats);
    }
    
    // 4. erase() - single character
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello");
            s.erase(2, 1);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s("hello");
                s.erase(2, 1);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello");
            s.erase(2, 1);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s("hello");
                s.erase(2, 1);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Modification", "Erase_SingleChar", 100000, std_stats, fl_stats);
    }
    
    // 5. replace()
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello world");
            s.replace(6, 5, "universe");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s("hello world");
                s.replace(6, 5, "universe");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello world");
            s.replace(6, 5, "universe");
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s("hello world");
                s.replace(6, 5, "universe");
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Modification", "Replace", 50000, std_stats, fl_stats);
    }
}

void benchmark_capacity() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    // 1. reserve() - small to large
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s;
            s.reserve(100);
            do_not_optimize(s.capacity());
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s;
                s.reserve(100);
                do_not_optimize(s.capacity());
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s;
            s.reserve(100);
            do_not_optimize(s.capacity());
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s;
                s.reserve(100);
                do_not_optimize(s.capacity());
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Capacity", "Reserve_100", 100000, std_stats, fl_stats);
    }
    
    // 2. resize() - grow
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello");
            s.resize(50, 'x');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s("hello");
                s.resize(50, 'x');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello");
            s.resize(50, 'x');
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s("hello");
                s.resize(50, 'x');
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Capacity", "Resize_Grow_5to50", 50000, std_stats, fl_stats);
    }
    
    // 3. resize() - shrink
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("this is a longer string that we will shrink");
            s.resize(10);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                std::string s("this is a longer string that we will shrink");
                s.resize(10);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("this is a longer string that we will shrink");
            s.resize(10);
            do_not_optimize_char(s[0]);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 50000; ++j) {
                fl::string s("this is a longer string that we will shrink");
                s.resize(10);
                do_not_optimize_char(s[0]);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Capacity", "Resize_Shrink_44to10", 50000, std_stats, fl_stats);
    }
    
    // 4. shrink_to_fit()
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s;
            s.reserve(1000);
            s = "short";
            s.shrink_to_fit();
            do_not_optimize(s.capacity());
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 10000; ++j) {
                std::string s;
                s.reserve(1000);
                s = "short";
                s.shrink_to_fit();
                do_not_optimize(s.capacity());
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s;
            s.reserve(1000);
            s = "short";
            s.shrink_to_fit();
            do_not_optimize(s.capacity());
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 10000; ++j) {
                fl::string s;
                s.reserve(1000);
                s = "short";
                s.shrink_to_fit();
                do_not_optimize(s.capacity());
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Capacity", "ShrinkToFit", 10000, std_stats, fl_stats);
    }
    
    // 5. clear()
    {
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            std::string s("hello world");
            s.clear();
            do_not_optimize(s.size());
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                std::string s("hello world");
                s.clear();
                do_not_optimize(s.size());
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            fl::string s("hello world");
            s.clear();
            do_not_optimize(s.size());
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                fl::string s("hello world");
                s.clear();
                do_not_optimize(s.size());
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Capacity", "Clear", 100000, std_stats, fl_stats);
    }
}

void benchmark_iterators() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    const char* text = "The quick brown fox jumps over the lazy dog and continues for a while longer";
    
    // 1. Forward iteration
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            char sum = 0;
            for (auto it = std_s.begin(); it != std_s.end(); ++it) {
                sum += *it;
            }
            do_not_optimize_char(sum);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                char sum = 0;
                for (auto it = std_s.begin(); it != std_s.end(); ++it) {
                    sum += *it;
                }
                do_not_optimize_char(sum);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            char sum = 0;
            for (auto it = fl_s.begin(); it != fl_s.end(); ++it) {
                sum += *it;
            }
            do_not_optimize_char(sum);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                char sum = 0;
                for (auto it = fl_s.begin(); it != fl_s.end(); ++it) {
                    sum += *it;
                }
                do_not_optimize_char(sum);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Iterators", "Forward_Iteration", 100000, std_stats, fl_stats);
    }
    
    // 2. Range-based for loop
    {
        std::string std_s(text);
        fl::string fl_s(text);
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            char sum = 0;
            for (char c : std_s) {
                sum += c;
            }
            do_not_optimize_char(sum);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                char sum = 0;
                for (char c : std_s) {
                    sum += c;
                }
                do_not_optimize_char(sum);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            char sum = 0;
            for (char c : fl_s) {
                sum += c;
            }
            do_not_optimize_char(sum);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                char sum = 0;
                for (char c : fl_s) {
                    sum += c;
                }
                do_not_optimize_char(sum);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Iterators", "RangeBased_For", 100000, std_stats, fl_stats);
    }
}

void benchmark_comparison() {
    const int warmup = 100;
    const int samples = 1000;
    BenchmarkRunner runner;
    
    // 1. Equality (==) - equal strings
    {
        std::string std_s1("hello world");
        std::string std_s2("hello world");
        fl::string fl_s1("hello world");
        fl::string fl_s2("hello world");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool eq = (std_s1 == std_s2);
            do_not_optimize(eq);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool eq = (std_s1 == std_s2);
                do_not_optimize(eq);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool eq = (fl_s1 == fl_s2);
            do_not_optimize(eq);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool eq = (fl_s1 == fl_s2);
                do_not_optimize(eq);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Comparison", "Equality_Equal", 100000, std_stats, fl_stats);
    }
    
    // 2. Equality (==) - different strings
    {
        std::string std_s1("hello world");
        std::string std_s2("hello universe");
        fl::string fl_s1("hello world");
        fl::string fl_s2("hello universe");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool eq = (std_s1 == std_s2);
            do_not_optimize(eq);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool eq = (std_s1 == std_s2);
                do_not_optimize(eq);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool eq = (fl_s1 == fl_s2);
            do_not_optimize(eq);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool eq = (fl_s1 == fl_s2);
                do_not_optimize(eq);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Comparison", "Equality_Different", 100000, std_stats, fl_stats);
    }
    
    // 3. Less than (<)
    {
        std::string std_s1("apple");
        std::string std_s2("banana");
        fl::string fl_s1("apple");
        fl::string fl_s2("banana");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool lt = (std_s1 < std_s2);
            do_not_optimize(lt);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool lt = (std_s1 < std_s2);
                do_not_optimize(lt);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool lt = (fl_s1 < fl_s2);
            do_not_optimize(lt);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool lt = (fl_s1 < fl_s2);
                do_not_optimize(lt);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Comparison", "LessThan", 100000, std_stats, fl_stats);
    }
    
    // 4. Inequality (!=)
    {
        std::string std_s1("hello");
        std::string std_s2("world");
        fl::string fl_s1("hello");
        fl::string fl_s2("world");
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool neq = (std_s1 != std_s2);
            do_not_optimize(neq);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool neq = (std_s1 != std_s2);
                do_not_optimize(neq);
            }
            runner.end_sample();
        }
        auto std_stats = runner.compute();
        
        runner.reset();
        for (int i = 0; i < warmup; ++i) {
            bool neq = (fl_s1 != fl_s2);
            do_not_optimize(neq);
        }
        for (int i = 0; i < samples; ++i) {
            runner.start_sample();
            for (int j = 0; j < 100000; ++j) {
                bool neq = (fl_s1 != fl_s2);
                do_not_optimize(neq);
            }
            runner.end_sample();
        }
        auto fl_stats = runner.compute();
        
        print_result("Comparison", "Inequality", 100000, std_stats, fl_stats);
    }
}

int main() {
    std::cout << "Comprehensive fl::string vs std::string Benchmark Suite\n";
    std::cout << "========================================================\n\n";
    
    print_csv_header();
    
    benchmark_construction();
    benchmark_assignment();
    benchmark_append();
    benchmark_find();
    benchmark_substr();
    benchmark_modification();
    benchmark_capacity();
    benchmark_iterators();
    benchmark_comparison();
    
    std::cout << "\nBenchmark complete.\n";
    return 0;
}
