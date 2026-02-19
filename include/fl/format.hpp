// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_FORMAT_HPP
#define FL_FORMAT_HPP

// Type-safe formatting engine with Python/std::format-style placeholders.
// Supports alignment, padding, width, precision, and base conversions for
// integral and floating-point types. Output is written through a sink
// abstraction so callers can target fixed buffers or growing storage.

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <type_traits>
#include <limits>
#include <stdexcept>
#include <functional>
#include <array>
#include "fl/profiling.hpp"

namespace fl {

// Forward declaration.
class string;

namespace detail {

// Abstract sink interface for formatting output.
struct sink_base {
    virtual ~sink_base() = default;
    virtual void write(const char* data, std::size_t len) = 0;
};

// Formatting sink that writes to a caller-provided fixed-size buffer. Throws
// std::overflow_error if the output exceeds the buffer capacity.
class buffer_sink : public sink_base {
public:
    buffer_sink(char* buffer, std::size_t capacity) noexcept
        : _buffer(buffer), _capacity(capacity), _size(0) {}

    void write(const char* data, std::size_t len) override {
        if (_size + len > _capacity) {
            throw std::overflow_error("fl::buffer_sink: output buffer overflow");
        }
        std::memcpy(_buffer + _size, data, len);
        _size += len;
    }

    std::size_t size() const noexcept { return _size; }
    char* buffer() const noexcept { return _buffer; }

private:
    char* _buffer;
    std::size_t _capacity;
    std::size_t _size;
};

// Formatting sink backed by a dynamically growing std::string.
class growing_sink : public sink_base {
public:
    growing_sink(std::size_t initial_capacity = 256) : _buffer(), _size(0) {
        _buffer.reserve(initial_capacity);
    }

    void write(const char* data, std::size_t len) override {
        _buffer.append(data, len);
        _size += len;
    }

    std::size_t size() const noexcept { return _size; }
    const std::string& buffer() const noexcept { return _buffer; }
    std::string& buffer() noexcept { return _buffer; }
    fl::string to_fl_string() const;

private:
    std::string _buffer;
    std::size_t _size;
};

// Stateless utility for converting integers to decimal strings without any
// heap allocation.
class integer_formatter {
public:
    static std::size_t format_int64(char* buffer, std::size_t capacity, int64_t value) noexcept {
        if (capacity == 0) return 0;

        if (value == 0) {
            buffer[0] = '0';
            return 1;
        }

        bool negative = value < 0;
        uint64_t uvalue = negative ? static_cast<uint64_t>(-value) : static_cast<uint64_t>(value);

        char temp[20];
        std::size_t len = 0;
        while (uvalue > 0) {
            temp[len++] = '0' + (uvalue % 10);
            uvalue /= 10;
        }

        if (negative && capacity > len) {
            buffer[0] = '-';
            std::reverse_copy(temp, temp + len, buffer + 1);
            return len + 1;
        }

        std::reverse_copy(temp, temp + len, buffer);
        return len;
    }

    static std::size_t format_uint64(char* buffer, std::size_t capacity, uint64_t value) noexcept {
        if (capacity == 0) return 0;

        if (value == 0) {
            buffer[0] = '0';
            return 1;
        }

        char temp[20];
        std::size_t len = 0;
        while (value > 0) {
            temp[len++] = '0' + (value % 10);
            value /= 10;
        }

        std::reverse_copy(temp, temp + len, buffer);
        return len;
    }

private:
    static void reverse_copy_impl(const char* first, const char* last, char* dest) noexcept {
        while (first != last) {
            *dest++ = *--last;
        }
    }
};

}  // namespace detail

// Reuse the sinks' buffer_sink implementation to avoid duplicate symbols.
// sinks.hpp is included before this header via fl.hpp, so import the
// implementation here for formatting APIs.
using buffer_sink = sinks::buffer_sink;

// Format implementation for common types.
namespace detail {

