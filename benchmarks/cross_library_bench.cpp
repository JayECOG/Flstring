// Cross-library string benchmark: fl vs std vs Boost.Container vs absl::Cord
//
// Libraries under test:
//   std::string              — libstdc++ (glibc) reference
//   fl::string               — this library, custom pool allocator + SSO ≤23 B
//   boost::container::string — Boost 1.83, alternative SSO impl
//
// For rope/concat/substr the comparison set is:
//   fl::rope                 — AVL-balanced concat tree, O(1) amortized concat
//   absl::Cord               — Google Abseil rope (chunk B-tree, 20220623 LTS)
//   std::string              — eager concatenation baseline
//
// Warm-up: 10 % of iteration count per cell, capped at 1 000 iterations.
// Results in ns/op.  Build -O2 Release; run CPU-pinned: taskset -c 0.
//
// Sections
//   A. Construction — SSO path (11 chars, no heap)
//   B. Construction — heap path (100 chars)
//   C. Append growth — build 4 KB via 256 × append("0123456789abcdef")
//   D. find()  — 1 024-byte haystack, 13-char needle, late position (90 %)
//   E. compare() — 64-char strings, three cases averaged
//   F. substr()  — 64 chars from middle of 512-char string
//   G. Rope concat — N × 100-char append then flatten  (N = 1 000 / 10 000 / 100 000)
//   H. Rope substr — 1 000-char slice from 1 MB rope

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <absl/strings/cord.h>
#include <absl/strings/string_view.h>  // absl::string_view != std::string_view in 20220623
#include <boost/container/string.hpp>

// Production hooks path (no FL_HOOKS_ALWAYS_DEFAULT): exercises the same code
// path a library consumer would hit, not the benchmark-only compile-time short-circuit.
#include "fl/string.hpp"
#include "fl/rope.hpp"

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

struct Timer {
    std::chrono::high_resolution_clock::time_point t0;
    Timer() : t0(std::chrono::high_resolution_clock::now()) {}
    double elapsed_ns() const {
        using namespace std::chrono;
        return duration<double, std::nano>(high_resolution_clock::now() - t0).count();
    }
};

static volatile std::size_t vsink;
static void sink(std::size_t v) { vsink = v; }

static std::string make_random_str(std::size_t len, uint32_t seed = 0xDEADBEEF) {
    std::mt19937 rng(seed);
    std::string s(len, '\0');
    for (char& c : s) c = 'a' + static_cast<char>(rng() % 26);
    return s;
}

template <typename Fn>
static double measure(int iters, Fn&& fn) {
    const int warmup = std::min(iters / 10, 1000);
    for (int i = 0; i < warmup; ++i) fn(i);
    Timer t;
    for (int i = 0; i < iters; ++i) fn(i);
    return t.elapsed_ns() / iters;
}

// ---------------------------------------------------------------------------
// Table helpers
// ---------------------------------------------------------------------------

static void section(const char* title) {
    std::cout << "\n=== " << title << " ===\n";
    std::cout << std::left
              << std::setw(30) << "Library"
              << std::setw(16) << "ns/op"
              << std::setw(14) << "vs std"
              << '\n'
              << std::string(60, '-') << '\n';
}

static void row(const char* lib, double ns, double std_ns) {
    double ratio = ns / std_ns;
    const char* verdict = (ratio < 0.93) ? "faster"
                        : (ratio > 1.07) ? "slower"
                                         : "parity";
    std::cout << std::left
              << std::setw(30) << lib
              << std::setw(16) << std::fixed << std::setprecision(2) << ns
              << std::setw(10) << std::setprecision(3) << ratio
              << "  " << verdict << '\n';
}

// ---------------------------------------------------------------------------
// A. Construction — SSO path (11 chars, no heap)
// ---------------------------------------------------------------------------
static void bench_construction_sso() {
    // Volatile pointer: compiler cannot prove the value is compile-time constant,
    // preventing DCE of the construction loop body.
    static const char* volatile sso_src = "hello world";   // 11 chars
    constexpr int ITERS = 200000;

    section("A. Construction — SSO (11 chars, no heap)  iters=200000");

    double std_ns = measure(ITERS, [](int) {
        const char* s = sso_src;
        std::string str(s, 11);
        sink(str.size());
    });
    double fl_ns = measure(ITERS, [](int) {
        const char* s = sso_src;
        fl::string str(s, 11);
        sink(str.size());
    });
    double boost_ns = measure(ITERS, [](int) {
        const char* s = sso_src;
        boost::container::string str(s, 11);
        sink(str.size());
    });

    row("std::string",               std_ns,   std_ns);
    row("fl::string",                fl_ns,    std_ns);
    row("boost::container::string",  boost_ns, std_ns);
}

