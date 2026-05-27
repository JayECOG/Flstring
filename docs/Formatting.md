# Formatting

The fl formatting system writes formatted output directly into a buffer or sink without creating intermediate string objects. This eliminates allocation overhead in formatting-intensive code paths such as logging, serialization, and protocol assembly.

## The `fl::format_to` Function

`fl::format_to` accepts a sink and a format string with `{}`-based placeholders,
writing the formatted result directly to the sink.

Format strings may be passed as `const char*` or `std::string_view`. Values may
be `std::string`, `std::string_view`, `fl::string`, C strings, `char`, `bool`,
integers, or floating-point values.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    char buffer[256];
    auto sink = fl::make_buffer_sink(buffer);

    int value = 42;
    fl::format_to(sink, "The value is: {}", value);

    sink.null_terminate();

    std::cout << buffer << std::endl; // Output: The value is: 42

    return 0;
}
```

## Convenience Functions

The library provides several convenience wrappers that eliminate the boilerplate of sink creation for common formatting tasks.

### `fl::format()`

Returns a formatted `std::string` directly — no sink creation needed. Accepts both `std::string_view` and `const char*` format strings.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    // Returns std::string directly — no sink required
    std::string result = fl::format("The value is: {}", 42);
    std::cout << result << std::endl; // Output: The value is: 42

    // Format with specifiers
    std::string padded = fl::format("{:0>5}", 42); // "00042"

    return 0;
}
```

### `fl::to_string()`

Single-argument quick formatting for converting any formattable value to its string representation.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    std::string s1 = fl::to_string(42);       // "42"
    std::string s2 = fl::to_string(3.14);     // "3.14"
    std::string s3 = fl::to_string(true);     // "true"

    std::cout << s1 << " " << s2 << " " << s3 << std::endl;
    return 0;
}
```

### `fl::print()` and `fl::println()`

Format directly to `stdout` without creating a sink. `println()` appends a newline after the output; `println()` with no arguments prints a bare newline.

```cpp
#include <fl.hpp>

int main() {
    fl::print("The answer is {}", 42);  // Writes to stdout: The answer is 42
    fl::println("Hello, world!");       // Writes to stdout with newline
    fl::println();                      // Writes a bare newline

    return 0;
}
```

## `fl::format_to()` with `string_builder`

The library provides full format-spec support for `string_builder` via a
`builder_sink_adapter`. Instead of calling the old `append_formatted()` method
(which only supported simple `{}`), use the new `fl::format_to()` overload:

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::string_builder builder;

    // New style — full format spec support
    fl::format_to(builder, "Value: {:>10}", 42);

    fl::string result = std::move(builder).build();
    std::cout << result << std::endl;  // Output: Value:         42
    return 0;
}
```

> **Deprecation note:** The `append_formatted()` method on `string_builder` is
> deprecated in favour of `fl::format_to(builder, ...)`. The old method remains
> for backward compatibility but will be removed in a future release.

## `fl::format_as()` ADL Adapter

Instead of writing a full `formatter<T>` specialisation, you can define an
`format_as()` function in your type's namespace. The library detects it
automatically via the `has_format_as_v<T>` trait:

```cpp
#include <fl.hpp>
#include <iostream>

struct Degrees {
    double value;
};

// Define in the same namespace as Degrees — found by ADL
auto format_as(const Degrees& d) -> fl::formattable_type {
    return d.value;  // Delegate to double formatting
}

int main() {
    Degrees angle{45.0};
    std::cout << fl::format("{}", angle) << std::endl;   // "45"
    std::cout << fl::format("{:.1f}", angle) << std::endl; // "45.0"
    return 0;
}
```

The return type `fl::formattable_type` is a type-erased wrapper that the
formatting engine understands directly. This approach is simpler than writing
a full `formatter<T>` specialisation when you only need to delegate to an
existing formattable type.

## `fl::ostream_formatter`

For types that already have `operator<<(std::ostream&, T)`, inherit from
`fl::ostream_formatter` to make them formattable with zero additional code:

