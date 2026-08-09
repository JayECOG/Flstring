// Rope stress test: interleaved read/write operations verified against
// a std::string oracle after every N operations.
//
// Exercises operator[], operator+=, insert, erase, substr, find, split,
// replace, compare, starts_with, ends_with in random sequences.

#include <fl/rope.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <cstring>
#include <cstdlib>

static int failures = 0;

#define TEST(cond, msg)                                                      \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n";  \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

// Deterministic RNG for reproducible tests.
static std::mt19937 rng(42);

// Generate a random string of given length.
static std::string random_str(std::size_t len) {
    std::string s(len, '\0');
    for (auto& c : s)
        c = static_cast<char>(' ' + (rng() % 95));  // printable ASCII
    return s;
}

// Verify rope == std::string content.
static void verify_eq(const fl::rope& rope, const std::string& expected,
                      const char* phase, int step) {
    bool ok = true;
    if (rope.size() != expected.size()) {
        std::cerr << "FAIL: " << phase << " " << step
                  << ": size mismatch rope=" << rope.size()
                  << " expected=" << expected.size() << "\n";
        ++failures;
        return;
    }

    // Compare via linear_view.
    {
        auto view = rope.linear_view();
        std::string_view rv(view.data(), view.size());
        if (rv != expected) {
            std::cerr << "FAIL: " << phase << " " << step
                      << ": content mismatch via linear_view\n";
            ++failures;
            return;
        }
    }

    // Compare via operator[].
    for (std::size_t i = 0; i < rope.size(); ++i) {
        if (rope[i] != expected[i]) {
            std::cerr << "FAIL: " << phase << " " << step
                      << ": char mismatch at " << i
                      << " rope='" << rope[i] << "' expected='"
                      << expected[i] << "'\n";
            ++failures;
            return;
        }
    }

    // Compare via flatten.
    auto flat = rope.flatten();
    if (flat != expected) {
        std::cerr << "FAIL: " << phase << " " << step
                  << ": content mismatch via flatten\n";
        ++failures;
        return;
    }

    // Compare via compare().
    if (rope.compare(expected) != 0) {
        std::cerr << "FAIL: " << phase << " " << step
                  << ": compare() returned non-zero\n";
        ++failures;
    }
}