// ---------------------------------------------------------------------------
// B. Construction — heap path (100 chars)
// ---------------------------------------------------------------------------
static void bench_construction_heap() {
    static std::string heap_buf = make_random_str(100);
    static const char* volatile heap_src = nullptr;
    heap_src = heap_buf.data();
    constexpr int ITERS = 100000;

    section("B. Construction — heap (100 chars)  iters=100000");

    double std_ns = measure(ITERS, [](int) {
        const char* s = heap_src;
        std::string str(s, 100);
        sink(str.size());
    });
    double fl_ns = measure(ITERS, [](int) {
        const char* s = heap_src;
        fl::string str(s, 100);
        sink(str.size());
    });
    double boost_ns = measure(ITERS, [](int) {
        const char* s = heap_src;
        boost::container::string str(s, 100);
        sink(str.size());
    });

    row("std::string",               std_ns,   std_ns);
    row("fl::string",                fl_ns,    std_ns);
    row("boost::container::string",  boost_ns, std_ns);
}

// ---------------------------------------------------------------------------
// C. Append growth — build 4 KB via 256 × append("0123456789abcdef", 16 bytes)
//
// No pre-reserve: exercises the reallocation / size-class-crossing path.
// Reports ns per individual append call (total run cost / APPENDS).
// ---------------------------------------------------------------------------
static void bench_append_growth() {
    constexpr int APPENDS = 256;
    constexpr int ITERS   = 30000;
    static const char* volatile frag_src = "0123456789abcdef";  // 16 bytes

    section("C. Append growth — 256 × 16 B → 4 KB  (ns/op = ns per append)  iters=30000");

    double std_ns = measure(ITERS, [](int) {
        const char* f = frag_src;
        std::string s;
        for (int i = 0; i < APPENDS; ++i) s.append(f, 16);
        sink(s.size());
    });
    double fl_ns = measure(ITERS, [](int) {
        const char* f = frag_src;
        fl::string s;
        for (int i = 0; i < APPENDS; ++i) s.append(f, 16);
        sink(s.size());
    });
    double boost_ns = measure(ITERS, [](int) {
        const char* f = frag_src;
        boost::container::string s;
        for (int i = 0; i < APPENDS; ++i) s.append(f, 16);
        sink(s.size());
    });

    row("std::string",               std_ns / APPENDS,   std_ns / APPENDS);
    row("fl::string",                fl_ns / APPENDS,    std_ns / APPENDS);
    row("boost::container::string",  boost_ns / APPENDS, std_ns / APPENDS);
}

// ---------------------------------------------------------------------------
// D. find() — 1 024-byte haystack, 13-char needle, planted at 90 %
// ---------------------------------------------------------------------------
static void bench_find() {
    const std::string_view needle("xyzabcdefghij");   // 13 chars, 'x' prefix
    constexpr std::size_t HSZ    = 1024;
    const std::size_t    plant   = static_cast<std::size_t>(HSZ * 0.90);

    std::mt19937 rng(0xC0FFEE);
    std::string haystack(HSZ, '\0');
    for (char& c : haystack) {
        char cand = 'a' + static_cast<char>(rng() % 25);  // a..y, skip 'x'
        c = (cand >= 'x') ? static_cast<char>(cand + 1) : cand;
    }
    std::memcpy(&haystack[plant], needle.data(), needle.size());

    const fl::string              fl_hay(haystack.data(), haystack.size());
    const boost::container::string boost_hay(haystack.data(), haystack.size());
    const std::string              needle_std(needle);

    constexpr int ITERS = 300000;
    section("D. find() — 1 024-byte haystack, 13-char needle at 90 %  iters=300000");

    double std_ns = measure(ITERS, [&](int) {
        sink(haystack.find(needle_std));
    });
    double fl_ns = measure(ITERS, [&](int) {
        sink(fl_hay.find(needle));
    });
    double boost_ns = measure(ITERS, [&](int) {
        // boost::container::string::find does not accept std::string;
        // use the (const char*, pos, count) overload instead.
        sink(boost_hay.find(needle_std.c_str(), 0, needle_std.size()));
    });

    row("std::string",               std_ns,   std_ns);
    row("fl::string",                fl_ns,    std_ns);
    row("boost::container::string",  boost_ns, std_ns);
}

