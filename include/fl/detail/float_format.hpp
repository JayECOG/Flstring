// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_DETAIL_FLOAT_FORMAT_HPP
#define FL_DETAIL_FLOAT_FORMAT_HPP

/// @file fl/detail/float_format.hpp
/// Locale-independent float-to-string conversion using std::to_chars.
///
/// On modern implementations (GCC libstdc++ >= 11, Clang libc++ >= 14, MSVC 2022),
/// std::to_chars for floating-point types uses Ryū or Dragonbox internally,
/// providing 5-10x faster float formatting than snprintf with guaranteed
/// round-trip and locale-independent output.

#include "fl/config.hpp"
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace fl {
namespace detail {

/// @brief Locale-independent float/double formatter using std::to_chars.
///
/// Replaces the previous snprintf-based float formatting, which was:
///   - Locale-dependent (output changed with LC_NUMERIC)
///   - 5-10x slower than modern algorithms (Dragonbox/Ryū)
///   - Bug-prone (caused 3 critical bugs in the audit)
///
/// This implementation delegates to std::to_chars, which on all modern
/// toolchains uses Dragonbox, Ryū, or Grisu-Exact internally.
class float_formatter {
public:
    /// Maximum buffer size needed for any double value.
    /// Longest possible: sign(1) + digits(309) + "e-"(2) + exp(3) = 315,
    /// plus padding for fixed-point precision.
    static constexpr std::size_t max_buffer_size = 4096;

    /// @brief Formats a float value as the shortest round-trip string.
    /// Uses the float overload of to_chars to avoid spurious precision from
    /// float-to-double promotion (e.g., 3.14f → "3.14" not "3.140000104904175").
    FL_INLINE static std::size_t format_shortest(char* buffer, std::size_t capacity, float value) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;
        if (FL_UNLIKELY(std::isnan(value))) return write_str(buffer, capacity, "nan");
        if (FL_UNLIKELY(std::isinf(value))) {
            return (value < 0) ? write_str(buffer, capacity, "-inf")
                               : write_str(buffer, capacity, "inf");
        }
        auto result = std::to_chars(buffer, buffer + capacity, value);
        if (result.ec == std::errc()) {
            return static_cast<std::size_t>(result.ptr - buffer);
        }
        return 0;
    }

    /// @brief Formats a double value as the shortest round-trip string.
    /// Equivalent to Dragonbox/Ryū shortest mode (better than printf %g).
    /// Returns the number of characters written, or 0 on error.
    FL_INLINE static std::size_t format_shortest(char* buffer, std::size_t capacity, double value) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;

        // Handle special values: NaN, inf, -0.0
        if (FL_UNLIKELY(std::isnan(value))) {
            return write_str(buffer, capacity, "nan");
        }
        if (FL_UNLIKELY(std::isinf(value))) {
            if (value < 0) {
                if (1 < capacity) { buffer[0] = '-'; buffer[1] = '\0'; }
                return write_str(buffer, capacity, "-inf");
            }
            return write_str(buffer, capacity, "inf");
        }

        auto result = std::to_chars(buffer, buffer + capacity, value);
        if (result.ec == std::errc()) {
            return static_cast<std::size_t>(result.ptr - buffer);
        }
        return 0;
    }

    /// @brief Formats a double with fixed-point notation and the given precision.
    FL_INLINE static std::size_t format_fixed(char* buffer, std::size_t capacity,
                                               double value, std::size_t precision) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;
        if (FL_UNLIKELY(std::isnan(value))) return write_str(buffer, capacity, "nan");
        if (FL_UNLIKELY(std::isinf(value))) {
            return (value < 0) ? write_str(buffer, capacity, "-inf")
                               : write_str(buffer, capacity, "inf");
        }
        if (FL_UNLIKELY(precision > 4096)) precision = 4096;

        auto result = std::to_chars(buffer, buffer + capacity, value,
                                     std::chars_format::fixed, static_cast<int>(precision));
        if (result.ec == std::errc()) {
            return static_cast<std::size_t>(result.ptr - buffer);
        }
        return 0;
    }

    /// @brief Formats a double with scientific notation and the given precision.
    FL_INLINE static std::size_t format_scientific(char* buffer, std::size_t capacity,
                                                    double value, std::size_t precision) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;
        if (FL_UNLIKELY(std::isnan(value))) return write_str(buffer, capacity, "nan");
        if (FL_UNLIKELY(std::isinf(value))) {
            return (value < 0) ? write_str(buffer, capacity, "-inf")
                               : write_str(buffer, capacity, "inf");
        }
        if (FL_UNLIKELY(precision > 4096)) precision = 4096;

        auto result = std::to_chars(buffer, buffer + capacity, value,
                                     std::chars_format::scientific, static_cast<int>(precision));
        if (result.ec == std::errc()) {
            return static_cast<std::size_t>(result.ptr - buffer);
        }
        return 0;
    }

    /// @brief Formats a double with general notation (shortest of fixed/scientific).
    FL_INLINE static std::size_t format_general(char* buffer, std::size_t capacity,
                                                 double value, std::size_t precision) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;
        if (FL_UNLIKELY(std::isnan(value))) return write_str(buffer, capacity, "nan");
        if (FL_UNLIKELY(std::isinf(value))) {
            return (value < 0) ? write_str(buffer, capacity, "-inf")
                               : write_str(buffer, capacity, "inf");
        }
        if (FL_UNLIKELY(precision > 4096)) precision = 4096;

        auto result = std::to_chars(buffer, buffer + capacity, value,
                                     std::chars_format::general, static_cast<int>(precision));
        if (result.ec == std::errc()) {
            return static_cast<std::size_t>(result.ptr - buffer);
        }
        return 0;
    }

    /// @brief Formats a float according to the given format specifier character.
    /// Uses to_chars with the float overload to avoid spurious precision.
    FL_INLINE static std::size_t format_with_spec(char* buffer, std::size_t capacity,
                                                   float value, char fmt_char,
                                                   std::size_t precision, bool sign,
                                                   bool sign_space) noexcept {
        return format_with_spec_impl(buffer, capacity, value, fmt_char, precision, sign, sign_space);
    }

    /// @brief Formats a double according to the given format specifier character.
    /// Handles e/E (scientific), f/F (fixed), g/G (general), and default (shortest).
    /// @param fmt_char  The format specifier character (e, E, f, F, g, G, or '\0')
    /// @param precision The precision (number of decimal places)
    /// @param sign      Whether to force a '+' sign for non-negative values
    /// @param sign_space Whether to output a leading space for non-negative values
    FL_INLINE static std::size_t format_with_spec(char* buffer, std::size_t capacity,
                                                   double value, char fmt_char,
                                                   std::size_t precision, bool sign,
                                                   bool sign_space) noexcept {
        return format_with_spec_impl(buffer, capacity, value, fmt_char, precision, sign, sign_space);
    }

