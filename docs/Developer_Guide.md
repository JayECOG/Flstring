# Developer Guide -- `fl` Library

## Project Overview

The `fl` (Forever Lightweight) library is a production-ready, high-performance C++ string library engineered to outperform `std::string` through the following core optimisations:

1.  **Small-String Optimisation (SSO)** -- Eliminates heap allocation for strings up to 23 bytes.
2.  **Arena-Backed Allocation** -- Stack-first temporary buffers with automatic overflow handling.
3.  **Zero-Allocation Formatting** -- Output sinks for direct buffer writing without intermediate strings.
4.  **Efficient Move Builders** -- Builder pattern with intelligent capacity management and growth policies.
5.  **Thread-Local Free-List Pool Allocator** -- Fast, lock-free small-block allocation via per-thread free lists.
6.  **Two-Way String Search Algorithm** -- Optimal `find()` implementation with AVX2-accelerated first-character scanning.
7.  **Dedicated Rope Node Slab Allocator** -- Efficient memory management for rope tree nodes.
8.  **Debug Thread-Safety Diagnostics** -- Optional runtime detection of concurrent access violations on string types.

## Core Library Implementation

The library is entirely header-only. All public headers reside under `include/`.

| File                                     | Purpose                                                           | Status     |
| :--------------------------------------- | :---------------------------------------------------------------- | :--------- |
| `include/fl.hpp`                         | Umbrella header (includes all components)                         | Complete   |
| `include/fl/string.hpp`                  | Core string class with SSO                                        | Complete   |
| `include/fl/builder.hpp`                 | String builder with configurable growth policies                  | Complete   |
| `include/fl/substring_view.hpp`          | Lightweight non-owning string views with shared ownership         | Complete   |
| `include/fl/rope.hpp`                    | Tree-based O(1) amortised concatenation                           | Complete   |
| `include/fl/immutable_string.hpp`        | Immutable string keys with cached hashes                          | Complete   |
| `include/fl/synchronised_string.hpp`     | Thread-safe mutable string (British spelling, primary header)     | Complete   |
| `include/fl/synchronized_string.hpp`     | US-spelling alias; redirects to `synchronised_string.hpp`         | Complete   |
| `include/fl/arena.hpp`                   | Arena allocators and temporary buffers                            | Complete   |
| `include/fl/format.hpp`                  | Formatting utilities with full format-spec parsing                | Complete   |
| `include/fl/sinks.hpp`                   | Output sink abstractions (`buffer_sink`, `growing_sink`, etc.)    | Complete   |
| `include/fl/alloc_hooks.hpp`             | Pluggable allocator hooks and thread-local free-list pool         | Complete   |
| `include/fl/config.hpp`                  | Compile-time configuration and feature detection macros           | Complete   |
| `include/fl/profiling.hpp`               | Optional scoped profiler (zero-cost when disabled)                | Complete   |
| `include/fl/debug/thread_safety.hpp`     | Runtime thread-safety diagnostics (debug builds only)             | Complete   |

### Key Architectural Concepts

*   **Small-String Optimisation (SSO)**: `fl::string` uses a union-based storage strategy. Strings below `fl::SSO_CAPACITY` (23 bytes) are stored directly within the object on the stack. Larger strings are heap-allocated transparently. This eliminates heap allocations for the majority of common string sizes.

*   **Arena-Backed Allocation**: `fl::arena_allocator` provides a fast, stack-first memory allocation strategy for temporary data. `fl::arena_buffer` wraps this, using stack memory first and falling back to the heap only when necessary, minimising calls to global `malloc`/`free`.

*   **Thread-Local Free-List Pool Allocator**: The allocation layer in `alloc_hooks.hpp` maintains per-thread free lists across seven size classes (up to 4096 bytes). This avoids lock contention and reduces allocator overhead for the small allocations that dominate string workloads.

*   **Two-Way String Search Algorithm**: `fl::string::find()` uses the Two-Way algorithm with an optional AVX2-vectorised first-character scan on large haystacks, providing optimal worst-case and practical performance for substring searches.

*   **Zero-Allocation Formatting**: The formatting system uses sinks (`fl::sinks::buffer_sink`, `fl::sinks::growing_sink`, etc.) to write formatted data directly to a target buffer, file, or stream without creating intermediate string objects.

*   **Efficient Builders**: `fl::string_builder` uses move-only semantics and configurable growth policies (linear or exponential) to efficiently append string fragments. The `build()` method transfers ownership of the internal buffer to the resulting `fl::string` with minimal overhead.

*   **Dedicated Rope Node Slab Allocator**: Rope tree nodes are allocated from a dedicated slab allocator, reducing per-node allocation overhead and improving cache locality during tree traversal.

*   **Advanced String Types**:
    *   `fl::substring_view`: A non-owning view into a portion of an existing string. It uses `std::shared_ptr`-based ownership tracking to ensure the underlying data remains valid for the view's lifetime.
    *   `fl::rope`: A tree-based data structure for O(1) amortised concatenation, suited to workloads with frequent modifications where linearisation can be deferred.
    *   `fl::immutable_string` / `fl::owning_immutable_string`: Designed for use as keys in associative containers, with cached hashes and atomic reference counting for thread-safe sharing.
    *   `fl::synchronised_string`: An explicitly thread-safe mutable string providing internal synchronisation via `std::shared_mutex` for concurrent read/write access.

*   **Debug Thread-Safety Diagnostics**: When the `FL_DEBUG_THREAD_SAFETY` macro is enabled, every read, write, and move operation on a string instance is tracked through an atomic state machine. Concurrent access violations are detected at runtime and reported with full diagnostics before aborting. In release builds this compiles to a zero-overhead stub.

## Building and Testing

The `fl` library uses CMake as its build system.

