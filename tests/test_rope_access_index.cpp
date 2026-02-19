#include <fl/rope.hpp>
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

#define TEST(condition, name) \
    if (!(condition)) { \
        std::cerr << "FAIL: " << name << "\n"; \
        return 1; \
    } else { \
        std::cout << "PASS: " << name << "\n"; \
    }

int main() {
    // Test basic indexed access on large ropes (>= 4096 bytes)
    {
        // Build a rope larger than 4096 bytes to trigger indexed access
        fl::rope r;
        std::string expected;
        constexpr int chunk_count = 200;  // 200 * 32 = 6400 bytes
        constexpr int chunk_size = 32;
        
        for (int i = 0; i < chunk_count; ++i) {
            std::string chunk(chunk_size, 'a' + (i % 26));
            r += chunk.c_str();
            expected += chunk;
        }
        
        TEST(r.size() == expected.size(), "large rope: size matches");
        TEST(r.size() >= 4096, "large rope: exceeds index threshold");
        
        // Test random access at various positions
        TEST(r[0] == expected[0], "indexed access: first char");
        TEST(r[100] == expected[100], "indexed access: position 100");
        TEST(r[1000] == expected[1000], "indexed access: position 1000");
        TEST(r[3000] == expected[3000], "indexed access: position 3000");
        TEST(r[r.size() - 1] == expected[expected.size() - 1], "indexed access: last char");
        
        // Test sequential access
        bool all_match = true;
        for (size_t i = 0; i < r.size(); i += 100) {
            if (r[i] != expected[i]) {
                all_match = false;
                break;
            }
        }
        TEST(all_match, "indexed access: sequential access every 100th char");
        
        // Test at() method
        TEST(r.at(0) == expected[0], "at() method: first char");
        TEST(r.at(2000) == expected[2000], "at() method: position 2000");
        TEST(r.at(r.size() - 1) == expected[expected.size() - 1], "at() method: last char");
    }

    // Test very large rope with >= 128 chunks (triggers sampled binary search)
    {
        fl::rope r;
        std::string expected;
        constexpr int chunk_count = 150;  // >= 128 chunks
        constexpr int chunk_size = 40;
        
        for (int i = 0; i < chunk_count; ++i) {
            std::string chunk(chunk_size, 'A' + (i % 26));
            r += chunk.c_str();
            expected += chunk;
        }
        
        TEST(r.size() >= 4096, "very large rope: exceeds index threshold");
        
        // Test access across different chunks
        TEST(r[0] == expected[0], "sampled index: first chunk");
        TEST(r[500] == expected[500], "sampled index: middle chunk");
        TEST(r[2500] == expected[2500], "sampled index: later chunk");
        TEST(r[r.size() - 1] == expected[expected.size() - 1], "sampled index: last chunk");
        
        // Verify multiple accesses
        std::vector<size_t> test_positions = {0, 1, 39, 40, 41, 79, 80, 
                                               500, 1000, 2000, 3000, 4000, 5000};
        bool all_correct = true;
        for (size_t pos : test_positions) {
            if (pos < r.size() && r[pos] != expected[pos]) {
                all_correct = false;
                std::cerr << "Mismatch at position " << pos << "\n";
                break;
            }
        }
        TEST(all_correct, "sampled index: multiple positions");
    }

    // Test rope modification invalidates index
    {
        fl::rope r;
        std::string chunk(100, 'x');
        
        // Build large rope
        for (int i = 0; i < 50; ++i) {
            r += chunk.c_str();
        }
        
        TEST(r.size() >= 4096, "modifiable rope: large enough for indexing");
        
        // Access to build index
        char first = r[100];
        TEST(first == 'x', "modifiable rope: access before modification");
        
        // Modify the rope (should invalidate index)
        r += "y";
        
        // Access after modification (should work correctly)
        TEST(r[r.size() - 1] == 'y', "modifiable rope: access after modification");
        TEST(r[100] == 'x', "modifiable rope: original position still correct");
    }

    // Test small rope doesn't use indexed access
    {
        fl::rope r = "small";
        TEST(r.size() < 4096, "small rope: below index threshold");
        TEST(r[0] == 's', "small rope: first char");
        TEST(r[4] == 'l', "small rope: last char");
        
        // Build up to just below threshold
        std::string chunk(100, 'a');
        for (int i = 0; i < 40; ++i) {  // 4000 bytes
            r += chunk.c_str();
        }
        TEST(r.size() < 4096, "medium rope: still below threshold");
        TEST(r[0] == 's', "medium rope: first char");
        TEST(r[1000] == 'a', "medium rope: access in middle");
    }

    // Test edge cases in indexed access
    {
        fl::rope r;
        std::string expected;
        constexpr int chunk_count = 200;
        constexpr int chunk_size = 32;
        
        for (int i = 0; i < chunk_count; ++i) {
            std::string chunk(chunk_size, static_cast<char>('0' + (i % 10)));
            r += chunk.c_str();
            expected += chunk;
        }
        
        // Test boundary positions
        for (int i = 0; i < chunk_count; ++i) {
            size_t boundary = i * chunk_size;
            if (boundary < r.size()) {
                TEST(r[boundary] == expected[boundary], "boundary access: chunk boundary");
            }
        }
        
        // Test off-by-one positions around boundaries
        for (int i = 1; i < 5; ++i) {
            size_t boundary = i * chunk_size;
            if (boundary > 0 && boundary < r.size()) {
                TEST(r[boundary - 1] == expected[boundary - 1], "boundary access: before boundary");
                TEST(r[boundary] == expected[boundary], "boundary access: at boundary");
                if (boundary + 1 < r.size()) {
                    TEST(r[boundary + 1] == expected[boundary + 1], "boundary access: after boundary");
                }
            }
        }
    }

    // Test concatenated ropes
    {
        fl::rope r1;
        fl::rope r2;
        std::string chunk1(2500, 'A');
        std::string chunk2(2500, 'B');
        
        r1 += chunk1.c_str();
        r2 += chunk2.c_str();
        
        fl::rope r3 = r1 + r2;
        
        TEST(r3.size() == 5000, "concatenated rope: size");
        TEST(r3.size() >= 4096, "concatenated rope: exceeds threshold");
        TEST(r3[0] == 'A', "concatenated rope: first char from r1");
        TEST(r3[2499] == 'A', "concatenated rope: last char from r1");
        TEST(r3[2500] == 'B', "concatenated rope: first char from r2");
        TEST(r3[4999] == 'B', "concatenated rope: last char from r2");
    }

    // Test repeated access patterns
    {
        fl::rope r;
        std::string expected;
        constexpr int chunk_count = 150;
        constexpr int chunk_size = 32;
        
        for (int i = 0; i < chunk_count; ++i) {
            std::string chunk(chunk_size, 'X');
            r += chunk.c_str();
            expected += chunk;
        }
        
        // Repeated forward access
        for (int pass = 0; pass < 3; ++pass) {
            for (size_t i = 0; i < r.size(); i += 200) {
                TEST(r[i] == expected[i], "repeated access: forward");
            }
        }
        
        // Repeated backward access
        for (int pass = 0; pass < 3; ++pass) {
            for (size_t i = r.size() > 0 ? r.size() - 1 : 0; i >= 200; i -= 200) {
                TEST(r[i] == expected[i], "repeated access: backward");
            }
        }
        
        // Random-ish access pattern
        std::vector<size_t> positions = {0, 1000, 500, 3000, 200, 4000, 100};
        for (size_t pos : positions) {
            if (pos < r.size()) {
                TEST(r[pos] == expected[pos], "repeated access: random pattern");
            }
        }
    }

    std::cout << "\nAll rope access index tests passed!\n";
    return 0;
}
