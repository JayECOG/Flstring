// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_FORMAT_HPP
#define FL_FORMAT_HPP

// Type-safe formatting engine with Python/std::format-style placeholders.
// Supports alignment, padding, width, precision, and base conversions for
// integral and floating-point types. Output is written through a sink
// abstraction so callers can target fixed buffers or growing storage.

#include "fl/config.hpp"
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <limits>
#include <stdexcept>
#include <functional>
#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <utility>
#include <cstdlib>
#include <ostream>
#include <streambuf>
#include "fl/profiling.hpp"
#include "fl/detail/float_format.hpp"
#include "sinks.hpp"
#include <streambuf>

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

private:
    std::string _buffer;
    std::size_t _size;
};

/// @brief Adapter that wraps any sink_base as a std::streambuf, enabling
/// use of std::ostream formatting operators.
class sink_streambuf : public std::streambuf {
    sink_base* _sink;
public:
    explicit sink_streambuf(sink_base* s) noexcept : _sink(s) {}
protected:
    int_type overflow(int_type c) override {
        if (c != EOF) {
            char ch = static_cast<char>(c);
            _sink->write(&ch, 1);
        }
        return c;
    }
};

// Stateless utility for converting integers to decimal strings without any
// heap allocation.
class integer_formatter {
public:
    FL_INLINE static std::size_t format_int64(char* buffer, std::size_t capacity, int64_t value) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;

        if (FL_UNLIKELY(value == 0)) {
            buffer[0] = '0';
            return 1;
        }

        bool negative = value < 0;
        uint64_t uvalue = negative
            ? (value == std::numeric_limits<int64_t>::min()
                   ? (uint64_t(1) << 63)
                   : static_cast<uint64_t>(-value))
            : static_cast<uint64_t>(value);

        char temp[20];
        std::size_t len = 0;
        while (uvalue > 0) {
            temp[len++] = '0' + (uvalue % 10);
            uvalue /= 10;
        }

        if (FL_UNLIKELY(negative && capacity > len)) {
            buffer[0] = '-';
            std::reverse_copy(temp, temp + len, buffer + 1);
            return len + 1;
        }

        std::reverse_copy(temp, temp + len, buffer);
        return len;
    }

    FL_INLINE static std::size_t format_uint64(char* buffer, std::size_t capacity, uint64_t value) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;

        if (FL_UNLIKELY(value == 0)) {
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
};

}  // namespace detail

// Reuse the sinks' buffer_sink implementation to avoid duplicate symbols.
// sinks.hpp is included before this header via fl.hpp, so import the
// implementation here for formatting APIs.
using buffer_sink = sinks::buffer_sink;

// Forward declaration of formatter<T> so detail code can reference it.
template <typename T, typename>
struct formatter;

// Format implementation for common types.
namespace detail {

    // Helper for dependent static_assert in template code.
    template <typename T>
    inline constexpr bool always_false_v = false;

    // Formats a single value and writes it to the sink.
    template <typename Sink, typename T>
    void format_value(Sink& sink, T value) {
        char temp[64];
        std::size_t len = 0;

        if constexpr (std::is_same_v<std::decay_t<T>, const char*> || std::is_same_v<std::decay_t<T>, char*>) {
            if (!value) {
                throw std::invalid_argument("fl::format_to: null C-string argument");
            }
            sink.write(value, std::strlen(value));
        } else if constexpr (std::is_same_v<std::decay_t<T>, std::string> ||
                             std::is_same_v<std::decay_t<T>, std::string_view> ||
                             std::is_same_v<std::decay_t<T>, fl::string>) {
            std::string_view sv(value);
            sink.write(sv.data(), sv.size());
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
            // Pass float/double directly to preserve correct precision.
            // Casting float to double produces spurious extra digits
            // (e.g., 3.14f -> 3.140000104904175 instead of 3.14).
            len = float_formatter::format_shortest(temp, sizeof(temp), value);
            if (len > 0) {
                sink.write(temp, len);
            }
        } else {
            static_assert(always_false_v<T>, "Unsupported type for formatting");
        }
    }

    // Parsed representation of a Python/std::format-style format specification
    // string such as ">20", "*^15", or "0>10x". Supports fill character,
    // alignment, sign, base prefix, width, precision, and type specifier.
    struct format_spec {
    char fill = ' ';
    char align = '\0';                  // '<' (left), '>' (right), '^' (center), '=' (numeric padding).
    bool sign = false;
    bool sign_space = false;            // Leading space for positive numbers.
    bool show_base = false;             // Show 0x, 0b, 0 prefix for integers.
    std::size_t width = 0;
    std::size_t precision = 6;
    bool precision_set = false;         // True when precision was explicitly provided.
    bool dynamic_width = false;         // Width comes from the next auto-indexed argument.
    bool dynamic_precision = false;     // Precision comes from the next auto-indexed argument.
    char type = '\0';                   // Type specifier: d, x, b, o, f, e, g, s, c.

