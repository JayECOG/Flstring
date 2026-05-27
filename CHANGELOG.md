# Changelog

All notable changes to the `fl` string library are recorded here. The project follows a Keep a Changelog-style format so that releases and work-in-progress items stay transparent to maintainers and downstream consumers.

## [2.0.0] - 2026-05-23

### Added
- Comprehensive memory safety fixes across all core components (arena allocator, string, format subsystem, immutable string, substring view, synchronised string, rope, alloc hooks).
- `fl::string` implicit and explicit conversion to `std::string` for interoperability with C++11/14/17 projects.
- `fl::string_builder` enhanced growth policy (linear/exponential).
- `fl::substring_view` full non-owning view API with optional shared ownership.
- `fl::synchronised_string` full comparison operator set (`==`, `!=`, `<`, `<=`, `>`, `>=`).
- `fl::arena_buffer::reserve()` for pre-allocation control.
- `fl::lazy_concat` deferred multi-part concatenation.
- Canonical benchmark baseline (`docs/fl_2_0_0_canonical_baseline.txt`).
- Third-party benchmark supplement (`docs/fl_2_0_0_third_party_baseline_supplement.txt`).
- Shared library build support (`FL_BUILD_SHARED` CMake option).
- Automatic dependency fetching (`FL_FETCH_DEPS` CMake option).
- Architecture-gated SIMD flags(SSE4.1, SSE4.2, AVX2 restricted to x86).
- Version macros and compiler detection in `include/fl/config.hpp`.
- Comprehensive test targets: test_arena, test_cpp_versions, test_format, test_format_comprehensive, test_format_stress, test_synchronised_string.
- Benchmark targets: cross_library_bench, multi_standard_bench, comprehensive_bench.
- CMAKE_SKIP_INSTALL_RULES option for downstream package management.

### Changed
- Minimum CMake version raised to 3.15.
- Rope: recursive `copy_to` replaced with iterative stack-based traversal (eliminates stack overflow risk).
- Rope: `compare()`, `operator==`, `operator<=>` changed from linearise-then-compare to tree-walking (avoids O(n) heap allocation).
- Rope: pre-reserved capacity and rvalue overloads for concatenation.
- String: lazy concatenation storage changed from `std::vector<string_view>` to `shared_ptr<string>` (fixes dangling-pointer bug on vector reallocation).
- Format subsystem: 14 critical and high-severity issues fixed (negative `snprintf` return, buffer overflow, unsigned formatting, bool/char formatting inconsistencies).
- Synchronised string: move constructor uses `scoped_lock` instead of exclusive write lock.
- Synchronised string: `compare()` uses known-length `string_view(data(), size())` instead of `string_view(data())` (avoids `strlen`).
- Synchronised string: removed C++17 fallback paths (requires C++20).
- Immutable string: `empty()` now checks both `_ctrl == nullptr` and `_ctrl->size == 0`.
- Immutable string: control block allocated via `allocate_bytes_aligned(..., 64)` for proper alignment.
- Alloc hooks: `pool_class_index` rewritten with branchless bit-scan intrinsics.
- Alloc hooks: dead includes removed.
- Arena: cross-thread buffer corruption protection, integer overflow guards, null-pointer guards, growth-loop overflow protection.
- Substring view: `find(const char*)` returns `npos` for null input (was returning offset).
- All C++ standard-detection macros below C++20 removed (`FL_HAS_CPP11`, `FL_HAS_CPP14`, `FL_HAS_CPP17`), along with their associated conditional code paths, fallback classes, and preprocessor guards. C++20 is now the hard baseline.
- Custom `fl::span<T>` fallback class removed from rope.hpp — replaced with unconditional `using span = std::span<T>`.
- `compat/string_view.hpp` stripped of 200-line polyfill class; now just includes `<string_view>`.
- All source headers converted to Doxygen-style documentation (`///`, `/** */`) with improved explanations and consistent British English. Section separators standardised to `// --` style. AI-sounding generic phrasing removed.

