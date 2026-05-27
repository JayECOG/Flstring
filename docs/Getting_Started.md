# Getting Started with the `fl` Library

This guide introduces the `fl` string library and covers basic usage patterns to help you write efficient string manipulation code.

## Installation

The `fl` library is a header-only C++20 library. For basic usage, include the main header file in your project:

```cpp
#include <fl.hpp>
```

Individual component headers may be included directly from `fl/` to keep translation units trim:

```cpp
#include <fl/string.hpp>        // fl::string only
#include <fl/rope.hpp>          // fl::rope only
#include <fl/format.hpp>        // formatting system only
```

### CMake Integration

To build the tests and examples, or to use fl as a project dependency:

```bash
# Configure with Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build --parallel

# Run tests
ctest --test-dir build --output-on-failure
```

Key CMake options for consumers:

| Option | Default | Effect |
|--------|---------|--------|
| `-DFL_BUILD_SHARED=ON` | OFF | Build as shared library (DLL on Windows) |
| `-DFL_FETCH_DEPS=ON` | OFF | Auto-fetch dependencies (Abseil, Boost) for benchmarks |
| `-DFL_BENCHMARK_THIRD_PARTY=ON` | OFF | Enable third-party benchmark targets |

For downstream projects, install the library and use `find_package`:

```bash
cmake --install build --prefix /path/to/install
```

```cmake
# In your CMakeLists.txt
find_package(fl CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE fl::fl)
```

Or use FetchContent directly:

```cmake
include(FetchContent)
FetchContent_Declare(fl
    GIT_REPOSITORY https://github.com/JayECOG/Flstring-cover
    GIT_TAG        main
)
FetchContent_MakeAvailable(fl)
target_link_libraries(your_target PRIVATE fl::fl)
```

## Your First `fl::string`

The `fl::string` class is designed to be a drop-in replacement for `std::string`. You can use it in much the same way you would use its standard library counterpart.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::string greeting = "Hello";
    fl::string target = "World";

    fl::string message = greeting + ", " + target + "!";

    std::cout << message << std::endl; // Output: Hello, World!

    return 0;
}
```

For small strings like the ones above, `fl::string` uses its Small-String Optimisation (SSO) to avoid allocating any memory on the heap. This can make a significant difference in performance-critical applications.

## Composing Strings with the `string_builder`

While the `+` operator works for simple concatenations, it can be inefficient when building up a string from many pieces. The `fl::string_builder` allows you to append to a string with minimal reallocations.

```cpp
#include <fl.hpp>
#include <iostream>
#include <vector>

int main() {
    std::vector<fl::string> items = {"one", "two", "three"};

    fl::string_builder builder;
    builder.append("Items: ");

    for (const auto& item : items) {
        builder.append(item).append(" ");
    }

    fl::string result = std::move(builder).build();

    std::cout << result << std::endl; // Output: Items: one two three

    return 0;
}
```

Notice the `std::move(builder).build()` at the end. The builder is designed to be moved from, not copied. This ensures that the internal buffer is transferred to the final string without unnecessary copies.

## Zero-Allocation Formatting

The `fl::format_to` function, when used with a sink, formats strings directly into a buffer without creating any temporary string objects.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    char buffer[256];
    auto sink = fl::make_buffer_sink(buffer);

    int value = 42;
    fl::format_to(sink, "The answer is: {}", value);

    sink.null_terminate();

    std::cout << buffer << std::endl; // Output: The answer is: 42

    return 0;
}
```

Several sink types are provided for writing to fixed buffers, growing buffers, files, and streams. See the [Formatting](./Formatting.md) guide for details.

## Efficient Concatenation with `fl::rope`

When you need to join many strings together, `fl::rope` provides amortised O(1) concatenation by building a balanced tree of string fragments rather than copying data.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::rope r;
    for (int i = 0; i < 1000; ++i) {
        r += fl::rope("fragment ");   // O(1) concatenation, no data copied
    }

    fl::string result = r.flatten();  // O(n) linearisation when you need it
    std::cout << "Rope length: " << r.size() << std::endl;
    return 0;
}
```

Rope is ideal for workloads that build up a large string incrementally and only need contiguous access once. See the [rope documentation](./Features.md#flrope----balanced-concatenation-tree) for details on rebalancing and access patterns.

## Thread-Safe Sharing with `fl::immutable_string`

For strings that are read from multiple threads but never mutated after construction, `fl::immutable_string` provides atomic reference counting and O(1) copies.

```cpp
#include <fl.hpp>
#include <unordered_map>
#include <thread>

int main() {
    fl::immutable_string key("shared_key");
    fl::immutable_string copy = key;    // O(1) atomic ref-count increment

    // Use as map keys with transparent hash/equality
    std::unordered_map<fl::immutable_string, int,
                       fl::immutable_string_hash,
                       fl::immutable_string_equal> table;
    table[key] = 42;

    return 0;
}
```

For mutable thread-safe strings, use `fl::synchronised_string` instead, which wraps all access through a `std::shared_mutex`.

## What's Next?

Explore the rest of the documentation to learn more about the library's capabilities:

*   **[Features](./Features.md):** A detailed look at every public component and its key implementation details.
*   **[API Reference](./API.md):** The complete API reference for every function, class, and method.
*   **[Examples](./Examples.md):** A collection of practical code examples covering common use cases.
*   **[Formatting](./Formatting.md):** A guide to the zero-allocation formatting system and sink types.
*   **[Performance](./Performance.md):** Benchmark methodology, cross-library comparisons, and known limitations.
*   **[Philosophy](./PHILOSOPHY.md):** The design principles behind the `fl` library.
*   **[Developer Guide](./Developer_Guide.md):** Architectural overview and guidelines for contributing.