```cpp
#include <fl.hpp>
#include <iostream>

struct Point {
    int x, y;
    friend std::ostream& operator<<(std::ostream& os, const Point& p) {
        return os << "(" << p.x << ", " << p.y << ")";
    }
};

template <>
struct fl::formatter<Point> : fl::ostream_formatter {};

int main() {
    Point p{3, 4};
    std::cout << fl::format("{}", p) << std::endl;       // "(3, 4)"
    std::cout << fl::format("{:>12}", p) << std::endl;    // "      (3, 4)"
    return 0;
}
```

Internally, `ostream_formatter` uses `detail::sink_streambuf`, which wraps any
sink as a `std::streambuf` so that `operator<<` writes through the formatting
pipeline directly.

## Debug `?` Specifier

The `{:?}` specifier produces escaped output suitable for debugging. It formats:

- **Strings**: double-quoted, with `\n`, `\t`, `\r`, `\\`, `\"`, `\0` escape
  sequences. Control bytes below `0x20` that lack a named escape use `\xNN`
  hexadecimal notation.
- **Characters**: single-quoted, with the same escaping rules.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    std::cout << fl::format("{:?}", "hello\nworld") << std::endl;
    // Output: "hello\nworld"

    std::cout << fl::format("{:?}", "tab\there") << std::endl;
    // Output: "tab\there"

    std::cout << fl::format("{:?}", '\n') << std::endl;
    // Output: '\n'

    std::cout << fl::format("{:?}", "bell\a") << std::endl;
    // Output: "bell\x07"

    return 0;
}
```

This is useful for logging, serialisation debugging, and any scenario where
you need to see the exact contents of a string including non-printable
characters.

## Custom Type Extensibility

Users can make their own types formattable by specialising `fl::formatter<T>` for their type. The primary template is empty; detection uses SFINAE on the presence of a `parse()` method in the specialisation.

```cpp
template <typename T, typename = void>
struct formatter {
    // Primary template — unspecialised types produce a compile-time error
    // via static_assert if you attempt to format them.
};
```

### Writing a Custom Formatter

A specialisation must provide:

- `constexpr std::size_t parse(std::string_view spec)` — parse any type-specific format specifiers (returns the number of characters consumed).
- `template <typename Sink> void format(Sink& sink, const T& value, const format_spec* spec)` — write the formatted value to the sink. When `spec` is null, format the value in its simplest form; when non-null, apply alignment, width, precision, etc.

```cpp
#include <fl.hpp>

struct Point { int x, y; };

template <>
struct fl::formatter<Point> {
    constexpr std::size_t parse(std::string_view /*spec*/) noexcept {
        return 0;  // No custom specifiers for this example
    }

    template <typename Sink>
    void format(Sink& sink, const Point& p, const fl::detail::format_spec* spec) {
        // Format as "(x, y)"
        std::string result = "(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
        if (spec) {
            fl::detail::format_text_with_spec(sink, result, *spec);
        } else {
            sink.write(result.data(), result.size());
        }
    }
};

int main() {
    Point p{3, 4};
    std::string s = fl::format("{}", p);       // "(3, 4)"
    std::string a = fl::format("{:>12}", p);    // "      (3, 4)"
    return 0;
}
```

### Detection Mechanism

The library detects custom formatters at compile time via `fl::detail::has_formatter_v<T>`, which checks for the presence of a `parse()` method on `formatter<T>`. When a custom formatter is detected, `format_argument()` dispatches to it instead of the built-in formatting path. Built-in formatters for existing types (`int`, `float`, `std::string`, etc.) remain unchanged and are not affected by this mechanism.

### Specialisation Location

Specialisations must be placed in the `fl` namespace (or, for types you own, in the same namespace as the type, resolved via ADL). The canonical approach for types defined in your own namespace is to open the `fl` namespace and add the specialisation there.

## Container and Range Formatting

All iterable containers (vectors, sets, arrays, deques, etc.) are formattable with `{}` out of the box. Strings and `string_view` are excluded from range formatting — they continue to format as their text content, not as character sequences.

### Default Container Format

```cpp
#include <fl.hpp>
#include <vector>
#include <map>
#include <set>
#include <iostream>