    // Formats a single value and writes it to the sink.
    template <typename Sink, typename T>
    void format_value(Sink& sink, T value) {
        char temp[64];
        std::size_t len = 0;

        if constexpr (std::is_same_v<T, const char*>) {
            sink.write(value, std::strlen(value));
        } else if constexpr (std::is_same_v<T, const fl::string&>) {
            sink.write(value.data(), value.size());
        } else if constexpr (std::is_same_v<T, char>) {
            temp[0] = value;
            sink.write(temp, 1);
        } else if constexpr (std::is_same_v<T, bool>) {
            if (value) {
                sink.write("true", 4);
            } else {
                sink.write("false", 5);
            }
        } else if constexpr (std::is_same_v<T, int64_t>) {
            len = integer_formatter::format_int64(temp, sizeof(temp), value);
            sink.write(temp, len);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            len = integer_formatter::format_uint64(temp, sizeof(temp), value);
            sink.write(temp, len);
        } else if constexpr (std::is_integral_v<T>) {
            if constexpr (std::is_signed_v<T>) {
                len = integer_formatter::format_int64(temp, sizeof(temp), static_cast<int64_t>(value));
            } else {
                len = integer_formatter::format_uint64(temp, sizeof(temp), static_cast<uint64_t>(value));
            }
            sink.write(temp, len);
        } else if constexpr (std::is_floating_point_v<T>) {
            len = std::snprintf(temp, sizeof(temp), "%g", static_cast<double>(value));
            if (len > 0) sink.write(temp, len);
        } else {
            static_assert(false, "Unsupported type for formatting");
        }
    }

    // Parsed representation of a Python/std::format-style format specification
    // string such as ">20", "*^15", or "0>10x". Supports fill character,
    // alignment, sign, base prefix, width, precision, and type specifier.
    struct format_spec {
    char fill = ' ';
    char align = '\0';                  // '<' (left), '>' (right), '^' (center), '=' (numeric padding).
    bool sign = false;
    bool show_base = false;             // Show 0x, 0b, 0 prefix for integers.
    std::size_t width = 0;
    std::size_t precision = 6;
    bool precision_set = false;         // True when precision was explicitly provided.
    char type = '\0';                   // Type specifier: d, x, b, o, f, e, g, s, c.

