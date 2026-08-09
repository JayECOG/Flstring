// Comprehensive test suite for fl::format_to and related formatting APIs.
//
// This file thoroughly exercises ALL existing formatting functionality:
// format string syntax, type-specific formatting, error handling, sink
// implementations, and edge cases.
//
// Build: linked against the fl header-only library.
//   g++ -std=c++20 -I../include -o test_format_comprehensive test_format_comprehensive.cpp

#include <fl/format.hpp>
#include <fl/sinks.hpp>
#include <fl.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <climits>
#include <stdexcept>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstdio>

// ---------------------------------------------------------------------------
// Test infrastructure (same pattern as existing test_format.cpp)
// ---------------------------------------------------------------------------
#define TEST(condition, name)                                                  \
    if (!(condition)) {                                                        \
        std::cerr << "FAIL: " << name << "\n";                                 \
        return 1;                                                              \
    } else {                                                                   \
        std::cout << "PASS: " << name << "\n";                                 \
    }

// Helper: render a formatting lambda into a std::string using a fixed buffer.
static std::string render(auto&& writer) {
    char buffer[4096];
    fl::buffer_sink sink(buffer, sizeof(buffer));
    writer(sink);
    sink.null_terminate();
    return std::string(buffer, sink.written());
}

// ============================================================================
// 1. Basic formatting: {} with various types
// ============================================================================
static int test_basic_formatting() {
    std::cout << "\n=== Basic Formatting ===\n";

    // int
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 42); });
        TEST(out == "42", "int basic");
    }
    // unsigned
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 42u); });
        TEST(out == "42", "unsigned basic");
    }
    // float
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 3.14f); });
        TEST(out == "3.14", "float basic");
    }
    // double
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 2.71828); });
        TEST(out == "2.71828", "double basic");
    }
    // bool
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", true); });
        TEST(out == "true", "bool true basic");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", false); });
        TEST(out == "false", "bool false basic");
    }
    // char
    {
        std::string out;
        try {
            out = render([](auto& sink) { fl::format_to(sink, "{}", 'A'); });
        } catch (const std::exception& e) {
            std::cerr << "  EXCEPTION in char basic: " << e.what() << "\n";
            return 1;
        }
        TEST(out == "A", "char basic");
    }
    // const char*
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", "hello"); });
        TEST(out == "hello", "const char* basic");
    }
    // std::string
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", std::string("world")); });
        TEST(out == "world", "std::string basic");
    }
    // std::string_view
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", std::string_view("view")); });
        TEST(out == "view", "std::string_view basic");
    }
    // fl::string
    {
        fl::string fs("flstring");
        auto out = render([&](auto& sink) { fl::format_to(sink, "{}", fs); });
        TEST(out == "flstring", "fl::string basic");
    }
    // Multiple args
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{} {} {} {}", 1, 2.5, "three", '4'); });
        TEST(out == "1 2.5 three 4", "multiple args basic");
    }

    return 0;
}

// ============================================================================
// 2. Positional indices: {0}, {1}, {0} {1} {0} (reuse)
// ============================================================================
static int test_positional_indices() {
    std::cout << "\n=== Positional Indices ===\n";

    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{0}", 42); });
        TEST(out == "42", "single positional {0}");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{1}", "a", "b"); });
        TEST(out == "b", "positional {1}");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{0} {1} {0}", "x", "y"); });
        TEST(out == "x y x", "positional reuse {0} {1} {0}");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{2} {0} {1}", 10, 20, 30); });
        TEST(out == "30 10 20", "positional out of order {2} {0} {1}");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{0:*>5} {1:*<5}", 1, 2); });
        TEST(out == "****1 2****", "positional with format spec");
    }

    return 0;
}