### Fixed
- Self-aliasing undefined behaviour in `string::append()`, `assign()`, `insert()`, `replace()`.
- Size arithmetic overflow in string growth paths.
- Capacity overflow in `resize()` and related methods.
- `snprintf` negative return → `SIZE_MAX` buffer over-read in format subsystem.
- Inconsistent overflow clamping between `format_value` and `format_float_with_spec`.
- `fmt_buffer[16]` overflow for large precision values in float formatting.
- Unsigned values above `INT64_MAX` formatted as negative.
- Bool formatted as `"1"/"0"` in single-arg `format_to` (now `"true"/"false"`).
- Char formatted as ASCII integer in single-arg `format_to`.
- Undefined `fl::detail::growing_sink::to_fl_string()` (declared but not defined).
- Zero-fill + `>` alignment sign position (deviated from `std::format` spec).
- Redundant `atomic_thread_fence(acquire)` in `immutable_string` destructor and `operator=` overloads.

### Removed
- `FL_BUILD_COMPAT_CHECKS` CMake option (C++11/14/17 compile-compatibility targets — the approach was abandoned in favour of `std::string` interop APIs).
- `FL_CXX_STANDARD` CMake option (C++20 is fixed; no other standards are supported for compilation).
- Dead `operator+=(const std::string&)` overload in synchronised_string.
- C++17 fallback paths from synchronised_string.
- Unused includes (`<span>`, `<functional>`, `<concepts>`, `<unordered_map>`, `<vector>`, `<mutex>`, `<fstream>`).
- Dead `thread_local g_current_thread_id` in arena.

### Deprecated
- `fl::string::reserve()` — use `shrink_to_fit()` instead (C++11/14/17 compatibility).

## [2.0.0] - 2026-05-24

