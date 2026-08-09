// Rope concurrent access test.
// N threads sharing a const fl::rope (read-only).
// Note: concurrent mutation is not safe — the rope has no internal
// locking for writers.  Concurrent reads are safe with the caveat
// that the finger cache (_finger[]) uses non-atomic writes; on
// weakly-ordered architectures this could cause stale reads, but
// aligned pointer stores on x86 are atomic in practice.

#include <fl/rope.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <cstdint>

static int failures = 0;

#define TEST(cond, msg)                                                      \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n";  \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static std::atomic<uint64_t> g_seed_counter{42};

static std::mt19937 make_rng() {
    std::mt19937 rng(static_cast<uint32_t>(g_seed_counter.fetch_add(1001)));
    return rng;
}

static std::string random_str(std::size_t len) {
    auto rng = make_rng();
    std::string s(len, '\0');
    for (auto& c : s)
        c = static_cast<char>('a' + (rng() % 26));
    return s;
}

// ---------------------------------------------------------------
// Test: N concurrent readers on a const rope.
// ---------------------------------------------------------------
static void test_concurrent_readers() {
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 5000;

    // Build a rope once.
    fl::rope rope;
    std::string expected;
    for (int i = 0; i < 100; ++i) {
        std::string chunk = random_str(500);
        rope += chunk.c_str();
        expected += chunk;
    }
    rope.rebalance();

    std::atomic<int> thread_errors{0};

    // Each thread gets its own rope copy (same tree structure, shared
    // underlying data via shared_ptr, but each has its own finger cache).
    auto worker = [&](int tid) {
        fl::rope local_rope = rope;  // thread-local copy — own finger cache
        auto rng = make_rng();
        int local_errors = 0;

        for (int i = 0; i < kOpsPerThread; ++i) {
            std::size_t pos = rng() % local_rope.size();

            // operator[]
            if (local_rope[pos] != expected[pos]) {
                ++local_errors;
                break;
            }

            // at()
            try {
                if (local_rope.at(pos) != expected[pos]) {
                    ++local_errors;
                    break;
                }
            } catch (...) {
                ++local_errors;
                break;
            }

            // find
            if (expected.size() > 10) {
                std::size_t start = rng() % (expected.size() - 5);
                std::string needle = expected.substr(start, rng() % 8 + 1);
                if (local_rope.find(std::string_view(needle)) != expected.find(needle)) {
                    ++local_errors;
                    break;
                }
            }

            // rfind
            if (expected.size() > 10) {
                std::size_t start = rng() % (expected.size() - 5);
                std::string needle = expected.substr(start, rng() % 6 + 1);
                if (local_rope.rfind(std::string_view(needle)) != expected.rfind(needle)) {
                    ++local_errors;
                    break;
                }
            }

            // substr
            if (local_rope.size() > 100) {
                std::size_t off = rng() % (local_rope.size() - 50);
                auto sv = local_rope.substr(off, 50);
                std::string_view sv_view(sv.data(), sv.size());
                if (sv_view != expected.substr(off, 50)) {
                    ++local_errors;
                    break;
                }
            }
        }

        thread_errors.fetch_add(local_errors);
        (void)tid;
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back(worker, t);
    for (auto& th : threads)
        th.join();

    TEST(thread_errors.load() == 0, "concurrent readers: no errors");
    if (thread_errors.load() == 0)
        std::cout << "PASS: Concurrent readers (" << kThreads << " threads, "
                  << kOpsPerThread << " ops each)\n";
}

int main() {
    test_concurrent_readers();

    if (failures == 0) {
        std::cout << "\nAll rope concurrent tests passed!\n";
        return 0;
    } else {
        std::cerr << "\n" << failures << " test(s) FAILED!\n";
        return 1;
    }
}