// ============================================================================
// 3. Fill and alignment: <, >, ^, = with various fill characters
// ============================================================================
static int test_fill_and_alignment() {
    std::cout << "\n=== Fill and Alignment ===\n";

    // Right align (default)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:>10}", 42); });
        TEST(out == "        42", "right align >10");
    }
    // Left align
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:<10}", 42); });
        TEST(out == "42        ", "left align <10");
    }
    // Center align
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:^10}", 42); });
        TEST(out == "    42    ", "center align ^10");
    }
    // Center align with odd width (extra padding goes to left)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:^11}", 42); });
        TEST(out == "     42    ", "center align ^11 (odd)");
    }
    // Fill character '*'
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*<10}", 42); });
        TEST(out == "42********", "fill * left align");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*>10}", 42); });
        TEST(out == "********42", "fill * right align");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*^10}", 42); });
        TEST(out == "****42****", "fill * center align");
    }
    // Fill character '0' (numeric padding style)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:0>10}", 42); });
        TEST(out == "0000000042", "fill 0 right align");
    }
    // Fill character with strings
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.>10}", "hi"); });
        TEST(out == "........hi", "fill . right align string");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.^10}", "hi"); });
        TEST(out == "....hi....", "fill . center align string");
    }
    // Fill character with bool
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*<8}", true); });
        TEST(out == "true****", "fill * left align bool");
    }
    // Fill character with char
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*>5}", 'Z'); });
        TEST(out == "****Z", "fill * right align char");
    }

    return 0;
}

// ============================================================================
// 4. Sign: + for positive/negative numbers
// ============================================================================
static int test_sign() {
    std::cout << "\n=== Sign ===\n";

    // Positive with sign
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:+}", 42); });
        TEST(out == "+42", "positive with sign");
    }
    // Negative with sign
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:+}", -42); });
        TEST(out == "-42", "negative with sign");
    }
    // Zero with sign
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:+}", 0); });
        TEST(out == "+0", "zero with sign");
    }
    // Sign with width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:+10}", 42); });
        TEST(out == "       +42", "sign with width 10");
    }
    // Sign with fill and alignment
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*>+10}", 42); });
        TEST(out == "*******+42", "sign with fill and alignment");
    }

    return 0;
}

// ============================================================================
// 5. Alternate form: # with x, X, b, B, o
// ============================================================================
static int test_alternate_form() {
    std::cout << "\n=== Alternate Form (#) ===\n";

    // Hex lower
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#x}", 255); });
        TEST(out == "0xff", "alternate #x");
    }
    // Hex upper
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#X}", 255); });
        TEST(out == "0XFF", "alternate #X");
    }
    // Binary lower
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#b}", 255); });
        TEST(out == "0b11111111", "alternate #b");
    }
    // Binary upper
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#B}", 255); });
        TEST(out == "0b11111111", "alternate #B");
    }
    // Octal
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#o}", 255); });
        TEST(out == "0377", "alternate #o");
    }
    // Zero value with alternate
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#x}", 0); });
        TEST(out == "0", "alternate #x with zero");
    }
    // Alternate with width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#10x}", 255); });
        TEST(out == "      0xff", "alternate #x with width");
    }

    return 0;
}

// ============================================================================
// 6. Width: Static width with various types
// ============================================================================
static int test_width() {
    std::cout << "\n=== Width ===\n";

    // Width with int
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:5}", 42); });
        TEST(out == "   42", "width 5 int");
    }
    // Width with unsigned
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:5}", 42u); });
        TEST(out == "   42", "width 5 unsigned");
    }
    // Width with float
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:10}", 3.14f); });
        TEST(out == "      3.14", "width 10 float");
    }
    // Width with double
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:10}", 2.71828); });
        TEST(out == "   2.71828", "width 10 double");
    }
    // Width with string
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:10}", "hi"); });
        TEST(out == "        hi", "width 10 string");
    }
    // Width with bool
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:6}", true); });
        TEST(out == "  true", "width 6 bool");
    }
    // Width with char
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:3}", 'X'); });
        TEST(out == "  X", "width 3 char");
    }
    // Width larger than content
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:50}", 1); });
        TEST(out.size() == 50 && out.find('1') != std::string::npos, "width 50 int");
    }

    return 0;
}

// ============================================================================
// 7. Precision: Static precision for floats and strings
// ============================================================================
static int test_precision() {
    std::cout << "\n=== Precision ===\n";

    // Float precision
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.2f}", 3.14159); });
        TEST(out == "3.14", "precision .2f");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.5f}", 3.14159); });
        TEST(out == "3.14159", "precision .5f");
    }
    // Double precision
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.3f}", 2.7182818); });
        TEST(out == "2.718", "precision .3f double");
    }
    // String precision (truncation)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.3}", "hello"); });
        TEST(out == "hel", "precision .3 string truncation");
    }
    // String precision with width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:10.3}", "hello"); });
        TEST(out == "       hel", "width 10 precision 3 string");
    }
    // Float precision with width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:10.2f}", 3.14159); });
        TEST(out == "      3.14", "width 10 precision .2f");
    }
    // Zero precision
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.0f}", 3.9); });
        TEST(out == "4", "zero precision .0f rounds");
    }
    // Large precision
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.10f}", 1.0); });
        TEST(out == "1.0000000000", "precision .10f");
    }

    return 0;
}