int main() {
    std::vector<int> vec = {1, 2, 3};
    std::cout << fl::format("{}", vec) << std::endl;       // "{1, 2, 3}"

    std::set<std::string> s = {"a", "b"};
    std::cout << fl::format("{}", s) << std::endl;         // "{a, b}"

    std::map<std::string, int> m = {{"k1", 1}, {"k2", 2}};
    std::cout << fl::format("{}", m) << std::endl;         // "{k1: 1, k2: 2}"

    std::vector<int> empty;
    std::cout << fl::format("{}", empty) << std::endl;     // "{}"

    return 0;
}
```

Map-like containers (those with `key_type` and `mapped_type` members) format as `{key1: val1, key2: val2}`. Plain iterable containers format as `{elem1, elem2, elem3}`.

### `fl::join()`

The `fl::join()` function formats a range with a separator between elements. It returns a `join_view` that can be passed directly to `fl::format()` or `fl::format_to()`.

```cpp
#include <fl.hpp>
#include <vector>
#include <array>
#include <iostream>

int main() {
    std::vector<int> vec = {1, 2, 3};
    std::cout << fl::format("{}", fl::join(vec, ", ")) << std::endl;   // "1, 2, 3"

    std::array<std::string, 3> arr = {"x", "y", "z"};
    std::cout << fl::format("{}", fl::join(arr, "|")) << std::endl;    // "x|y|z"

    // Also supports iterator-pair form:
    std::cout << fl::format("{}", fl::join(vec.begin(), vec.end(), " : ")) << std::endl;
    // "1 : 2 : 3"

    return 0;
}
```

When a format specifier is supplied, the entire joined output is aligned and padded as a single block.

### Nested Containers

Containers of containers are supported:

```cpp
std::vector<std::vector<int>> nested = {{1}, {2, 3}};
std::cout << fl::format("{}", nested) << std::endl;   // "{{1}, {2, 3}}"
```

## Dynamic Width and Precision

Width and precision values can be taken from the argument list at runtime, enabling dynamic formatting scenarios such as table generation where column widths are computed programmatically.

### Syntax

| Pattern | Meaning | Example |
|---------|---------|---------|
| `{:{}}` | Width taken from the next argument | `fl::format("{:{}}", 42, 10)` → `"        42"` |
| `{:.{}}f` | Precision taken from the next argument | `fl::format("{:.{}}f", 3.14159, 2)` → `"3.14"` |
| `{:{}.{}}` | Both width and precision from arguments | `fl::format("{:{}.{}}", 3.14159, 10, 2)` → `"      3.14"` |

### Examples

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    int width = 10;
    int precision = 3;

    // Dynamic width
    std::cout << fl::format("{:{}}", 42, width) << std::endl;
    // "        42"

    // Dynamic precision
    std::cout << fl::format("{:.{}}f", 3.14159265, precision) << std::endl;
    // "3.142"

    // Both dynamic
    std::cout << fl::format("{:{}.{}}", 3.14159265, width, 2) << std::endl;
    // "       3.14"

    // Combined with alignment
    std::cout << fl::format("{:>{}}", 42, 5) << std::endl;    // "   42"
    std::cout << fl::format("{:<{}}", 42, 5) << std::endl;    // "42   "

    return 0;
}
```

### Edge Cases

- **Negative width**: Treated as zero (no padding).
- **Negative precision**: Treated as unset (full precision displayed).
- **Zero width/precision**: Applied literally — `fl::format("{:.{}}f", 3.14, 0)` produces `"3"`.
- **Argument type**: Dynamic width and precision arguments must be integers. Non-integer arguments produce a compile-time or runtime error depending on the call site.

## Sinks

A sink is a destination for formatted output. The fl library provides these sink types:

- `fl::sinks::buffer_sink` — Writes to a fixed-size buffer. No heap allocation.
  Throws `std::overflow_error` on overflow.
- `fl::sinks::growing_sink` — Writes to a dynamically growing `std::vector<char>`.
  Alias for `basic_growing_sink<std::allocator<char>>`.
