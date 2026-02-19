#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory_resource>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "fl/string.hpp"

namespace {

using clock_type = std::chrono::high_resolution_clock;

struct bench_result {
    std::string name;
    double std_median_us;
    double fl_median_us;
    double std_mean_us;
    double fl_mean_us;
    double std_cv_pct;
    double fl_cv_pct;
    double std_min_us;
    double fl_min_us;
    double std_max_us;
    double fl_max_us;
};

struct sample_stats {
    double median_us;
    double mean_us;
    double stdev_us;
    double cv_pct;
    double min_us;
    double max_us;
};

template <typename Fn>
double time_us(Fn&& fn) {
    auto start = clock_type::now();
    fn();
    auto end = clock_type::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

template <typename T>
inline void do_not_optimize(const T& value) {
#if defined(_MSC_VER)
    (void)*reinterpret_cast<const volatile unsigned char*>(&value);
    _ReadWriteBarrier();
#else
    if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(void*)) {
        asm volatile("" : : "g"(value) : "memory");
    } else {
        asm volatile("" : : "g"(&value) : "memory");
    }
#endif
}

inline void clobber_memory() {
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    asm volatile("" : : : "memory");
#endif
}

sample_stats summarize_samples(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t n = samples.size();

    const double median = (n % 2 == 1)
        ? samples[n / 2]
        : (samples[(n / 2) - 1] + samples[n / 2]) * 0.5;

    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(n);

    double sq_sum = 0.0;
    for (double value : samples) {
        const double diff = value - mean;
        sq_sum += diff * diff;
    }

    const double variance = (n > 1) ? (sq_sum / static_cast<double>(n - 1)) : 0.0;
    const double stdev = std::sqrt(variance);
    const double cv_pct = (mean > 0.0) ? (stdev / mean) * 100.0 : 0.0;

    return sample_stats{ median, mean, stdev, cv_pct, samples.front(), samples.back() };
}

int get_repeat_count() {
    constexpr int kDefaultRepeats = 5;
    constexpr int kMinRepeats = 3;
    constexpr int kMaxRepeats = 15;

    const char* env = std::getenv("FL_BENCH_REPEATS");
    if (!env) return kDefaultRepeats;

    const int parsed = std::atoi(env);
    if (parsed < kMinRepeats) return kMinRepeats;
    if (parsed > kMaxRepeats) return kMaxRepeats;
    return parsed;
}

bool expect_equal(const fl::string& actual, const std::string& expected, const char* label) {
    std::string_view actual_view = static_cast<std::string_view>(actual);
    if (actual_view != expected) {
        std::cerr << "[FAIL] " << label << "\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << actual_view << "\n";
        return false;
    }
    return true;
}

bool expect_true(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "[FAIL] " << label << "\n";
        return false;
    }
    return true;
}