    // Parses the format specification starting at spec_start and populates
    // the given spec struct. Returns the number of characters consumed.
    // Must be constexpr for use in consteval compile().
    static constexpr std::size_t parse(const char* spec_start, format_spec& spec) {
        const char* p = spec_start;

        // Check for sign (+). Allow sign before or after fill+align.
        bool sign_seen = false;
        if (*p == '+') {
            spec.sign = true;
            sign_seen = true;
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

        if (!sign_seen && *p == '+') {
            spec.sign = true;
            ++p;
        }

        // Check for base prefix (#).
        if (*p == '#') {
            spec.show_base = true;
            ++p;
        }

        // Parse width (literal or dynamic from argument).
        if (*p == '{') {
            spec.dynamic_width = true;
            ++p;  // skip '{'
            if (*p != '}') {
                throw std::invalid_argument("fl::format_to: dynamic width must use {}");
            }
            ++p;  // skip '}'
        } else {
            while (*p && *p >= '0' && *p <= '9') {
                spec.width = spec.width * 10 + (*p - '0');
                ++p;
            }
        }

        // Parse precision (literal or dynamic from argument).
        if (*p == '.') {
            ++p;
            if (*p == '{') {
                spec.dynamic_precision = true;
                ++p;  // skip '{'
                if (*p != '}') {
                    throw std::invalid_argument("fl::format_to: dynamic precision must use {}");
                }
                ++p;  // skip '}'
            } else {
                spec.precision = 0;
                spec.precision_set = true;
                while (*p && *p >= '0' && *p <= '9') {
                    spec.precision = spec.precision * 10 + (*p - '0');
                    ++p;
                }
            }
        }

        // Parse type specifier.
        if (*p && (*p == 'd' || *p == 'x' || *p == 'X' || *p == 'b' || *p == 'B' ||
                   *p == 'o' || *p == 'f' || *p == 'F' || *p == 'e' || *p == 'E' ||
                   *p == 'g' || *p == 'G' || *p == 's' || *p == 'c' || *p == '?')) {
            spec.type = *p;
            ++p;
        }

        return p - spec_start;
    }
};

struct parsed_placeholder {
    bool has_index = false;
    std::size_t index = 0;
    format_spec spec{};
};

inline parsed_placeholder parse_placeholder(std::string_view placeholder) {
    parsed_placeholder result;
    std::size_t pos = 0;

    while (pos < placeholder.size() && placeholder[pos] >= '0' && placeholder[pos] <= '9') {
        result.has_index = true;
        result.index = result.index * 10 + static_cast<std::size_t>(placeholder[pos] - '0');
        ++pos;
    }

    if (pos < placeholder.size()) {
        if (placeholder[pos] != ':') {
            throw std::invalid_argument("fl::format_to: invalid format placeholder");
        }

        ++pos;
        std::size_t consumed = format_spec::parse(placeholder.data() + pos, result.spec);
        pos += consumed;
    }

    if (pos != placeholder.size()) {
        throw std::invalid_argument("fl::format_to: invalid format placeholder");
    }

    return result;
}

template <typename Sink, typename FormatterArray>
void format_impl_dispatch(Sink& sink, std::string_view fmt, const FormatterArray& formatters,
                          const std::function<long long()>* extractors = nullptr,
                          std::size_t extractor_count = 0) {
    std::size_t next_index = 0;
    bool saw_explicit_index = false;
    bool saw_implicit_index = false;

    auto emit_char = [&sink](char ch) {
        sink.write(&ch, 1);
    };

    for (std::size_t i = 0; i < fmt.size(); ) {
        char ch = fmt[i];

        if (ch == '{') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
                emit_char('{');
                i += 2;
                continue;
            }

            // Brace-depth-aware scan: handle nested {} like {:{}}.
            std::size_t end = i + 1;
            int brace_depth = 1;
            while (end < fmt.size() && brace_depth > 0) {
                if (fmt[end] == '{') ++brace_depth;
                else if (fmt[end] == '}') --brace_depth;
                if (brace_depth > 0) ++end;
            }
            if (end >= fmt.size()) {
                throw std::invalid_argument("fl::format_to: unmatched '{'");
            }

            std::string_view placeholder = fmt.substr(i + 1, end - (i + 1));
            if (placeholder.empty()) {
                saw_implicit_index = true;
                if (saw_explicit_index) {
                    throw std::invalid_argument("fl::format_to: cannot mix automatic and explicit argument indices");
                }
                if (next_index >= formatters.size()) {
                    throw std::invalid_argument("fl::format_to: argument index out of range");
                }
                formatters[next_index++](sink, nullptr);
            } else {
                parsed_placeholder parsed = parse_placeholder(placeholder);
                if (parsed.has_index) {
                    saw_explicit_index = true;
                    if (saw_implicit_index) {
                        throw std::invalid_argument("fl::format_to: cannot mix automatic and explicit argument indices");
                    }
                    if (parsed.spec.dynamic_width || parsed.spec.dynamic_precision) {
                        throw std::invalid_argument("fl::format_to: dynamic width/precision with explicit index not supported");
                    }
                    if (parsed.index >= formatters.size()) {
                        throw std::invalid_argument("fl::format_to: argument index out of range");
                    }
                    formatters[parsed.index](sink, &parsed.spec);
                } else {
                    saw_implicit_index = true;
                    if (saw_explicit_index) {
                        throw std::invalid_argument("fl::format_to: cannot mix automatic and explicit argument indices");
                    }

                    // Handle dynamic width/precision from arguments.
                    // Dynamic args come AFTER the value args in the argument list.
                    format_spec resolved = parsed.spec;
                    std::size_t dyn_count = (resolved.dynamic_width ? 1 : 0)
                                          + (resolved.dynamic_precision ? 1 : 0);
                    if (resolved.dynamic_width) {
                        // Width comes from the arg AFTER the value arg.
                        std::size_t wi = next_index + 1;
                        if (!extractors || wi >= extractor_count) {
                            throw std::invalid_argument("fl::format_to: argument index out of range for dynamic width");
                        }
                        long long w = extractors[wi]();
                        resolved.width = (w >= 0) ? static_cast<std::size_t>(w) : 0;
                    }
                    if (resolved.dynamic_precision) {
                        // Precision comes from the arg after the value and width.
                        std::size_t pi = next_index + (resolved.dynamic_width ? 2 : 1);
                        if (!extractors || pi >= extractor_count) {
                            throw std::invalid_argument("fl::format_to: argument index out of range for dynamic precision");
                        }
                        long long p = extractors[pi]();
                        if (p >= 0) {
                            resolved.precision = static_cast<std::size_t>(p);
                            resolved.precision_set = true;
                        } else {
                            resolved.precision_set = false;
                        }
                    }
                    if (next_index >= formatters.size()) {
                        throw std::invalid_argument("fl::format_to: argument index out of range");
                    }
                    // Consume the value arg itself plus the dynamic args.
                    formatters[next_index](sink, &resolved);
                    next_index += 1 + dyn_count;
                }
            }

            i = end + 1;
            continue;
        }

        if (ch == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') {
                emit_char('}');
                i += 2;
                continue;
            }
            throw std::invalid_argument("fl::format_to: unmatched '}'");
        }

