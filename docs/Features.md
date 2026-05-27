# FL Library Features

This document provides a comprehensive reference for every public component in the `fl` string library. Each section describes the component's purpose, key implementation details, and usage patterns.

---

## Table of Contents

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
12. [C++ Standard Compatibility](#c-standard-compatibility)

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
| Sign | `+`, ` ` (space) | Force sign (`+`) or space (` `) for positive numbers; minus for negatives |
| Base prefix | `#` | Show `0x`, `0b`, `0` prefix |
| Width | Integer | Minimum field width |
| Precision | `.N` | Decimal precision for floats; truncation length for strings |
| Type | `d`, `x`, `X`, `b`, `B`, `o`, `f`, `e`, `E`, `g`, `G`, `s`, `c` | Output type specifier |

### Sink Types

All sinks inherit from `fl::sinks::output_sink` and implement `write(const char*, size_t)`.

| Sink | Description |
|---|---|
| `fl::sinks::buffer_sink` | Fixed-size caller-provided buffer. Throws `std::overflow_error` on overflow. |
| `fl::sinks::growing_sink` | Dynamically growing `std::vector<char>` buffer. Alias for `basic_growing_sink<>`. |
| `fl::sinks::basic_growing_sink<Alloc>` | Allocator-aware growing sink. Defaults to `std::allocator<char>`. |
| `fl::sinks::file_sink` | Writes to a C `FILE*` handle. Supports owned and borrowed handles. |
| `fl::sinks::stream_sink` | Writes to a `std::ostream` reference. |
| `fl::sinks::null_sink` | Discards all output. Useful for benchmarking formatting overhead. |
| `fl::sinks::multi_sink` | Fans out writes to multiple sinks simultaneously. |

Factory helpers: `fl::make_buffer_sink()`, `fl::make_file_sink()`, `fl::make_stream_sink()`, `fl::make_growing_sink()`, `fl::make_null_sink()`.

### Convenience Formatting Functions

Several convenience wrappers eliminate the boilerplate of sink creation for common tasks:

| Function | Description | Example |
|----------|-------------|---------|
| `fl::format(fmt, args...)` | Returns a formatted `std::string` directly | `fl::format("Hello, {}!", name)` |
| `fl::to_string(value)` | Single-value quick formatting | `fl::to_string(42)` → `"42"` |
| `fl::print(fmt, args...)` | Formats to `stdout` | `fl::print("x = {}", x)` |
| `fl::println(fmt, args...)` | Formats to `stdout` with trailing newline | `fl::println("done")` |
| `fl::println()` | Prints a bare newline | `fl::println()` |
| `fl::vformat(fmt, store)` | Formats from a runtime argument store | `fl::vformat("{} {}", store)` |
| `fl::vformat_to(sink, fmt, store)` | Formats to a sink from a runtime store | `fl::vformat_to(sink, "{}", store)` |

### `fl::format_to()` with `string_builder`

Full format spec support for `string_builder` is available via
`fl::format_to(builder, fmt, args...)`. This delegates through a
`builder_sink_adapter` to the format engine:

```cpp
fl::string_builder builder(128);
fl::format_to(builder, "Score: {:>8}", 42);
fl::string result = std::move(builder).build();
// result == "Score:       42"
```

> The old `append_formatted()` method is deprecated in favour of
> `fl::format_to(builder, ...)`.

### `fl::ostream_formatter`

A convenience base class for types that already provide
`operator<<(std::ostream&, T)`. Inherit from `ostream_formatter` in your
`formatter<T>` specialisation to make any streamable type formattable:

```cpp
struct Point { int x, y; };

template <>
struct fl::formatter<Point> : fl::ostream_formatter {};
```

Internally uses `detail::sink_streambuf` to wrap any sink as a `std::streambuf`.

### `fl::format_as()` ADL Adapter

Define `auto format_as(const MyType&) -> fl::formattable_type` in your type's
namespace. The library detects it automatically via `has_format_as_v<T>`:

```cpp
struct Degrees { double value; };
auto format_as(const Degrees& d) -> fl::formattable_type { return d.value; }
// Now Degrees is formattable without any formatter<T> specialisation
```

### Debug `?` Specifier

The `{:?}` specifier formats strings with escape sequences for debugging:
double-quoted, with `\n`, `\t`, `\r`, `\\`, `\"`, `\0`, and `\xNN` for
control bytes. Characters use single quotes.

```cpp
fl::format("{:?}", "hello\nworld");  // "\"hello\\nworld\""
fl::format("{:?}", '\n');            // "'\\n'"
```

### Compile-Time Format String Checking

`fl::detail::format_string<Args...>` wraps format strings with `consteval`
validation of balanced braces. New overloads for `format_to()`, `format()`,
`print()`, `println()` accept `format_string`. Pass a literal string for
compile-time checking; runtime strings use the unvalidated fallback.

### `vformat()` and `dynamic_format_arg_store`

Build argument lists at runtime with `fl::dynamic_format_arg_store`:

```cpp
fl::dynamic_format_arg_store store;
store.push_back(42);
store.push_back(3.14);
store.push_back("hello");
std::string s = fl::vformat("{} {} {}", store);  // "42 3.14 hello"
```

`fl::vformat_to(sink, fmt, store)` writes to any sink. Type erasure is
handled by `format_arg_base` / `format_arg_impl<T>`.

### Compiled Format Strings

`fl::detail::compile<MaxSegments>(fmt)` produces a `compiled_format<N>` with
pre-parsed segments. `fl::format_to(sink, cfmt, args...)` dispatches without
runtime parsing overhead:

```cpp
constexpr auto cfmt = fl::detail::compile<8>("Value: {:>10}");
fl::format_to(sink, cfmt, 42);  // no parsing at runtime
```

### Custom Allocator Support

`basic_growing_sink<Alloc>` is an allocator-aware version of `growing_sink`.
Defaults to `std::allocator<char>`:

```cpp
using fl::sinks::growing_sink = fl::sinks::basic_growing_sink<>;  // default allocator
fl::sinks::basic_growing_sink<MyAllocator<char>> custom_sink(256);
```

Internally, the formatter array uses `format_fn<Sink>` — a 32-byte small-buffer
optimisation replacing `std::function`, eliminating heap allocation for small
callables.

### `__int128` Support

On GCC and Clang (detected via `__SIZEOF_INT128__`), `__int128` and `unsigned __int128` are supported in all formatting paths: `format_value()`, `format_argument()`, and spec-based formatting via `format_int_with_spec()`. Not available on MSVC.

### Float Formatting Performance

Float formatting uses `std::to_chars` (Dragonbox/Ryū internally on modern toolchains), replacing the previous `snprintf`-based engine. This delivers:

- **5–10× faster** float-to-string conversion
- **Locale-independent output** — no `LC_NUMERIC` surprises
- **Shortest-round-trip** by default — produces the shortest string that round-trips to the same value
- **Correct `float` precision** — `3.14f` produces `"3.14"`, not `"3.140000104904175"`
- All format specifiers supported: `e`/`E`, `f`/`F`, `g`/`G`, and default shortest format
- Proper handling of `NaN`, `inf`, and `-0.0`

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

### Custom Type Extensibility (`fl::formatter<T>`)

Users can make their own types formattable by specialising `fl::formatter<T>`. The primary template is empty; the library uses SFINAE on the presence of a `parse()` method to detect specialisations. When detected, `format_argument()` dispatches to the custom formatter.

```cpp
struct Point { int x, y; };

template <>
struct fl::formatter<Point> {
    constexpr std::size_t parse(std::string_view) noexcept { return 0; }
    template <typename Sink>
    void format(Sink& sink, const Point& p, const fl::detail::format_spec* spec);
};
```

See the [Formatting documentation](./Formatting.md#custom-type-extensibility) for a complete example.

### Container and Range Formatting

All iterable containers are formattable with `{}`:
- Plain containers format as `{elem1, elem2, elem3}`
- Map-like containers format as `{key1: val1, key2: val2}`
- Strings and `string_view` are excluded from range formatting

```cpp
std::vector<int> vec = {1, 2, 3};
fl::format("{}", vec);           // "{1, 2, 3}"
```

### `fl::join()`

`fl::join(range, sep)` and `fl::join(first, last, sep)` format a range with a separator between elements. Returns a `join_view` that integrates with the formatting pipeline.

```cpp
fl::format("{}", fl::join(vec, ", "));   // "1, 2, 3"
fl::format("{}", fl::join(arr, "|"));    // "x|y|z"
```

### Dynamic Width and Precision

Width and precision can be taken from arguments at runtime:

| Pattern | Meaning |
|---------|---------|
| `{:{}}` | Width from next argument |
| `{:.{}}f` | Precision from next argument |
| `{:{}.{}}` | Both width and precision from arguments |

Negative width is treated as zero; negative precision is treated as unset.

### Wide String Wrappers

`fl::wformat(fmt, args...)` returns `std::wstring` (formats narrow, then widens). `fl::to_wstring(value)` returns `std::wstring` for a single value.

```cpp
std::wstring ws = fl::wformat("Value: {}", 42);  // L"Value: 42"
std::wstring ws2 = fl::to_wstring(3.14);          // L"3.14"
```

### Chrono Formatting (Optional Header)

**Header:** `#include "fl/chrono_format.hpp"`

| Type | Default Format | Example |
|------|---------------|---------|
| `std::chrono::duration<Rep, Period>` | Value + SI unit suffix | `5s`, `1500ms`, `2.5min` |
| `std::chrono::time_point<Clock, Duration>` | ISO 8601 via strftime | `2026-05-23 19:04:30` |

### Terminal Colour and Style (Optional Header)

**Header:** `#include "fl/color.hpp"`

The `text_style` struct describes terminal text appearance (fg/bg colours and attributes). The `fl::ansi` namespace provides 16 named colour constants. Combinator functions (`fg()`, `bg()`, `bold()`, `italic()`, `underline()`, `dim()`, `blink()`, `reverse()`, `strikethrough()`) compose via `operator|`. Use `fl::styled(value, style)` to wrap any value with ANSI SGR codes:

```cpp
fl::print("{}", fl::styled("Error", fl::fg(fl::ansi::red) | fl::bold));
```

### Format String Updates

The format specifier table now includes dynamic values:

| Component | Syntax | Description |
|-----------|--------|-------------|
| Dynamic width | `{:{}}` | Width taken from next integer argument |
| Dynamic precision | `{:.{}}` | Precision taken from next integer argument |

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
fl::version();       // Returns "2.0.0"
fl::MAJOR_VERSION;   // 2
fl::MINOR_VERSION;   // 0
fl::PATCH_VERSION;   // 0
```

For detailed API signatures, see the [API Reference](./API.md).

## C++ Standard Compatibility

The `fl` library targets C++20 and does not support compilation under earlier C++ standards. For users working in C++11, C++14, or C++17 environments, the library provides `std::string` conversion operators (`operator std::string()` and `to_std_string()`) for passing fl strings into standard-library APIs.

### Deprecations

- **`fl::string::reserve()`** with no arguments is deprecated. Use `shrink_to_fit()` instead. This change aligns the API with C++11/14/17 conventions where `reserve()` takes a capacity argument and `shrink_to_fit()` releases excess capacity.
