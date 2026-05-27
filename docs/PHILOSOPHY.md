# The Philosophy of fl: Forever Lightweight

## What is fl?

The letters **fl** stand for **Forever Lightweight**. This is not merely the name of a single library — it is a commitment that every library bearing the `fl` prefix must uphold. Whether the library provides strings, memory allocation, formatting, or any other utility, it should be exceptionally efficient, impose minimal overhead, and be designed for longevity and adaptability across C++ projects.

The reference implementation of this philosophy is the `fl::string` library, along with its companion components (`fl::rope`, `fl::string_builder`, `fl::arena_allocator`, `fl::format`). These serve as the practical embodiment of what "Forever Lightweight" means in real code.

## Motivation

The genesis of fl grew from a common challenge in C++ development: the need for
high-performance, low-resource string and memory management without sacrificing
developer ergonomics. Existing solutions often presented trade-offs:

- **Standard library overhead:** While powerful, `std::string` introduces performance
  bottlenecks through mandatory heap allocation for strings beyond its SSO threshold,
  and lacks control over the allocation strategy.
- **External dependencies:** Relying on large external libraries (Folly, Abseil) can
  lead to dependency bloat, increased build times, and versioning conflicts.
- **Lack of control:** Developers needing fine-grained control over memory allocation,
  string representation, and concurrency primitives often find the standard library
  insufficient.

fl emerged as a response to these needs: a lean, self-contained, header-only C++20
toolkit that integrates into projects without imposing external dependencies. Any
library that wishes to carry the `fl` prefix should be judged against the same
standards: it must solve its problem with minimal overhead, no compulsory
dependencies, and a clear performance justification for every feature it adds.

## Design Principles

### 1. Minimalism and zero-overhead abstractions

Every feature in fl is evaluated for its cost-to-value ratio. Abstractions exist to
enable performance, not to add indirection:

- 23-byte SSO eliminates heap allocation for the majority of real-world strings.
- Thread-local free-list pool serves heap allocations without global locks.
- Arena allocators provide stack-first bump allocation for temporary buffers.

### 2. Performance first

Optimized algorithms and data structures are at the heart of fl. Benchmarking and
profiling are integral to the development process:

- Two-Way (Crochemore-Rytter) string search with AVX2 pre-scan for large haystacks.
- AVL-balanced rope with dedicated slab allocator for node allocation.
- Cache-conscious TLS pool layout (512 bytes across 8 cache lines).

### 3. Resource efficiency

Designed for environments where memory and CPU cycles matter: server workloads,
low-latency services, and applications with high string throughput.

- Pool allocator over-allocates to pool class boundaries, exposing full block capacity.
- Arena buffers serve temporary allocations from the stack with automatic heap fallback.
- Move-only string builder transfers ownership to `fl::string` without copying.

### 4. Thread safety as an explicit choice

Thread safety is never hidden behind global locks. Instead, fl offers a spectrum of
concurrency models:

- `fl::string` — Single-owner, not thread-safe. Maximum performance.
- `fl::synchronised_string` — `std::shared_mutex`-guarded mutable string with
  callback-based access to prevent reference leakage.
- `fl::immutable_string` — Atomic reference-counted immutable string. O(1) lock-free
  copies across threads.
- `FL_DEBUG_THREAD_SAFETY` — Debug-mode runtime detection of concurrent access
  violations on `fl::string`.

### 5. Predictability

fl strives for predictable behavior and performance characteristics:

- No hidden global state beyond thread-local pool caches.
- Deterministic allocation patterns (pool class sizes are fixed at compile time).
- Rope rebalance threshold (64) prevents unexpected O(n) flattening at small tree depths.

### 6. Simplicity and ease of use

Despite its performance focus, fl provides an intuitive API:

- `fl::string` is a drop-in replacement for `std::string` with compatible member
  functions, iterators, and operators.
- `fl::format_to()` uses familiar `{}`-based placeholder syntax.
- `fl::string_builder` provides fluent chaining: `sb.append("a").append("b")`.

### 7. Extensibility

fl is designed with an open architecture:

- Custom allocation hooks via `fl::alloc_hooks::install_hooks()`.
- Six output sink types for the formatting API, plus a `multi_sink` fan-out.
- `fl::pool_alloc<T>` standard allocator adapter for use with `std::allocate_shared`
  and other allocator-aware containers.

## Summary

fl is a commitment to building software that performs optimally and respects the
computational resources it consumes. Every design decision prioritises measurable
performance over theoretical elegance, while maintaining an API that C++ developers
find familiar and productive. This commitment extends beyond the string library: any
component that carries the `fl` prefix must meet the same bar for efficiency,
simplicity, and longevity.