- `fl::sinks::file_sink` — Writes to a `FILE*` handle. Supports owned and borrowed files.
- `fl::sinks::stream_sink` — Writes to a `std::ostream` reference.
- `fl::sinks::null_sink` — Discards all output. Counts discarded bytes.
- `fl::sinks::multi_sink` — Fan-out to multiple `shared_ptr<output_sink>` targets.

### Custom Allocator Support

The `basic_growing_sink<Alloc>` template extends `growing_sink` with allocator awareness.
It accepts any allocator satisfying the `Allocator` concept and defaults to
`std::allocator<char>`. The existing `growing_sink` type is retained as a
backward-compatible alias:

```cpp
// Default — uses std::allocator<char>
fl::sinks::basic_growing_sink<> sink(256);

// Custom allocator — e.g., a pool-backed allocator
fl::sinks::basic_growing_sink<MyAllocator<char>> custom_sink(256);
```

Internally, the formatter array uses `format_fn<Sink>` — a 32-byte small-buffer
optimisation that replaces `std::function`. This avoids heap allocation for the
common case of small callables, reducing allocation pressure in formatting hot
paths.

## Format Specifiers

`fl::format_to` supports a rich set of format specifiers for controlling value
presentation.

The general form of a format specifier is:

`{[fill][align][sign][#][width][.precision][type]}`

Placeholders can also use explicit positional indices such as `{0}` or
`{1:>8}`. Automatic `{}` placeholders and explicit indices cannot be mixed in
one format string.

### Fill and Align

Fill character and alignment:

```cpp
// Right-aligned, padded with zeros
fl::format_to(sink, "{:0>5}", 42); // "00042"

// Centred, padded with asterisks
fl::format_to(sink, "{:*^7}", 42); // "**42***"
```

The alignment options are:

- `<` — Left align
- `>` — Right align (default for numbers)
- `^` — Centre align
- `=` — Numeric padding (places padding between sign/prefix and digits)

### Sign

Control the display of the sign:

```cpp
// Always show the sign
fl::format_to(sink, "{:+}", 42); // "+42"

// Space for positives, minus for negatives
fl::format_to(sink, "{: }", 42); // " 42"
fl::format_to(sink, "{: }", -42); // "-42"
```

The sign options are:

- `+` — Always show a sign (`+` for positive, `-` for negative)
- ` ` (space) — Show a space for positive values, `-` for negative values
- `-` — Show sign only for negatives (default behaviour; no specifier required)

### Base Prefix

The `#` specifier includes the base prefix for integer types:

```cpp
fl::format_to(sink, "{:#x}", 255); // "0xff"
fl::format_to(sink, "{:#b}", 5);   // "0b101"
fl::format_to(sink, "{:#o}", 64);  // "0100"
```

### Width

A numeric value specifies the minimum field width:

```cpp
fl::format_to(sink, "{:10}", "Hello"); // "Hello     " (left-aligned for strings, right for numbers)
```

### Precision

For floating-point numbers, `.N` specifies the number of decimal places:

```cpp
fl::format_to(sink, "{:.2f}", 3.14159); // "3.14"
```

### Type

Type specifiers:

```cpp
// Hexadecimal
fl::format_to(sink, "{:x}", 255); // "ff"

// Binary
fl::format_to(sink, "{:b}", 5); // "101"

// Floating point with a specified precision
fl::format_to(sink, "{:.2f}", 3.14159); // "3.14"
```

## Usage Examples

### Basic
```cpp
char buffer[256];
auto sink = fl::make_buffer_sink(buffer);
fl::format_to(sink, "Count: {:5}", 42);
sink.null_terminate();
// Output (in buffer): "Count:    42"
```

### Width & Alignment
```cpp
char buffer[256];
auto sink = fl::make_buffer_sink(buffer);

fl::format_to(sink, "Left:   {:<5}", 42);     // "Left:   42   "
fl::format_to(sink, "Right:  {:>5}", 42);    // "Right:     42"
fl::format_to(sink, "Center: {:^5}", 42);    // "Center:  42  "

sink.null_terminate();
```

