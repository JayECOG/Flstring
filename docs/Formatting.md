# Formatting

The fl formatting system writes formatted output directly into a buffer or sink
without creating intermediate string objects. This eliminates allocation overhead
in formatting-intensive code paths such as logging, serialization, and protocol
assembly.

## The `fl::format_to` Function

`fl::format_to` accepts a sink and a format string with `{}`-based placeholders,
writing the formatted result directly to the sink.

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

## Sinks

A sink is a destination for formatted output. The fl library provides six sink types:

- `fl::sinks::buffer_sink` — Writes to a fixed-size buffer. No heap allocation.
  Throws `std::overflow_error` on overflow.
- `fl::sinks::growing_sink` — Writes to a dynamically growing `std::vector<char>`.
- `fl::sinks::file_sink` — Writes to a `FILE*` handle. Supports owned and borrowed files.
- `fl::sinks::stream_sink` — Writes to a `std::ostream` reference.
- `fl::sinks::null_sink` — Discards all output. Counts discarded bytes.
- `fl::sinks::multi_sink` — Fan-out to multiple `shared_ptr<output_sink>` targets.

## Format Specifiers

`fl::format_to` supports a rich set of format specifiers for controlling value
presentation.

The general form of a format specifier is:

`{[fill][align][sign][#][width][.precision][type]}`

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
```

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

## Limitations and Workarounds

### Multiple Arguments with Specs

```cpp
// ✓ Works: Single argument with spec
fl::format_to(sink, "Value: {:5}", 42);

// ✗ Doesn't work: Multiple arguments with specs (current implementation)
// fl::format_to(sink, "{:5} {:5}", 42, 100);

// ✓ Workaround 1: Multiple format_to calls
fl::format_to(sink, "Values: {:5}", 42);
fl::format_to(sink, " {:5}", 100);

// ✓ Workaround 2: Use basic {} for multiple args
fl::format_to(sink, "Values: {} {}", 42, 100);
```

### Unicode/UTF-8

```cpp
// ✓ ASCII works fine
fl::format_to(sink, "ASCII: {:10}", "Hello");

// ✗ UTF-8 alignment not supported (width is byte-based, not character-based)
// fl::format_to(sink, "UTF-8: {:10}", "日本");
```