// ---------------------------------------------------------------------------
// E. compare() — 64-char strings, three cases averaged
//
// Cases: equal, early-differ (pos 0), late-differ (pos 63).
// Reports the average ns per compare call across all three patterns.
// ---------------------------------------------------------------------------
static void bench_compare() {
    const std::string a_str = make_random_str(64, 0xAAAA);
    std::string b_equal = a_str;
    std::string b_early = a_str; b_early[0]  = '\x01';  // a_str > b_early
    std::string b_late  = a_str; b_late[63]  = '\x01';

    const fl::string fl_a(a_str.data(), a_str.size());
    const fl::string fl_b_eq(b_equal.data(), b_equal.size());
    const fl::string fl_b_early(b_early.data(), b_early.size());
    const fl::string fl_b_late(b_late.data(), b_late.size());

    const boost::container::string boost_a(a_str.data(), a_str.size());
    const boost::container::string boost_b_eq(b_equal.data(), b_equal.size());
    const boost::container::string boost_b_early(b_early.data(), b_early.size());
    const boost::container::string boost_b_late(b_late.data(), b_late.size());

    constexpr int ITERS = 500000;
    section("E. compare() — 64-char strings, 3-case average  iters=500000×3");

    auto std_ns = measure(ITERS * 3, [&](int i) {
        int r = 0;
        switch (i % 3) {
            case 0: r = a_str.compare(b_equal); break;
            case 1: r = a_str.compare(b_early); break;
            case 2: r = a_str.compare(b_late);  break;
        }
        sink(static_cast<std::size_t>(r + 128));
    }) / 3.0;

    auto fl_ns = measure(ITERS * 3, [&](int i) {
        int r = 0;
        switch (i % 3) {
            case 0: r = fl_a.compare(fl_b_eq);    break;
            case 1: r = fl_a.compare(fl_b_early); break;
            case 2: r = fl_a.compare(fl_b_late);  break;
        }
        sink(static_cast<std::size_t>(r + 128));
    }) / 3.0;

    auto boost_ns = measure(ITERS * 3, [&](int i) {
        int r = 0;
        switch (i % 3) {
            case 0: r = boost_a.compare(boost_b_eq);    break;
            case 1: r = boost_a.compare(boost_b_early); break;
            case 2: r = boost_a.compare(boost_b_late);  break;
        }
        sink(static_cast<std::size_t>(r + 128));
    }) / 3.0;

    row("std::string",               std_ns,   std_ns);
    row("fl::string",                fl_ns,    std_ns);
    row("boost::container::string",  boost_ns, std_ns);
}

// ---------------------------------------------------------------------------
// F. substr() — extract 64 chars from pos 224 of a 512-char string
// ---------------------------------------------------------------------------
static void bench_substr() {
    const std::string src512 = make_random_str(512);
    const fl::string fl_src(src512.data(), src512.size());
    const boost::container::string boost_src(src512.data(), src512.size());
    constexpr std::size_t POS = 224;
    constexpr std::size_t LEN = 64;
    constexpr int ITERS = 300000;

    section("F. substr() — 64 chars from pos 224 of 512-char string  iters=300000");

    double std_ns = measure(ITERS, [&](int) {
        auto s = src512.substr(POS, LEN);
        sink(s.size());
    });
    double fl_ns = measure(ITERS, [&](int) {
        auto s = fl_src.substr(POS, LEN);
        sink(s.size());
    });
    double boost_ns = measure(ITERS, [&](int) {
        auto s = boost_src.substr(POS, LEN);
        sink(s.size());
    });

    row("std::string",               std_ns,   std_ns);
    row("fl::string",                fl_ns,    std_ns);
    row("boost::container::string",  boost_ns, std_ns);
}

// ---------------------------------------------------------------------------
// G. Rope/concat — N × 100-char append then flatten
//
// Three N values: 1 000 / 10 000 / 100 000.
// Libraries: fl::rope (AVL tree), absl::Cord (chunk B-tree), std::string (eager).
// Cost is entire build-then-flatten; ns/append = total cost / N.
// ---------------------------------------------------------------------------
static void bench_rope_concat() {
    const std::string frag100 = make_random_str(100, 0xF00D);
    const std::string_view frag_sv(frag100);
    constexpr int Ns[] = {1000, 10000, 100000};

    std::cout << "\n=== G. Rope/concat — N × 100-char append + flatten ===\n"
              << "  (one build + one materialise per row; ns/append = total/N)\n\n"
              << std::left
              << std::setw(10) << "N"
              << std::setw(30) << "Library"
              << std::setw(14) << "ms total"
              << std::setw(14) << "ns/append"
              << "vs std\n"
              << std::string(68, '-') << '\n';

    for (int N : Ns) {
        double std_ms, fl_ms, cord_ms;

        {
            Timer t;
            std::string s;
            for (int i = 0; i < N; ++i) s += frag_sv;
            sink(s.size());
            std_ms = t.elapsed_ns() / 1e6;
        }
        {
            Timer t;
            fl::rope r;
            for (int i = 0; i < N; ++i) r += frag_sv;
            auto flat = r.to_std_string();
            sink(flat.size());
            fl_ms = t.elapsed_ns() / 1e6;
        }
        {
            Timer t;
            absl::Cord c;
            // absl::Cord::Append needs absl::string_view, not std::string_view
            absl::string_view absl_frag(frag_sv.data(), frag_sv.size());
            for (int i = 0; i < N; ++i) c.Append(absl_frag);
            std::string flat(c);
            sink(flat.size());
            cord_ms = t.elapsed_ns() / 1e6;
        }

        double std_ns_pa  = std_ms  * 1e6 / N;
        double fl_ns_pa   = fl_ms   * 1e6 / N;
        double cord_ns_pa = cord_ms * 1e6 / N;

        auto grow = [&](const char* lib, double ms, double ns_pa) {
            double ratio = ns_pa / std_ns_pa;
            const char* v = (ratio<0.93)?"faster":(ratio>1.07)?"slower":"parity";
            std::cout << std::left
                      << std::setw(10) << N
                      << std::setw(30) << lib
                      << std::setw(14) << std::fixed << std::setprecision(3) << ms
                      << std::setw(14) << std::setprecision(2) << ns_pa
                      << std::setprecision(3) << ratio << "  " << v << '\n';
        };
        grow("std::string +=",  std_ms,  std_ns_pa);
        grow("fl::rope +=",     fl_ms,   fl_ns_pa);
        grow("absl::Cord",      cord_ms, cord_ns_pa);
        std::cout << '\n';
    }
}