### Number Bases
```cpp
char buffer[256];
auto sink = fl::make_buffer_sink(buffer);

fl::format_to(sink, "Hex: {:#x}", 255);      // "Hex: 0xff"
fl::format_to(sink, "Bin: {:#b}", 15);       // "Bin: 0b1111"
fl::format_to(sink, "Oct: {:#o}", 64);       // "Oct: 0100"

sink.null_terminate();
```

### Zero Padding
```cpp
char buffer[256];
auto sink = fl::make_buffer_sink(buffer);

fl::format_to(sink, "ID: {:0>6}", 123);      // "ID: 000123"
fl::format_to(sink, "Year: {:0>4}", 24);     // "Year: 0024"

sink.null_terminate();
```

### Escaped Braces

```cpp
fl::format_to(sink, "{{}}"); // "{}"
```

## Performance Notes

### Zero-Allocation Formatting

```cpp
// All formatting happens on the stack - no heap allocation
char buffer[256];
auto sink = fl::make_buffer_sink(buffer);
fl::format_to(sink, "Value: {:0>10}", 12345);
// Stack: ~256 bytes for buffer
// Heap: 0 allocations
```

### Direct Sink Writing

```cpp
// Output is written directly to sink
// No temporary strings created
// Perfect for:
// - Logging systems
// - Network protocols  
// - File output
// - Memory-constrained systems
```

### Complexity

- **Parsing**: O(n) where n ≤ 10 (typical spec length)
- **Formatting**: O(log m) for integers (m = value magnitude)
- **Alignment**: O(w) where w = width
- **Total**: O(n + log m + w) = O(w) typically

### Float Formatting

Float formatting uses a modern `std::to_chars`-based engine that employs the Dragonbox/Ryū algorithm internally on modern toolchains:

- **5–10× faster** than the previous `snprintf`-based implementation
- **Locale-independent output** — the decimal separator is always `.`, regardless of the system locale (`LC_NUMERIC`)
- **Shortest-round-trip by default** — produces the shortest decimal string that, when parsed back, yields the same float/double value
- **Correct `float` precision** — `3.14f` produces `"3.14"`, not `"3.140000104904175"`
- **Eliminates the 3 CRITICAL audit bugs** that were caused by `snprintf` (buffer over-read on `snprintf` returning `-1`, buffer overflow in precision strings, and locale-dependent behaviour)

## Compile-Time Format String Checking

The library provides compile-time validation of format strings via
`fl::detail::format_string<Args...>`. This wrapper performs `consteval`
brace-balance checking, catching mismatched braces and placeholder syntax
errors before runtime.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    // Compile-time validated — literal format string
    std::string s = fl::format("Hello, {}!", "world");
    std::cout << s << std::endl;   // "Hello, world!"

    // Runtime fallback — non-literal strings use the unvalidated overload
    std::string_view runtime_fmt = get_format_from_config();
    std::string t = fl::format(runtime_fmt, 42);

    return 0;
}
```

The validated overloads are available for `fl::format_to()`, `fl::format()`,
`fl::print()`, and `fl::println()`. Pass a literal string to opt in; runtime
strings fall back to the existing runtime-validated path.

## Dynamic Arguments at Runtime (`vformat`)

The `fl::dynamic_format_arg_store` lets you build argument lists at runtime
instead of at compile time. This is useful when the number or types of arguments
are not known until the program runs (e.g., a logging framework with dynamic
field injection).

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    fl::dynamic_format_arg_store store;

    store.push_back(42);               // int
    store.push_back(3.14);             // double
    store.push_back("hello");          // const char*

    // Format using the dynamic store
    std::string result = fl::vformat("{} {} {}", store);
    std::cout << result << std::endl;  // "42 3.14 hello"

    // Or write to a sink
    char buf[128];
    auto sink = fl::make_buffer_sink(buf);
    fl::vformat_to(sink, "Value: {}", store);
    sink.null_terminate();

    std::cout << buf << std::endl;     // "Value: 42"
    return 0;
}
```

The store uses type-erased dispatch via `format_arg_base` / `format_arg_impl<T>`,
so it supports the same set of formattable types as the compile-time API.
Format specs and auto/explicit indexing work identically.

## Compiled Format Strings