// ============================================================================
// 8. Type specifiers: d, x, X, b, B, o, f, F, e, E, g, G, s, c
// ============================================================================
static int test_type_specifiers() {
    std::cout << "\n=== Type Specifiers ===\n";

    // d - decimal (explicit)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:d}", 255); });
        TEST(out == "255", "type d decimal");
    }
    // x - hex lower
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:x}", 255); });
        TEST(out == "ff", "type x hex lower");
    }
    // X - hex upper
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:X}", 255); });
        TEST(out == "FF", "type X hex upper");
    }
    // b - binary lower
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:b}", 255); });
        TEST(out == "11111111", "type b binary");
    }
    // B - binary upper
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:B}", 255); });
        TEST(out == "11111111", "type B binary");
    }
    // o - octal
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:o}", 255); });
        TEST(out == "377", "type o octal");
    }
    // f - fixed float
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:f}", 3.14); });
        TEST(out == "3.140000", "type f fixed float");
    }
    // F - fixed float upper
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:F}", 3.14); });
        TEST(out == "3.140000", "type F fixed float");
    }
    // e - scientific lower
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:e}", 1000.0); });
        TEST(out.find('e') != std::string::npos, "type e scientific");
    }
    // E - scientific upper
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:E}", 1000.0); });
        TEST(out.find('E') != std::string::npos, "type E scientific");
    }
    // g - general (default)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:g}", 3.14); });
        TEST(out == "3.14", "type g general");
    }
    // G - general upper
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:G}", 3.14); });
        TEST(out == "3.14", "type G general");
    }
    // s - string
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:s}", "hello"); });
        TEST(out == "hello", "type s string");
    }
    // c - char
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:c}", 'A'); });
        TEST(out == "A", "type c char");
    }
    // Negative number in hex
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:x}", -255); });
        TEST(out == "-ff", "negative hex");
    }

    return 0;
}

// ============================================================================
// 9. Escaping: {{ and }}
// ============================================================================
static int test_escaping() {
    std::cout << "\n=== Escaping ===\n";

    // Double braces
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{{}}"); });
        TEST(out == "{}", "escaped braces {{}}");
    }
    // Single escaped brace
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{{"); });
        TEST(out == "{", "escaped single {{");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "}}"); });
        TEST(out == "}", "escaped single }}");
    }
    // Mixed escaped and real braces
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{{{} {}}}", 1, 2); });
        TEST(out == "{1 2}", "mixed escaped and real braces");
    }
    // Multiple escaped braces
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{{{{}}}}"); });
        TEST(out == "{{}}", "multiple escaped braces");
    }
    // Escaped braces with text
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "Hello {{name}}"); });
        TEST(out == "Hello {name}", "escaped braces with text");
    }

    return 0;
}

// ============================================================================
// 10. Mixed specifiers: Combining fill, align, sign, width, precision, type
// ============================================================================
static int test_mixed_specifiers() {
    std::cout << "\n=== Mixed Specifiers ===\n";

    // Fill + align + width + type
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*>10x}", 255); });
        TEST(out == "********ff", "fill align width type");
    }
    // Sign + width + type
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:+10d}", 42); });
        TEST(out == "       +42", "sign width type");
    }
    // Fill + align + sign + width + precision + type (float)
    // Note: format_float_with_spec uses snprintf which doesn't add '+' for
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*>+10.2f}", 3.14159); });
        TEST(out == "*****+3.14", "fill align sign width precision type float");
    }
    // Alternate + width + type
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:#10x}", 255); });
        TEST(out == "      0xff", "alternate width type");
    }
    // Fill + align + width + precision (string)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.>10.3}", "hello"); });
        TEST(out == ".......hel", "fill align width precision string");
    }
    // Center align with fill and width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*^10x}", 255); });
        // "ff" is 2 chars, padding = 8, left = (8+1)/2 = 4, right = 4
        TEST(out == "****ff****", "center align fill width type");
    }
    // All together: fill, align, sign, alternate, width, precision, type
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*>+#15.5f}", 3.14159); });
        TEST(out == "*******+3.14159", "all specifiers combined");
    }

    return 0;
}

