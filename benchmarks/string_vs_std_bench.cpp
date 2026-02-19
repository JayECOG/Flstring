#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <random>
#include <numeric>

#include "fl/string.hpp"

// Use a volatile sink to prevent compiler from optimising away the results
template <typename T>
void do_not_optimize(T&& value) {
    using RawT = typename std::remove_cvref<T>::type;
    static volatile RawT sink;
    sink = std::forward<T>(value);
    (void)sink;  // Volatile read prevents -Wunused-but-set-variable
}

// Specialization for pointers/references if needed, but for sum it's fine
void do_not_optimize_char(char c) {
    static volatile char sink;
    sink = c;
    (void)sink;  // Volatile read prevents -Wunused-but-set-variable
}

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

void run_benchmarks() {
    const int iterations = 1000000;
    const int large_iterations = 100000;
    
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Benchmarking fl::string vs std::string (" << iterations << " iterations)\n";
    std::cout << "------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(30) << "Operation" << std::setw(15) << "std::string" << std::setw(15) << "fl::string" << "Ratio (std/fl)\n";
    std::cout << "------------------------------------------------------------------\n";

    // 1. SSO Construction
    {
        Timer t1;
        for (int i = 0; i < iterations; ++i) {
            std::string s("hello world");
            do_not_optimize_char(s[0]);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < iterations; ++i) {
            fl::string s("hello world");
            do_not_optimize_char(s[0]);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "SSO Construction (11 chars)" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    // 2. Heap Construction
    {
        constexpr char long_literal[] = "This is a much longer string that will definitely trigger heap allocation in both implementations.";
        constexpr std::size_t long_len = sizeof(long_literal) - 1;

        Timer t1;
        for (int i = 0; i < large_iterations; ++i) {
            std::string s(long_literal, long_len);
            do_not_optimize_char(s[0]);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < large_iterations; ++i) {
            fl::string s(long_literal, long_len);
            do_not_optimize_char(s[0]);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "Heap Construction (98 chars)" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    // 3. SSO Append
    {
        Timer t1;
        for (int i = 0; i < iterations; ++i) {
            std::string s;
            s.append("abc");
            s.append("def");
            do_not_optimize_char(s[0]);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < iterations; ++i) {
            fl::string s;
            s.append("abc");
            s.append("def");
            do_not_optimize_char(s[0]);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "SSO Simple Append" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    // 4. Repeated Char Constuction
    {
        Timer t1;
        for (int i = 0; i < iterations; ++i) {
            std::string s(10, 'A');
            do_not_optimize_char(s[0]);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < iterations; ++i) {
            fl::string s(10, 'A');
            do_not_optimize_char(s[0]);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "Repeated Char Config (10)" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    // 5. Heap Append (Repeated)
    {
        Timer t1;
        for (int i = 0; i < large_iterations; ++i) {
            std::string s;
            for(int j=0; j<100; ++j) s.append("data");
            do_not_optimize_char(s[0]);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < large_iterations; ++i) {
            fl::string s;
            for(int j=0; j<100; ++j) s.append("data");
            do_not_optimize_char(s[0]);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "Heap Append (100x4 chars)" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    // 6. Find
    {
        fl::string fl_s("The quick brown fox jumps over the lazy dog");
        std::string std_s("The quick brown fox jumps over the lazy dog");
        
        Timer t1;
        for (int i = 0; i < iterations; ++i) {
            auto pos = std_s.find("fox");
            do_not_optimize(pos);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < iterations; ++i) {
            auto pos = fl_s.find("fox");
            do_not_optimize(pos);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "Find Substring" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    // 7. Substring Substr
    {
        std::string std_s("A very long string that we want a part of");
        fl::string fl_s("A very long string that we want a part of");

        Timer t1;
        for (int i = 0; i < iterations; ++i) {
            std::string sub = std_s.substr(2, 4);
            do_not_optimize_char(sub[0]);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < iterations; ++i) {
            fl::string sub = fl_s.substr(2, 4);
            do_not_optimize_char(sub[0]);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "Substr Comparison" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    // 8. Concatenation operator+ (if optimized version exists)
    {
        fl::string f1("abc"), f2("def");
        std::string s1("abc"), s2("def");

        Timer t1;
        for (int i = 0; i < iterations; ++i) {
            std::string res = s1 + s2;
            do_not_optimize_char(res[0]);
        }
        double std_time = t1.elapsed_us();

        Timer t2;
        for (int i = 0; i < iterations; ++i) {
            // Test if we can use + or just append for now
            fl::string res = f1;
            res.append(f2);
            do_not_optimize_char(res[0]);
        }
        double fl_time = t2.elapsed_us();
        std::cout << std::left << std::setw(30) << "Append (Concatenation)" 
                  << std::setw(15) << std_time << std::setw(15) << fl_time 
                  << std_time / fl_time << "x\n";
    }

    std::cout << "------------------------------------------------------------------\n";
}

int main() {
    run_benchmarks();
    return 0;
}