    // Parses the format specification starting at spec_start and populates
    // the given spec struct. Returns the number of characters consumed.
    static std::size_t parse(const char* spec_start, format_spec& spec) {
        const char* p = spec_start;

        // Check for sign (+). Allow sign before or after fill+align.
        if (*p == '+') {
            spec.sign = true;
            ++p;
        }

        // Check for fill + align (e.g., "0>" or "*<" or "*^").
        if (*p && *(p + 1) && (*(p + 1) == '<' || *(p + 1) == '>' || *(p + 1) == '^' || *(p + 1) == '=')) {
            spec.fill = *p;
            spec.align = *(p + 1);
            p += 2;
        } else if (*p && (*p == '<' || *p == '>' || *p == '^' || *p == '=')) {
            spec.align = *p;
            ++p;
        }

        // Check for base prefix (#).
        if (*p == '#') {
            spec.show_base = true;
            ++p;
        }

        // Parse width.
        while (*p && *p >= '0' && *p <= '9') {
            spec.width = spec.width * 10 + (*p - '0');
            ++p;
        }

        // Parse precision.
        if (*p == '.') {
            ++p;
            spec.precision = 0;
            spec.precision_set = true;
            while (*p && *p >= '0' && *p <= '9') {
                spec.precision = spec.precision * 10 + (*p - '0');
                ++p;
            }
        }

        // Parse type specifier.
        if (*p && (*p == 'd' || *p == 'x' || *p == 'X' || *p == 'b' || *p == 'B' ||
                   *p == 'o' || *p == 'f' || *p == 'e' || *p == 'E' ||
                   *p == 'g' || *p == 'G' || *p == 's' || *p == 'c')) {
            spec.type = *p;
            ++p;
        }

        return p - spec_start;
    }
};

// Formats an integer value according to the given format_spec, handling base
// conversion, sign, prefix, alignment, and padding.
template <typename Sink>
void format_int_with_spec(Sink& sink, int64_t value, const format_spec& spec) {
    char digits[128];
    std::size_t digit_len = 0;
    const char* prefix = "";
    std::size_t prefix_len = 0;

    char base_char = spec.type ? spec.type : 'd';
    int base = 10;
    bool is_negative = value < 0;
    uint64_t abs_value = is_negative ? static_cast<uint64_t>(-value) : static_cast<uint64_t>(value);

    switch (base_char) {
        case 'x':
            base = 16;
            if (spec.show_base && value != 0) { prefix = "0x"; prefix_len = 2; }
            break;
        case 'X':
            base = 16;
            if (spec.show_base && value != 0) { prefix = "0X"; prefix_len = 2; }
            break;
        case 'b':
        case 'B':
            base = 2;
            if (spec.show_base && value != 0) { prefix = "0b"; prefix_len = 2; }
            break;
        case 'o':
            base = 8;
            if (spec.show_base && value != 0) { prefix = "0"; prefix_len = 1; }
            break;
        default:
            base = 10;
            break;
    }

    if (abs_value == 0) {
        digits[digit_len++] = '0';
    } else {
        char temp[128];
        std::size_t temp_len = 0;
        while (abs_value > 0) {
            int digit = abs_value % base;
            temp[temp_len++] = (digit < 10) ? ('0' + digit) :
                          (base_char == 'X' || base_char == 'B') ?
                          ('A' + digit - 10) : ('a' + digit - 10);
            abs_value /= base;
        }
        for (std::size_t i = 0; i < temp_len; ++i) {
            digits[digit_len++] = temp[temp_len - 1 - i];
        }
    }

    char sign_char = '\0';
    std::size_t sign_len = 0;
    if (is_negative) {
        sign_char = '-';
        sign_len = 1;
    } else if (spec.sign) {
        sign_char = '+';
        sign_len = 1;
    }

    std::size_t total_content = sign_len + prefix_len + digit_len;

    if (total_content < spec.width) {
        std::size_t padding = spec.width - total_content;
        char fill_char = spec.fill ? spec.fill : ' ';

        if (spec.align == '<') {
            if (sign_len) sink.write(&sign_char, 1);
            if (prefix_len) sink.write(prefix, prefix_len);
            sink.write(digits, digit_len);
            for (std::size_t i = 0; i < padding; ++i) {
                sink.write(&fill_char, 1);
            }
        } else if (spec.align == '^') {
            // Center align: bias extra padding to the left.
            std::size_t left_pad = (padding + 1) / 2;
            std::size_t right_pad = padding - left_pad;
            for (std::size_t i = 0; i < left_pad; ++i) sink.write(&fill_char, 1);
            if (sign_len) sink.write(&sign_char, 1);
            if (prefix_len) sink.write(prefix, prefix_len);
            sink.write(digits, digit_len);
            for (std::size_t i = 0; i < right_pad; ++i) sink.write(&fill_char, 1);
        } else if (spec.align == '=' || (spec.fill == '0' && spec.align == '>') || (spec.align == '\0' && spec.fill != ' ' && spec.fill != '\0')) {
            // Numeric padding: sign/prefix, then padding, then digits.
            if (sign_len) sink.write(&sign_char, 1);
            if (prefix_len) sink.write(prefix, prefix_len);
            for (std::size_t i = 0; i < padding; ++i) {
                sink.write(&fill_char, 1);
            }
            sink.write(digits, digit_len);
        } else {
            // Default: right align.
            for (std::size_t i = 0; i < padding; ++i) {
                sink.write(&fill_char, 1);
            }
            if (sign_len) sink.write(&sign_char, 1);
            if (prefix_len) sink.write(prefix, prefix_len);
            sink.write(digits, digit_len);
        }
    } else {
        if (sign_len) sink.write(&sign_char, 1);
        if (prefix_len) sink.write(prefix, prefix_len);
        sink.write(digits, digit_len);
    }
}

// Formats a floating-point value according to the given format_spec,
// handling precision, format character (e, f, g), alignment, and padding.
template <typename Sink>
void format_float_with_spec(Sink& sink, double value, const format_spec& spec) {
    char temp[256];
    std::size_t len = 0;

    char fmt_char = spec.type ? spec.type : 'g';
    [[maybe_unused]] const char* fmt_str = nullptr;
    char fmt_buffer[16];

    switch (fmt_char) {
        case 'e':
        case 'E':
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%%zu%c", spec.precision, fmt_char);
            break;
        case 'f':
        case 'F':
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%.%zuf", spec.precision);
            break;
        case 'g':
        case 'G':
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%.%zu%c", spec.precision, fmt_char);
            break;
        default:
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%g");
            break;
    }

    len = std::snprintf(temp, sizeof(temp), fmt_buffer, value);
    if (len >= sizeof(temp)) len = sizeof(temp) - 1;

    if (len < spec.width) {
        std::size_t padding = spec.width - len;
        char fill_char = spec.fill ? spec.fill : ' ';

        if (spec.align == '<') {
            sink.write(temp, len);
            for (std::size_t i = 0; i < padding; ++i) {
                sink.write(&fill_char, 1);
            }
        } else if (spec.align == '^') {
            // Center align: bias extra padding to the left.
            std::size_t left_pad = (padding + 1) / 2;
            std::size_t right_pad = padding - left_pad;
            for (std::size_t i = 0; i < left_pad; ++i) {
                sink.write(&fill_char, 1);
            }
            sink.write(temp, len);
            for (std::size_t i = 0; i < right_pad; ++i) {
                sink.write(&fill_char, 1);
            }
        } else {
            // Default: right align.
            for (std::size_t i = 0; i < padding; ++i) {
                sink.write(&fill_char, 1);
            }
            sink.write(temp, len);
        }
    } else {
        sink.write(temp, len);
    }
}

// Parses the format string, matches each "{}" or "{:spec}" placeholder to the
// corresponding argument, and writes the formatted output to the sink.
    template <typename Sink, typename... Args>
    void format_impl(Sink& sink, const char* fmt, Args&&... args) {
        int arg_index = 0;

        using formatter_t = std::function<void(Sink&, const format_spec*)>;
        const std::array<formatter_t, sizeof...(Args)> formatters{
            [&args](Sink& s, const format_spec* spec) {
                if (!spec) {
                    format_value(s, args);
                    return;
                }

                if constexpr (std::is_integral_v<std::decay_t<decltype(args)>>) {
                    if constexpr (std::is_signed_v<std::decay_t<decltype(args)>>) {
                        format_int_with_spec(s, static_cast<int64_t>(args), *spec);
                    } else {
                        // Cast unsigned to signed for consistent handling.
                        format_int_with_spec(s, static_cast<int64_t>(args), *spec);
                    }
                    return;
                }

                if constexpr (std::is_floating_point_v<std::decay_t<decltype(args)>>) {
                    format_float_with_spec(s, static_cast<double>(args), *spec);
                    return;
                }

                // Other types: format into a temporary string, then apply
                // alignment and width.
                {
                    growing_sink gs;
                    format_value(gs, args);
                    const std::string& tmp = gs.buffer();
                    std::size_t len = tmp.size();

                    // Apply precision truncation for strings when provided.
                    std::size_t effective_len = len;
                    if (spec->precision_set && spec->precision < effective_len) {
                        effective_len = spec->precision;
                    }

                    std::size_t width = spec->width;
                    char fill_char = spec->fill ? spec->fill : ' ';

                    if (effective_len < width) {
                        std::size_t padding = width - effective_len;
                        if (spec->align == '<') {
                            s.write(tmp.data(), effective_len);
                            for (std::size_t i = 0; i < padding; ++i) s.write(&fill_char, 1);
                        } else if (spec->align == '^') {
                            // Center align: bias extra padding to the left.
                            std::size_t left = (padding + 1) / 2;
                            std::size_t right = padding - left;
                            for (std::size_t i = 0; i < left; ++i) s.write(&fill_char, 1);
                            s.write(tmp.data(), effective_len);
                            for (std::size_t i = 0; i < right; ++i) s.write(&fill_char, 1);
                        } else {
                            // Right align (default).
                            for (std::size_t i = 0; i < padding; ++i) s.write(&fill_char, 1);
                            s.write(tmp.data(), effective_len);
                        }
                    } else {
                        s.write(tmp.data(), effective_len);
                    }
                }
            }...
        };

        while (*fmt) {
            if (*fmt == '{') {
                if (*(fmt + 1) == '{') {
                    sink.write("{", 1);
                    fmt += 2;
                    continue;
                }

                const char* start = fmt + 1;
                const char* end = start;
                while (*end && *end != '}') ++end;
                if (*end == '}') {
                    if (start == end) {
                        if (arg_index < static_cast<int>(formatters.size())) {
                            formatters[arg_index++](sink, nullptr);
                        }
                    } else {
                        if (*start == ':' && arg_index < static_cast<int>(formatters.size())) {
                            format_spec spec;
                            const char* to_parse = start + 1;
                            std::size_t consumed = format_spec::parse(to_parse, spec);
                            const char* after = to_parse + consumed;
                            if (after == end) {
                                formatters[arg_index++](sink, &spec);
                            } else {
                                sink.write("{", 1);
                            }
                        } else {
                            sink.write("{", 1);
                        }
                    }
                    fmt = end + 1;
                    continue;
                } else {
                    sink.write("{", 1);
                    ++fmt;
                    continue;
                }
            }

            if (*fmt == '}') {
                if (*(fmt + 1) == '}') {
                    sink.write("}", 1);
                    fmt += 2;
                    continue;
                }
                sink.write("}", 1);
                ++fmt;
                continue;
            }

            sink.write(fmt, 1);
            ++fmt;
        }
    }

}  // namespace detail

// Formats the arguments according to the format string and writes the result
// to the given buffer sink. Supports format specifications such as {},
// {:10}, {:>20}, {:*^15}, {:0>10}, etc.
template <typename... Args>
void format_to(buffer_sink& sink, const char* fmt, Args&&... args) {
    detail::format_impl(sink, fmt, std::forward<Args>(args)...);
}

// Specialization for a single integer argument. Avoids the overhead of the
// generic variadic path when only one integral value needs formatting.
template <typename T>
typename std::enable_if<std::is_integral_v<T>>::type
format_to(buffer_sink& sink, const char* fmt, T value)
{
    const char* p = fmt;
    while (*p) {
        if (*p == '{') {
            if (*(p + 1) == '{') {
                sink.write("{", 1);
                p += 2;
                continue;
            }

            const char* start = p + 1;
            const char* end = start;
            while (*end && *end != '}') ++end;
            if (*end == '}') {
                if (start == end) {
                    char temp[64];
                    std::size_t len = 0;
                    if constexpr (std::is_signed_v<T>) {
                        len = detail::integer_formatter::format_int64(temp, sizeof(temp), static_cast<int64_t>(value));
                    } else {
                        len = detail::integer_formatter::format_uint64(temp, sizeof(temp), static_cast<uint64_t>(value));
                    }
                    if (len > 0) sink.write(temp, len);
                } else if (*start == ':') {
                    detail::format_spec spec;
                    const char* to_parse = start + 1;
                    std::size_t consumed = detail::format_spec::parse(to_parse, spec);
                    const char* after = to_parse + consumed;
                    if (after == end) {
                        detail::format_int_with_spec(sink, static_cast<int64_t>(value), spec);
                    } else {
                        sink.write("{", 1);
                    }
                } else {
                    sink.write("{", 1);
                }

                p = end + 1;
                continue;
            } else {
                sink.write("{", 1);
                ++p;
                continue;
            }
        }

        if (*p == '}') {
            if (*(p + 1) == '}') {
                sink.write("}", 1);
                p += 2;
                continue;
            }
            sink.write("}", 1);
            ++p;
            continue;
        }

        sink.write(p, 1);
        ++p;
    }
}

}  // namespace fl

#endif  // FL_FORMAT_HPP