// ---------------------------------------------------------------------------
// H. Rope substr — extract 1 000-char slice from a pre-built 1 MB body
//
// Body: 10 000 × 100-char fragments.
// Slice: pos 500 000, len 1 000.
// std::string baseline is already contiguous (pre-built, no concat cost here).
// ---------------------------------------------------------------------------
static void bench_rope_substr() {
    const std::string frag100 = make_random_str(100, 0x5EED);
    constexpr int     CONCAT_N = 10000;
    constexpr std::size_t POS  = 500000;
    constexpr std::size_t LEN  = 1000;
    constexpr int ITERS        = 5000;

    fl::rope fl_body;
    for (int i = 0; i < CONCAT_N; ++i) fl_body += frag100;

    absl::Cord cord_body;
    {
        absl::string_view absl_frag(frag100.data(), frag100.size());
        for (int i = 0; i < CONCAT_N; ++i) cord_body.Append(absl_frag);
    }

    const std::string std_body = [&] {
        std::string s;
        s.reserve(static_cast<std::size_t>(CONCAT_N) * 100);
        for (int i = 0; i < CONCAT_N; ++i) s += frag100;
        return s;
    }();

    section("H. Rope substr — 1 000-char slice from 1 MB body  iters=5000");
    std::cout << "  (std baseline = pre-materialised contiguous string)\n\n";

    double std_ns = measure(ITERS, [&](int) {
        auto s = std_body.substr(POS, LEN);
        sink(s.size());
    });
    double fl_ns = measure(ITERS, [&](int) {
        auto sv = fl_body.substr(POS, LEN);
        sink(sv.size());
    });
    // absl::Cord::Subcord is O(log n) on the cord; materialise to string is O(len)
    double cord_ns = measure(ITERS, [&](int) {
        auto sub = cord_body.Subcord(POS, LEN);
        std::string s(sub);
        sink(s.size());
    });

    std::cout << std::left
              << std::setw(30) << "Library"
              << std::setw(16) << "ns/op"
              << std::setw(14) << "vs std"
              << '\n'
              << std::string(60, '-') << '\n';

    auto pr = [&](const char* lib, double ns) {
        double ratio = ns / std_ns;
        const char* v = (ratio<0.93)?"faster":(ratio>1.07)?"slower":"parity";
        std::cout << std::left
                  << std::setw(30) << lib
                  << std::setw(16) << std::fixed << std::setprecision(2) << ns
                  << std::setprecision(3) << ratio << "  " << v << '\n';
    };
    pr("std::string (contiguous)", std_ns);
    pr("fl::rope::substr",         fl_ns);
    pr("absl::Cord::Subcord",      cord_ns);

    std::cout << "\n  Note: fl::rope and absl::Cord avoid the O(n) eagercopy during build;\n"
                 "  substr cost is O(log n) tree-walk + O(len) copy.\n"
                 "  std::string substr from a pre-built buffer is O(len) only.\n";
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "=== Cross-library string benchmark ===\n"
              << "std::string / fl::string / boost::container::string\n"
              << "Rope section: fl::rope / absl::Cord / std::string\n"
              << "Build: -O2 Release.  Run with: taskset -c 0 ./cross_library_bench\n"
              << "absl 20220623  Boost 1.83  fl (this build)\n";

    bench_construction_sso();
    bench_construction_heap();
    bench_append_growth();
    bench_find();
    bench_compare();
    bench_substr();
    bench_rope_concat();
    bench_rope_substr();

    std::cout << "\nDone.\n";
    return 0;
}