### Added
- **`fl::format(fmt, args...)` → `std::string`**: Convenience wrapper that formats directly into a string without manual sink creation. Both `std::string_view` and `const char*` overloads.
- **`fl::print(fmt, args...)` and `fl::println(fmt, args...)`**: Format to stdout. `println()` with no arguments prints a bare newline.
- **`fl::to_string(value)` → `std::string`**: Single-value formatting convenience. Delegates to `format("{}", value)`.
- **Dragonbox/Ryū float formatting engine**: The `snprintf`-based float formatter has been replaced with `std::to_chars` (uses Dragonbox/Ryū internally on modern toolchains). Delivers 5–10× faster float formatting, locale-independent output, and shortest-round-trip representation. Three CRITICAL audit bugs caused by `snprintf` are structurally eliminated. See `include/fl/detail/float_format.hpp`.
- **Space sign support**: `{:\ }` syntax — space for positive values, minus for negatives. Works with both integer and floating-point types.
- **`__int128` and `unsigned __int128` support**: Guarded by `#ifdef __SIZEOF_INT128__`, available on GCC/Clang. Not available on MSVC.
- **`fl::formatter<T>` custom type extensibility**: Users can specialise `fl::formatter<T>` for their own types to make them formattable through all formatting APIs. Leverages SFINAE-based detection for seamless integration with built-in type dispatch.
- **Range/container formatting**: All iterable containers (vector, set, map, list, etc.) are formattable out of the box with `{}`. Maps use `{key: val}` syntax. Strings and string views are excluded from range formatting to avoid ambiguity.
- **`fl::join(range, sep)` and `fl::join(first, last, sep)`**: Formats a range with a separator between elements. Returns a `join_view` that is formattable through the normal formatting pipeline.
- **Dynamic width and precision from arguments**: `{:{}}` accepts width from the next argument, `{:.{}}` accepts precision from the next argument. Combined syntax `{:{}.{}}` works for full dynamic control.
- **`fl::chrono_format.hpp` (optional header)**: Formatting support for `std::chrono::duration` (with SI unit suffixes: ns, us, ms, s, min, h) and `std::chrono::time_point` (via strftime with `%Y-%m-%d %H:%M:%S` default). Include separately.
- **`fl::color.hpp` (optional header)**: Terminal colour/style support via ANSI escape codes. Provides `text_style`, `fg()`, `bg()`, `bold()`, `italic()`, `underline()`, and ANSI colour constants. The `fl::styled(value, style)` wrapper and `formatter<styled_text<T>>` emit ANSI SGR sequences around formatted output.
- **`fl::wformat(fmt, args...)` → `std::wstring`**: Wide string convenience wrapper. Formats through narrow `format()` then converts to wide. Also `fl::to_wstring(value)`.
- **`fl::format_to(string_builder&, ...)`**: Full format spec support for `string_builder` via builder_sink_adapter. Replaces the old `append_formatted()` which only supported simple `{}`.
- **`fl::ostream_formatter`**: Bridge for types with `operator<<(std::ostream&, T)`. Inheriting `ostream_formatter` makes any streamable type formattable. Uses `sink_streambuf` internally.
- **`fl::format_as()` ADL detection**: Users define `auto format_as(const MyType&) -> formattable_type` in their type's namespace. Auto-detected and dispatched without a full `formatter<T>` specialisation.
- **Debug `?` specifier**: `{:?}` escapes special characters for debugging — strings in double quotes (`\n`, `\t`, `\0`, `\\`, `\"`), chars in single quotes. Control chars < 0x20 use `\xNN` notation.
- **Compile-time format string checking**: `fl::detail::format_string<Args...>` wraps format strings with `consteval` validation of balanced braces. New overloads for `format_to()`, `format()`, `print()`, `println()` accept `format_string`.
- **`fl::dynamic_format_arg_store` + `vformat_to()`/`vformat()`**: Build argument lists dynamically at runtime with `push_back()`. Type-erased dispatch via `format_arg_base`/`format_arg_impl<T>`. Supports format specs and auto/explicit indexing.
- **Compiled format strings**: `fl::detail::compile<MaxSegments>(fmt)` → `compiled_format<N>` — constexpr-parsed format string. `fl::format_to(sink, cfmt, args...)` dispatches from pre-parsed segments, avoiding runtime parsing overhead.
- **Custom allocator support**: `basic_growing_sink<Alloc>` templated on allocator type (default `std::allocator<char>`). Backward-compatible `using growing_sink = basic_growing_sink<>;`. Allocator-aware constructors.
- **SBO-optimised formatter array**: `format_fn<Sink>` replaces `std::function` with a 32-byte small-buffer optimisation for the formatter lambda array. Avoids heap allocation for the common case of small callables.

### Changed
- Float formatting in `format_value()`, `format_argument()`, and `format_float_with_spec()` now uses `std::to_chars` (Dragonbox/Ryū) instead of `snprintf`. Output is locale-independent and shortest-round-trip rather than `%g`-truncated. Float values (3.14f) retain their correct precision without spurious digits from float-to-double promotion.
- `format_float_with_spec()` is now a template accepting both `float` and `double`, avoiding precision loss from `static_cast<double>`.
- `format_int_with_spec_impl()` is now a template accepting any unsigned integer type `UInt`, enabling natural support for `unsigned __int128`.

### Fixed
- **HIGH-1 (partial fix completed)**: Single-arg `format_to` for unsigned integers > `INT64_MAX` incorrectly cast to `int64_t` (e.g., `UINT64_MAX` produced `-1`). Now dispatches based on signedness, correctly using `format_uint_with_spec`.

## [1.0.0] - 2026-02-18

### Added
- Initial public headers for `fl::string`, `fl::arena_allocator`, `fl::string_builder`, `fl::rope`, and the zero-allocation formatting sinks.
- Benchmarking targets and developer guidance.
- Comprehensive documentation covering philosophy, features, examples, and contribution/security guidelines.
