# Getting Started with the `fl` Library

This guide introduces the `fl` library and covers basic usage patterns to help you write efficient string manipulation code.

## Installation

The `fl` library is a header-only C++20 library. For basic usage, include the main header file in your project:

```cpp
#include <fl.hpp>
```

If you wish to build the tests and examples, you can use CMake:

```bash
mkdir build && cd build
cmake ..
cmake --build .
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

## What's Next?

Explore the rest of the documentation to learn more about the library's capabilities:

*   [Features](./Features.md): A detailed look at the core features of the library.
*   [API Reference](./API.md): The complete API reference for the library's functions and classes.
*   [Examples](./Examples.md): A collection of practical code examples.
*   [Formatting](./Formatting.md): A guide to the zero-allocation formatting system.
*   [Developer Guide](./Developer_Guide.md): Guidelines for contributing to the project.