int main() {
    // ---------------------------------------------------------------
    // Phase 1: Sequential concat + access + find interleave
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;
        constexpr int kOps = 500;

        // Build up incrementally.
        for (int i = 0; i < kOps; ++i) {
            std::string chunk = random_str(rng() % 64 + 1);
            r += chunk.c_str();
            expected += chunk;

            // Every 50 operations, verify everything.
            if (i % 50 == 0) {
                verify_eq(r, expected, "phase1 build", i);
            }

            // Every 20 operations, do a random access + find.
            if (i % 20 == 0 && !expected.empty()) {
                std::size_t pos = rng() % expected.size();
                char c_rope = r[pos];
                char c_std = expected[pos];
                TEST(c_rope == c_std, "phase1 random access");

                // find a short substring.
                if (expected.size() > 10) {
                    std::size_t start = rng() % (expected.size() - 5);
                    std::string needle = expected.substr(start, rng() % 8 + 1);
                    auto f_rope = r.find(std::string_view(needle));
                    auto f_std = expected.find(needle);
                    TEST(f_rope == f_std,
                         "phase1 find needle=" << needle << " rope=" << f_rope
                                               << " std=" << f_std);
                }
            }
        }

        verify_eq(r, expected, "phase1 final", 0);
        std::cout << "PASS: Phase 1 (sequential concat + interleaved access)\n";
    }

    // ---------------------------------------------------------------
    // Phase 2: Insert at random positions
    // ---------------------------------------------------------------
    {
        fl::rope r("Hello World");
        std::string expected("Hello World");

        for (int i = 0; i < 100; ++i) {
            std::string insert_str = random_str(rng() % 16 + 1);
            std::size_t pos = expected.empty() ? 0 : rng() % expected.size();
            r.insert(pos, insert_str);
            expected.insert(pos, insert_str);

            if (i % 25 == 0) verify_eq(r, expected, "phase2", i);
        }
        verify_eq(r, expected, "phase2 final", 0);
        std::cout << "PASS: Phase 2 (random insert)\n";
    }

    // ---------------------------------------------------------------
    // Phase 3: Erase at random positions
    // ---------------------------------------------------------------
    {
        // Build a rope large enough for repeated erasure.
        fl::rope r;
        std::string expected;
        for (int i = 0; i < 100; ++i) {
            std::string chunk = random_str(rng() % 100 + 1);
            r += chunk.c_str();
            expected += chunk;
        }

        for (int i = 0; i < 50; ++i) {
            if (expected.empty()) break;
            std::size_t pos = rng() % expected.size();
            std::size_t len = std::min<std::size_t>(rng() % 50 + 1, expected.size() - pos);
            r.erase(pos, len);
            expected.erase(pos, len);

            if (i % 10 == 0) verify_eq(r, expected, "phase3", i);
        }
        verify_eq(r, expected, "phase3 final", 0);
        std::cout << "PASS: Phase 3 (random erase)\n";
    }

    // ---------------------------------------------------------------
    // Phase 4: Replace at random positions
    // ---------------------------------------------------------------
    {
        fl::rope r("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        std::string expected("ABCDEFGHIJKLMNOPQRSTUVWXYZ");

        for (int i = 0; i < 50; ++i) {
            std::size_t pos = rng() % expected.size();
            std::size_t len = std::min<std::size_t>(rng() % 10 + 1, expected.size() - pos);
            std::string repl = random_str(rng() % 20 + 1);
            r.replace(pos, len, repl);
            expected.replace(pos, len, repl);

            if (i % 10 == 0) verify_eq(r, expected, "phase4", i);
        }
        verify_eq(r, expected, "phase4 final", 0);
        std::cout << "PASS: Phase 4 (random replace)\n";
    }

    // ---------------------------------------------------------------
    // Phase 5: Split operations
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;
        for (int i = 0; i < 50; ++i) {
            std::string chunk = random_str(rng() % 50 + 1);
            r += chunk.c_str();
            expected += chunk;
        }

        for (int i = 0; i < 20; ++i) {
            if (expected.empty()) break;
            std::size_t pos = rng() % expected.size();
            fl::rope rhs = r.split(pos);
            std::string rhs_expected = expected.substr(pos);
            expected = expected.substr(0, pos);

            verify_eq(r, expected, "phase5 left", i);
            verify_eq(rhs, rhs_expected, "phase5 right", i);

            // Recombine for next iteration.
            r += rhs;
            expected += rhs_expected;
        }
        std::cout << "PASS: Phase 5 (split)\n";
    }

    // ---------------------------------------------------------------
    // Phase 6: starts_with / ends_with / contains
    // ---------------------------------------------------------------
    {
        fl::rope r("The quick brown fox jumps over the lazy dog");

        TEST(r.starts_with("The"), "starts_with prefix");
        TEST(!r.starts_with("the"), "starts_with wrong case");
        TEST(r.starts_with(""), "starts_with empty");

        TEST(r.ends_with("dog"), "ends_with suffix");
        TEST(!r.ends_with("cat"), "ends_with absent");
        TEST(r.ends_with(""), "ends_with empty");

        TEST(r.contains("fox"), "contains middle");
        TEST(r.contains("quick brown"), "contains multi-word");
        TEST(!r.contains("turtle"), "contains absent");
        TEST(r.contains(""), "contains empty");
        std::cout << "PASS: Phase 6 (prefix/suffix/contains)\n";
    }

    // ---------------------------------------------------------------
    // Phase 7: Compare and spaceship
    // ---------------------------------------------------------------
    {
        fl::rope a("apple");
        fl::rope b("banana");
        fl::rope c("apple");

        TEST((a <=> b) < 0,  "spaceship less");
        TEST((b <=> a) > 0,  "spaceship greater");
        TEST((a <=> c) == 0, "spaceship equal");
        TEST(a == c, "equality true");
        TEST(!(a == b), "equality false");
        TEST(a != b, "inequality true");
        TEST(a < b, "less than");
        TEST(!(b < a), "not less than");

        // Test against string_view.
        TEST(a.compare("apple") == 0, "compare sv equal");
        TEST(a.compare("banana") < 0, "compare sv less");

        std::cout << "PASS: Phase 7 (comparison)\n";
    }

    // ---------------------------------------------------------------
    // Phase 8: Mixed rapid-fire (all operations interleaved)
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;

        for (int iter = 0; iter < 200; ++iter) {
            int op = rng() % 7;
            switch (op) {
            case 0: {  // append
                std::string s = random_str(rng() % 32 + 1);
                r += s.c_str();
                expected += s;
                break;
            }
            case 1: {  // insert
                if (expected.empty()) break;
                std::string s = random_str(rng() % 16 + 1);
                std::size_t p = rng() % expected.size();
                r.insert(p, s);
                expected.insert(p, s);
                break;
            }
            case 2: {  // erase
                if (expected.empty()) break;
                std::size_t p = rng() % expected.size();
                std::size_t l = std::min<std::size_t>(rng() % 20 + 1, expected.size() - p);
                r.erase(p, l);
                expected.erase(p, l);
                break;
            }
            case 3: {  // replace
                if (expected.empty()) break;
                std::size_t p = rng() % expected.size();
                std::size_t l = std::min<std::size_t>(rng() % 10 + 1, expected.size() - p);
                std::string s = random_str(rng() % 20 + 1);
                r.replace(p, l, s);
                expected.replace(p, l, s);
                break;
            }
            case 4: {  // split + rejoin
                if (expected.size() < 10) break;
                std::size_t p = rng() % expected.size();
                fl::rope rhs = r.split(p);
                std::string rhs_exp = expected.substr(p);
                expected = expected.substr(0, p);
                // Verify both halves.
                verify_eq(r, expected, "phase8 split left", iter);
                verify_eq(rhs, rhs_exp, "phase8 split right", iter);
                // Rejoin.
                r += rhs;
                expected += rhs_exp;
                break;
            }
            case 5: {  // find
                if (expected.size() < 5) break;
                std::size_t start = rng() % (expected.size() - 3);
                std::string needle = expected.substr(start, rng() % 8 + 1);
                auto fr = r.find(std::string_view(needle));
                auto fe = expected.find(needle);
                TEST(fr == fe, "phase8 find in mixed ops");
                break;
            }
            case 6: {  // access and compare
                if (expected.empty()) break;
                std::size_t p = rng() % expected.size();
                TEST(r[p] == expected[p], "phase8 access in mixed ops");
                break;
            }
            }

            // Full verification every 50 iterations.
            if (iter % 50 == 49) {
                verify_eq(r, expected, "phase8 verify", iter);
            }
        }

        verify_eq(r, expected, "phase8 final", 0);
        std::cout << "PASS: Phase 8 (mixed rapid-fire)\n";
    }

    // ---------------------------------------------------------------
    // Phase 9: Find across leaf boundaries with large ropes
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;

        // Build a rope where the needle straddles leaf boundaries.
        // Each leaf is up to 16 KB; fill with 'x', then place a unique
        // pattern at the very end to test cross-boundary find.
        for (int i = 0; i < 5; ++i) {
            std::string chunk(10000, 'x');
            r += chunk.c_str();
            expected += chunk;
        }

        // Replace the last few chars with a test pattern.
        std::string marker = "UNIQUESTRING!";
        r.erase(r.size() - marker.size(), marker.size());
        r += marker.c_str();
        expected.erase(expected.size() - marker.size(), marker.size());
        expected += marker;

        auto pos = r.find(std::string_view(marker));
        TEST(pos != fl::rope::npos, "phase9 find cross-boundary marker");
        TEST(pos == expected.find(marker),
             "phase9 find cross-boundary position correct");

        // rfind the same marker.
        auto rpos = r.rfind(std::string_view(marker));
        TEST(rpos == pos, "phase9 rfind cross-boundary");

        std::cout << "PASS: Phase 9 (cross-boundary find)\n";
    }

    // ---------------------------------------------------------------
    // Phase 10: Large rope stress (concatenation, flatten, substr)
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;
        constexpr int kChunks = 500;
        constexpr int kChunkSize = 1024;

        for (int i = 0; i < kChunks; ++i) {
            std::string chunk = random_str(kChunkSize);
            r += chunk.c_str();
            expected += chunk;
        }

        TEST(r.size() == expected.size(), "phase10 large size");

        // flatten.
        auto flat = r.flatten();
        TEST(flat.size() == expected.size(), "phase10 flatten size");
        TEST(flat == expected, "phase10 flatten content");

        // substr from middle.
        auto sv = r.substr(400000, 5000);
        auto exp_sv = expected.substr(400000, 5000);
        std::string_view sv_view(sv.data(), sv.size());
        TEST(sv_view == exp_sv, "phase10 substr content");

        // find across large ropes.
        std::string needle = expected.substr(300000, 20);
        auto fr = r.find(std::string_view(needle), 290000);
        auto fe = expected.find(needle, 290000);
        TEST(fr == fe, "phase10 find at offset");

        // sequential access (stress the finger cache).
        std::uint64_t sum_rope = 0, sum_std = 0;
        for (std::size_t i = 0; i < r.size(); i += 17) {
            sum_rope += static_cast<unsigned char>(r[i]);
            sum_std += static_cast<unsigned char>(expected[i]);
        }
        TEST(sum_rope == sum_std, "phase10 seq access checksum");

        std::cout << "PASS: Phase 10 (large rope stress)\n";
    }

    // ---------------------------------------------------------------
    // Summary
    // ---------------------------------------------------------------
    if (failures == 0) {
        std::cout << "\nAll rope stress tests passed!\n";
        return 0;
    } else {
        std::cerr << "\n" << failures << " test(s) FAILED!\n";
        return 1;
    }
}