bool run_correctness_suite() {
    std::cout << "=== Correctness Suite: fl::string vs std::string ===\n";

    bool ok = true;

    {
        fl::string fl_s;
        std::string std_s;
        ok &= expect_equal(fl_s, std_s, "default constructor");
    }

    {
        fl::string fl_s("hello");
        std::string std_s("hello");
        ok &= expect_equal(fl_s, std_s, "cstr constructor");
    }

    {
        fl::string fl_s(10, 'x');
        std::string std_s(10, 'x');
        ok &= expect_equal(fl_s, std_s, "repeated-char constructor");
    }

    {
        fl::string fl_s("abc");
        std::string std_s("abc");
        fl_s.append("def");
        std_s.append("def");
        fl_s.append(static_cast<size_t>(3), 'g');
        std_s.append(3, 'g');
        ok &= expect_equal(fl_s, std_s, "append cstr + repeat");
    }

    {
        fl::string fl_s("abc");
        std::string std_s("abc");
        fl_s += "def";
        std_s += "def";
        fl_s += 'X';
        std_s += 'X';
        ok &= expect_equal(fl_s, std_s, "operator+=");
    }

    {
        fl::string fl_s("abc");
        std::string std_s("abc");
        fl_s.push_back('Z');
        std_s.push_back('Z');
        fl_s.pop_back();
        std_s.pop_back();
        ok &= expect_equal(fl_s, std_s, "push_back/pop_back");
    }

    {
        fl::string fl_s("0123456789");
        std::string std_s("0123456789");
        fl_s.erase(3, 4);
        std_s.erase(3, 4);
        ok &= expect_equal(fl_s, std_s, "erase range");
    }

    {
        fl::string fl_s("01239");
        std::string std_s("01239");
        fl_s.insert(3, "456", 3);
        std_s.insert(3, "456", 3);
        ok &= expect_equal(fl_s, std_s, "insert cstr,len");
    }

    {
        fl::string fl_s("hello there");
        std::string std_s("hello there");
        fl_s.replace(6, 5, "world", 5);
        std_s.replace(6, 5, "world", 5);
        ok &= expect_equal(fl_s, std_s, "replace cstr,len");
    }

    {
        fl::string fl_s("abc");
        std::string std_s("abc");
        fl_s.resize(6, 'x');
        std_s.resize(6, 'x');
        ok &= expect_equal(fl_s, std_s, "resize grow");
        fl_s.resize(2);
        std_s.resize(2);
        ok &= expect_equal(fl_s, std_s, "resize shrink");
    }

    {
        fl::string fl_s("the quick brown fox");
        std::string std_s("the quick brown fox");
        ok &= expect_true(fl_s.find("quick") == std_s.find("quick"), "find substring");
        ok &= expect_true(fl_s.find('o') == std_s.find('o'), "find char");
        ok &= expect_true(fl_s.rfind('o') == std_s.rfind('o'), "rfind char");
        ok &= expect_true(fl_s.find_first_of('q') == std_s.find_first_of('q'), "find_first_of");
        ok &= expect_true(fl_s.find_last_of('o') == std_s.find_last_of('o'), "find_last_of");
        
        // Verify string_view overloads
        ok &= expect_true(fl_s.find(fl::string("quick")) == std_s.find(std::string("quick")), "find string");
        ok &= expect_true(fl_s.find(std::string_view("brown")) == std_s.find("brown"), "find string_view");
        ok &= expect_true(fl_s.find_first_of("aeiou") == std_s.find_first_of("aeiou"), "find_first_of vowels");
        ok &= expect_true(fl_s.find_first_not_of("aeiou") == std_s.find_first_not_of("aeiou"), "find_first_not_of vowels");
    }

    {
        fl::string fl_s("substring-test");
        std::string std_s("substring-test");
        fl::string fl_sub = fl_s.substr(3, 6);
        std::string std_sub = std_s.substr(3, 6);
        ok &= expect_equal(fl_sub, std_sub, "substr");

        auto fl_view = fl_s.substr_view(3, 6);
        std::string_view std_view(std_s.data() + 3, 6);
        ok &= expect_true(
            std::string_view(fl_view.data(), fl_view.size()) == std_view,
            "substr_view"
        );

        auto fl_slice = fl_s.slice(3, 6);
        ok &= expect_true(
            std::string_view(fl_slice.data(), fl_slice.size()) == std_view,
            "slice"
        );

        auto fl_left = fl_s.left_view(9);
        ok &= expect_true(
            std::string_view(fl_left.data(), fl_left.size()) == std::string_view("substring"),
            "left_view"
        );

        auto fl_right = fl_s.right_view(4);
        ok &= expect_true(
            std::string_view(fl_right.data(), fl_right.size()) == std::string_view("test"),
            "right_view"
        );

        auto fl_found_view = fl_s.find_view("string");
        ok &= expect_true(
            std::string_view(fl_found_view.data(), fl_found_view.size()) == std::string_view("string"),
            "find_view"
        );
    }

    {
        fl::string a("alpha-");
        fl::string b("beta-");
        fl::string c("gamma");

        fl::lazy_concat chain;
        chain.append(a).append(b).append(c);
        fl::string materialized = chain.materialize();
        ok &= expect_equal(materialized, std::string("alpha-beta-gamma"), "lazy_concat materialize");

        std::pmr::monotonic_buffer_resource pool;
        fl::basic_lazy_concat<std::pmr::polymorphic_allocator<fl::string>> pooled_chain{
            std::pmr::polymorphic_allocator<fl::string>(&pool)
        };
        pooled_chain.append("A").append("B").append("C");
        ok &= expect_equal(pooled_chain.materialize(), std::string("ABC"), "allocator lazy_concat");
    }

    {
        fl::string fl_a("alpha");
        fl::string fl_b("beta");
        std::string std_a("alpha");
        std::string std_b("beta");

        ok &= expect_true((fl_a == fl_a) == (std_a == std_a), "operator== true");
        ok &= expect_true((fl_a == fl_b) == (std_a == std_b), "operator== false");

        fl::string fl_c = fl_a + fl_b;
        std::string std_c = std_a + std_b;
        ok &= expect_equal(fl_c, std_c, "operator+");
    }

    {
        fl::string fl_s("reserve-me");
        std::string std_s("reserve-me");
        fl_s.reserve(256);
        std_s.reserve(256);
        fl_s.shrink_to_fit();
        std_s.shrink_to_fit();
        ok &= expect_equal(fl_s, std_s, "reserve/shrink_to_fit content");
    }

    std::cout << (ok ? "Correctness: PASS\n\n" : "Correctness: FAIL\n\n");
    return ok;
}

