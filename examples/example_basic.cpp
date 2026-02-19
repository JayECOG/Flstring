#include <iostream>
#include "../include/fl.hpp"

/// Basic fl::string usage examples
int main() {
    std::cout << "=== fl library version " << fl::version() << " ===" << std::endl << std::endl;

    // Example 1: Small-string optimisation
    {
        std::cout << "1. Small-string optimisation:" << std::endl;
        
        fl::string short_str("Hi");
        std::cout << "  Short string: '" << short_str.c_str() << "'" << std::endl;
        std::cout << "  Size: " << short_str.size() << ", Capacity: " << short_str.capacity() << std::endl;
        std::cout << "  Uses SSO (stack storage)" << std::endl << std::endl;

        fl::string medium_str("This is a medium length string here");
        std::cout << "  Medium string size: " << medium_str.size() << std::endl;
        std::cout << "  Capacity: " << medium_str.capacity() << std::endl << std::endl;

        fl::string long_str("This is a very long string that definitely exceeds the SSO threshold and will use heap allocation");
        std::cout << "  Long string size: " << long_str.size() << std::endl;
        std::cout << "  Capacity: " << long_str.capacity() << " (heap allocated)" << std::endl << std::endl;
    }

    // Example 2: String operations
    {
        std::cout << "2. String operations:" << std::endl;
        
        fl::string str("Hello");
        std::cout << "  Original: '" << str.c_str() << "'" << std::endl;

        str.append(" World");
        std::cout << "  After append: '" << str.c_str() << "'" << std::endl;

        str.push_back('!');
        std::cout << "  After push_back: '" << str.c_str() << "'" << std::endl;

        fl::string sub = str.substr(0, 5);
        std::cout << "  Substring (0, 5): '" << sub.c_str() << "'" << std::endl << std::endl;
    }

    // Example 3: Searching and comparison
    {
        std::cout << "3. Search and comparison:" << std::endl;
        
        fl::string text("The quick brown fox jumps over the lazy dog");
        std::cout << "  Text: '" << text.c_str() << "'" << std::endl;

        auto pos = text.find("brown");
        if (pos != fl::string::npos) {
            std::cout << "  Found 'brown' at position: " << pos << std::endl;
        }

        auto char_pos = text.find('q');
        if (char_pos != fl::string::npos) {
            std::cout << "  Found 'q' at position: " << char_pos << std::endl;
        }

        fl::string s1("abc");
        fl::string s2("abc");
        std::cout << "  'abc' == 'abc': " << (s1 == s2 ? "true" : "false") << std::endl << std::endl;
    }

    // Example 4: Iterator support
    {
        std::cout << "4. Iterator support:" << std::endl;
        
        fl::string str("Iterator");
        std::cout << "  String: '" << str.c_str() << "'" << std::endl;
        std::cout << "  Characters: ";
        for (auto ch : str) {
            std::cout << ch << " ";
        }
        std::cout << std::endl << std::endl;
    }

    // Example 5: Capacity management
    {
        std::cout << "5. Capacity management:" << std::endl;
        
        fl::string str;
        std::cout << "  Empty string capacity: " << str.capacity() << std::endl;

        str.reserve(200);
        std::cout << "  After reserve(200): " << str.capacity() << std::endl;

        str = "Small";
        str.shrink_to_fit();
        std::cout << "  After shrink_to_fit with 'Small': " << str.capacity() << std::endl << std::endl;
    }

    // Example 6: String literals
    {
        std::cout << "6. String literals:" << std::endl;
        
        using namespace fl;
        fl::string s = "Literal"_fs;
        std::cout << "  Using _fs literal: '" << s.c_str() << "'" << std::endl << std::endl;
    }

    // Example 7: Copy and move semantics
    {
        std::cout << "7. Copy and move semantics:" << std::endl;
        
        fl::string original("Original Data");
        
        fl::string copy = original;
        std::cout << "  Copy: '" << copy.c_str() << "'" << std::endl;
        
        fl::string moved = std::move(original);
        std::cout << "  Moved: '" << moved.c_str() << "'" << std::endl;
        std::cout << "  Original after move: (empty: " << (original.empty() ? "true" : "false") << ")" << std::endl << std::endl;
    }

    std::cout << "=== Examples completed ===" << std::endl;
    return 0;
}