// ============================================================================
// 11. Signed integers: All widths, edge values
// ============================================================================
static int test_signed_integers() {
    std::cout << "\n=== Signed Integers ===\n";

    // int8_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int8_t(42)); });
        TEST(out == "42", "int8_t");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int8_t(-42)); });
        TEST(out == "-42", "int8_t negative");
    }
    // int16_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int16_t(1000)); });
        TEST(out == "1000", "int16_t");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int16_t(-1000)); });
        TEST(out == "-1000", "int16_t negative");
    }
    // int32_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int32_t(100000)); });
        TEST(out == "100000", "int32_t");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int32_t(-100000)); });
        TEST(out == "-100000", "int32_t negative");
    }
    // int64_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int64_t(10000000000LL)); });
        TEST(out == "10000000000", "int64_t");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", int64_t(-10000000000LL)); });
        TEST(out == "-10000000000", "int64_t negative");
    }
    // Edge values
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 0); });
        TEST(out == "0", "zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 1); });
        TEST(out == "1", "one");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", -1); });
        TEST(out == "-1", "negative one");
    }
    // INT_MIN / INT_MAX
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", INT_MAX); });
        TEST(out == std::to_string(INT_MAX), "INT_MAX");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", INT_MIN); });
        TEST(out == std::to_string(INT_MIN), "INT_MIN");
    }
    // INT64_MIN / INT64_MAX
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", INT64_MAX); });
        TEST(out == std::to_string(INT64_MAX), "INT64_MAX");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", INT64_MIN); });
        TEST(out == std::to_string(INT64_MIN), "INT64_MIN");
    }

    return 0;
}

// ============================================================================
// 12. Unsigned integers: All widths, edge values
// ============================================================================
static int test_unsigned_integers() {
    std::cout << "\n=== Unsigned Integers ===\n";

    // uint8_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", uint8_t(42)); });
        TEST(out == "42", "uint8_t");
    }
    // uint16_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", uint16_t(1000)); });
        TEST(out == "1000", "uint16_t");
    }
    // uint32_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", uint32_t(100000)); });
        TEST(out == "100000", "uint32_t");
    }
    // uint64_t
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", uint64_t(10000000000ULL)); });
        TEST(out == "10000000000", "uint64_t");
    }
    // Edge values
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 0u); });
        TEST(out == "0", "unsigned zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 1u); });
        TEST(out == "1", "unsigned one");
    }
    // UINT_MAX
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", UINT_MAX); });
        TEST(out == std::to_string(UINT_MAX), "UINT_MAX");
    }

    return 0;
}

// ============================================================================
// 13. Floating point: float, double, various values
// ============================================================================
static int test_floating_point() {
    std::cout << "\n=== Floating Point ===\n";

    // float
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 0.0f); });
        TEST(out == "0", "float zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 1.0f); });
        TEST(out == "1", "float one");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", -1.0f); });
        TEST(out == "-1", "float negative one");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 3.14159f); });
        TEST(out == "3.14159", "float pi");
    }
    // double
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 0.0); });
        TEST(out == "0", "double zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", -0.0); });
        TEST(out == "0" || out == "-0", "double negative zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 1.0); });
        TEST(out == "1", "double one");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", -1.0); });
        TEST(out == "-1", "double negative one");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 3.14159265358979); });
        // Default formatting uses Dragonbox shortest-round-trip (full precision)
        TEST(out == "3.14159265358979", "double pi full precision (Dragonbox shortest)");
    }
    // Large values
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 1e10); });
        TEST(out == "1e+10", "double large value");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 1e-10); });
        TEST(out == "1e-10", "double small value");
    }
    // Float with format spec
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.1f}", 3.14159); });
        TEST(out == "3.1", "float .1f");
    }

    return 0;
}

// ============================================================================
// 14. Bool: true, false
// ============================================================================
static int test_bool() {
    std::cout << "\n=== Bool ===\n";

    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", true); });
        TEST(out == "true", "bool true");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", false); });
        TEST(out == "false", "bool false");
    }
    // Bool with width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:10}", true); });
        TEST(out == "      true", "bool true width 10");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:<10}", false); });
        TEST(out == "false     ", "bool false left align");
    }
    // Bool with fill
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*^10}", true); });
        // Center align: "true" = 4 chars, padding = 6, left = 3, right = 3
        TEST(out == "***true***", "bool center align fill *");
    }

    return 0;
}