For performance-critical paths, you can pre-parse a format string at compile
time using `fl::detail::compile<MaxSegments>(fmt)`. This produces a
`compiled_format<N>` that captures the parsed structure as segments (literal
text, spec placeholders, placeholder flags), enabling dispatch without runtime
parsing overhead.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    // Compile-time parsing — no runtime overhead
    constexpr auto cfmt = fl::detail::compile<8>("Value: {:>10}");

    char buf[128];
    auto sink = fl::make_buffer_sink(buf);
    fl::format_to(sink, cfmt, 42);
    sink.null_terminate();

    std::cout << buf << std::endl;  // "Value:         42"
    return 0;
}
```

The `MaxSegments` template parameter bounds the number of segments (literal
blocks + placeholders) that the compiled format can hold. Choose a value that
comfortably exceeds your typical format string complexity — 8 or 16 is adequate
for most use cases. The compiled format is a constexpr value with zero
allocation overhead.

## Limitations and Workarounds

### Multiple Arguments and Positional Indices

```cpp
// ✓ Works: Single argument with spec
fl::format_to(sink, "Value: {:5}", 42);

// ✓ Works: multiple arguments with independent specs
fl::format_to(sink, "Values: {:5} {:>5}", 42, 100);

// ✓ Works: explicit positional reuse
fl::format_to(sink, "{1} {0} {1}", "left", "right");
```

### Unicode/UTF-8

```cpp
// ✓ ASCII works fine
fl::format_to(sink, "ASCII: {:10}", "Hello");

// ✗ UTF-8 alignment not supported (width is byte-based, not character-based)
// fl::format_to(sink, "UTF-8: {:10}", "日本");
```

### Errors

Malformed placeholders raise `std::invalid_argument` instead of being treated
as literal text. This includes unmatched braces, invalid placeholder syntax,
and mixing automatic with explicit indices.

## Optional Features

The following features live in separate headers and must be included explicitly. They are not pulled in by `#include <fl.hpp>`.

### Chrono Formatting (`fl/chrono_format.hpp`)

**Header:** `#include "fl/chrono_format.hpp"`

Provides formatter specialisations for `std::chrono::duration` and `std::chrono::time_point` types.

#### Duration Formatting

`std::chrono::duration` values are formatted as a numeric value followed by the SI unit suffix:

```cpp
#include <fl.hpp>
#include "fl/chrono_format.hpp"
#include <iostream>

int main() {
    using namespace std::chrono_literals;

    std::cout << fl::format("{}", 5s)        << std::endl;   // "5s"
    std::cout << fl::format("{}", 1500ms)    << std::endl;   // "1500ms"
    std::cout << fl::format("{}", 2.5min)    << std::endl;   // "2.5min"
    std::cout << fl::format("{}", 3h)        << std::endl;   // "3h"
    std::cout << fl::format("{}", 100ns)     << std::endl;   // "100ns"
    std::cout << fl::format("{}", 200us)     << std::endl;   // "200us"

    return 0;
}
```

The internal format string (`chrono_fmt`, default `"%Q%q"`) supports:
- `%Q` — the numeric value
- `%q` — the unit suffix (e.g. `s`, `ms`, `min`, `h`)

#### Time Point Formatting

`std::chrono::time_point` values are formatted via `strftime` using ISO 8601 format by default:

```cpp
#include <fl.hpp>
#include "fl/chrono_format.hpp"
#include <iostream>
#include <chrono>

int main() {
    auto now = std::chrono::system_clock::now();
    std::cout << fl::format("{}", now) << std::endl;
    // e.g. "2026-05-23 19:04:30"

    return 0;
}
```

The default format string is `"%Y-%m-%d %H:%M:%S"`. Only clocks that convert to `system_clock::time_point` are supported.

### Colour and Style (`fl/color.hpp`)

**Header:** `#include "fl/color.hpp"`

Provides ANSI terminal colour and text attribute support via `styled_text`.

#### Named Colour Constants

The `fl::ansi` namespace provides 16 colour constants (standard 8 plus bright variants):

