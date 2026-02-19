#include <fl/string.hpp>
#include <iostream>
#include <cassert>

#define TEST(condition, name) \
    if (!(condition)) { \
        std::cerr << "FAIL: " << name << "\\n"; \
        return 1; \
    } else { \
        std::cout << "PASS: " << name << "\\n"; \
    }

int main() {
    // Test starts_with
    {
        fl::string s = "Hello World";
        TEST(s.starts_with("Hello"), "starts_with prefix");
        TEST(s.starts_with('H'), "starts_with char");
        TEST(!s.starts_with("World"), "starts_with false");
    }

    // Test ends_with
    {
        fl::string s = "Hello World";
        TEST(s.ends_with("World"), "ends_with suffix");
        TEST(s.ends_with('d'), "ends_with char");
        TEST(!s.ends_with("Hello"), "ends_with false");
    }

    // Test contains
    {
        fl::string s = "Hello World";
        TEST(s.contains("lo Wo"), "contains substring");
        TEST(s.contains('o'), "contains char");
        TEST(!s.contains("xyz"), "contains false");
    }

    // Test compare
    {
        fl::string s1 = "apple";
        fl::string s2 = "banana";
        fl::string s3 = "apple";
        TEST(s1.compare(s2) < 0, "compare less");
        TEST(s2.compare(s1) > 0, "compare greater");
        TEST(s1.compare(s3) == 0, "compare equal");
        TEST(s1.compare(std::string_view("apple")) == 0, "compare string_view");
        TEST(s1.compare(0, 3, "app") == 0, "compare substring");
    }

    // Test assign
    {
        fl::string s;
        s.assign("test");
        TEST(s == "test", "assign c-string");
        
        s.assign("hello", 3);
        TEST(s == "hel", "assign c-string with length");
        
        fl::string s2 = "world";
        s.assign(s2);
        TEST(s == "world", "assign string");
        
        s.assign(5, '*');
        TEST(s == "*****", "assign repeated char");
    }

    // Test insert variations
    {
        fl::string s = "ac";
        s.insert(1, "b");
        TEST(s == "abc", "insert c-string");
        
        s = "ac";
        s.insert(1, "xyz", 1);
        TEST(s == "axc", "insert c-string with length");
        
        s = "ac";
        s.insert(1, 3, 'b');
        TEST(s == "abbbc", "insert repeated char");
        
        s = "hello";
        auto it = s.insert(s.begin() + 2, '!');
        TEST(s == "he!llo", "insert iterator single char");
        TEST(*it == '!', "insert iterator returns correct position");
        
        s = "hello";
        it = s.insert(s.begin() + 2, 2, '!');
        TEST(s == "he!!llo", "insert iterator repeated char");
    }

    // Test erase variations
    {
        fl::string s = "hello";
        auto it = s.erase(s.begin() + 1);
        TEST(s == "hllo", "erase iterator single");
        TEST(it == s.begin() + 1, "erase returns correct iterator");
        
        s = "hello world";
        it = s.erase(s.begin() + 5, s.begin() + 11);
        TEST(s == "hello", "erase iterator range");
    }

    // Test replace variations
    {
        fl::string s = "hello";
        s.replace(1, 3, "iya");
        TEST(s == "hiyao", "replace with string");
        
        s = "hello";
        s.replace(1, 3, "ABCD", 2);
        TEST(s == "hABo", "replace with c-string and length");
        
        s = "hello";
        s.replace(1, 3, "xyz");
        TEST(s == "hxyzo", "replace with c-string");
        
        s = "hello";
        s.replace(1, 3, 2, '*');
        TEST(s == "h**o", "replace with repeated char");
    }

    // Test operator+ with move semantics
    {
        fl::string s1 = "Hello";
        fl::string s2 = " World";
        fl::string result = std::move(s1) + s2;
        TEST(result == "Hello World", "operator+ move lhs");
        
        s1 = "Hello";
        s2 = " World";
        result = s1 + std::move(s2);
        TEST(result == "Hello World", "operator+ move rhs");
        
        s1 = "Hello";
        s2 = " World";
        result = std::move(s1) + std::move(s2);
        TEST(result == "Hello World", "operator+ move both");
        
        s1 = "Hello";
        result = std::move(s1) + " World";
        TEST(result == "Hello World", "operator+ move lhs with cstr");
        
        s1 = " World";
        result = "Hello" + std::move(s1);
        TEST(result == "Hello World", "operator+ cstr with move rhs");
    }

    std::cout << "\\nAll tests passed!\\n";
    return 0;
}