        std::size_t start = i;
        while (i < fmt.size() && fmt[i] != '{' && fmt[i] != '}') {
            ++i;
        }
        // Bounds-safe write: i - start never exceeds fmt.size() - start
        // because the while loop stops at i == fmt.size() or at a brace.
        // The std::string_view fmt guarantees a contiguous source buffer
        // of at least fmt.size() bytes starting at fmt.data().
        if (FL_LIKELY(i > start)) {
            std::size_t literal_len = i - start;
            (void)fmt.data();  // ensure null pointer is not being used (assert elsewhere)
            sink.write(fmt.data() + start, literal_len);
        }
    }
}

// Internal: formats a uint64_t absolute value with an explicit sign flag.
// Handles base conversion, prefix, alignment, and padding.
template <typename Sink>
void format_int_with_spec_impl(Sink& sink, uint64_t abs_value, bool is_negative, const format_spec& spec) {
    char digits[128];
    std::size_t digit_len = 0;
    const char* prefix = "";
    std::size_t prefix_len = 0;

    char base_char = spec.type ? spec.type : 'd';
    int base = 10;

    switch (base_char) {
        case 'x':
            base = 16;
            if (spec.show_base && abs_value != 0) { prefix = "0x"; prefix_len = 2; }
            break;
        case 'X':
            base = 16;
            if (spec.show_base && abs_value != 0) { prefix = "0X"; prefix_len = 2; }
            break;
        case 'b':
        case 'B':
            base = 2;
            if (spec.show_base && abs_value != 0) { prefix = "0b"; prefix_len = 2; }
            break;
        case 'o':
            base = 8;
            if (spec.show_base && abs_value != 0) { prefix = "0"; prefix_len = 1; }
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
        } else if (spec.align == '=' || (spec.align == '\0' && spec.fill != ' ' && spec.fill != '\0')) {
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

// Formats a signed integer value according to the given format_spec.
template <typename Sink>
void format_int_with_spec(Sink& sink, int64_t value, const format_spec& spec) {
    bool is_negative = value < 0;
    uint64_t abs_value = is_negative
        ? (value == std::numeric_limits<int64_t>::min()
               ? (uint64_t(1) << 63)
               : static_cast<uint64_t>(-value))
        : static_cast<uint64_t>(value);
    format_int_with_spec_impl(sink, abs_value, is_negative, spec);
}

// Formats an unsigned integer value according to the given format_spec.
template <typename Sink>
void format_uint_with_spec(Sink& sink, uint64_t value, const format_spec& spec) {
    format_int_with_spec_impl(sink, value, false, spec);
}

// Formats a floating-point value according to the given format_spec,
// handling precision, format character (e, f, g), alignment, and padding.
template <typename Sink>
void format_float_with_spec(Sink& sink, double value, const format_spec& spec) {
    char temp[256];
    std::size_t len = 0;

    char fmt_char = spec.type ? spec.type : '\0';
    // When precision is set but no type is given, default to 'f'
    // (fmtlib compatibility: {:.2} on 3.14159 produces "3.14").
    if (fmt_char == '\0' && spec.precision_set) {
        fmt_char = 'f';
    }

    char fmt_buffer[32];

    // Build the snprintf format string, incorporating the sign flag.
    // '+' forces display of the sign for non-negative values.
    // ' ' displays a leading space for positive values (snprintf uses ' ' sign flag).
    const char* sign_prefix = "";
    if (spec.sign) {
        sign_prefix = "+";
    } else if (spec.sign_space) {
        sign_prefix = " ";
    }
    switch (fmt_char) {
        case 'e':
        case 'E':
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%%s.%zu%c", sign_prefix, spec.precision, fmt_char);
            break;
        case 'f':
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%%s.%zuf", sign_prefix, spec.precision);
            break;
        case 'F':
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%%s.%zuF", sign_prefix, spec.precision);
            break;
        case 'g':
        case 'G':
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%%s.%zu%c", sign_prefix, spec.precision, fmt_char);
            break;
        default:
            std::snprintf(fmt_buffer, sizeof(fmt_buffer), "%%%sg", sign_prefix);
            break;
    }

    int ret = std::snprintf(temp, sizeof(temp), fmt_buffer, value);
    len = (ret > 0) ? static_cast<std::size_t>(ret) : 0;
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

template <typename Sink>
void format_text_with_spec(Sink& sink, std::string_view text, const format_spec& spec) {
    std::size_t effective_len = text.size();
    if (spec.precision_set && spec.precision < effective_len) {
        effective_len = spec.precision;
    }

    std::size_t width = spec.width;
    char fill_char = spec.fill ? spec.fill : ' ';

    if (effective_len < width) {
        std::size_t padding = width - effective_len;
        if (spec.align == '<') {
            sink.write(text.data(), effective_len);
            for (std::size_t i = 0; i < padding; ++i) sink.write(&fill_char, 1);
        } else if (spec.align == '^') {
            std::size_t left = (padding + 1) / 2;
            std::size_t right = padding - left;
            for (std::size_t i = 0; i < left; ++i) sink.write(&fill_char, 1);
            sink.write(text.data(), effective_len);
            for (std::size_t i = 0; i < right; ++i) sink.write(&fill_char, 1);
        } else {
            for (std::size_t i = 0; i < padding; ++i) sink.write(&fill_char, 1);
            sink.write(text.data(), effective_len);
        }
    } else {
        sink.write(text.data(), effective_len);
    }
}

    /// @brief Detects at compile time whether formatter<T> has been specialised.
    template <typename T, typename = void>
    struct has_formatter_impl : std::false_type {};

    template <typename T>
    struct has_formatter_impl<T, std::void_t<decltype(std::declval<fl::formatter<std::decay_t<T>, void>&>().parse(std::string_view{}))>>
        : std::true_type {};

    template <typename T>
    inline constexpr bool has_formatter_v = has_formatter_impl<T>::value;

    /// @brief Detects whether `format_as(const T&)` is a valid ADL customisation point.
    template <typename T, typename = void>
    struct has_format_as_impl : std::false_type {};

    template <typename T>
    struct has_format_as_impl<T, std::void_t<decltype(
        format_as(std::declval<const std::decay_t<T>&>())
    )>> : std::true_type {};

    template <typename T>
    inline constexpr bool has_format_as_v = has_format_as_impl<T>::value;

    /// @brief Detects string-like types (std::string, std::string_view).
    template <typename T>
    inline constexpr bool is_std_string_v =
        std::is_same_v<std::decay_t<T>, std::string> ||
        std::is_same_v<std::decay_t<T>, std::string_view>;

    /// @brief Detects C-string pointer types (const char*, char*).
    template <typename T>
    inline constexpr bool is_char_ptr_v =
        std::is_same_v<std::decay_t<T>, const char*> ||
        std::is_same_v<std::decay_t<T>, char*>;

    /// @brief Writes a string with escape sequences for non-printable characters,
    /// enclosed in double quotes. Supports the '?' debug specifier.
    template <typename Sink>
    void write_escaped_string(Sink& sink, std::string_view text) {
        sink.write("\"", 1);  // opening quote
        for (char ch : text) {
            switch (ch) {
                case '\n': sink.write("\\n", 2); break;
                case '\t': sink.write("\\t", 2); break;
                case '\r': sink.write("\\r", 2); break;
                case '\\': sink.write("\\\\", 2); break;
                case '\"': sink.write("\\\"", 2); break;
                case '\0': sink.write("\\0", 2); break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20) {
                        char buf[5] = {'\\', 'x',
                            "0123456789ABCDEF"[(ch >> 4) & 0xF],
                            "0123456789ABCDEF"[ch & 0xF],
                            '\0'};
                        sink.write(buf, 4);
                    } else {
                        sink.write(&ch, 1);
                    }
                    break;
            }
        }
        sink.write("\"", 1);  // closing quote
    }

    /// @brief Writes a single character with escape sequences where needed,
    /// enclosed in single quotes. Supports the '?' debug specifier.
    template <typename Sink>
    void write_escaped_char(Sink& sink, char ch) {
        sink.write("'", 1);
        switch (ch) {
            case '\n': sink.write("\\n", 2); break;
            case '\t': sink.write("\\t", 2); break;
            case '\r': sink.write("\\r", 2); break;
            case '\\': sink.write("\\\\", 2); break;
            case '\'': sink.write("\\'", 2); break;
            case '\0': sink.write("\\0", 2); break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[5] = {'\\', 'x',
                        "0123456789ABCDEF"[(ch >> 4) & 0xF],
                        "0123456789ABCDEF"[ch & 0xF], '\0'};
                    sink.write(buf, 4);
                } else {
                    sink.write(&ch, 1);
                }
                break;
        }
        sink.write("'", 1);
    }

template <typename Sink, typename T>
void format_argument(Sink& sink, T&& value, const format_spec* spec) {
    using value_type = std::decay_t<T>;

    // Check for custom formatter<T> specialisation first (highest priority).
    if constexpr (has_formatter_v<value_type>) {
        fl::formatter<value_type, void> f;
        f.format(sink, std::forward<T>(value), spec);
        return;
    }

    if (!spec) {
        // Check for format_as ADL customisation before falling back to
        // format_value (which has a static_assert for non-built-in types).
        if constexpr (has_format_as_v<value_type>) {
            format_argument(sink, format_as(static_cast<const value_type&>(std::forward<T>(value))), spec);
            return;
        }
        format_value(sink, std::forward<T>(value));
        return;
    }

    if constexpr (std::is_same_v<value_type, bool>) {
        format_text_with_spec(sink, value ? std::string_view("true") : std::string_view("false"), *spec);
    } else if constexpr (std::is_same_v<value_type, char>) {
        if (spec->type == '?') {
            write_escaped_char(sink, value);
        } else {
            char ch = value;
            format_text_with_spec(sink, std::string_view(&ch, 1), *spec);
        }
    } else if constexpr (std::is_integral_v<value_type>) {
        if constexpr (std::is_signed_v<value_type>) {
            format_int_with_spec(sink, static_cast<int64_t>(value), *spec);
        } else {
            format_uint_with_spec(sink, static_cast<uint64_t>(value), *spec);
        }
    } else if constexpr (std::is_floating_point_v<value_type>) {
        format_float_with_spec(sink, static_cast<double>(value), *spec);
    } else if constexpr (is_std_string_v<value_type>) {
        std::string_view sv(value);
        if (spec->type == '?') {
            write_escaped_string(sink, sv);
        } else {
            format_text_with_spec(sink, sv, *spec);
        }
    } else if constexpr (is_char_ptr_v<value_type>) {
        if (!value) {
            throw std::invalid_argument("fl::format_to: null C-string argument");
        }
        std::string_view sv(value, std::strlen(value));
        if (spec->type == '?') {
            write_escaped_string(sink, sv);
        } else {
            format_text_with_spec(sink, sv, *spec);
        }
    } else if constexpr (has_format_as_v<value_type>) {
        // Check for format_as ADL customisation (after built-in types, before fallback).
        format_argument(sink, format_as(static_cast<const value_type&>(std::forward<T>(value))), spec);
    } else {
        growing_sink gs;
        format_value(gs, std::forward<T>(value));
        const std::string& tmp = gs.buffer();
        format_text_with_spec(sink, std::string_view(tmp.data(), tmp.size()), *spec);
    }
}

// Small-buffer-optimised function wrapper for the formatter array.
// Avoids std::function's heap allocation for small callables such as
// lambdas that capture a single reference (one or two pointers).
template <typename Sink>
class format_fn {
    using invoke_fn = void (*)(const void*, Sink&, const format_spec*);
    using destroy_fn = void (*)(void*);

    alignas(void*) char _storage[4 * sizeof(void*)];  // 32-byte SBO on x64
    invoke_fn _invoke = nullptr;
    destroy_fn _destroy = nullptr;

    template <typename Fn>
    static void invoke_stub(const void* data, Sink& s, const format_spec* spec) {
        (*static_cast<const Fn*>(data))(s, spec);
    }

    template <typename Fn>
    static void destroy_stub(void* data) {
        static_cast<Fn*>(data)->~Fn();
    }

public:
    template <typename Fn>
    format_fn(Fn&& fn) {
        using decayed = std::decay_t<Fn>;
        static_assert(sizeof(decayed) <= sizeof(_storage),
                      "Callable too large for format_fn SBO buffer");
        ::new (_storage) decayed(std::forward<Fn>(fn));
        _invoke = &invoke_stub<decayed>;
        _destroy = &destroy_stub<decayed>;
    }

    format_fn(const format_fn&) = delete;
    format_fn& operator=(const format_fn&) = delete;

    format_fn(format_fn&& other) noexcept {
        std::memcpy(_storage, other._storage, sizeof(_storage));
        _invoke = other._invoke;
        _destroy = other._destroy;
        other._invoke = nullptr;
        other._destroy = nullptr;
    }

    ~format_fn() {
        if (_destroy) _destroy(_storage);
    }

    void operator()(Sink& s, const format_spec* spec) const {
        if (_invoke) _invoke(_storage, s, spec);
    }
};

// Parses the format string, matches each "{}" or "{:spec}" placeholder to the
// corresponding argument, and writes the formatted output to the sink.
template <typename Sink, typename... Args>
void format_impl(Sink& sink, std::string_view fmt, Args&&... args) {
    // Use a direct function-based approach to avoid lambda ADL issues with GCC
    auto do_format = [&sink](auto& arg, const format_spec* spec) {
        using raw_type = std::decay_t<decltype(arg)>;
        // Check for format_as ADL customisation inside this lambda context
        // where ADL works reliably (vs inside format_argument through std::function).
        if constexpr (requires(const raw_type& t) { format_as(t); }) {
            format_argument(sink, format_as(static_cast<const raw_type&>(arg)), spec);
        }
        // Check for custom formatter<T> specialisation
        else if constexpr (requires { std::declval<fl::formatter<raw_type, void>&>().parse(std::string_view{}); }) {
            fl::formatter<raw_type, void> f;
            f.format(sink, arg, spec);
        } else {
            format_argument(sink, arg, spec);
        }
    };
    using format_spec_t = detail::format_spec;
    using formatter_t = format_fn<Sink>;
    using extractor_t = std::function<long long()>;
    const std::array<formatter_t, sizeof...(Args)> formatters{
        formatter_t{[&args, &do_format](Sink& s, const format_spec* spec) {
            do_format(args, spec);
        }}...
    };
    // Build extractors for dynamic width/precision.
    // Each extractor captures the argument and tries to get its numeric value
    // by formatting to a temporary growing_sink and parsing the output.
    const std::array<extractor_t, sizeof...(Args)> extractors{
        extractor_t{[&args]() -> long long {
            detail::growing_sink gs;
            detail::format_value(gs, args);
            const std::string& s = gs.buffer();
            if (!s.empty()) {
                char* end = nullptr;
                long long val = std::strtoll(s.c_str(), &end, 10);
                if (end != s.c_str()) return val;
            }
            return 0;
        }}...
    };

    format_impl_dispatch(sink, fmt, formatters, extractors.data(), extractors.size());
}

// ---------------------------------------------------------------------------
// TASK 1: Compile-time format string checking via format_string<Args...>
// ---------------------------------------------------------------------------

/// @brief Checks that brace characters in a format string are properly
/// balanced.  Throws on mismatch, which in a consteval context becomes a
/// compile-time error.
consteval void check_balanced_braces(const char* s) {
    int depth = 0;
    if (!s) throw;  // null pointer
    for (const char* p = s; *p; ++p) {
        if (*p == '{') {
            if (*(p + 1) == '{') { ++p; continue; }
            ++depth;
        } else if (*p == '}') {
            if (*(p + 1) == '}') { ++p; continue; }
            --depth;
            if (depth < 0) throw;  // unmatched closing brace
        }
    }
    if (depth != 0) throw;  // unmatched opening brace
}

/// @brief Wraps a format string and validates it at compile time when
/// constructed from a string literal.  Runtime construction from a
/// std::string_view bypasses validation.
template <typename... Args>
struct format_string {
    std::string_view fmt;

    /// @brief consteval constructor for string literals.  Validates brace
    /// balance at compile time.
    consteval format_string(const char* s) : fmt(s) {
        check_balanced_braces(s);
    }

    /// @brief Runtime fallback — no validation performed.
    format_string(std::string_view s) noexcept : fmt(s) {}

    /// @brief Implicit conversion from string literals for seamless usage.
    template <typename CharT, std::size_t N>
        requires std::is_same_v<CharT, char>
    consteval format_string(const CharT (&s)[N]) : fmt(s, N - 1) {
        check_balanced_braces(s);
    }
};

// ---------------------------------------------------------------------------
// TASK 2: Dynamic argument store + vformat_to / vformat
// ---------------------------------------------------------------------------

/// @brief Abstract base for a type-erased format argument.  Each concrete
/// subclass knows how to format its value to a sink_base.
struct format_arg_base {
    virtual ~format_arg_base() = default;
    virtual void format(sink_base&, const format_spec*) const = 0;
    virtual std::unique_ptr<format_arg_base> clone() const = 0;
};

/// @brief Lightweight adapter that wraps a fl::sinks::output_sink (the
/// external sink base) as a detail::sink_base (the internal sink base),
/// allowing the virtual dispatch path (used by vformat) to write through
/// either sink hierarchy.
struct output_sink_adapter : sink_base {
    fl::sinks::output_sink& inner;
    explicit output_sink_adapter(fl::sinks::output_sink& s) noexcept : inner(s) {}
    void write(const char* data, std::size_t len) override {
        inner.write(data, len);
    }
};

/// @brief Concrete implementation of format_arg_base for a specific type T.
template <typename T>
struct format_arg_impl : format_arg_base {
    T value;

    template <typename U>
    explicit format_arg_impl(U&& v) : value(std::forward<U>(v)) {}

    void format(sink_base& s, const format_spec* spec) const override {
        // Delegate to format_argument which handles all built-in types,
        // custom formatters, and format_as ADL customisation.
        format_argument(s, value, spec);
    }

    std::unique_ptr<format_arg_base> clone() const override {
        return std::make_unique<format_arg_impl>(value);
    }
};

/// @brief Holds a heterogeneous collection of format arguments in a
/// type-safe manner, enabling runtime vformat dispatch.
class dynamic_format_arg_store {
    std::vector<std::unique_ptr<format_arg_base>> _args;
public:
    dynamic_format_arg_store() = default;

    /// @brief Appends an argument, type-erasing it behind format_arg_base.
    template <typename T>
    void push_back(T&& value) {
        _args.push_back(
            std::make_unique<format_arg_impl<std::decay_t<T>>>(std::forward<T>(value)));
    }

    std::size_t size() const noexcept { return _args.size(); }
    bool empty() const noexcept { return _args.empty(); }

    void clear() { _args.clear(); }

    /// @brief Retrieves the argument at index i.
    /// @pre i < size()
    const format_arg_base& get(std::size_t i) const {
        return *_args[i];
    }
};

// ---------------------------------------------------------------------------
// vformat implementation detail — walks the format string and dispatches
// to the dynamic argument store.
// ---------------------------------------------------------------------------

void vformat_to_impl(sink_base& sink, std::string_view fmt,
                     const dynamic_format_arg_store& args) {
    std::size_t arg_idx = 0;
    bool saw_explicit_index = false;
    bool saw_implicit_index = false;

    for (std::size_t i = 0; i < fmt.size(); ) {
        if (fmt[i] == '{') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
                sink.write("{", 1);
                i += 2;
                continue;
            }

            // Brace-depth-aware scan for the matching close brace.
            std::size_t end = i + 1;
            int brace_depth = 1;
            while (end < fmt.size() && brace_depth > 0) {
                if (fmt[end] == '{') ++brace_depth;
                else if (fmt[end] == '}') --brace_depth;
                if (brace_depth > 0) ++end;
            }
            if (end >= fmt.size()) {
                throw std::invalid_argument(
                    "fl::vformat_to: unmatched '{'");
            }

            std::string_view content = fmt.substr(i + 1, end - i - 1);
            const format_spec* spec_ptr = nullptr;
            format_spec resolved_spec{};

            if (!content.empty()) {
                parsed_placeholder parsed = parse_placeholder(content);
                if (parsed.has_index) {
                    if (saw_implicit_index) {
                        throw std::invalid_argument(
                            "fl::vformat_to: cannot mix automatic and "
                            "explicit argument indices");
                    }
                    saw_explicit_index = true;
                    arg_idx = parsed.index;
                } else {
                    if (saw_explicit_index) {
                        throw std::invalid_argument(
                            "fl::vformat_to: cannot mix automatic and "
                            "explicit argument indices");
                    }
                    saw_implicit_index = true;
                }
                // Check whether the parsed spec differs from default.
                if (parsed.spec.precision_set || parsed.spec.width != 0 ||
                    parsed.spec.align != '\0' || parsed.spec.type != '\0' ||
                    parsed.spec.fill != ' ' || parsed.spec.sign ||
                    parsed.spec.show_base) {
                    resolved_spec = parsed.spec;
                    spec_ptr = &resolved_spec;
                }
            } else {
                saw_implicit_index = true;
                spec_ptr = nullptr;
            }

            if (arg_idx >= args.size()) {
                throw std::invalid_argument(
                    "fl::vformat_to: argument index out of range");
            }
            args.get(arg_idx++).format(sink, spec_ptr);
            i = end + 1;
            continue;
        }

        if (fmt[i] == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') {
                sink.write("}", 1);
                i += 2;
                continue;
            }
            throw std::invalid_argument("fl::vformat_to: unmatched '}'");
        }

        std::size_t start = i;
        while (i < fmt.size() && fmt[i] != '{' && fmt[i] != '}') ++i;
        sink.write(fmt.data() + start, i - start);
    }
}

