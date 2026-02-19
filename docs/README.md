# fl: A High-Performance C++ String Library

The `fl` library is a header-only, high-performance C++20 string library engineered for systems
where memory efficiency and allocation overhead are critical.

## Philosophy

The `fl` library is designed for systems where performance is a primary requirement. It addresses
fundamental limitations in `std::string`:

* **Heap allocations for small strings**: Many strings encountered in practice are small, yet
  `std::string` allocates them on the heap.
* **Inefficient building operations**: Repeated appends cause unnecessary reallocations.
* **Memory fragmentation**: The creation of temporary objects creates allocation pressure,
  particularly in formatting-intensive code.

The `fl` library provides the following solutions:

1. **Small-String Optimisation (SSO)**: Strings up to 23 bytes are stored on the stack.
2. **Arena Allocators**: Temporary buffers with automatic overflow handling for stack-backed
   allocations.
3. **Zero-Allocation Formatting**: A system of output "sinks" for direct-to-buffer writing,
   avoiding temporary objects entirely.
4. **Efficient Builders**: A move-semantic `string_builder` with configurable growth policies for
   composing strings efficiently.

---

## Getting Started

### Installation

The `fl` library is header-only. For basic usage, include the main header file:
```cpp
#include <fl.hpp>
```

To build the tests and examples, use CMake:
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Your First `fl::string`

The `fl::string` is designed as a drop-in replacement for `std::string`.
```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::string greeting = "Hello";
    fl::string target   = "World";

    fl::string message = greeting + ", " + target + "!";

    std::cout << message << std::endl; // Output: Hello, World!
}
```

For small strings like the ones above, `fl::string` uses its Small-String Optimisation (SSO) to
avoid allocating any memory on the heap.

---

## Core Features

### Small-String Optimisation (SSO)

For strings of 23 bytes or fewer, `fl::string` avoids the heap entirely, storing the data within
the string object itself.
```cpp
fl::string short_str("Hi");                                                    // Stack.
fl::string long_str("This string is rather long and will be on the heap.");    // Heap.
```

### The `string_builder`

For composing a string from many fragments, the `fl::string_builder` provides a fluent interface
and minimises reallocations.
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
}
```

### The Arena Allocator

For situations that involve the creation of many temporary strings, the `fl::arena_allocator`
provides a pre-allocated block of memory from which smaller allocations can be made.
```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    // A 4 KB stack-backed arena.
    fl::arena_buffer<4096> arena;

    arena.append("This is a ");
    arena.append("rather long string being built up from many smaller pieces.");

    fl::string result = arena.to_string();

    std::cout << result << std::endl;
}
```

### Zero-Allocation Formatting with Sinks

The formatting system allows formatting strings directly into a "sink" without creating
intermediate string objects.
```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    char buffer[256];
    auto sink = fl::make_buffer_sink(buffer);

    int user_id = 123;
    fl::format_to(sink, "User ID: {}", user_id);
    sink.null_terminate();

    std::cout << buffer << std::endl;
}
```

---

## Performance

Benchmarked on CPU-pinned (`taskset -c 0`) release builds (`-O2 -DNDEBUG`) against `std::string`
and `boost::container::string`. Full methodology, raw figures, and known limitations in
[Performance.md](./Performance.md).

| Operation | fl vs `std::string` | fl vs `boost` |
|---|---|---|
| Heap construction (100 chars) | **2.2x faster** | **2.7x faster** |
| `find()` — low-entropy / periodic data | **up to 15.6x faster** | **130x faster** |
| `compare()` 64 B | parity | 1.2x faster |
| `substr()` 64 B | parity | 2.0x faster |
| Append growth (repeated small appends) | 2x slower* | 2.1x faster |

\* `fl`'s pool allocator cannot extend allocations in-place; `std::string` benefits from
`realloc`. For append-heavy workloads, use `fl::string_builder` instead.

`fl::string::find` uses a **Two-Way (Crochemore-Rytter)** algorithm that guarantees O(n+m)
behaviour, making it particularly strong on XML, HTML, binary protocols, or any
repeated-structure data where glibc `memmem` degrades toward O(n×m).

---

## Performance Notes

We run performance tracking with repeated samples, median reporting, and spread (IQR) to reduce
one-off noise.

Recent hot-path analysis identified three priority areas:

* `fl::rope` random access traversal overhead.
* `fl::string` short-needle substring search (`find`).
* `fl::string` heap-construction behaviour in mid-sized cases.

A dedicated report with methodology, command lines, current figures, and dependency caveats is
available in **[Performance.md](./Performance.md)**.

Optional third-party benchmark targets are dependency-gated: `cross_library_bench` is generated
only when Abseil and Boost are present, and `folly_benchmark` only when Folly is present. This
keeps the benchmark build resilient on lean environments while still enabling richer comparisons
where dependencies are available.

---

## Contributing and Security

Contribution guidelines are available in [CONTRIBUTING.md](../CONTRIBUTING.md). Security policies
and vulnerability reporting procedures are documented in [SECURITY.md](../SECURITY.md).

---

## What's Next?

Explore the rest of the documentation to learn more:

* **[Features.md](./Features.md)**: A detailed look at the core features.
* **[API.md](./API.md)**: The complete API reference for the library.
* **[Examples.md](./Examples.md)**: A collection of code examples.
* **[Formatting.md](./Formatting.md)**: A guide to the formatting system.
* **[Developer_Guide.md](./Developer_Guide.md)**: Guidelines for contributing to the project.
* **[PHILOSOPHY.md](./PHILOSOPHY.md)**: The design philosophy behind the library.
* **[Performance.md](./Performance.md)**: Performance analysis and benchmarking methodology.