### Compilation Requirements

*   **C++ Standard**: C++20 (required; set via `CMAKE_CXX_STANDARD 20`)
*   **Compilers**: GCC 10+, Clang 10+, MSVC 2019+ (all must support C++20)
*   **CMake**: 3.15 or later
*   **Dependencies**: None (header-only core). Optional third-party libraries (Abseil, Boost, Folly) are used only for comparative benchmarks.

### Standard Build Process

```bash
# Create a build directory
mkdir build && cd build

# Configure with CMake
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build the project (tests, benchmarks, examples)
cmake --build .
```

### Running Tests

The project includes four CTest targets.

```bash
# From the build directory
ctest --output-on-failure
```

The test targets are:

| Target                         | Description                                          |
| :----------------------------- | :--------------------------------------------------- |
| `rope_linear_access_vs_std`    | Rope linear character access compared to `std::string` |
| `fl_string_vs_std_full_test`   | Full `fl::string` operation tests against `std::string` |
| `test_adaptive_find`           | Adaptive substring search algorithm tests            |
| `test_rope_access_index`       | Rope index-based access correctness tests            |

The test suite covers:

*   `fl::string` operations and SSO transitions.
*   `fl::arena_allocator` behaviour, including stack-to-heap fallback.
*   `fl::string_builder` functionality and growth policies.
*   Formatting with various sinks and format specifiers.
*   `fl::substring_view` creation, access, and lifetime management.
*   `fl::rope` concatenation, flattening, and character access.
*   `fl::immutable_string` and `fl::synchronised_string` behaviour, including thread-safety aspects.
*   Edge cases and boundary conditions across all components.

### Benchmarking

Performance benchmarks are provided as separate executables. There is no single unified benchmark binary.

**Always-built benchmark targets:**

| Target                          | Description                                                  |
| :------------------------------ | :----------------------------------------------------------- |
| `string_vs_std_bench`           | Core `fl::string` vs `std::string` operation benchmarks      |
| `rope_vs_std_string_benchmarks` | Rope concatenation vs `std::string` benchmarks               |
| `comprehensive_bench`           | Comprehensive benchmark suite across all components          |
| `find_haystack_bench`           | Substring search throughput (256--4096 byte haystacks)       |
| `rope_rebalance_bench`          | Rope `rebalance()` cost isolation benchmark                  |
| `pmr_vs_pool_bench`             | `fl` pool allocator vs `std::pmr::monotonic_buffer_resource` |
| `aslr_construction_bench`       | ASLR / allocator warm-up construction investigation          |

**Conditionally-built benchmark targets:**

| Target               | Condition                           | Description                                 |
| :------------------- | :---------------------------------- | :------------------------------------------ |
| `cross_library_bench`| Abseil and Boost found              | Comparison with `absl::Cord` and Boost      |
| `folly_benchmark`    | Folly found                         | Comparison with Facebook Folly strings       |

To enable third-party benchmarks, configure with:

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DFL_BENCHMARK_THIRD_PARTY=ON ..
```

Run any individual benchmark from the build directory:

```bash
./string_vs_std_bench
./comprehensive_bench
```

## Code Quality

### Implementation Quality Checklist

*   **C++20 Compliant**: All code uses modern C++20 idioms and language features.
*   **Header-Only Core**: The core library is header-only with no external dependencies beyond the C++ standard library.
*   **Exception Safety**: Operations adhere to basic, strong, or no-throw guarantees as appropriate.
*   **Memory Management**: Emphasis on zero-allocation strategies, thread-local pooling, and efficient memory reuse.
*   **Thread Safety**: Explicitly defined for `fl::immutable_string` and `fl::synchronised_string`. `fl::string` itself is not thread-safe for mutable operations. Debug diagnostics are available via `FL_DEBUG_THREAD_SAFETY`.

### Comment Style

The codebase follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) for comments. Comments use `//` line-comment syntax. Doxygen-style comments (`///`, `/** */`) are not used.

### Testing Coverage

*   Every feature, including edge cases and combinations, should be covered by unit tests.
*   Benchmarks should regularly compare `fl` components against `std::string` to verify performance claims.
*   The test suite should prevent regressions as the library evolves.

## File Structure

```
Flstring/
├── include/              # Public headers
│   ├── fl.hpp            # Umbrella header
│   └── fl/               # Component headers
│       ├── string.hpp
│       ├── builder.hpp
│       ├── substring_view.hpp
│       ├── rope.hpp
│       ├── immutable_string.hpp
│       ├── synchronised_string.hpp
│       ├── synchronized_string.hpp  # US spelling alias
│       ├── arena.hpp
│       ├── format.hpp
│       ├── sinks.hpp
│       ├── alloc_hooks.hpp
│       ├── config.hpp
│       ├── profiling.hpp
│       └── debug/
│           └── thread_safety.hpp
├── tests/                # Test suite (4 CTest targets)
├── examples/             # Usage examples
├── benchmarks/           # Performance benchmarks (9 targets)
├── docs/                 # Documentation
├── .github/              # CI workflows and templates
└── CMakeLists.txt
```

## Contributing

Contributions are welcome. Please refer to [`CONTRIBUTING.md`](../CONTRIBUTING.md) at the repository root for detailed contribution guidelines.

In summary:

1.  **Read the documentation**: Review `README.md`, `API.md`, `Features.md`, `Examples.md`, and `Formatting.md` to understand the library's design.
2.  **Follow the comment style**: Use `//` line comments per the Google C++ Style Guide. Do not use `///` or Doxygen annotations.
3.  **Write tests**: New features and bug fixes must include corresponding unit tests and, where applicable, benchmark cases.
4.  **Update documentation**: Reflect any API or feature changes in the relevant `.md` files under `docs/`.
