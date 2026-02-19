# FL Library Features

This document provides a comprehensive reference for every public component in the `fl` library. Each section describes the component's purpose, key implementation details, and usage patterns.

---

## Table of Contents

1. [fl::string -- Core String Class](#flstring----core-string-class)
2. [fl::string_builder -- Fluent String Builder](#flstring_builder----fluent-string-builder)
3. [fl::substring_view -- Non-Owning String View](#flsubstring_view----non-owning-string-view)
4. [fl::rope -- Balanced Concatenation Tree](#flrope----balanced-concatenation-tree)
5. [fl::immutable_string / fl::immutable_string_view -- Thread-Safe Immutable Strings](#flimmutable_string--flimmutable_string_view----thread-safe-immutable-strings)
6. [fl::synchronised_string -- Mutex-Guarded Mutable String](#flsynchronised_string----mutex-guarded-mutable-string)
7. [Arena Utilities](#arena-utilities)
8. [Formatting and Sinks](#formatting-and-sinks)
9. [Allocator Infrastructure](#allocator-infrastructure)
10. [Debug Utilities](#debug-utilities)
11. [Profiling](#profiling)

---

## fl::string -- Core String Class

**Header:** `fl/string.hpp`

The primary string type in the library. It provides a `std::string`-compatible API with performance-oriented internals.

### Small-String Optimization (SSO)

Strings of up to 23 bytes (`SSO_CAPACITY = 23`) are stored inline within the object itself, avoiding heap allocation entirely. The threshold for heap allocation is `SSO_THRESHOLD = 24`. The internal storage is a union of a 24-byte SSO buffer and a heap pointer/capacity pair.

### Pool-Backed Heap Allocation

Strings exceeding the SSO buffer are heap-allocated through a thread-local free-list pool (see [Allocator Infrastructure](#allocator-infrastructure)). The pool rounds allocations up to the next size class, and the resulting usable capacity is stored so that subsequent appends within the same class avoid reallocation.

### SIMD-Accelerated Search

The `find()` family dispatches to different algorithms depending on needle and haystack size:

| Condition | Algorithm |
|---|---|
| Single character | SSE2 `_mm_cmpeq_epi8` scan, fallback to `memchr` |
| Needle <= 4 bytes | SIMD first-character scan + short suffix verification |
| Haystack >= 2048 bytes, needle >= 16 bytes | Boyer-Moore-Horspool with full 256-entry shift table |
| Haystack >= 64 KB, needle >= 2 bytes | Two-Way (Crochemore-Rytter) with optional AVX2 pre-scan |
| All other cases | `std::string_view::find` (glibc `memmem`) |

The Two-Way algorithm (`detail::two_way::search`) runs in O(n + m) time and O(1) space. On AVX2-capable hardware, it uses 32-byte vector scans to skip blocks where the critical-factorization pivot character is absent, accelerating both the periodic and non-periodic search paths.

### User-Defined Literal

```cpp
#include <fl.hpp>

auto greeting = "hello, world"_fs;  // fl::string via operator""_fs
```

### Additional Facilities

- **`fl::lazy_concat`** / **`fl::basic_lazy_concat<Allocator>`**: Deferred multi-part concatenation that accumulates `std::string_view` references and materializes them into a single `fl::string` in one allocation.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::string s1 = "A small string";         // SSO, no heap allocation.
    fl::string s2(100, 'x');                   // Heap-allocated via TLS pool.

    std::cout << "SSO capacity: " << fl::SSO_CAPACITY << " bytes\n";

    // Substring search dispatches to SIMD or Two-Way depending on size.
    auto pos = s2.find("xx");

    // Zero-copy views.
    auto view = s1.substr_view(2, 5);         // fl::substring_view
    auto left = s1.left_view(5);
    auto right = s1.right_view(6);

    return 0;
}
```

---

## fl::string_builder -- Fluent String Builder

**Header:** `fl/builder.hpp`

A move-only builder that accumulates characters into a contiguous buffer and produces an `fl::string` via an rvalue-qualified `build()` method.

### Key Properties

- **Non-copyable**: The builder owns a raw buffer and disallows copies.
- **Configurable growth**: Supports `fl::growth_policy::linear` (fixed increment, default 32 bytes) and `fl::growth_policy::exponential` (2x below 256 bytes, 1.5x above).
- **Zero-allocation ownership transfer**: When the accumulated content exceeds the SSO threshold, `build()` transfers the heap buffer directly into the returned `fl::string` without copying.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::string_builder builder(128);
    builder.set_growth_policy(fl::growth_policy::exponential);

    builder.append("Name: ").append("Alice").append('\n');
    builder.append("Score: ").append_formatted("{}", 42);

    fl::string result = std::move(builder).build();
    std::cout << result << std::endl;

    return 0;
}
```

---

## fl::substring_view -- Non-Owning String View

**Header:** `fl/substring_view.hpp`

A lightweight, non-owning view over a character range, backed by a `std::string_view` internally.

### Lifetime Management

- When constructed from a raw `const char*`, no ownership is tracked; the caller must ensure the data outlives the view.
- When constructed from a `std::string`, the view copies the string into `std::shared_ptr`-managed storage so it can outlive the original.
- When constructed from an `fl::string` (via `substr_view()`, `slice()`, `left_view()`, `right_view()`, or `find_view()`), the view does **not** extend the string's lifetime. The caller is responsible for keeping the `fl::string` alive.

### Zero-Copy Slicing

`fl::string` provides five methods that return `fl::substring_view` without allocation:

| Method | Description |
|---|---|
| `substr_view(pos, len)` | General substring view |
| `slice(pos, len)` | Alias for `substr_view` |
| `left_view(count)` | First `count` characters |
| `right_view(count)` | Last `count` characters |
| `find_view(needle, pos)` | View of the matched substring, or empty if not found |

### Hash and Equality Functors

`fl::substring_view_hash` (FNV-1a) and `fl::substring_view_equal` are provided for use in associative containers such as `std::unordered_map`.

---

## fl::rope -- Balanced Concatenation Tree

**Header:** `fl/rope.hpp`

An AVL-balanced binary concatenation tree that provides amortized O(1) concatenation by composing tree nodes rather than copying data.

### Performance Characteristics

| Operation | Complexity |
|---|---|
| Concatenation (`+`, `+=`) | O(1) amortized |
| Flatten to `fl::string` | O(n) |
| Character access (`operator[]`) | O(log n), amortized O(1) with access index for ropes >= 4096 bytes |
| Substring extraction | O(n) for the extracted range |

### Dedicated TLS Slab Allocator

All `shared_ptr<leaf_node>` and `shared_ptr<concat_node>` allocations go through `basic_rope_node_alloc<T>`, a two-class slab allocator stored in thread-local storage. It maintains 32 slots for allocations <= 64 bytes (leaf nodes) and 32 slots for allocations <= 128 bytes (concat nodes). This bypasses the general pool's 7-comparison class lookup, providing better hit rates during bulk concatenation sequences. Total TLS footprint: 520 bytes (9 cache lines).

### Rebalancing

- `_balanced_concat()` maintains an AVL invariant (height difference <= 1) via single and double rotations on every concatenation, keeping tree depth at O(log n) without explicit rebalancing.
- `rebalance()` flattens the tree only when depth exceeds `kRebalanceDepthThreshold = 64`, which is effectively a no-op for trees built through `operator+=`.
- `flatten_if_deep(threshold)` conditionally flattens the tree to a single contiguous leaf, intended for C-API interoperability where a `const char*` pointer is required.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::rope r1("hello");
    fl::rope r2(" world");
    fl::rope combined = r1 + r2;       // O(1) concatenation.

    fl::string flat = combined.flatten();  // O(n) linearization.
    std::cout << flat << std::endl;

    // Conditional flatten for C-API use.
    combined.flatten_if_deep(32);

    return 0;
}
```

---

## fl::immutable_string / fl::immutable_string_view -- Thread-Safe Immutable Strings

**Header:** `fl/immutable_string.hpp`

### fl::immutable_string

An immutable string with atomic reference counting for thread-safe O(1) copies. The control block is cache-line-aligned (`alignas(64)`) to avoid false sharing. No mutation operations are exposed; immutability is enforced at compile time.

- **Copy**: O(1), atomic `fetch_add` with `memory_order_relaxed`.
- **Destruction**: O(1) atomic decrement; the last owner deallocates with an acquire fence to ensure visibility of all prior writes.
- **Hash**: Lazily computed FNV-1a hash, cached in the control block. Thread-safe via `memory_order_acquire`/`memory_order_release` on `hash_computed`.

### fl::immutable_string_view

A lightweight, non-owning view with a lazily computed FNV-1a hash. Suitable for use as map keys. The hash is memoized after the first call to `hash()`.

### Functors

`fl::immutable_string_hash` and `fl::immutable_string_equal` (with `is_transparent` tag) are provided for use in `std::unordered_map` and similar containers.

```cpp
#include <fl.hpp>
#include <unordered_map>

int main() {
    fl::immutable_string key("config_key");
    fl::immutable_string copy = key;    // O(1) atomic ref-count increment.

    std::unordered_map<fl::immutable_string,
                       int,
                       fl::immutable_string_hash,
                       fl::immutable_string_equal> table;
    table[key] = 42;

    return 0;
}
```

---

## fl::synchronised_string -- Mutex-Guarded Mutable String

**Header:** `fl/synchronised_string.hpp`

A thread-safe mutable string wrapper backed by `std::shared_mutex`. Concurrent reads are permitted (shared lock); writes acquire an exclusive lock.

### Callback-Based Access

All access to the underlying `fl::string` is mediated through `read()` and `write()` callbacks. This design prevents raw reference leaks that could bypass the mutex.

- `read(Func&&)`: Acquires a shared lock and invokes the callback with a `const fl::string&`.
- `write(Func&&)`: Acquires an exclusive lock and invokes the callback with an `fl::string&`.

Both methods are concept-constrained (`std::invocable`) and conditionally `noexcept`.

### Spelling Alias

The US spelling alias `fl::synchronized_string` is defined as a `using` declaration that maps to `fl::synchronised_string`.

```cpp
#include <fl.hpp>
#include <thread>

int main() {
    fl::synchronised_string shared_str("initial");

    std::thread writer([&] {
        shared_str.write([](fl::string& s) {
            s += " appended";
        });
    });

    std::thread reader([&] {
        shared_str.read([](const fl::string& s) {
            // Safe concurrent read.
        });
    });

    writer.join();
    reader.join();

    fl::string snapshot = shared_str.snapshot();
    return 0;
}
```

---

## Arena Utilities

**Header:** `fl/arena.hpp`

### fl::arena_allocator\<StackSize\>

A bump-pointer allocator that serves allocations from a fixed-size stack-local buffer (default 4096 bytes) and falls back to the heap for requests that do not fit. All allocations are 8-byte aligned. Non-copyable and non-movable.

### fl::arena_buffer\<StackSize\>

An append-only character buffer backed by an `arena_allocator`. For typical sizes, all memory comes from the arena's stack region, avoiding the global heap entirely. Provides `append()`, `clear()`, `reset()`, and `to_string()`.

### fl::temp_buffer / get_pooled_temp_buffer()

`fl::temp_buffer` is a `std::unique_ptr<arena_buffer<4096>>` with a custom deleter that returns the buffer to a thread-local pool (capacity 8) instead of destroying it. `get_pooled_temp_buffer()` retrieves a buffer from the pool or allocates a new one.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    // Stack-backed arena buffer (4 KB).
    fl::arena_buffer<4096> arena;
    arena.append("Building ");
    arena.append("a string ");
    arena.append("from pieces.");
    fl::string result = arena.to_string();
    std::cout << result << std::endl;

    // Pooled temporary buffer (recycled across calls).
    auto tmp = fl::get_pooled_temp_buffer();
    tmp->append("temporary data");
    fl::string tmp_result = tmp->to_string();
    // Buffer is returned to TLS pool when tmp goes out of scope.

    return 0;
}
```

---

## Formatting and Sinks

**Headers:** `fl/format.hpp`, `fl/sinks.hpp`

### fl::format_to()

A type-safe formatting function that writes output directly into a sink using `{}`-based placeholder syntax. Supports positional formatting and rich format specifiers.

### Format Specifiers

The format specification syntax inside `{:...}` supports:

| Component | Syntax | Description |
|---|---|---|
| Fill | Any character | Padding character (default: space) |
| Alignment | `<`, `>`, `^`, `=` | Left, right, center, numeric padding |
| Sign | `+` | Force sign display for positive numbers |
| Base prefix | `#` | Show `0x`, `0b`, `0` prefix |
| Width | Integer | Minimum field width |
| Precision | `.N` | Decimal precision for floats; truncation length for strings |
| Type | `d`, `x`, `X`, `b`, `B`, `o`, `f`, `e`, `E`, `g`, `G`, `s`, `c` | Output type specifier |

### Sink Types

All sinks inherit from `fl::sinks::output_sink` and implement `write(const char*, size_t)`.

| Sink | Description |
|---|---|
| `fl::sinks::buffer_sink` | Fixed-size caller-provided buffer. Throws `std::overflow_error` on overflow. |
| `fl::sinks::growing_sink` | Dynamically growing `std::vector<char>` buffer. |
| `fl::sinks::file_sink` | Writes to a C `FILE*` handle. Supports owned and borrowed handles. |
| `fl::sinks::stream_sink` | Writes to a `std::ostream` reference. |
| `fl::sinks::null_sink` | Discards all output. Useful for benchmarking formatting overhead. |
| `fl::sinks::multi_sink` | Fans out writes to multiple sinks simultaneously. |

Factory helpers: `fl::make_buffer_sink()`, `fl::make_file_sink()`, `fl::make_stream_sink()`, `fl::make_growing_sink()`, `fl::make_null_sink()`.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    char buffer[256];
    auto sink = fl::make_buffer_sink(buffer);

    int id = 42;
    fl::format_to(sink, "User {:0>6d} -- {:*^20s}", id, "Alice");
    sink.null_terminate();

    std::cout << buffer << std::endl;
    // Output: User 000042 -- ********Alice********

    return 0;
}
```

---

## Allocator Infrastructure

**Header:** `fl/alloc_hooks.hpp`

### Thread-Local Free-List Pool

The pool manages 7 size classes: **64, 128, 256, 512, 1024, 2048, 4096** bytes. Each class holds up to 8 recycled blocks (`POOL_SLAB_DEPTH = 8`).

The per-thread structure (`TlsFreeLists`) uses a hot/cold cache-line layout:

| Region | Offset | Content |
|---|---|---|
| Hot (cache line 0) | 0 -- 63 | 7 count bytes + padding |
| Cold (cache lines 1--7) | 64 -- 511 | 7 x 8 slot pointers |

Total footprint: **512 bytes = 8 cache lines**. On every allocation, only the hot cache line is loaded to check the count. The cold slots cache line is fetched only when a block is actually retrieved or returned.

### Allocation Path

1. Compute the pool class index for the requested size.
2. If the TLS slab for that class has a cached block, return it (pool hit).
3. Otherwise, allocate a full class-sized block from the system allocator (pool miss).
4. On deallocation, return the block to the TLS slab if it has capacity; otherwise free it to the system (eviction).

When `align <= alignof(std::max_align_t)` (16 bytes on x86-64), allocations use `std::malloc` directly, bypassing `aligned_alloc`/`posix_memalign` and remaining compatible with glibc's tcache/fastbin bins.

### fl::pool_alloc\<T\>

A C++ standard-conforming allocator adapter that routes through the TLS pool. Suitable for use with `std::allocate_shared`, `std::vector`, and other allocator-aware containers.

### Pluggable Hooks

Custom allocators can be installed via `fl::set_alloc_hooks(allocate_fn, deallocate_fn, ...)`. When no custom hooks are installed, the default pool path is taken.

### FL_HOOKS_ALWAYS_DEFAULT

Define this macro to hard-wire the default allocation path at compile time, eliminating the atomic load on `hooks_customised()` in every allocation. This allows the compiler to inline and dead-code-eliminate `fl::string` construction at `-O2`, matching the optimization level `std::string` receives for constant literals. Custom hooks installed via `set_alloc_hooks()` have no effect when this macro is defined.

### Pool Instrumentation

In debug builds (`NDEBUG` not defined), atomic counters track pool hits, misses, pushes, and evictions per class. A snapshot is available via `fl::alloc_hooks::get_pool_stats()`.

---

## Debug Utilities

**Header:** `fl/debug/thread_safety.hpp`, `fl/config.hpp`

### FL_DEBUG_THREAD_SAFETY

Set to `1` (via `-DFL_DEBUG_THREAD_SAFETY=1`) to enable runtime concurrent-access detection on `fl::string`, `fl::immutable_string`, and `fl::synchronised_string`. In release builds (default `0`), the tracker compiles to a zero-overhead stub.

### thread_access_tracker

An atomic state machine embedded in each tracked object. The state is a packed `uint32_t`:

| Bits | Content |
|---|---|
| 0 -- 7 | `AccessType` (None=0, Read=1, Write=2, Moved=4) |
| 8 -- 31 | Active thread count (up to 16 million) |

- **Read access**: Increments the thread count. Concurrent reads are permitted; a read during a write or moved state triggers a violation.
- **Write access**: Requires exclusive ownership (state must be zero).
- **Move**: Permanently marks the object as moved-from; subsequent access aborts.

### RAII AccessGuard

`begin_read()` and `begin_write()` return an `AccessGuard` that automatically releases the caller's slot on destruction. Moves transfer ownership; copies are prohibited.

### Diagnostic History

When `FL_DEBUG_THREAD_SAFETY_HISTORY > 0` (default 32), a bounded ring buffer of `AccessRecord` entries retains recent access events per object. On a violation, the history is printed to `stderr` before aborting via `FL_THREAD_SAFETY_ABORT()` (default: `std::abort()`).

---

## Profiling

**Header:** `fl/profiling.hpp`

### fl::profiler

An optional scoped profiler enabled by defining `FL_ENABLE_PROFILING` before including any library header. When enabled, constructing an `fl::profiler` records the current time; the destructor computes the elapsed duration and logs it to `std::clog` in microseconds.

When `FL_ENABLE_PROFILING` is not defined, the profiler is a constexpr no-op with zero runtime cost.

```cpp
#define FL_ENABLE_PROFILING
#include <fl.hpp>

void expensive_operation() {
    fl::profiler p("expensive_operation");
    // ... work ...
}  // Logs: [fl::profiler] expensive_operation took 1234 us
```

---

## Umbrella Header

Including `<fl.hpp>` pulls in every public component listed above. The library version is available via:

```cpp
fl::version();       // Returns "1.0.0"
fl::MAJOR_VERSION;   // 1
fl::MINOR_VERSION;   // 0
fl::PATCH_VERSION;   // 0
```

For detailed API signatures, see the [API Reference](./API.md).