std::vector<bench_result> run_performance_suite() {
    std::cout << "=== Performance Suite: fl::string vs std::string ===\n";

    constexpr int iterations = 250000;
    constexpr int large_iterations = 50000;
    constexpr int lazy_profile_iterations = 50000;
    const int repeats = get_repeat_count();

    volatile std::size_t sink_size = 0;
    volatile char sink_char = 0;

    std::vector<bench_result> results;
    results.reserve(14);

    auto add_result = [&](std::string name, std::vector<double> std_samples, std::vector<double> fl_samples) {
        const sample_stats std_stats = summarize_samples(std::move(std_samples));
        const sample_stats fl_stats = summarize_samples(std::move(fl_samples));

        results.push_back({
            std::move(name),
            std_stats.median_us,
            fl_stats.median_us,
            std_stats.mean_us,
            fl_stats.mean_us,
            std_stats.cv_pct,
            fl_stats.cv_pct,
            std_stats.min_us,
            fl_stats.min_us,
            std_stats.max_us,
            fl_stats.max_us
        });
    };

    auto benchmark_pair = [&](auto&& std_fn, auto&& fl_fn) {
        constexpr int inner_runs = 3;
        std::vector<double> std_samples;
        std::vector<double> fl_samples;
        std_samples.reserve(static_cast<std::size_t>(repeats));
        fl_samples.reserve(static_cast<std::size_t>(repeats));

        std_fn();
        fl_fn();

        auto averaged_time = [&](auto&& fn) {
            double total_us = 0.0;
            for (int i = 0; i < inner_runs; ++i) {
                total_us += time_us(fn);
                clobber_memory();
            }
            return total_us / static_cast<double>(inner_runs);
        };

        for (int r = 0; r < repeats; ++r) {
            std_samples.push_back(averaged_time(std_fn));
            fl_samples.push_back(averaged_time(fl_fn));
        }

        return std::pair<std::vector<double>, std::vector<double>>{std::move(std_samples), std::move(fl_samples)};
    };

    auto benchmark_single = [&](auto&& fn) {
        constexpr int inner_runs = 3;
        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(repeats));

        fn();

        for (int r = 0; r < repeats; ++r) {
            double total_us = 0.0;
            for (int i = 0; i < inner_runs; ++i) {
                total_us += time_us(fn);
                clobber_memory();
            }
            samples.push_back(total_us / static_cast<double>(inner_runs));
        }

        return summarize_samples(std::move(samples));
    };

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < iterations; ++i) {
                std::string s("hello world");
                sink_char ^= s[static_cast<std::size_t>(i) % s.size()];
                do_not_optimize(s);
            }
        }, [&] {
            for (int i = 0; i < iterations; ++i) {
                fl::string s("hello world");
                sink_char ^= s[static_cast<std::size_t>(i) % s.size()];
                do_not_optimize(s);
            }
        });
        add_result("Construct small (SSO)", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < large_iterations; ++i) {
                std::string s("This is a longer payload that should live on heap for both string implementations.");
                sink_char ^= s[static_cast<std::size_t>(i) % s.size()];
                do_not_optimize(s);
            }
        }, [&] {
            for (int i = 0; i < large_iterations; ++i) {
                fl::string s("This is a longer payload that should live on heap for both string implementations.");
                sink_char ^= s[static_cast<std::size_t>(i) % s.size()];
                do_not_optimize(s);
            }
        });
        add_result("Construct large (heap)", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < large_iterations; ++i) {
                std::string s;
                for (int j = 0; j < 64; ++j) s.append("abcd");
                sink_size += s.size() + static_cast<std::size_t>(s[static_cast<std::size_t>(i) % s.size()]);
                do_not_optimize(s);
            }
        }, [&] {
            for (int i = 0; i < large_iterations; ++i) {
                fl::string s;
                for (int j = 0; j < 64; ++j) s.append("abcd");
                sink_size += s.size() + static_cast<std::size_t>(s[static_cast<std::size_t>(i) % s.size()]);
                do_not_optimize(s);
            }
        });
        add_result("Append small chunks", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            std::string s("the quick brown fox jumps over the lazy dog");
            for (int i = 0; i < iterations; ++i) {
                const auto pos = s.find("fox", static_cast<std::size_t>(i & 7));
                sink_size += (pos == std::string::npos) ? std::numeric_limits<std::size_t>::max() : pos;
            }
            do_not_optimize(sink_size);
        }, [&] {
            fl::string s("the quick brown fox jumps over the lazy dog");
            for (int i = 0; i < iterations; ++i) {
                const auto pos = s.find("fox", static_cast<std::size_t>(i & 7));
                sink_size += (pos == fl::string::npos) ? std::numeric_limits<std::size_t>::max() : pos;
            }
            do_not_optimize(sink_size);
        });
        add_result("Find substring", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            std::string s(4096, 'a');
            s[3580] = 'x';
            s[3581] = 'y';
            s[3582] = 'z';
            for (int i = 0; i < iterations; ++i) {
                const auto pos = s.find("xyz", static_cast<std::size_t>((i * 13) & 127));
                sink_size += (pos == std::string::npos) ? std::numeric_limits<std::size_t>::max() : pos;
            }
            do_not_optimize(s);
        }, [&] {
            fl::string s(std::string(4096, 'a'));
            s.replace(3580, 3, "xyz", 3);
            for (int i = 0; i < iterations; ++i) {
                const auto pos = s.find("xyz", static_cast<std::size_t>((i * 13) & 127));
                sink_size += (pos == fl::string::npos) ? std::numeric_limits<std::size_t>::max() : pos;
            }
            do_not_optimize(s);
        });
        add_result("Find substring (long)", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            std::string s("the quick brown fox jumps over the lazy dog");
            for (int i = 0; i < iterations; ++i) {
                std::string sub = s.substr(4, 15);
                sink_char ^= sub[static_cast<std::size_t>(i) % sub.size()];
                sink_size += sub.size();
                do_not_optimize(sub);
            }
        }, [&] {
            fl::string s("the quick brown fox jumps over the lazy dog");
            for (int i = 0; i < iterations; ++i) {
                fl::string sub = s.substr(4, 15);
                sink_char ^= sub[static_cast<std::size_t>(i) % sub.size()];
                sink_size += sub.size();
                do_not_optimize(sub);
            }
        });
        add_result("Substr", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < large_iterations; ++i) {
                std::string s("0123456789");
                const std::size_t pos = static_cast<std::size_t>(2 + (i % 6));
                s.insert(pos, "ABCD", 4);
                sink_size += s.size() + static_cast<std::size_t>(s[pos]);
                do_not_optimize(s);
            }
        }, [&] {
            for (int i = 0; i < large_iterations; ++i) {
                fl::string s("0123456789");
                const std::size_t pos = static_cast<std::size_t>(2 + (i % 6));
                s.insert(pos, "ABCD", 4);
                sink_size += s.size() + static_cast<std::size_t>(s[pos]);
                do_not_optimize(s);
            }
        });
        add_result("Insert mid", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < large_iterations; ++i) {
                std::string s("0123456789ABCDEFG");
                const std::size_t pos = static_cast<std::size_t>(2 + (i % 5));
                s.erase(pos, 3);
                sink_size += s.size() + static_cast<std::size_t>(s[pos % s.size()]);
                do_not_optimize(s);
            }
        }, [&] {
            for (int i = 0; i < large_iterations; ++i) {
                fl::string s("0123456789ABCDEFG");
                const std::size_t pos = static_cast<std::size_t>(2 + (i % 5));
                s.erase(pos, 3);
                sink_size += s.size() + static_cast<std::size_t>(s[pos % s.size()]);
                do_not_optimize(s);
            }
        });
        add_result("Erase mid", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < large_iterations; ++i) {
                std::string s("hello there world");
                const std::size_t pos = static_cast<std::size_t>(2 + (i % 8));
                s.replace(pos, 3, "wide", 4);
                sink_char ^= s[pos];
                do_not_optimize(s);
            }
        }, [&] {
            for (int i = 0; i < large_iterations; ++i) {
                fl::string s("hello there world");
                const std::size_t pos = static_cast<std::size_t>(2 + (i % 8));
                s.replace(pos, 3, "wide", 4);
                sink_char ^= s[pos];
                do_not_optimize(s);
            }
        });
        add_result("Replace mid", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            std::string a("compare-this-blob");
            std::string b("compare-this-blob");
            for (int i = 0; i < iterations; ++i) {
                b[0] = static_cast<char>((i & 1) ? 'c' : 'x');
                sink_size += (a == b) ? 1u : 0u;
            }
            do_not_optimize(b);
        }, [&] {
            fl::string a("compare-this-blob");
            fl::string b("compare-this-blob");
            for (int i = 0; i < iterations; ++i) {
                b[0] = static_cast<char>((i & 1) ? 'c' : 'x');
                sink_size += (a == b) ? 1u : 0u;
            }
            do_not_optimize(b);
        });
        add_result("Comparison ==", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            std::string a("left-");
            std::string b("right");
            for (int i = 0; i < iterations; ++i) {
                std::string c = a + b;
                do_not_optimize(c);
            }
            sink_size += static_cast<std::size_t>(a.size() + b.size());
        }, [&] {
            fl::string a("left-");
            fl::string b("right");
            for (int i = 0; i < iterations; ++i) {
                fl::string c = a + b;
                do_not_optimize(c);
            }
            sink_size += static_cast<std::size_t>(a.size() + b.size());
        });
        add_result("Operator+", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < large_iterations; ++i) {
                std::string out;
                out.reserve(4096);
                for (int j = 0; j < 128; ++j) {
                    out += "segment-0123456789abcdef";
                }
                sink_size += out.size() + static_cast<std::size_t>(out[static_cast<std::size_t>(i) & 15]);
                do_not_optimize(out);
            }
        }, [&] {
            for (int i = 0; i < large_iterations; ++i) {
                fl::lazy_concat chain;
                chain.reserve(128);
                for (int j = 0; j < 128; ++j) {
                    chain.append("segment-0123456789abcdef");
                }
                fl::string out = chain.materialize();
                sink_size += out.size() + static_cast<std::size_t>(out[static_cast<std::size_t>(i) & 15]);
                do_not_optimize(out);
            }
        });
        add_result("Lazy concat large", std::move(std_samples), std::move(fl_samples));
    }

    {
        auto [std_samples, fl_samples] = benchmark_pair([&] {
            for (int i = 0; i < iterations; ++i) {
                std::string out;
                out.reserve(256);
                for (int j = 0; j < 8; ++j) {
                    out += "segment-0123456789abcdef";
                }
                sink_size += out.size() + static_cast<std::size_t>(out[static_cast<std::size_t>(i) & 7]);
                do_not_optimize(out);
            }
        }, [&] {
            for (int i = 0; i < iterations; ++i) {
                fl::lazy_concat chain;
                chain.reserve(8);
                for (int j = 0; j < 8; ++j) {
                    chain.append("segment-0123456789abcdef");
                }
                fl::string out = chain.materialize();
                sink_size += out.size() + static_cast<std::size_t>(out[static_cast<std::size_t>(i) & 7]);
                do_not_optimize(out);
            }
        });
        add_result("Lazy concat micro", std::move(std_samples), std::move(fl_samples));
    }

    const sample_stats lazy_empty_branch = benchmark_single([&] {
        for (int i = 0; i < lazy_profile_iterations; ++i) {
            fl::lazy_concat chain;
            fl::string out = chain.materialize();
            sink_size += out.size();
            do_not_optimize(out);
        }
    });

    const sample_stats lazy_single_view_branch = benchmark_single([&] {
        for (int i = 0; i < lazy_profile_iterations; ++i) {
            fl::lazy_concat chain;
            chain.append("segment-0123456789abcdef");
            fl::string out = chain.materialize();
            sink_size += out.size() + static_cast<std::size_t>(out[0]);
            do_not_optimize(out);
        }
    });

    const sample_stats lazy_single_owned_branch = benchmark_single([&] {
        for (int i = 0; i < lazy_profile_iterations; ++i) {
            fl::lazy_concat chain;
            fl::string part("segment-0123456789abcdef");
            chain.append(std::move(part));
            fl::string out = chain.materialize();
            sink_size += out.size() + static_cast<std::size_t>(out[0]);
            do_not_optimize(out);
        }
    });

    const sample_stats lazy_multi_view_branch = benchmark_single([&] {
        for (int i = 0; i < lazy_profile_iterations; ++i) {
            fl::lazy_concat chain;
            chain.reserve(16);
            for (int j = 0; j < 16; ++j) {
                chain.append("segment-0123456789abcdef");
            }
            fl::string out = chain.materialize();
            sink_size += out.size() + static_cast<std::size_t>(out[static_cast<std::size_t>(i) & 15]);
            do_not_optimize(out);
        }
    });

    const sample_stats lazy_multi_owned_branch = benchmark_single([&] {
        for (int i = 0; i < lazy_profile_iterations; ++i) {
            fl::lazy_concat chain;
            chain.reserve(16);
            for (int j = 0; j < 16; ++j) {
                fl::string part("segment-0123456789abcdef");
                chain.append(std::move(part));
            }
            fl::string out = chain.materialize();
            sink_size += out.size() + static_cast<std::size_t>(out[static_cast<std::size_t>(i) & 15]);
            do_not_optimize(out);
        }
    });

    std::cout << "Samples per operation: " << repeats << " (median of per-sample 3-run averages)\n";
    std::cout << std::left << std::setw(24) << "Operation"
              << std::setw(14) << "std_med(us)"
              << std::setw(14) << "fl_med(us)"
              << std::setw(10) << "std/fl"
              << std::setw(12) << "std_cv(%)"
              << std::setw(12) << "fl_cv(%)"
              << "\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    for (const auto& item : results) {
        const double ratio = item.fl_median_us > 0.0 ? (item.std_median_us / item.fl_median_us) : 0.0;
        std::cout << std::left << std::setw(24) << item.name
                  << std::setw(14) << std::fixed << std::setprecision(3) << item.std_median_us
                  << std::setw(14) << std::fixed << std::setprecision(3) << item.fl_median_us
                  << std::setw(10) << std::fixed << std::setprecision(3) << ratio
                  << std::setw(12) << std::fixed << std::setprecision(2) << item.std_cv_pct
                  << std::setw(12) << std::fixed << std::setprecision(2) << item.fl_cv_pct
                  << "\n";
    }

    std::cout << "\n=== Lazy Concat Branch Profiling (fl::lazy_concat) ===\n";
    std::cout << std::left << std::setw(28) << "Case"
              << std::setw(16) << "median(us)"
              << std::setw(16) << "mean(us)"
              << std::setw(12) << "cv(%)"
              << std::setw(14) << "median(ns/op)"
              << "\n";
    std::cout << "------------------------------------------------------------------------------\n";

    auto print_lazy_profile = [&](const char* name, const sample_stats& stats) {
        const double ns_per_op = (stats.median_us * 1000.0) / static_cast<double>(lazy_profile_iterations);
        std::cout << std::left << std::setw(28) << name
                  << std::setw(16) << std::fixed << std::setprecision(3) << stats.median_us
                  << std::setw(16) << std::fixed << std::setprecision(3) << stats.mean_us
                  << std::setw(12) << std::fixed << std::setprecision(2) << stats.cv_pct
                  << std::setw(14) << std::fixed << std::setprecision(2) << ns_per_op
                  << "\n";
    };

    print_lazy_profile("materialize: empty", lazy_empty_branch);
    print_lazy_profile("materialize: single view", lazy_single_view_branch);
    print_lazy_profile("materialize: single owned", lazy_single_owned_branch);
    print_lazy_profile("materialize: multi view", lazy_multi_view_branch);
    print_lazy_profile("materialize: multi owned", lazy_multi_owned_branch);

    std::cout << "\n";
    return results;
}

}  // namespace

int main() {
    const bool correctness_ok = run_correctness_suite();
    const auto results = run_performance_suite();

    std::size_t fl_faster = 0;
    std::size_t std_faster = 0;
    std::size_t ties = 0;

    for (const auto& item : results) {
        if (item.fl_median_us < item.std_median_us) {
            ++fl_faster;
        } else if (item.fl_median_us > item.std_median_us) {
            ++std_faster;
        } else {
            ++ties;
        }
    }

    std::cout << "Summary: fl faster in " << fl_faster
              << ", std faster in " << std_faster
              << ", ties " << ties << " operations.\n";

    if (!correctness_ok) {
        std::cout << "Overall: FAIL\n";
        return 1;
    }

    std::cout << "Overall: PASS\n";
    return 0;
}