// ---------------------------------------------------------------------------
// TASK 3: Compiled format strings
// ---------------------------------------------------------------------------

/// @brief A single segment of a pre-parsed (compiled) format string.  Each
/// segment is either a literal text portion or a placeholder with an optional
/// format_spec.
struct compiled_segment {
    std::string_view literal;       ///< Literal text (valid when !is_placeholder)
    format_spec spec;               ///< Format specification (valid when has_spec)
    bool is_placeholder = false;    ///< True if this segment is a placeholder
    bool has_spec = false;          ///< True if the placeholder has a format spec
};

/// @brief Fixed-capacity array of compiled_segment that stores the result of
/// compile-time format string parsing.
template <std::size_t N>
struct compiled_format {
    std::array<compiled_segment, N> segments{};
    std::size_t segments_count = 0;   ///< Number of populated segments
};

/// @brief Parses a format string at compile time, producing a compiled_format
/// that can be dispatched at runtime without re-parsing.
/// @param fmt The format string view.
/// @return compiled_format with up to MaxSegments segments.
/// @throws On unbalanced braces or too many segments (compile-time error
/// in consteval context).
template <std::size_t MaxSegments = 32>
consteval auto compile(std::string_view fmt) {
    compiled_format<MaxSegments> result;
    std::size_t seg_idx = 0;

    for (std::size_t i = 0; i < fmt.size() && seg_idx < MaxSegments; ) {
        if (fmt[i] == '{') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '{') {
                // Escaped brace — emit literal '{'.
                auto& seg = result.segments[seg_idx++];
                seg.is_placeholder = false;
                seg.literal = std::string_view(fmt.data() + i, 1);
                i += 2;
                continue;
            }

            // Scan for matching close brace.
            std::size_t end = i + 1;
            int brace_depth = 1;
            while (end < fmt.size() && brace_depth > 0) {
                if (fmt[end] == '{') ++brace_depth;
                else if (fmt[end] == '}') --brace_depth;
                if (brace_depth > 0) ++end;
            }
            if (end >= fmt.size()) throw;  // unmatched '{'

            std::string_view content = fmt.substr(i + 1, end - i - 1);
            auto& seg = result.segments[seg_idx++];
            seg.is_placeholder = true;
            seg.has_spec = false;

            if (!content.empty()) {
                if (content[0] >= '0' && content[0] <= '9') {
                    // Has explicit index — look for format spec after ':'
                    auto colon = content.find(':');
                    if (colon != std::string_view::npos) {
                        format_spec::parse(content.data() + colon + 1, seg.spec);
                        seg.has_spec = true;
                    }
                } else if (content[0] == ':') {
                    // Has format spec
                    format_spec::parse(content.data() + 1, seg.spec);
                    seg.has_spec = true;
                }
                // else: content is e.g. "0" with no spec — treat as simple {}
            }

            i = end + 1;
        } else if (fmt[i] == '}') {
            if (i + 1 < fmt.size() && fmt[i + 1] == '}') {
                // Escaped brace — emit literal '}'.
                auto& seg = result.segments[seg_idx++];
                seg.is_placeholder = false;
                seg.literal = std::string_view(fmt.data() + i, 1);
                i += 2;
                continue;
            }
            throw;  // unmatched '}'
        } else {
            // Literal text run.
            std::size_t start = i;
            while (i < fmt.size() && fmt[i] != '{' && fmt[i] != '}') ++i;
            auto& seg = result.segments[seg_idx++];
            seg.is_placeholder = false;
            seg.literal = fmt.substr(start, i - start);
        }
    }

    result.segments_count = seg_idx;
    return result;
}

}  // namespace detail

