// Rope edge case test.
// Tests boundary conditions, empty ropes, SSO transitions,
// leaf boundaries, and extreme sizes.

#include <fl/rope.hpp>
#include <iostream>
#include <string>
#include <limits>
#include <cstdint>

static int failures = 0;

#define TEST(cond, name) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << name << " (line " << __LINE__ << ")\n"; \
            ++failures; \
        } else { \
            std::cout << "PASS: " << name << "\n"; \
        } \
    } while (0)

int main() {
    // ---------------------------------------------------------------
    // 1. Empty rope
    // ---------------------------------------------------------------
    {
        fl::rope r;
        TEST(r.empty(), "empty rope: empty");
        TEST(r.size() == 0, "empty rope: size 0");
        TEST(r.length() == 0, "empty rope: length 0");
        TEST(r == fl::rope(), "empty rope: equality with another empty");
        TEST(!(r != fl::rope()), "empty rope: inequality false");

        auto sv = r.substr(0, 0);
        TEST(sv.size() == 0, "empty rope: substr returns empty");

        auto f = r.find("hello");
        TEST(f == fl::rope::npos, "empty rope: find returns npos");

        auto rf = r.rfind("hello");
        TEST(rf == fl::rope::npos, "empty rope: rfind returns npos");

        auto flat = r.flatten();
        TEST(flat.empty(), "empty rope: flatten is empty");

        std::string s = r.to_std_string();
        TEST(s.empty(), "empty rope: to_std_string is empty");

        TEST(r.starts_with("") == true, "empty rope: starts_with empty");
        TEST(r.ends_with("") == true, "empty rope: ends_with empty");
        TEST(r.contains("") == true, "empty rope: contains empty");

        std::cout << "PASS: Edge 1 (empty rope)\n";
    }

    // ---------------------------------------------------------------
    // 2. Single character
    // ---------------------------------------------------------------
    {
        fl::rope r("a");
        TEST(r.size() == 1, "single char: size");
        TEST(r[0] == 'a', "single char: operator[]");
        TEST(r.at(0) == 'a', "single char: at()");
        TEST(r.front() == 'a', "single char: front()");
        TEST(r.back() == 'a', "single char: back()");

        auto sv = r.substr(0, 1);
        std::string_view sv_view(sv.data(), sv.size());
        TEST(sv_view == "a", "single char: substr");

        auto f = r.find("a");
        TEST(f == 0, "single char: find 'a'");

        auto flat = r.flatten();
        TEST(flat == "a", "single char: flatten");

        std::cout << "PASS: Edge 2 (single character)\n";
    }

    // ---------------------------------------------------------------
    // 3. SSO boundary: 23 bytes (fits SSO)
    // ---------------------------------------------------------------
    {
        std::string s = std::string(23, 'x');
        fl::rope r(s.c_str(), s.size());
        TEST(r.size() == 23, "SSO boundary 23: size");

        auto flat = r.flatten();
        TEST(flat == s, "SSO boundary 23: flatten");

        bool all_ok = true;
        for (std::size_t i = 0; i < 23; ++i)
            if (r[i] != 'x') all_ok = false;
        TEST(all_ok, "SSO boundary 23: all chars correct");

        std::cout << "PASS: Edge 3 (SSO boundary 23)\n";
    }

    // ---------------------------------------------------------------
    // 4. Just beyond SSO: 24 bytes
    // ---------------------------------------------------------------
    {
        std::string s = std::string(24, 'y');
        fl::rope r(s.c_str(), s.size());
        TEST(r.size() == 24, "SSO+1 boundary 24: size");

        auto flat = r.flatten();
        TEST(flat == s, "SSO+1 boundary 24: flatten");

        bool all_ok = true;
        for (std::size_t i = 0; i < 24; ++i)
            if (r[i] != 'y') all_ok = false;
        TEST(all_ok, "SSO+1 boundary 24: all chars correct");

        // Compare against string_view.
        TEST(r.compare(s) == 0, "SSO+1 boundary 24: compare");

        std::cout << "PASS: Edge 4 (SSO+1 boundary 24)\n";
    }

    // ---------------------------------------------------------------
    // 5. Single leaf boundary: near 16 KB
    // ---------------------------------------------------------------
    {
        std::string s(16000, 'z');
        fl::rope r(s.c_str(), s.size());
        TEST(r.size() == 16000, "16KB leaf: size");

        auto flat = r.flatten();
        TEST(flat == s, "16KB leaf: flatten");

        // Random access checksum.
        std::uint64_t sum_rope = 0, sum_std = 0;
        for (std::size_t i = 0; i < 16000; i += 100) {
            sum_rope += static_cast<unsigned char>(r[i]);
            sum_std += static_cast<unsigned char>(s[i]);
        }
        TEST(sum_rope == sum_std, "16KB leaf: access checksum");

        // substr from near the end.
        auto sv = r.substr(15900, 50);
        std::string_view sv_view(sv.data(), sv.size());
        auto exp_sv = s.substr(15900, 50);
        TEST(sv_view == exp_sv, "16KB leaf: tail substr");

        std::cout << "PASS: Edge 5 (single leaf ~16KB)\n";
    }

    // ---------------------------------------------------------------
    // 6. Just above leaf boundary: 16385 bytes → forces split
    // ---------------------------------------------------------------
    {
        std::string s(16385, 'w');
        fl::rope r(s.c_str(), s.size());
        TEST(r.size() == 16385, "above leaf max: size");

        auto flat = r.flatten();
        TEST(flat == s, "above leaf max: flatten");

        // Find something.
        auto f = r.find("www");
        TEST(f == 0, "above leaf max: find at start");

        // Cross-boundary find with a unique marker only at the boundary.
        // Build: 16380 bytes of 'x', then "UNIQUE!" (7 bytes to span leaves).
        std::string s2(16380, 'x');
        s2 += "UNIQUE!";
        fl::rope r2(s2.c_str(), s2.size());
        TEST(r2.size() == 16387, "cross-boundary rope: size");
        auto fb = r2.find("UNIQUE!");
        TEST(fb == 16380, "above leaf max: cross-boundary find");

        // rfind should find the same.
        auto rfb = r2.rfind("UNIQUE!");
        TEST(rfb == 16380, "above leaf max: cross-boundary rfind");

        std::cout << "PASS: Edge 6 (above leaf max 16385)\n";
    }

    // ---------------------------------------------------------------
    // 7. Many small concatenations (stress tree depth)
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;
        for (int i = 0; i < 5000; ++i) {
            r += "a";
            expected += "a";
        }
        TEST(r.size() == 5000, "5000 concats: size");

        auto flat = r.flatten();
        TEST(flat == expected, "5000 concats: flatten");

        // Find at the very end.
        auto f = r.find("a", 4999);
        TEST(f == 4999, "5000 concats: find at last char");

        // starts_with and ends_with.
        TEST(r.starts_with("aaa"), "5000 concats: starts_with");
        TEST(r.ends_with("aaa"), "5000 concats: ends_with");

        std::cout << "PASS: Edge 7 (5000 concats)\n";
    }

    // ---------------------------------------------------------------
    // 8. Complex mix of single chars across multi-leaf boundaries
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;

        // Build 3 leaves worth of data (each ~10 KB).
        for (int i = 0; i < 3; ++i) {
            std::string chunk(10000, 'a' + i);
            r += chunk.c_str();
            expected += chunk;
        }

        // Cross-boundary substr.
        auto sv = r.substr(9995, 15);
        std::string_view sv_view(sv.data(), sv.size());
        auto exp_sv = expected.substr(9995, 15);
        TEST(sv_view == exp_sv, "multi-leaf: cross-boundary substr");

        // Cross-boundary find.
        std::string needle = expected.substr(9995, 10);
        auto f = r.find(std::string_view(needle));
        auto fe = expected.find(needle);
        TEST(f == fe, "multi-leaf: cross-boundary find");

        // Cross-boundary rfind.
        auto rf = r.rfind(std::string_view(needle));
        auto rfe = expected.rfind(needle);
        TEST(rf == rfe, "multi-leaf: cross-boundary rfind");

        // Compare against std::string.
        TEST(r == fl::rope(expected.c_str(), expected.size()),
             "multi-leaf: equality with constructed rope");
        TEST(r.compare(expected) == 0, "multi-leaf: compare with string");

        std::cout << "PASS: Edge 8 (multi-leaf boundaries)\n";
    }

    // ---------------------------------------------------------------
    // 9. Repeated split/recombine (sibling chain stress)
    // ---------------------------------------------------------------
    {
        fl::rope r;
        std::string expected;
        for (int i = 0; i < 100; ++i) {
            r += "hello world ";
            expected += "hello world ";
        }

        for (int i = 0; i < 50; ++i) {
            if (expected.size() < 20) break;
            std::size_t pos = (i * 137) % expected.size();
            fl::rope rhs = r.split(pos);
            std::string rhs_exp = expected.substr(pos);
            expected = expected.substr(0, pos);

            // Recombine.
            r += rhs;
            expected += rhs_exp;
        }

        auto flat = r.flatten();
        TEST(flat == expected, "split-recombine: final content");
        std::cout << "PASS: Edge 9 (repeated split/recombine)\n";
    }

    // ---------------------------------------------------------------
    // 10. Very large rope (> 100 MB) — basic sanity
    // ---------------------------------------------------------------
    {
        constexpr std::size_t kTargetSize = 1024 * 1024; // 1 MB
        constexpr std::size_t kChunkSize = 16000; // 16 KB chunks

        fl::rope r;
        std::string chunk(kChunkSize, 'X');
        std::size_t written = 0;
        while (written + kChunkSize <= kTargetSize) {
            r += chunk.c_str();
            written += kChunkSize;
        }
        // Add remaining bytes if any.
        if (written < kTargetSize) {
            std::string tail(kTargetSize - written, 'X');
            r += tail.c_str();
        }
        written = kTargetSize;

        TEST(r.size() == kTargetSize, "large rope: size");

        // Spot-check a few positions.
        TEST(r[0] == 'X', "large rope: first char");
        TEST(r[kTargetSize - 1] == 'X', "large rope: last char");
        TEST(r[kTargetSize / 2] == 'X', "large rope: middle char");

        // Find at the very end.
        auto f = r.find("XX", kTargetSize - 10);
        TEST(f != fl::rope::npos, "large rope: find near end");

        // Check depth is reasonable.
        int d = r.depth();
        TEST(d <= 4, "large rope: depth reasonable");
        std::cout << "  (depth=" << d << ")\n";

        std::cout << "PASS: Edge 10 (large rope)\n";
    }

    // ---------------------------------------------------------------
    // Summary
    // ---------------------------------------------------------------
    if (failures == 0) {
        std::cout << "\nAll rope edge case tests passed!\n";
        return 0;
    } else {
        std::cerr << "\n" << failures << " test(s) FAILED!\n";
        return 1;
    }
}