// ============================================================================
// 15. Char: Regular chars, special chars
// ============================================================================
static int test_char() {
    std::cout << "\n=== Char ===\n";

    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 'A'); });
        TEST(out == "A", "char A");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", 'z'); });
        TEST(out == "z", "char z");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", '0'); });
        TEST(out == "0", "char digit '0'");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", ' '); });
        TEST(out == " ", "char space");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", '\n'); });
        std::string expected(1, '\n');
        TEST(out == expected, "char newline");
    }
    // Char with width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:5}", 'X'); });
        TEST(out == "    X", "char width 5");
    }
    // Char with fill and alignment
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:*<5}", 'X'); });
        TEST(out == "X****", "char fill left align");
    }

    return 0;
}

// ============================================================================
// 16. C-strings: Normal strings, empty string
// ============================================================================
static int test_cstrings() {
    std::cout << "\n=== C-strings ===\n";

    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", "hello"); });
        TEST(out == "hello", "c-string hello");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", ""); });
        TEST(out == "", "c-string empty");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", "a"); });
        TEST(out == "a", "c-string single char");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", "a"); });
        TEST(out == "a", "c-string single char");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", "longer string here"); });
        TEST(out == "longer string here", "c-string longer");
    }
    // C-string with width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:10}", "hi"); });
        TEST(out == "        hi", "c-string width 10");
    }

    return 0;
}

// ============================================================================
// 17. std::string: Empty, short, long strings
// ============================================================================
static int test_std_string() {
    std::cout << "\n=== std::string ===\n";

    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", std::string("hello")); });
        TEST(out == "hello", "std::string hello");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", std::string("")); });
        TEST(out == "", "std::string empty");
    }
    {
        std::string long_str(1000, 'x');
        auto out = render([&](auto& sink) { fl::format_to(sink, "{}", long_str); });
        TEST(out == long_str, "std::string long (1000 chars)");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:>10}", std::string("hi")); });
        TEST(out == "        hi", "std::string with width");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.3}", std::string("hello")); });
        TEST(out == "hel", "std::string with precision");
    }

    return 0;
}

// ============================================================================
// 18. std::string_view: Various views
// ============================================================================
static int test_string_view() {
    std::cout << "\n=== std::string_view ===\n";

    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", std::string_view("view")); });
        TEST(out == "view", "string_view basic");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{}", std::string_view("")); });
        TEST(out == "", "string_view empty");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:>10}", std::string_view("hi")); });
        TEST(out == "        hi", "string_view with width");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.4}", std::string_view("hello")); });
        TEST(out == "hell", "string_view with precision");
    }
    // View into a substring
    {
        std::string full = "hello world";
        std::string_view sub(full.data() + 6, 5);
        auto out = render([&](auto& sink) { fl::format_to(sink, "{}", sub); });
        TEST(out == "world", "string_view substring");
    }

    return 0;
}

// ============================================================================
// 19. fl::string: If available via the fl.hpp include
// ============================================================================
static int test_fl_string() {
    std::cout << "\n=== fl::string ===\n";

    {
        fl::string fs("flstring");
        auto out = render([&](auto& sink) { fl::format_to(sink, "{}", fs); });
        TEST(out == "flstring", "fl::string basic");
    }
    {
        fl::string fs("");
        auto out = render([&](auto& sink) { fl::format_to(sink, "{}", fs); });
        TEST(out == "", "fl::string empty");
    }
    {
        fl::string fs("hello world");
        auto out = render([&](auto& sink) { fl::format_to(sink, "{:>20}", fs); });
        TEST(out == "         hello world", "fl::string with width");
    }

    return 0;
}