private:
    /// Internal implementation templated on float/double.
    template <typename Float>
    FL_INLINE static std::size_t format_with_spec_impl(char* buffer, std::size_t capacity,
                                                        Float value, char fmt_char,
                                                        std::size_t precision, bool sign,
                                                        bool sign_space) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;

        // Handle special values first
        if (FL_UNLIKELY(std::isnan(value))) {
            std::size_t pos = 0;
            if (sign || sign_space) { buffer[pos++] = ' '; }
            if (fmt_char == 'F' || fmt_char == 'E' || fmt_char == 'G') {
                return write_str_at(buffer + pos, capacity - pos, "NAN");
            }
            return write_str_at(buffer + pos, capacity - pos, "nan");
        }
        if (FL_UNLIKELY(std::isinf(value))) {
            if (value < 0) {
                buffer[0] = '-';
                std::size_t written = write_str_at(buffer + 1, capacity - 1, "inf");
                if (fmt_char == 'F' || fmt_char == 'E' || fmt_char == 'G') {
                    return write_str_at(buffer + 1, capacity - 1, "INF");
                }
                if (written > 0) return written + 1;
            }
            std::size_t pos = 0;
            if (sign) { buffer[pos++] = '+'; }
            else if (sign_space) { buffer[pos++] = ' '; }
            std::size_t written = write_str_at(buffer + pos, capacity - pos, "inf");
            if (fmt_char == 'F' || fmt_char == 'E' || fmt_char == 'G') {
                written = write_str_at(buffer + pos, capacity - pos, "INF");
            }
            if (written > 0) return written + pos;
            return 0;
        }

        if (FL_UNLIKELY(precision > 4096)) precision = 4096;

        std::to_chars_result result;
        bool is_upper = false;
        switch (fmt_char) {
            case 'e':
                result = std::to_chars(buffer, buffer + capacity, value,
                                       std::chars_format::scientific, static_cast<int>(precision));
                break;
            case 'E':
                is_upper = true;
                result = std::to_chars(buffer, buffer + capacity, value,
                                       std::chars_format::scientific, static_cast<int>(precision));
                break;
            case 'f':
                result = std::to_chars(buffer, buffer + capacity, value,
                                       std::chars_format::fixed, static_cast<int>(precision));
                break;
            case 'F':
                is_upper = true;
                result = std::to_chars(buffer, buffer + capacity, value,
                                       std::chars_format::fixed, static_cast<int>(precision));
                break;
            case 'g':
                result = std::to_chars(buffer, buffer + capacity, value,
                                       std::chars_format::general, static_cast<int>(precision));
                break;
            case 'G':
                is_upper = true;
                result = std::to_chars(buffer, buffer + capacity, value,
                                       std::chars_format::general, static_cast<int>(precision));
                break;
            default:
                // No spec or unknown char: shortest round-trip (Dragonbox/Ryū)
                result = std::to_chars(buffer, buffer + capacity, value);
                break;
        }

        if (result.ec == std::errc()) {
            std::size_t written = static_cast<std::size_t>(result.ptr - buffer);
            // Uppercase the output for E/F/G specifiers (to_chars always
            // produces lowercase 'e', "inf", "nan").
            if (is_upper) {
                for (char* p = buffer; p < result.ptr; ++p) {
                    if (*p == 'e' || *p == 'i' || *p == 'n' || *p == 'f' || *p == 'a') {
                        *p = static_cast<char>(*p - 32);  // ASCII lowercase→uppercase
                    }
                }
            }
            return written;
        }
        return 0;
    }

private:
    /// Writes a string literal to a buffer, returning the length.
    FL_INLINE static std::size_t write_str(char* buffer, std::size_t capacity,
                                            const char* str) noexcept {
        std::size_t len = 0;
        while (str[len] != '\0') ++len;
        if (len >= capacity) len = capacity - 1;
        if (len > 0) std::memcpy(buffer, str, len);
        buffer[len] = '\0';
        return len;
    }

    /// Writes at a buffer offset.
    FL_INLINE static std::size_t write_str_at(char* buffer, std::size_t capacity,
                                               const char* str) noexcept {
        if (FL_UNLIKELY(capacity == 0)) return 0;
        return write_str(buffer, capacity, str);
    }
};

}  // namespace detail
}  // namespace fl

#endif  // FL_DETAIL_FLOAT_FORMAT_HPP
