#include <fl/string.hpp>
#include <iostream>
#include <cassert>
#include <string>

#define TEST(condition, name) \
    if (!(condition)) { \
        std::cerr << "FAIL: " << name << "\n"; \
        return 1; \
    } else { \
        std::cout << "PASS: " << name << "\n"; \
    }

int main() {
    // Test size-2 needle with various haystack sizes
    {
        fl::string s1 = "ab";
        TEST(s1.find("ab") == 0, "size-2 needle: exact match");
        
        fl::string s2 = "xabcd";
        TEST(s2.find("ab") == 1, "size-2 needle: found at position 1");
        
        fl::string s3 = "abcdefghij";
        TEST(s3.find("ab") == 0, "size-2 needle: found at start");
        TEST(s3.find("ij") == 8, "size-2 needle: found at end");
        TEST(s3.find("gh") == 6, "size-2 needle: found in middle");
        
        // Create a larger haystack (> 256 bytes) for size-2 needle
        std::string large_haystack(300, 'x');
        large_haystack[100] = 'a';
        large_haystack[101] = 'b';
        fl::string s4 = large_haystack;
        TEST(s4.find("ab") == 100, "size-2 needle: large haystack");
        
        // Not found cases
        fl::string s5 = "xyz";
        TEST(s5.find("ab") == fl::string::npos, "size-2 needle: not found");
        
        // Edge case: haystack too small
        fl::string s6 = "a";
        TEST(s6.find("ab") == fl::string::npos, "size-2 needle: haystack too small");
        
        // Empty haystack
        fl::string s7 = "";
        TEST(s7.find("ab") == fl::string::npos, "size-2 needle: empty haystack");
    }

    // Test size-3 needle with haystacks > 256 bytes
    {
        // Small haystack (should not trigger size-3 specialized path)
        fl::string s1 = "abc";
        TEST(s1.find("abc") == 0, "size-3 needle: exact match small");
        
        // Create a haystack > 256 bytes to trigger specialized path
        std::string large_haystack(300, 'x');
        large_haystack[150] = 'a';
        large_haystack[151] = 'b';
        large_haystack[152] = 'c';
        fl::string s2 = large_haystack;
        TEST(s2.find("abc") == 150, "size-3 needle: large haystack");
        
        // Test at start
        std::string large_haystack2(300, 'x');
        large_haystack2[0] = 'a';
        large_haystack2[1] = 'b';
        large_haystack2[2] = 'c';
        fl::string s3 = large_haystack2;
        TEST(s3.find("abc") == 0, "size-3 needle: at start of large haystack");
        
        // Test at end
        std::string large_haystack3(300, 'x');
        large_haystack3[297] = 'a';
        large_haystack3[298] = 'b';
        large_haystack3[299] = 'c';
        fl::string s4 = large_haystack3;
        TEST(s4.find("abc") == 297, "size-3 needle: at end of large haystack");
        
        // Not found
        std::string large_haystack4(300, 'y');
        fl::string s5 = large_haystack4;
        TEST(s5.find("abc") == fl::string::npos, "size-3 needle: not found in large haystack");
        
        // Edge case: haystack too small
        fl::string s6 = "ab";
        TEST(s6.find("abc") == fl::string::npos, "size-3 needle: haystack too small");
    }

    // Test size-4 needle with haystacks > 256 bytes
    {
        // Create a haystack > 256 bytes to trigger specialized path
        std::string large_haystack(300, 'x');
        large_haystack[200] = 'a';
        large_haystack[201] = 'b';
        large_haystack[202] = 'c';
        large_haystack[203] = 'd';
        fl::string s1 = large_haystack;
        TEST(s1.find("abcd") == 200, "size-4 needle: large haystack");
        
        // Test at start
        std::string large_haystack2(300, 'x');
        large_haystack2[0] = 'a';
        large_haystack2[1] = 'b';
        large_haystack2[2] = 'c';
        large_haystack2[3] = 'd';
        fl::string s2 = large_haystack2;
        TEST(s2.find("abcd") == 0, "size-4 needle: at start of large haystack");
        
        // Test at end
        std::string large_haystack3(300, 'x');
        large_haystack3[296] = 'a';
        large_haystack3[297] = 'b';
        large_haystack3[298] = 'c';
        large_haystack3[299] = 'd';
        fl::string s3 = large_haystack3;
        TEST(s3.find("abcd") == 296, "size-4 needle: at end of large haystack");
        
        // Not found
        std::string large_haystack4(300, 'z');
        fl::string s4 = large_haystack4;
        TEST(s4.find("abcd") == fl::string::npos, "size-4 needle: not found in large haystack");
        
        // Small haystack
        fl::string s5 = "abcd";
        TEST(s5.find("abcd") == 0, "size-4 needle: exact match small");
        
        // Edge case: haystack too small
        fl::string s6 = "abc";
        TEST(s6.find("abcd") == fl::string::npos, "size-4 needle: haystack too small");
    }

    // Test single-character needle (should use memchr path)
    {
        fl::string s1 = "hello";
        TEST(s1.find("h") == 0, "size-1 needle: at start");
        TEST(s1.find("e") == 1, "size-1 needle: at position 1");
        TEST(s1.find("o") == 4, "size-1 needle: at end");
        TEST(s1.find("l") == 2, "size-1 needle: first occurrence");
        TEST(s1.find("z") == fl::string::npos, "size-1 needle: not found");
    }

    // Test low-entropy needles (should trigger BMH path)
    {
        // Create a medium-sized haystack with a low-entropy needle
        std::string haystack(500, 'a');
        haystack.append("aaabaaac");
        fl::string s1 = haystack;
        
        // Low-entropy pattern
        TEST(s1.find("aaab") == 500, "low-entropy needle: BMH path");
        TEST(s1.find("aaac") == 504, "low-entropy needle: at end");
    }

    // Test edge cases
    {
        fl::string s1 = "test";
        
        // Empty needle
        TEST(s1.find("") == 0, "empty needle: returns position");
        
        // Needle larger than haystack
        TEST(s1.find("testing") == fl::string::npos, "needle larger than haystack");
        
        // Position beyond string
        TEST(s1.find("test", 10) == fl::string::npos, "position beyond string");
        
        // Position at end
        TEST(s1.find("test", 4) == fl::string::npos, "position at end");
        
        // Find from middle
        fl::string s2 = "ababab";
        TEST(s2.find("ab", 2) == 2, "find from middle");
        TEST(s2.find("ab", 3) == 4, "find from middle + 1");
    }

    // Test repeated patterns
    {
        fl::string s1 = "abcabcabc";
        TEST(s1.find("abc") == 0, "repeated pattern: first occurrence");
        TEST(s1.find("abc", 1) == 3, "repeated pattern: second occurrence");
        TEST(s1.find("abc", 4) == 6, "repeated pattern: third occurrence");
        
        fl::string s2 = "aaaaaabaaaaaabaaaaa";
        TEST(s2.find("ab") == 5, "repeated chars: first ab");
        TEST(s2.find("ab", 6) == 12, "repeated chars: second ab");
    }

    std::cout << "\nAll adaptive find tests passed!\n";
    return 0;
}