// ============================================================================
// 20-25. Error Handling Tests
// ============================================================================
static int test_error_handling() {
    std::cout << "\n=== Error Handling ===\n";

    // 20. Unmatched '{'
    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) { fl::format_to(sink, "{"); });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "unmatched '{' throws invalid_argument");
    }

    // 21. Unmatched '}'
    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) { fl::format_to(sink, "}"); });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "unmatched '}' throws invalid_argument");
    }

    // 22. Out-of-range index
    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) { fl::format_to(sink, "{5}", "a", "b"); });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "out-of-range index {5} throws");
    }

    // 23. Mixed explicit/implicit indices
    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) { fl::format_to(sink, "{0} {}", 1, 2); });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "mixed explicit/implicit indices throw");
    }
    {
        bool threw = false;
        try {
            auto out = render([](auto& sink) { fl::format_to(sink, "{} {0}", 1, 2); });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "mixed implicit/explicit indices throw (reverse)");
    }

    // 24. Null C-string
    {
        bool threw = false;
        const char* null_str = nullptr;
        try {
            auto out = render([&](auto& sink) { fl::format_to(sink, "{}", null_str); });
            (void)out;
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        TEST(threw, "null C-string throws invalid_argument");
    }

    // 25. Buffer overflow
    {
        bool threw = false;
        try {
            char small_buf[4];
            fl::buffer_sink sink(small_buf, sizeof(small_buf));
            fl::format_to(sink, "{}", "hello world too long");
        } catch (const std::overflow_error&) {
            threw = true;
        }
        TEST(threw, "buffer overflow throws overflow_error");
    }

    return 0;
}

// ============================================================================
// 26-31. Sink Tests
// ============================================================================
static int test_sinks() {
    std::cout << "\n=== Sink Tests ===\n";

    // 26. buffer_sink: Basic write, overflow behavior, null_terminate
    {
        char buf[32];
        fl::buffer_sink sink(buf, sizeof(buf));
        sink.write("hello", 5);
        sink.null_terminate();
        TEST(std::strcmp(buf, "hello") == 0, "buffer_sink basic write");
        TEST(sink.written() == 5, "buffer_sink written count");
    }
    {
        char buf[4];
        fl::buffer_sink sink(buf, sizeof(buf));
        bool threw = false;
        try {
            sink.write("toolong", 7);
        } catch (const std::overflow_error&) {
            threw = true;
        }
        TEST(threw, "buffer_sink overflow throws");
    }
    {
        char buf[16];
        fl::buffer_sink sink(buf, sizeof(buf));
        sink.write("abc", 3);
        sink.write("def", 3);
        TEST(sink.written() == 6, "buffer_sink multiple writes");
        sink.null_terminate();
        TEST(std::strcmp(buf, "abcdef") == 0, "buffer_sink null_terminate");
    }

    // 27. growing_sink: Dynamic growth, to_fl_string
    {
        fl::sinks::growing_sink sink;
        sink.write("hello", 5);
        sink.write(" world", 6);
        TEST(sink.written() == 11, "growing_sink size");
        sink.null_terminate();
        TEST(std::string(sink.data()) == "hello world", "growing_sink null_terminate content");
    }
    {
        fl::sinks::growing_sink sink(4);  // small initial capacity
        const std::string_view long_str = "this is a long string that exceeds initial capacity";
        sink.write(long_str.data(), long_str.size());
        TEST(sink.written() == long_str.size(), "growing_sink dynamic growth");
    }
    {
        fl::sinks::growing_sink sink;
        sink.write("fl", 2);
        fl::string fs = sink.to_fl_string();
        TEST(fs == fl::string("fl"), "growing_sink to_fl_string");
    }

    // 28. file_sink: Write to temp file, read back
    {
        const char* tmpfile = "test_format_temp.txt";
        // Remove if exists
        std::remove(tmpfile);
        {
            fl::sinks::file_sink sink(tmpfile);
            sink.write("hello file", 10);
            sink.flush();
        }
        // Read back
        std::ifstream in(tmpfile);
        std::string content;
        std::getline(in, content);
        TEST(content == "hello file", "file_sink write and read back");
        std::remove(tmpfile);
    }

    // 29. stream_sink: Write to ostringstream
    {
        std::ostringstream oss;
        fl::sinks::stream_sink sink(oss);
        sink.write("stream test", 11);
        sink.flush();
        TEST(oss.str() == "stream test", "stream_sink write to ostringstream");
    }

    // 30. null_sink: Verify bytes counted but nothing written
    {
        fl::sinks::null_sink sink;
        TEST(sink.written() == 0, "null_sink initially zero");
        sink.write("data", 4);
        TEST(sink.written() == 4, "null_sink counts bytes");
        sink.write("more", 4);
        TEST(sink.written() == 8, "null_sink accumulates bytes");
        sink.reset();
        TEST(sink.written() == 0, "null_sink reset");
    }

    // 31. multi_sink: Fan-out to multiple sinks
    {
        auto buf1 = std::make_shared<fl::sinks::growing_sink>();
        auto buf2 = std::make_shared<fl::sinks::growing_sink>();
        fl::sinks::multi_sink multi;
        multi.add_sink(buf1);
        multi.add_sink(buf2);
        multi.write("fanout", 6);
        TEST(buf1->written() == 6, "multi_sink first sink received data");
        TEST(buf2->written() == 6, "multi_sink second sink received data");
        multi.flush();
    }

    return 0;
}