// -- Custom type extensibility (formatter<T>) ---------------------------------

/// @brief Primary template for formatting custom types.
///
/// Specialise this struct for your own types to make them formattable via
/// fl::format_to, fl::format, etc.  Provide a method with the signature:
///
///   template <typename Sink>
///   void format(Sink& sink, const T& value, const format_spec* spec);
///
/// When spec is null, format the value in its simplest form.  When spec is
/// non-null, apply the format specification (alignment, width, precision, etc.)
/// from the parsed format_spec.
template <typename T, typename = void>
struct formatter {
    /// @brief Marker type present only in the primary (unspecialised) template.
    /// Specialisations must omit this type.  The SFINAE detection idiom in
    /// detail::has_formatter_impl checks for the *absence* of this member
    /// to determine whether a type has a custom formatter.
    struct _fl_primary {};
};

/// @brief Inherit from this formatter to make types that define operator<<
/// automatically formattable via fl::format.
///
/// Usage:
///   struct MyType {};
///   std::ostream& operator<<(std::ostream& os, const MyType& t);
///   template<> struct fl::formatter<MyType> : fl::ostream_formatter {};
///
/// When no format spec is given, streams directly to the sink (fast path).
/// When alignment/padding/width is specified, buffers through a growing_sink
/// and then applies the spec.
struct ostream_formatter {
    constexpr std::size_t parse(std::string_view) noexcept { return 0; }