| Constant | ANSI Code |
|----------|-----------|
| `ansi::black` / `ansi::bright_black` | 30 / 90 |
| `ansi::red` / `ansi::bright_red` | 31 / 91 |
| `ansi::green` / `ansi::bright_green` | 32 / 92 |
| `ansi::yellow` / `ansi::bright_yellow` | 33 / 93 |
| `ansi::blue` / `ansi::bright_blue` | 34 / 94 |
| `ansi::magenta` / `ansi::bright_magenta` | 35 / 95 |
| `ansi::cyan` / `ansi::bright_cyan` | 36 / 96 |
| `ansi::white` / `ansi::bright_white` | 37 / 97 |

#### The `text_style` Struct

A `text_style` describes terminal text appearance: foreground colour, background colour, and text attributes (bold, dim, italic, underline, blink, reverse video, strikethrough). Fields set to `-1` mean "not set" — the terminal attribute is left unchanged.

#### Style Combinator Functions

Combine attributes using the provided functions. Each function can be called with no arguments to create a style with that attribute, or with an existing `text_style` to augment it:

| Function | SGR Code |
|----------|----------|
| `fg(color)` | Set foreground colour |
| `bg(color)` | Set background colour |
| `bold()` | Bold text (code 1) |
| `dim()` | Dim text (code 2) |
| `italic()` | Italic text (code 3) |
| `underline()` | Underline text (code 4) |
| `blink()` | Blink text (code 5) |
| `reverse()` | Reverse video (code 7) |
| `strikethrough()` | Strikethrough text (code 9) |

Styles compose via `operator|`:

```cpp
auto style = fl::fg(fl::ansi::red) | fl::bold | fl::underline;
```

#### `fl::styled()` and `fl::styled_text<T>`

Wrap any value with a style using `fl::styled(value, style)`. The result is a `styled_text<T>` that integrates with the formatting pipeline.

```cpp
#include <fl.hpp>
#include "fl/color.hpp"
#include <iostream>

int main() {
    // Red bold text
    fl::print("{}", fl::styled("Error: something went wrong",
                 fl::fg(fl::ansi::red) | fl::bold));

    // Green italic text
    fl::println("{}", fl::styled("Success!",
                 fl::fg(fl::ansi::green) | fl::italic));

    return 0;
}
```

The formatter emits ANSI SGR (Select Graphic Rendition) escape sequences before and after the formatted value, then resets to terminal defaults with `\033[0m`.

### Wide String Wrappers

No separate header is required — `fl::wformat()` and `fl::to_wstring()` are defined in `fl/format.hpp`.

#### `fl::wformat(fmt, args...)`

Convenience wrapper that formats arguments as a narrow string via the standard `fl::format()` machinery, then widens the result to `std::wstring`.

```cpp
#include <fl.hpp>
#include <iostream>

int main() {
    std::wstring ws = fl::wformat("Value: {}", 42);
    // ws == L"Value: 42"

    std::wcout << ws << std::endl;
    return 0;
}
```

#### `fl::to_wstring(value)`

Single-value quick formatting returning `std::wstring`. Delegates to `wformat("{}", value)`.

```cpp
std::wstring ws = fl::to_wstring(3.14);    // L"3.14"
std::wstring ws2 = fl::to_wstring(true);   // L"true"
```

## Version History