// ============================================================================
// 32-40. Edge Cases
// ============================================================================
static int test_edge_cases() {
    std::cout << "\n=== Edge Cases ===\n";

    // 32. Empty format string
    {
        auto out = render([](auto& sink) { fl::format_to(sink, ""); });
        TEST(out == "", "empty format string");
    }

    // 33. Format string with only literals
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "hello world"); });
        TEST(out == "hello world", "literal only format string");
    }

    // 34. Very long format string (stress with long literal)
    // Note: render() uses a 4096-byte buffer, so 5000 chars will overflow.
    // We verify the overflow behavior instead.
    {
        std::string long_fmt(5000, 'a');
        bool threw = false;
        try {
            auto out = render([&](auto& sink) { fl::format_to(sink, long_fmt); });
            (void)out;
        } catch (const std::overflow_error&) {
            threw = true;
        }
        TEST(threw, "very long format string (5000 chars) overflows buffer");
    }

    // 35. Very long arguments (overflows 4096-byte buffer)
    {
        std::string long_arg(5000, 'b');
        bool threw = false;
        try {
            auto out = render([&](auto& sink) { fl::format_to(sink, "{}", long_arg); });
            (void)out;
        } catch (const std::overflow_error&) {
            threw = true;
        }
        TEST(threw, "very long string argument (5000 chars) overflows buffer");
    }

    // 36. Zero width
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:0}", 42); });
        TEST(out == "42", "zero width (no padding)");
    }

    // 37. Zero precision
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.0f}", 3.9); });
        TEST(out == "4", "zero precision .0f rounds up");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:.0f}", 3.1); });
        TEST(out == "3", "zero precision .0f rounds down");
    }

    // 38. Maximum width (large width values)
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:100}", 42); });
        TEST(out.size() == 100, "large width 100");
        // 42 should be right-aligned
        TEST(out.find("42") == 98, "large width 100 content position");
    }

    // 39. All type specifiers with zero
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:d}", 0); });
        TEST(out == "0", "type d with zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:x}", 0); });
        TEST(out == "0", "type x with zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:b}", 0); });
        TEST(out == "0", "type b with zero");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:o}", 0); });
        TEST(out == "0", "type o with zero");
    }

    // 40. Negative numbers with various specifiers
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:d}", -42); });
        TEST(out == "-42", "negative with d specifier");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:x}", -255); });
        TEST(out.find("ff") != std::string::npos, "negative with x specifier");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:+}", -42); });
        TEST(out == "-42", "negative with sign specifier");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:>10}", -42); });
        TEST(out == "       -42", "negative with width and right align");
    }
    {
        auto out = render([](auto& sink) { fl::format_to(sink, "{:<10}", -42); });
        TEST(out == "-42       ", "negative with width and left align");
    }

    return 0;
}

// ============================================================================
// main: Run all tests
// ============================================================================
int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
    int result = 0;

    result |= test_basic_formatting();
    result |= test_positional_indices();
    result |= test_fill_and_alignment();
    result |= test_sign();
    result |= test_alternate_form();
    result |= test_width();
    result |= test_precision();
    result |= test_type_specifiers();
    result |= test_escaping();
    result |= test_mixed_specifiers();
    result |= test_signed_integers();
    result |= test_unsigned_integers();
    result |= test_floating_point();
    result |= test_bool();
    result |= test_char();
    result |= test_cstrings();
    result |= test_std_string();
    result |= test_string_view();
    result |= test_fl_string();
    result |= test_error_handling();
    result |= test_sinks();
    result |= test_edge_cases();

    if (result == 0) {
        std::cout << "\nAll comprehensive tests passed!\n";
    } else {
        std::cerr << "\nSome comprehensive tests FAILED.\n";
    }
    return result;
}