    template <typename Sink, typename T>
    void format(Sink& sink, const T& value, const detail::format_spec* spec) {
        if (!spec || (spec->width == 0 && !spec->precision_set && spec->align == '\0')) {
            // Fast path: stream directly to sink
            detail::sink_streambuf sb(&sink);
            std::ostream os(&sb);
            const_cast<T&>(value) = value;  // operator<< isn't always const
            os << value;
        } else {
            // Buffered path: stream to temporary, apply spec
            detail::growing_sink gs;
            detail::sink_streambuf sb(&gs);
            std::ostream os(&sb);
            const_cast<T&>(value) = value;
            os << value;
            detail::format_text_with_spec(sink,
                std::string_view(gs.buffer().data(), gs.size()), *spec);
        }
    }
};

// Formats the arguments according to the format string and writes the result
// to the given buffer sink. Supports format specifications such as {},
// {:10}, {:>20}, {:*^15}, {:0>10}, etc.
template <typename... Args>
void format_to(buffer_sink& sink, std::string_view fmt, Args&&... args) {
    detail::format_impl(sink, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void format_to(buffer_sink& sink, const char* fmt, Args&&... args) {
    if (FL_UNLIKELY(!fmt)) {
        throw std::invalid_argument("fl::format_to: null format string");
    } else {
        detail::format_impl(sink, std::string_view(fmt), std::forward<Args>(args)...);
    }
}

// Specialization for a single integer argument. Avoids the overhead of the
// generic variadic path when only one integral value needs formatting.
template <typename T>
typename std::enable_if<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>::type
format_to(buffer_sink& sink, std::string_view fmt, T value)
{
    auto formatter = [&value](buffer_sink& s, const detail::format_spec* spec) {
        if (FL_UNLIKELY(!spec)) {
            char temp[64];
            std::size_t len = 0;
            if constexpr (std::is_signed_v<T>) {
                len = detail::integer_formatter::format_int64(temp, sizeof(temp), static_cast<int64_t>(value));
            } else {
                len = detail::integer_formatter::format_uint64(temp, sizeof(temp), static_cast<uint64_t>(value));
            }
            if (FL_LIKELY(len > 0)) {
                s.write(temp, len);
            }
            return;
        }

        detail::format_int_with_spec(s, static_cast<int64_t>(value), *spec);
    };

    const std::array<detail::format_fn<buffer_sink>, 1> formatters{std::move(formatter)};
    detail::format_impl_dispatch(sink, fmt, formatters);
}

template <typename T>
typename std::enable_if<std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>>::type
format_to(buffer_sink& sink, const char* fmt, T value)
{
    if (FL_UNLIKELY(!fmt)) {
        throw std::invalid_argument("fl::format_to: null format string");
    }
    format_to(sink, std::string_view(fmt), value);
}

// -- Convenience functions ----------------------------------------------------

/// @brief Formats arguments according to the format string and returns the
/// result as a std::string.  Equivalent to Python's str.format() or
/// std::format().
template <typename... Args>
inline std::string format(std::string_view fmt, Args&&... args) {
    detail::growing_sink sink;
    detail::format_impl(sink, fmt, std::forward<Args>(args)...);
    return std::string(sink.buffer().data(), sink.size());
}

template <typename... Args>
inline std::string format(const char* fmt, Args&&... args) {
    if (FL_UNLIKELY(!fmt)) {
        throw std::invalid_argument("fl::format: null format string");
    }
    return format(std::string_view(fmt), std::forward<Args>(args)...);
}

/// @brief Formats a single value and returns it as a std::string.
template <typename T>
inline std::string to_string(const T& value) {
    return format("{}", value);
}

/// @brief Formats arguments and writes the result to stdout.
template <typename... Args>
inline void print(std::string_view fmt, Args&&... args) {
    std::string result = format(fmt, std::forward<Args>(args)...);
    if (!result.empty()) {
        std::fwrite(result.data(), 1, result.size(), stdout);
    }
}

template <typename... Args>
inline void print(const char* fmt, Args&&... args) {
    print(std::string_view(fmt ? fmt : ""), std::forward<Args>(args)...);
}

/// @brief Formats arguments, writes the result to stdout, and appends a newline.
template <typename... Args>
inline void println(std::string_view fmt, Args&&... args) {
    std::string result = format(fmt, std::forward<Args>(args)...);
    if (!result.empty()) {
        std::fwrite(result.data(), 1, result.size(), stdout);
    }
    std::fwrite("\n", 1, 1, stdout);
}

template <typename... Args>
inline void println(const char* fmt, Args&&... args) {
    println(std::string_view(fmt ? fmt : ""), std::forward<Args>(args)...);
}

inline void println() {
    std::fwrite("\n", 1, 1, stdout);
}

// ---------------------------------------------------------------------------
// TASK 1: Public API overloads accepting format_string<Args...>
// ---------------------------------------------------------------------------

/// @brief Formats arguments to a buffer_sink with compile-time-validated
/// format string.
template <typename... Args>
void format_to(buffer_sink& sink, detail::format_string<Args...> fmt, Args&&... args) {
    detail::format_impl(sink, fmt.fmt, std::forward<Args>(args)...);
}

/// @brief Formats arguments with compile-time-validated format string and
/// returns the result as a std::string.
template <typename... Args>
inline std::string format(detail::format_string<Args...> fmt, Args&&... args) {
    detail::growing_sink sink;
    detail::format_impl(sink, fmt.fmt, std::forward<Args>(args)...);
    return std::string(sink.buffer().data(), sink.size());
}

/// @brief Formats and writes to stdout with compile-time-validated format string.
template <typename... Args>
inline void print(detail::format_string<Args...> fmt, Args&&... args) {
    std::string result = format(fmt, std::forward<Args>(args)...);
    if (!result.empty()) {
        std::fwrite(result.data(), 1, result.size(), stdout);
    }
}

/// @brief Formats, writes to stdout, and appends a newline with
/// compile-time-validated format string.
template <typename... Args>
inline void println(detail::format_string<Args...> fmt, Args&&... args) {
    std::string result = format(fmt, std::forward<Args>(args)...);
    if (!result.empty()) {
        std::fwrite(result.data(), 1, result.size(), stdout);
    }
    std::fwrite("\n", 1, 1, stdout);
}

// ---------------------------------------------------------------------------
// TASK 2: Public API for vformat_to / vformat with dynamic_arg_store
// ---------------------------------------------------------------------------

/// @brief Formats arguments from a dynamic_format_arg_store according to the
/// format string and writes the result to a buffer_sink.  Uses an internal
/// adapter to bridge the public output_sink hierarchy to the internal
/// sink_base hierarchy used by the virtual dispatch path.
inline void vformat_to(buffer_sink& sink, std::string_view fmt,
                       const detail::dynamic_format_arg_store& args) {
    detail::output_sink_adapter adapter(sink);
    detail::vformat_to_impl(adapter, fmt, args);
}

/// @brief Formats arguments from a dynamic_format_arg_store according to the
/// format string and returns the result as a std::string.
inline std::string vformat(std::string_view fmt,
                           const detail::dynamic_format_arg_store& args) {
    detail::growing_sink sink;
    detail::vformat_to_impl(sink, fmt, args);
    return std::string(sink.buffer().data(), sink.size());
}

// ---------------------------------------------------------------------------
// TASK 3: Public API for compiled_format — runtime dispatch from pre-parsed
// format segments.
// ---------------------------------------------------------------------------

/// @brief Formats arguments using a pre-compiled format string.  The format
/// string is parsed at compile time, so no runtime parsing overhead is incurred.
template <typename Sink, typename... Args, std::size_t N>
void format_to(Sink& sink, const detail::compiled_format<N>& cfmt,
               Args&&... args) {
    using formatter_t = detail::format_fn<Sink>;
    const std::array<formatter_t, sizeof...(Args)> formatters{
        formatter_t{[&args](Sink& s, const detail::format_spec* spec) {
            detail::format_argument(s, args, spec);
        }}...
    };

    std::size_t arg_idx = 0;
    for (std::size_t i = 0; i < cfmt.segments_count; ++i) {
        const auto& seg = cfmt.segments[i];
        if (!seg.is_placeholder) {
            sink.write(seg.literal.data(), seg.literal.size());
        } else {
            if (arg_idx >= sizeof...(Args)) {
                throw std::invalid_argument(
                    "fl::format_to (compiled): argument index out of range");
            }
            formatters[arg_idx++](sink, seg.has_spec ? &seg.spec : nullptr);
        }
    }
}

/// @brief Formats arguments using a pre-compiled format string and returns
/// the result as a std::string.
template <typename... Args, std::size_t N>
inline std::string format(const detail::compiled_format<N>& cfmt,
                          Args&&... args) {
    detail::growing_sink sink;
    format_to(sink, cfmt, std::forward<Args>(args)...);
    return std::string(sink.buffer().data(), sink.size());
}

}  // namespace fl

#endif  // FL_FORMAT_HPP