### 2.1.0
- **`fl::format_to(string_builder&, ...)`**: Full format spec support for `string_builder` via `builder_sink_adapter`. Old `append_formatted()` deprecated in favour of `fl::format_to(builder, "spec {:>10}", value)`.
- **`fl::ostream_formatter`**: Bridge for types with `operator<<(ostream&, T)`. Inherit from `ostream_formatter` to make any streamable type formattable. Uses `detail::sink_streambuf`.
- **`fl::format_as()` ADL adapter**: Define `auto format_as(const MyType&) -> formattable_type` in the type's namespace. Auto-detected via `has_format_as_v<T>`. No full `formatter<T>` specialisation needed.
- **Debug `?` specifier**: `{:?}` escapes strings for debugging — double-quoted, with `\n`, `\t`, `\r`, `\\`, `\"`, `\0`, and `\xNN` for control bytes. Single-quote chars.
- **Compile-time format string checking**: `fl::detail::format_string<Args...>` wraps format strings with `consteval` brace-balance validation. New overloads for `format_to()`, `format()`, `print()`, `println()` accept `format_string`.
- **Dynamic argument store + `vformat`**: `fl::dynamic_format_arg_store` builds argument lists at runtime with `push_back(value)`. Type-erased dispatch. `fl::vformat_to(sink, fmt, store)` and `fl::vformat(fmt, store) → string`.
- **Compiled format strings**: `fl::detail::compile<MaxSegments>(fmt)` → `compiled_format<N>` for constexpr-parsed format strings. `fl::format_to(sink, cfmt, args...)` dispatches from pre-parsed segments without runtime parsing.
- **Custom allocator support**: `basic_growing_sink<Alloc>` — allocator-aware sink. Defaults to `std::allocator<char>`. Alias `using growing_sink = basic_growing_sink<>;` for backward compatibility. SBO-optimised `format_fn<Sink>` replaces `std::function` in formatter arrays.
- **Custom type extensibility**: `fl::formatter<T>` primary template added. Users can specialise `formatter<T>` for their own types using SFINAE detection of a `parse()` method. Built-in formatters for existing types unaffected.
- **Container and range formatting**: All iterable containers (vectors, sets, maps, arrays) are now formattable with `{}`. Maps format as `{key: val}`, plain containers as `{elem1, elem2}`. Strings and `string_view` are correctly excluded.
- **`fl::join()` function**: `fl::join(range, sep)` and `fl::join(first, last, sep)` formats a range with a separator. Returns a `join_view` that integrates with the formatting pipeline.
- **Dynamic width and precision**: `{:{}}`, `{:.{}}f`, and `{:{}.{}}` syntax support. Width and precision taken from subsequent arguments. Negative width → 0; negative precision → unset.
- **Chrono formatting** (optional): `fl/chrono_format.hpp` provides formatters for `std::chrono::duration` (value + SI unit suffix) and `std::chrono::time_point` (via strftime).
- **Terminal colour and style** (optional): `fl/color.hpp` provides `text_style`, `ansi` colour constants, combinator functions (`fg()`, `bg()`, `bold()`, etc.), and `styled(value, style)` for ANSI SGR-coloured output.
- **Wide string wrappers**: `fl::wformat(fmt, args...)` returns `std::wstring`. Single-argument `fl::to_wstring(value)` also available.

### 2.0.0 (updated)
- **Convenience functions added**: `fl::format(fmt, args...)` returns `std::string` directly.`fl::to_string(value)` formats a single value. `fl::print(fmt, args...)` and `fl::println(fmt, args...)` write to stdout. `println()` with no arguments prints a bare newline.
- **Float formatting replaced**: The old `snprintf`-based engine has been replaced with `std::to_chars` (Dragonbox/Ryū internally on modern toolchains). This delivers 5–10× faster formatting, locale-independent output, and shortest-round-trip behaviour by default. Also eliminates the 3 CRITICAL audit bugs caused by `snprintf`.
- **Space sign support**: `{:\ }` syntax now supported — shows a space for positive values and a minus for negatives. Works for both integers and floating-point values.
- **`__int128` support**: `__int128` and `unsigned __int128` are now supported on GCC/Clang (guarded by `__SIZEOF_INT128__`). Not available on MSVC.
- **HIGH-1 security fix**: Single-argument `format_to` for unsigned integers > `INT64_MAX` now formats correctly instead of producing negative output from an incorrect `int64_t` cast.

### 2.0.0
- **Bool and char formatting fixed**: `format_to(sink, "{}", true)` now produces `"true"` instead of `"1"`. `format_to(sink, "{}", 'A')` now produces `"A"` instead of `"65"`. These fixes apply to the single-argument overload, which previously used a generic integer path that did not distinguish bool or char types.
- **Format audit corrections**: 14 critical- and high-severity issues resolved, including a buffer over-read when `snprintf` returns `-1`, a buffer overflow in float-formatting precision strings, and unsigned integer formatting for values above `INT64_MAX`.
