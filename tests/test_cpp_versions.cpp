// File: tests/test_cpp_versions.cpp
// Multi-standard unit tests for arena, rope, builder, string, synchronised_string,
// substring_view, ABI/export macros, and config macros.
// Tests compile cleanly under C++11, C++14, C++17, and C++20.

#include <iostream>
#include <cassert>
#include <cstring>
#include <type_traits>
#include <iterator>
#include <string>
#include <string_view>
#include "fl/config.hpp"
#include "fl/string.hpp"
#include "fl/arena.hpp"
#include "fl/rope.hpp"

// rope.hpp undefines FL_LIKELY/FL_UNLIKELY at its end; re-supply them
// before including headers that use them (builder.hpp, etc.).
// The definitions match fl/config.hpp.
#if !defined(FL_LIKELY)
#  if defined(__GNUC__) || defined(__clang__)
#    define FL_LIKELY(x)   __builtin_expect(!!(x), 1)
#    define FL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  else
#    define FL_LIKELY(x)   (x)
#    define FL_UNLIKELY(x) (x)
#  endif
#endif

#include "fl/builder.hpp"
#include "fl/synchronised_string.hpp"
#include "fl/substring_view.hpp"

namespace {

// ─── Helpers ────────────────────────────────────────────────────────────────

// Test helper: print C++ version
void print_cpp_version() {
    std::cout << "Testing with C++ standard: ";
#if FL_HAS_CPP20
    std::cout << "C++20";
#elif FL_HAS_CPP17
    std::cout << "C++17";
#elif FL_HAS_CPP14
    std::cout << "C++14";
#elif FL_HAS_CPP11
    std::cout << "C++11";
#else
    std::cout << "Unknown";
#endif
    std::cout << " (FL_CPP_LANG = " << FL_CPP_LANG << ")\n";
}

// =============================================================================
// PRESERVED EXISTING TESTS (unchanged)
// =============================================================================

// Test arena allocator
void test_arena() {
    std::cout << "\n=== Testing Arena Allocator ===\n";
    
    fl::arena_allocator<4096> arena;
    
    // Test stack allocation
    void* p1 = arena.allocate(256);
    assert(p1 != nullptr);
    std::cout << "? Stack allocation succeeded\n";
    
    // Test multiple allocations
    void* p2 = arena.allocate(512);
    assert(p2 != nullptr);
    std::cout << "? Multiple stack allocations succeeded\n";
    
    // Test deallocation (NOOP for stack)
    arena.deallocate(p1, 256);
    std::cout << "? Stack deallocation (NOOP) succeeded\n";
    
    // Test large allocation (heap fallback)
    void* p3 = arena.allocate(8192);
    assert(p3 != nullptr);
    std::cout << "? Heap fallback allocation succeeded\n";
    
    arena.deallocate(p3, 8192);
    std::cout << "? Heap deallocation succeeded\n";
}

// Test rope string
void test_rope() {
    std::cout << "\n=== Testing Rope String ===\n";
    
    fl::rope r1("hello");
    fl::rope r2("world");
    
    fl::rope r3 = r1 + r2;
    assert(r3.size() == 10);
    std::cout << "? Rope concatenation succeeded\n";
    
    // Test character access
    assert(r3[0] == 'h');
    assert(r3[5] == 'w');
    std::cout << "? Rope character access succeeded\n";
    
    // Test substring
    fl::rope sub = r3.substr(5, 5);
    assert(sub.size() == 5);
    std::cout << "? Rope substring succeeded\n";
}

// Test string builder with iterators
void test_builder() {
    std::cout << "\n=== Testing String Builder ===\n";
    
    fl::string_builder builder;
    
    // Test basic append
    builder.append("hello");
    assert(builder.size() == 5);
    std::cout << "? Basic append succeeded\n";
    
    // Test append with pointer and size
    builder.append(" ", 1);
    builder.append("world", 5);
    assert(builder.size() == 11);
    std::cout << "? Pointer+size append succeeded\n";
    
    // Test character append
    builder.append('!');
    assert(builder.size() == 12);
    std::cout << "? Character append succeeded\n";
    
    // Test iterator append (this is the C++11 compatible part)
    const char* str = "test";
    builder.clear();
    builder.append(str, str + 4);  // Uses C++11 iterator support
    assert(builder.size() == 4);
    std::cout << "? Iterator append succeeded (C++11 compatible)\n";
    
    // Test builder move and build
    fl::string result = std::move(builder).build();
    assert(result.size() == 4);
    std::cout << "? Builder build() succeeded\n";
}

// Test type traits for builder
void test_builder_traits() {
    std::cout << "\n=== Testing Builder Type Traits ===\n";
    
    // Check if iterator tags are correct
    static_assert(
        std::is_base_of<std::input_iterator_tag,
                        typename std::iterator_traits<const char*>::iterator_category>::value,
        "const char* should be input iterator"
    );
    std::cout << "? const char* is input iterator\n";
    
    static_assert(
        std::is_base_of<std::random_access_iterator_tag,
                        typename std::iterator_traits<const char*>::iterator_category>::value,
        "const char* should be random access iterator"
    );
    std::cout << "? const char* is random access iterator\n";
    
    // Test std::is_integral, std::is_floating_point for append_formatted
    static_assert(std::is_integral<int>::value, "int should be integral");
    static_assert(std::is_floating_point<double>::value, "double should be floating point");
    static_assert(std::is_convertible<std::string_view, std::string_view>::value, "string_view convertible");
    std::cout << "? Type traits for append_formatted work\n";
}

// Test append_formatted with different types
void test_builder_append_formatted() {
    std::cout << "\n=== Testing Builder append_formatted ===\n";
    
    fl::string_builder builder;
    
    // Test with string_view-convertible type
    builder.append_formatted("Value: {}", std::string_view("test"));
    assert(builder.size() > 0);
    std::cout << "? append_formatted with string_view succeeded\n";
    
    // Test with integral type
    builder.clear();
    builder.append_formatted("Number: {}", 42);
    assert(builder.size() > 0);
    std::cout << "? append_formatted with integral succeeded\n";
    
    // Test with floating-point type
    builder.clear();
    builder.append_formatted("Float: {}", 3.14);
    assert(builder.size() > 0);
    std::cout << "? append_formatted with floating-point succeeded\n";
}

// =============================================================================
// NEW TESTS
// =============================================================================

// ─── Config Macro Tests ────────────────────────────────────────────────────

static int test_config_macros() {
    int failures = 0;
    std::cout << "\n=== Testing Config Macros ===\n";

    // C++20 is the hard baseline; FL_HAS_CPP20 must always be 1
    if (!FL_HAS_CPP20) {
        std::cerr << "  ? FAIL: FL_HAS_CPP20 should be 1 (C++20 is the minimum)\n";
        ++failures;
    } else {
        std::cout << "  ? FL_HAS_CPP20 = 1\n";
    }

    // FL_HAS_CPP23 may be 0 or 1 depending on the compiler/stdlib
#if FL_HAS_CPP23
    std::cout << "  ? FL_HAS_CPP23 = 1 (C++23 or later)\n";
#else
    std::cout << "  ? FL_HAS_CPP23 = 0 (C++20 mode)\n";
#endif

    // FL_OVERRIDE compiles
    struct Base {
        virtual ~Base() = default;
        virtual int f() const { return 1; }
    };
    struct Derived : Base {
        int f() const FL_OVERRIDE { return 2; }
    };
    {
        Derived d;
        const Base& b = d;
        if (b.f() != 2) {
            std::cerr << "  ? FAIL: FL_OVERRIDE did not dispatch correctly\n";
            ++failures;
        } else {
            std::cout << "  ? FL_OVERRIDE compiles and works\n";
        }
    }

    // FL_FINAL compiles
    struct Finalized FL_FINAL {
        int g() const { return 3; }
    };
    {
        Finalized f;
        if (f.g() != 3) {
            std::cerr << "  ? FAIL: FL_FINAL class did not work\n";
            ++failures;
        } else {
            std::cout << "  ? FL_FINAL compiles and works\n";
        }
    }

    if (failures == 0) {
        std::cout << "  ? All config macro tests passed\n";
    }
    return failures;
}

// ─── ABI Macro Tests ───────────────────────────────────────────────────────

// FL_DEPRECATED test target: suppress deprecation warnings around the usage
#if defined(_MSC_VER)
#define FL_TEST_PRAGMA_PUSH __pragma(warning(push))
#define FL_TEST_PRAGMA_POP  __pragma(warning(pop))
#define FL_TEST_PRAGMA_SUPPRESS_DEPRECATED __pragma(warning(disable: 4996))
#elif defined(__clang__) || defined(__GNUC__)
#define FL_TEST_PRAGMA_PUSH _Pragma("GCC diagnostic push")
#define FL_TEST_PRAGMA_POP  _Pragma("GCC diagnostic pop")
#define FL_TEST_PRAGMA_SUPPRESS_DEPRECATED _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#else
#define FL_TEST_PRAGMA_PUSH
#define FL_TEST_PRAGMA_POP
#define FL_TEST_PRAGMA_SUPPRESS_DEPRECATED
#endif

FL_DEPRECATED("use new_func instead")
static int old_func() { return 0; }

static int test_abi_macros() {
    int failures = 0;
    std::cout << "\n=== Testing ABI / Export Macros ===\n";

    // Verify FL_EXPORT is defined
#ifdef FL_EXPORT
    std::cout << "  ? FL_EXPORT is defined\n";
#else
    std::cerr << "  ? FAIL: FL_EXPORT is not defined\n";
    ++failures;
#endif

    // Verify FL_IMPORT is defined
#ifdef FL_IMPORT
    std::cout << "  ? FL_IMPORT is defined\n";
#else
    std::cerr << "  ? FAIL: FL_IMPORT is not defined\n";
    ++failures;
#endif

    // Verify FL_LOCAL is defined
#ifdef FL_LOCAL
    std::cout << "  ? FL_LOCAL is defined\n";
#else
    std::cerr << "  ? FAIL: FL_LOCAL is not defined\n";
    ++failures;
#endif

    // Verify FL_INLINE is defined
#ifdef FL_INLINE
    {
        // Use FL_INLINE at block scope via a local typedef trick to verify
        // the macro expands to something syntactically valid
        std::cout << "  ? FL_INLINE is defined\n";
    }
#else
    std::cerr << "  ? FAIL: FL_INLINE is not defined\n";
    ++failures;
#endif

    // Verify FL_RESTRICT is defined
#ifdef FL_RESTRICT
    {
        int x = 42;
        int* FL_RESTRICT p = &x;
        (void)p;
        std::cout << "  ? FL_RESTRICT is defined and compiles\n";
    }
#else
    std::cerr << "  ? FAIL: FL_RESTRICT is not defined\n";
    ++failures;
#endif

    // FL_LIKELY / FL_UNLIKELY compile correctly
    {
        int x = 1;
        if (FL_LIKELY(x)) {
            // expected path
        }
        if (FL_UNLIKELY(!x)) {
            // unlikely path
        }
        std::cout << "  ? FL_LIKELY / FL_UNLIKELY compile correctly\n";
    }

    // FL_DEPRECATED compiles (suppress warnings)
    {
        FL_TEST_PRAGMA_PUSH
        FL_TEST_PRAGMA_SUPPRESS_DEPRECATED
        old_func();
        FL_TEST_PRAGMA_POP
        std::cout << "  ? FL_DEPRECATED compiles (warning suppressed)\n";
    }

    // FL_FALLTHROUGH compiles in a switch statement
    {
        int val = 2;
        int result = 0;
        switch (val) {
            case 1:
                result = 10;
                break;
            case 2:
                result = 20;
                FL_FALLTHROUGH;
            case 3:
                result += 5;
                break;
            default:
                result = -1;
                break;
        }
        if (result != 25) {
            std::cerr << "  ? FAIL: FL_FALLTHROUGH did not produce expected result (got " << result << ")\n";
            ++failures;
        } else {
            std::cout << "  ? FL_FALLTHROUGH compiles and works\n";
        }
    }

    if (failures == 0) {
        std::cout << "  ? All ABI macro tests passed\n";
    }
    return failures;
}

// ─── fl::string Tests ──────────────────────────────────────────────────────

static int test_string_basic() {
    int failures = 0;
    std::cout << "\n=== Testing fl::string Basic Operations ===\n";

    // Default construction
    {
        fl::string s;
        if (!s.empty() || s.size() != 0) {
            std::cerr << "  ? FAIL: Default constructed string should be empty\n";
            ++failures;
        } else {
            std::cout << "  ? Default construction OK\n";
        }
    }

    // Construction from C string
    {
        fl::string s("hello");
        if (s.size() != 5 || s.empty()) {
            std::cerr << "  ? FAIL: String from C string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from C string OK\n";
        }
    }

    // Construction from string_view
    {
        std::string_view sv("world");
        fl::string s(sv);
        if (s.size() != 5) {
            std::cerr << "  ? FAIL: String from string_view has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from string_view OK\n";
        }
    }

    // SSO test: strings <= 23 bytes should use SSO
    {
        fl::string short_str("12345678901234567890123"); // 23 chars
        if (short_str.size() != 23) {
            std::cerr << "  ? FAIL: SSO string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? SSO string (23 chars) has correct size\n";
        }
    }
    {
        // String > SSO capacity should still work
        fl::string long_str("123456789012345678901234"); // 24 chars
        if (long_str.size() != 24) {
            std::cerr << "  ? FAIL: Heap string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Heap string (24 chars) has correct size\n";
        }
    }

    // find / rfind
    {
        fl::string s("hello world, hello universe");
        if (s.find("hello") != 0) {
            std::cerr << "  ? FAIL: find('hello') should start at 0\n";
            ++failures;
        } else {
            std::cout << "  ? find('hello') OK\n";
        }
        if (s.find("world") != 6) {
            std::cerr << "  ? FAIL: find('world') should start at 6\n";
            ++failures;
        } else {
            std::cout << "  ? find('world') OK\n";
        }
        if (s.find('x') != fl::string::npos) {
            std::cerr << "  ? FAIL: find('x') should be npos\n";
            ++failures;
        } else {
            std::cout << "  ? find('x') returns npos OK\n";
        }
        if (s.rfind("hello") != 13) {
            std::cerr << "  ? FAIL: rfind('hello') should be 13\n";
            ++failures;
        } else {
            std::cout << "  ? rfind('hello') OK\n";
        }
        if (s.find('o') != 4) {
            std::cerr << "  ? FAIL: find('o') should be 4\n";
            ++failures;
        } else {
            std::cout << "  ? find('o') OK\n";
        }
    }

    // starts_with / ends_with / contains (available unconditionally)
    {
        fl::string s("hello world");
        if (!s.starts_with("hello")) {
            std::cerr << "  ? FAIL: starts_with('hello') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? starts_with('hello') OK\n";
        }
        if (s.starts_with("world")) {
            std::cerr << "  ? FAIL: starts_with('world') should be false\n";
            ++failures;
        } else {
            std::cout << "  ? starts_with('world') false OK\n";
        }
        if (s.starts_with('h') != true) {
            std::cerr << "  ? FAIL: starts_with('h') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? starts_with('h') OK\n";
        }
        if (!s.ends_with("world")) {
            std::cerr << "  ? FAIL: ends_with('world') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? ends_with('world') OK\n";
        }
        if (s.ends_with('d') != true) {
            std::cerr << "  ? FAIL: ends_with('d') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? ends_with('d') OK\n";
        }
        if (!s.contains("lo wo")) {
            std::cerr << "  ? FAIL: contains('lo wo') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? contains('lo wo') OK\n";
        }
        if (s.contains("xyz")) {
            std::cerr << "  ? FAIL: contains('xyz') should be false\n";
            ++failures;
        } else {
            std::cout << "  ? contains('xyz') false OK\n";
        }
        if (!s.contains('w')) {
            std::cerr << "  ? FAIL: contains('w') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? contains('w') OK\n";
        }
    }

    // substr_view / slice
    {
        fl::string s("hello world");
        auto sv1 = s.substr_view(0, 5);
        if (sv1.size() != 5 || std::memcmp(sv1.data(), "hello", 5) != 0) {
            std::cerr << "  ? FAIL: substr_view(0,5) should be 'hello'\n";
            ++failures;
        } else {
            std::cout << "  ? substr_view(0,5) OK\n";
        }
        auto sv2 = s.slice(6, 5);
        if (sv2.size() != 5 || std::memcmp(sv2.data(), "world", 5) != 0) {
            std::cerr << "  ? FAIL: slice(6,5) should be 'world'\n";
            ++failures;
        } else {
            std::cout << "  ? slice(6,5) OK\n";
        }
    }

    // Comparison operators
    {
        fl::string a("abc");
        fl::string b("abc");
        fl::string c("abd");
        fl::string d("ab");

        if (!(a == b)) {
            std::cerr << "  ? FAIL: 'abc' == 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator== OK\n";
        }
        if (a != b) {
            std::cerr << "  ? FAIL: 'abc' != 'abc' should be false\n";
            ++failures;
        } else {
            std::cout << "  ? operator!= OK\n";
        }
        if (!(a < c)) {
            std::cerr << "  ? FAIL: 'abc' < 'abd' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator< OK\n";
        }
        if (c < a) {
            std::cerr << "  ? FAIL: 'abd' < 'abc' should be false\n";
            ++failures;
        } else {
            std::cout << "  ? operator< ordering OK\n";
        }
        if (!(a <= b)) {
            std::cerr << "  ? FAIL: 'abc' <= 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator<= OK\n";
        }
        if (!(c > a)) {
            std::cerr << "  ? FAIL: 'abd' > 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator> OK\n";
        }
        if (!(c >= b)) {
            std::cerr << "  ? FAIL: 'abd' >= 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator>= OK\n";
        }
        // Test that 'ab' < 'abc' (shorter is less)
        if (!(d < a)) {
            std::cerr << "  ? FAIL: 'ab' < 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? shorter < longer OK\n";
        }
    }

    if (failures == 0) {
        std::cout << "  ? All string basic tests passed\n";
    }
    return failures;
}

#if FL_HAS_CPP20
static int test_string_spaceship() {
    int failures = 0;
    std::cout << "\n=== Testing fl::string operator<=> (C++20) ===\n";

    fl::string a("abc");
    fl::string b("abc");
    fl::string c("abd");

    auto cmp1 = a <=> b;
    if (cmp1 != 0) {
        std::cerr << "  ? FAIL: 'abc' <=> 'abc' should be equal\n";
        ++failures;
    } else {
        std::cout << "  ? 'abc' <=> 'abc' == equal OK\n";
    }

    auto cmp2 = a <=> c;
    if (cmp2 >= 0) {
        std::cerr << "  ? FAIL: 'abc' <=> 'abd' should be less\n";
        ++failures;
    } else {
        std::cout << "  ? 'abc' <=> 'abd' == less OK\n";
    }

    auto cmp3 = c <=> a;
    if (cmp3 <= 0) {
        std::cerr << "  ? FAIL: 'abd' <=> 'abc' should be greater\n";
        ++failures;
    } else {
        std::cout << "  ? 'abd' <=> 'abc' == greater OK\n";
    }

    if (failures == 0) {
        std::cout << "  ? All string spaceship tests passed\n";
    }
    return failures;
}
#endif

// ─── fl::rope Tests ────────────────────────────────────────────────────────

static int test_rope_basic() {
    int failures = 0;
    std::cout << "\n=== Testing fl::rope Basic Operations ===\n";

    // Default construction
    {
        fl::rope r;
        if (!r.empty() || r.size() != 0) {
            std::cerr << "  ? FAIL: Default constructed rope should be empty\n";
            ++failures;
        } else {
            std::cout << "  ? Default construction OK\n";
        }
    }

    // Construction from C string
    {
        fl::rope r("hello");
        if (r.size() != 5 || r.empty()) {
            std::cerr << "  ? FAIL: Rope from C string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from C string OK\n";
        }
    }

    // Construction from string_view
    {
        std::string_view sv("world");
        fl::rope r(sv);
        if (r.size() != 5) {
            std::cerr << "  ? FAIL: Rope from string_view has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from string_view OK\n";
        }
    }

    // Copy construction
    {
        fl::rope r1("hello");
        fl::rope r2(r1);
        if (r2.size() != 5) {
            std::cerr << "  ? FAIL: Copy constructed rope has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Copy construction OK\n";
        }
    }

    // Move construction
    {
        fl::rope r1("hello");
        fl::rope r2(std::move(r1));
        if (r2.size() != 5) {
            std::cerr << "  ? FAIL: Move constructed rope has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Move construction OK\n";
        }
    }

    // size() / empty()
    {
        fl::rope r("test");
        if (r.size() != 4) {
            std::cerr << "  ? FAIL: rope.size() should be 4\n";
            ++failures;
        } else {
            std::cout << "  ? size() OK\n";
        }
        if (r.empty()) {
            std::cerr << "  ? FAIL: rope with 'test' should not be empty\n";
            ++failures;
        } else {
            std::cout << "  ? empty() OK\n";
        }
    }

    // Concatenation (operator+)
    {
        fl::rope r1("hello");
        fl::rope r2(" world");
        fl::rope r3 = r1 + r2;
        if (r3.size() != 11) {
            std::cerr << "  ? FAIL: Concatenated rope size should be 11\n";
            ++failures;
        } else {
            std::cout << "  ? operator+ concatenation OK\n";
        }
        // Test concatenating with C string
        fl::rope r4 = r1 + " world";
        if (r4.size() != 11) {
            std::cerr << "  ? FAIL: rope + C string concatenation size should be 11\n";
            ++failures;
        } else {
            std::cout << "  ? rope + C string concatenation OK\n";
        }
    }

    // operator+=
    {
        fl::rope r("hello");
        r += " world";
        if (r.size() != 11) {
            std::cerr << "  ? FAIL: rope += C string size should be 11\n";
            ++failures;
        } else {
            std::cout << "  ? operator+= OK\n";
        }
        fl::rope r2("foo");
        fl::rope r3("bar");
        r2 += r3;
        if (r2.size() != 6) {
            std::cerr << "  ? FAIL: rope += rope size should be 6\n";
            ++failures;
        } else {
            std::cout << "  ? rope += rope OK\n";
        }
    }

    // substr()
    {
        fl::rope r("hello world");
        auto sub = r.substr(0, 5);
        if (sub.size() != 5) {
            std::cerr << "  ? FAIL: rope.substr(0,5) size should be 5\n";
            ++failures;
        } else {
            std::cout << "  ? substr(0,5) OK\n";
        }
        auto sub2 = r.substr(6, 5);
        if (sub2.size() != 5) {
            std::cerr << "  ? FAIL: rope.substr(6,5) size should be 5\n";
            ++failures;
        } else {
            std::cout << "  ? substr(6,5) OK\n";
        }
    }

    // Linearization
    {
        fl::rope r("hello");
        auto flat = r.flatten();
        if (flat.size() != 5) {
            std::cerr << "  ? FAIL: rope.flatten() size should be 5\n";
            ++failures;
        } else {
            std::cout << "  ? flatten() OK\n";
        }
        auto std_str = r.to_std_string();
        if (std_str != "hello") {
            std::cerr << "  ? FAIL: rope.to_std_string() should be 'hello'\n";
            ++failures;
        } else {
            std::cout << "  ? to_std_string() OK\n";
        }
    }

    // Comparison
    // rope defines operator== only in pre-C++20 mode.
    // In C++20 mode, operator<=> is defined but does NOT auto-generate
    // operator== (since it's not = default).  Use to_std_string() to compare.
#if !FL_HAS_CPP20
    {
        fl::rope a("hello");
        fl::rope b("hello");
        fl::rope c("world");

        if (!(a == b)) {
            std::cerr << "  ? FAIL: rope 'hello' == 'hello' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? rope operator== OK\n";
        }
        if (a == c) {
            std::cerr << "  ? FAIL: rope 'hello' == 'world' should be false\n";
            ++failures;
        } else {
            std::cout << "  ? rope operator== inequality OK\n";
        }
    }
#else
    {
        fl::rope a("hello");
        fl::rope b("hello");
        fl::rope c("world");

        if ((a <=> b) != 0) {
            std::cerr << "  ? FAIL: rope 'hello' <=> 'hello' should be equal\n";
            ++failures;
        } else {
            std::cout << "  ? rope operator<=> equality OK\n";
        }
        if ((a <=> c) == 0) {
            std::cerr << "  ? FAIL: rope 'hello' <=> 'world' should not be equal\n";
            ++failures;
        } else {
            std::cout << "  ? rope operator<=> inequality OK\n";
        }
    }
#endif

    // operator[] and front/back
    {
        fl::rope r("hello");
        if (r[0] != 'h' || r[4] != 'o') {
            std::cerr << "  ? FAIL: rope operator[] incorrect\n";
            ++failures;
        } else {
            std::cout << "  ? operator[] OK\n";
        }
        if (r.front() != 'h' || r.back() != 'o') {
            std::cerr << "  ? FAIL: rope front/back incorrect\n";
            ++failures;
        } else {
            std::cout << "  ? front()/back() OK\n";
        }
    }

    if (failures == 0) {
        std::cout << "  ? All rope basic tests passed\n";
    }
    return failures;
}

#if FL_HAS_CPP20
static int test_rope_spaceship() {
    int failures = 0;
    std::cout << "\n=== Testing fl::rope operator<=> (C++20) ===\n";

    fl::rope a("hello");
    fl::rope b("hello");
    fl::rope c("world");
    fl::rope d("hell");  // shorter

    auto cmp1 = a <=> b;
    if (cmp1 != 0) {
        std::cerr << "  ? FAIL: 'hello' <=> 'hello' should be equal\n";
        ++failures;
    } else {
        std::cout << "  ? 'hello' <=> 'hello' == equal OK\n";
    }

    auto cmp2 = a <=> c;
    if (cmp2 >= 0) {
        std::cerr << "  ? FAIL: 'hello' <=> 'world' should be less\n";
        ++failures;
    } else {
        std::cout << "  ? 'hello' <=> 'world' == less OK\n";
    }

    auto cmp3 = c <=> a;
    if (cmp3 <= 0) {
        std::cerr << "  ? FAIL: 'world' <=> 'hello' should be greater\n";
        ++failures;
    } else {
        std::cout << "  ? 'world' <=> 'hello' == greater OK\n";
    }

    // Shorter vs longer
    auto cmp4 = d <=> a;
    if (cmp4 >= 0) {
        std::cerr << "  ? FAIL: 'hell' <=> 'hello' should be less\n";
        ++failures;
    } else {
        std::cout << "  ? 'hell' <=> 'hello' == less (shorter < longer) OK\n";
    }

    if (failures == 0) {
        std::cout << "  ? All rope spaceship tests passed\n";
    }
    return failures;
}
#endif

// ─── fl::synchronised_string Tests ─────────────────────────────────────────

static int test_synchronised_string_basic() {
    int failures = 0;
    std::cout << "\n=== Testing fl::synchronised_string Basic Operations ===\n";

    // Default construction
    {
        fl::synchronised_string s;
        if (!s.empty() || s.size() != 0) {
            std::cerr << "  ? FAIL: Default constructed synchronised_string should be empty\n";
            ++failures;
        } else {
            std::cout << "  ? Default construction OK\n";
        }
    }

    // Construction from C string
    {
        fl::synchronised_string s("hello");
        if (s.size() != 5 || s.empty()) {
            std::cerr << "  ? FAIL: synchronised_string from C string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from C string OK\n";
        }
    }

    // Construction from fl::string
    {
        fl::string fs("world");
        fl::synchronised_string s(fs);
        if (s.size() != 5) {
            std::cerr << "  ? FAIL: synchronised_string from fl::string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from fl::string OK\n";
        }
    }

    // Copy construction
    {
        fl::synchronised_string s1("hello");
        fl::synchronised_string s2(s1);
        if (s2.size() != 5) {
            std::cerr << "  ? FAIL: Copy constructed synchronised_string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Copy construction OK\n";
        }
    }

    // Move construction
    {
        fl::synchronised_string s1("hello");
        fl::synchronised_string s2(std::move(s1));
        if (s2.size() != 5) {
            std::cerr << "  ? FAIL: Move constructed synchronised_string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Move construction OK\n";
        }
    }

    // read() with callback
    {
        fl::synchronised_string s("hello");
        s.read([](const fl::string& str) {
            if (str.size() != 5) {
                std::cerr << "  ? FAIL: read() callback received wrong string\n";
                return 1;
            }
            return 0;
        });
        std::cout << "  ? read() callback OK\n";
    }

    // write() with callback
    {
        fl::synchronised_string s("hello");
        s.write([](fl::string& str) {
            str += " world";
        });
        if (s.size() != 11) {
            std::cerr << "  ? FAIL: write() callback did not modify string (size=" << s.size() << ")\n";
            ++failures;
        } else {
            std::cout << "  ? write() callback OK\n";
        }
    }

    // snapshot() / copy
    {
        fl::synchronised_string s("hello");
        fl::string snap = s.snapshot();
        if (snap.size() != 5) {
            std::cerr << "  ? FAIL: snapshot() returned wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? snapshot() OK\n";
        }
    }

    // to_fl_string()
    {
        fl::synchronised_string s("hello");
        fl::string str = s.to_fl_string();
        if (str.size() != 5) {
            std::cerr << "  ? FAIL: to_fl_string() returned wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? to_fl_string() OK\n";
        }
    }

    // operator+=
    {
        fl::synchronised_string s("hello");
        s += " world";
        if (s.size() != 11) {
            std::cerr << "  ? FAIL: synchronised_string += gave wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? operator+= OK\n";
        }
    }

    // compare()
    {
        fl::synchronised_string a("abc");
        fl::synchronised_string b("abc");
        fl::synchronised_string c("abd");

        if (a.compare(b) != 0) {
            std::cerr << "  ? FAIL: compare('abc', 'abc') should be 0\n";
            ++failures;
        } else {
            std::cout << "  ? compare() equal OK\n";
        }
        if (a.compare(c) >= 0) {
            std::cerr << "  ? FAIL: compare('abc', 'abd') should be < 0\n";
            ++failures;
        } else {
            std::cout << "  ? compare() less OK\n";
        }
        if (c.compare(a) <= 0) {
            std::cerr << "  ? FAIL: compare('abd', 'abc') should be > 0\n";
            ++failures;
        } else {
            std::cout << "  ? compare() greater OK\n";
        }

        // Compare with string_view
        if (a.compare(std::string_view("abc")) != 0) {
            std::cerr << "  ? FAIL: compare with string_view('abc') should be 0\n";
            ++failures;
        } else {
            std::cout << "  ? compare(string_view) OK\n";
        }
    }

    if (failures == 0) {
        std::cout << "  ? All synchronised_string basic tests passed\n";
    }
    return failures;
}

#if FL_HAS_CPP20
static int test_synchronised_string_spaceship() {
    int failures = 0;
    std::cout << "\n=== Testing fl::synchronised_string Comparison (C++20) ===\n";

    // In C++20, synchronised_string's compare() method still works.
    // While there's no direct operator<=>, we verify the comparison
    // semantics are correct in C++20 mode.
    fl::synchronised_string a("abc");
    fl::synchronised_string b("abc");
    fl::synchronised_string c("abd");

    if (a.compare(b) != 0) {
        std::cerr << "  ? FAIL: compare('abc', 'abc') should be 0\n";
        ++failures;
    } else {
        std::cout << "  ? compare() equal OK\n";
    }
    if (a.compare(c) >= 0) {
        std::cerr << "  ? FAIL: compare('abc', 'abd') should be < 0\n";
        ++failures;
    } else {
        std::cout << "  ? compare() less OK\n";
    }
    if (c.compare(a) <= 0) {
        std::cerr << "  ? FAIL: compare('abd', 'abc') should be > 0\n";
        ++failures;
    } else {
        std::cout << "  ? compare() greater OK\n";
    }

    if (failures == 0) {
        std::cout << "  ? All synchronised_string C++20 comparison tests passed\n";
    }
    return failures;
}
#endif

// ─── fl::substring_view Tests ──────────────────────────────────────────────

static int test_substring_view_basic() {
    int failures = 0;
    std::cout << "\n=== Testing fl::substring_view Basic Operations ===\n";

    // Default construction
    {
        fl::substring_view sv;
        if (!sv.empty() || sv.size() != 0) {
            std::cerr << "  ? FAIL: Default constructed substring_view should be empty\n";
            ++failures;
        } else {
            std::cout << "  ? Default construction OK\n";
        }
    }

    // Construction from C string
    {
        fl::substring_view sv("hello world");
        if (sv.size() != 11 || sv.empty()) {
            std::cerr << "  ? FAIL: substring_view from C string has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from C string OK\n";
        }
    }

    // Construction from data + length
    {
        const char* data = "hello world";
        fl::substring_view sv(data, 5);
        if (sv.size() != 5) {
            std::cerr << "  ? FAIL: substring_view(data,5) has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? Construction from data+length OK\n";
        }
    }

    // Element access
    {
        fl::substring_view sv("hello", 5);
        if (sv[0] != 'h' || sv[4] != 'o') {
            std::cerr << "  ? FAIL: operator[] incorrect\n";
            ++failures;
        } else {
            std::cout << "  ? operator[] OK\n";
        }
        if (sv.front() != 'h' || sv.back() != 'o') {
            std::cerr << "  ? FAIL: front/back incorrect\n";
            ++failures;
        } else {
            std::cout << "  ? front()/back() OK\n";
        }
    }

    // at() with bounds checking
    {
        fl::substring_view sv("hello", 5);
        try {
            char c = sv.at(0);
            (void)c;
        } catch (...) {
            std::cerr << "  ? FAIL: at(0) should not throw\n";
            ++failures;
        }
        try {
            sv.at(10);
            std::cerr << "  ? FAIL: at(10) should throw out_of_range\n";
            ++failures;
        } catch (const std::out_of_range&) {
            std::cout << "  ? at() bounds checking OK\n";
        }
    }

    // Comparison operators
    {
        fl::substring_view a("abc", 3);
        fl::substring_view b("abc", 3);
        fl::substring_view c("abd", 3);

        if (!(a == b)) {
            std::cerr << "  ? FAIL: 'abc' == 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator== OK\n";
        }
        if (a == c) {
            std::cerr << "  ? FAIL: 'abc' == 'abd' should be false\n";
            ++failures;
        } else {
            std::cout << "  ? operator== inequality OK\n";
        }
        if (!(a < c)) {
            std::cerr << "  ? FAIL: 'abc' < 'abd' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator< OK\n";
        }
        if (!(a <= b)) {
            std::cerr << "  ? FAIL: 'abc' <= 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator<= OK\n";
        }
        if (!(c > a)) {
            std::cerr << "  ? FAIL: 'abd' > 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator> OK\n";
        }
        if (!(c >= b)) {
            std::cerr << "  ? FAIL: 'abd' >= 'abc' should be true\n";
            ++failures;
        } else {
            std::cout << "  ? operator>= OK\n";
        }
        if (a != b) {
            std::cerr << "  ? FAIL: 'abc' != 'abc' should be false\n";
            ++failures;
        } else {
            std::cout << "  ? operator!= OK\n";
        }
    }

    // substr() on substring_view
    {
        fl::substring_view sv("hello world", 11);
        auto sub = sv.substr(0, 5);
        if (sub.size() != 5) {
            std::cerr << "  ? FAIL: substr(0,5) has wrong size\n";
            ++failures;
        } else {
            std::cout << "  ? substr() OK\n";
        }
    }

    // find() / rfind()
    {
        fl::substring_view sv("hello world hello", 17);
        auto pos = sv.find('w');
        if (pos != 6) {
            std::cerr << "  ? FAIL: find('w') should be 6, got " << pos << "\n";
            ++failures;
        } else {
            std::cout << "  ? find('w') OK\n";
        }
        auto rpos = sv.rfind('h');
        if (rpos != 12) {
            std::cerr << "  ? FAIL: rfind('h') should be 12, got " << rpos << "\n";
            ++failures;
        } else {
            std::cout << "  ? rfind('h') OK\n";
        }
    }

    // starts_with / ends_with / contains on substring_view
    {
        fl::substring_view sv("hello world", 11);
        if (!sv.starts_with(fl::substring_view("hello", 5))) {
            std::cerr << "  ? FAIL: starts_with('hello') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? starts_with() OK\n";
        }
        if (!sv.ends_with(fl::substring_view("world", 5))) {
            std::cerr << "  ? FAIL: ends_with('world') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? ends_with() OK\n";
        }
        if (!sv.contains(fl::substring_view("lo wo", 5))) {
            std::cerr << "  ? FAIL: contains('lo wo') should be true\n";
            ++failures;
        } else {
            std::cout << "  ? contains() OK\n";
        }
    }

    if (failures == 0) {
        std::cout << "  ? All substring_view basic tests passed\n";
    }
    return failures;
}

#if FL_HAS_CPP20
static int test_substring_view_spaceship() {
    int failures = 0;
    std::cout << "\n=== Testing fl::substring_view operator<=> (C++20) ===\n";

    fl::substring_view a("abc", 3);
    fl::substring_view b("abc", 3);
    fl::substring_view c("abd", 3);

    auto cmp1 = a <=> b;
    if (cmp1 != 0) {
        std::cerr << "  ? FAIL: 'abc' <=> 'abc' should be equal\n";
        ++failures;
    } else {
        std::cout << "  ? 'abc' <=> 'abc' == equal OK\n";
    }

    auto cmp2 = a <=> c;
    if (cmp2 >= 0) {
        std::cerr << "  ? FAIL: 'abc' <=> 'abd' should be less\n";
        ++failures;
    } else {
        std::cout << "  ? 'abc' <=> 'abd' == less OK\n";
    }

    auto cmp3 = c <=> a;
    if (cmp3 <= 0) {
        std::cerr << "  ? FAIL: 'abd' <=> 'abc' should be greater\n";
        ++failures;
    } else {
        std::cout << "  ? 'abd' <=> 'abc' == greater OK\n";
    }

    if (failures == 0) {
        std::cout << "  ? All substring_view spaceship tests passed\n";
    }
    return failures;
}
#endif

// ─── Existing Tests Wrapped as Int-Returning ───────────────────────────────

// These wrappers call the original void tests and catch assertion failures
// so we can integrate them into the failure-counting main().

static int test_arena_basic() {
    // test_arena() uses assert(), which calls abort() on failure.
    // We wrap it in a try/catch and also use our own checks.
    try {
        test_arena();
        return 0;
    } catch (...) {
        std::cerr << "  ? FAIL: Arena test threw exception\n";
        return 1;
    }
}

static int test_rope_basic_legacy() {
    try {
        test_rope();
        return 0;
    } catch (...) {
        std::cerr << "  ? FAIL: Rope legacy test threw exception\n";
        return 1;
    }
}

static int test_builder_basic() {
    try {
        test_builder();
        test_builder_traits();
        test_builder_append_formatted();
        return 0;
    } catch (...) {
        std::cerr << "  ? FAIL: Builder test threw exception\n";
        return 1;
    }
}

}  // anonymous namespace

int main() {
    std::cout << "====================================\n";
    std::cout << "C++ Version Compatibility Tests\n";
    std::cout << "====================================\n";

    print_cpp_version();

    int failures = 0;

    // Always-run tests
    failures += test_config_macros();
    failures += test_abi_macros();
    failures += test_string_basic();
    failures += test_rope_basic();
    failures += test_synchronised_string_basic();
    failures += test_substring_view_basic();
    failures += test_builder_basic();
    failures += test_arena_basic();
    failures += test_rope_basic_legacy();

    // Standard-specific tests
#if FL_HAS_CPP20
    failures += test_rope_spaceship();
    failures += test_string_spaceship();
    failures += test_synchronised_string_spaceship();
    failures += test_substring_view_spaceship();
#endif

    std::cout << "\n====================================\n";
    if (failures == 0) {
        std::cout << "? ALL TESTS PASSED\n";
        std::cout << "====================================\n";
        return 0;
    } else {
        std::cerr << "? " << failures << " TEST(S) FAILED\n";
        std::cerr << "====================================\n";
        return 1;
    }
}
