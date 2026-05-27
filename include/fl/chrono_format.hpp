// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_CHRONO_FORMAT_HPP
#define FL_CHRONO_FORMAT_HPP

/// @file fl/chrono_format.hpp
/// Optional extension: formatter specialisations for std::chrono types.
///
/// Include this header after fl/format.hpp to enable formatting of
/// std::chrono::duration and std::chrono::time_point values using the
/// same {} placeholder syntax as the core formatting engine.
///
/// Duration formatter supports the %Q (value) and %q (unit) escape
/// sequences and outputs the value with an SI unit suffix by default
/// (e.g., "5s", "1500ms", "2.5min").
///
/// Time point formatter uses strftime-compatible format strings and
/// defaults to "%Y-%m-%d %H:%M:%S" (ISO 8601 date + time).
///
/// Both formatters respect the alignment, width, and padding settings
/// in the format specification.

#include "fl/format.hpp"
#include <chrono>
#include <ctime>
#include <charconv>

namespace fl {

// -----------------------------------------------------------------------
// Helper: SI unit suffix for common std::chrono duration periods.
// -----------------------------------------------------------------------

/// @brief Returns the standard SI unit suffix for a given Period type.
template <typename Period>
const char* period_suffix() noexcept {
    if constexpr (std::is_same_v<Period, std::nano>)          return "ns";
    else if constexpr (std::is_same_v<Period, std::micro>)    return "us";
    else if constexpr (std::is_same_v<Period, std::milli>)    return "ms";
    else if constexpr (std::is_same_v<Period, std::ratio<1>>) return "s";
    else if constexpr (std::is_same_v<Period, std::ratio<60>>)return "min";
    else if constexpr (std::is_same_v<Period, std::ratio<3600>>) return "h";
    else return "";
}

// -----------------------------------------------------------------------
// Formatter for std::chrono::duration<Rep, Period>
// -----------------------------------------------------------------------

/// @brief Formatter for std::chrono::duration values.
///
/// Default output: value followed by the SI unit suffix
/// (e.g., std::chrono::seconds(5) => "5s").
///
/// The internal format string (%Q%q by default) can be extended in a
/// future version to support %H, %M, %S for chrono::hh_mm_ss-style
/// decomposition.
template <typename Rep, typename Period>
struct formatter<std::chrono::duration<Rep, Period>> {
    /// The chrono-specific format string. Supports %Q (the numeric value)
    /// and %q (the unit suffix). Default "%Q%q" produces e.g. "5s".
    std::string chrono_fmt = "%Q%q";

    /// @brief Parses the chrono-format string from the format specification.
    /// This stub is called by the framework but does not currently consume
    /// any characters from the outer spec — it is reserved for future use.
    constexpr std::size_t parse(std::string_view /*spec*/) noexcept {
        return 0;
    }

    /// @brief Formats the duration and writes the result to the sink.
    template <typename Sink>
    void format(Sink& sink, const std::chrono::duration<Rep, Period>& dur,
                const detail::format_spec* spec)
    {
        // Render the chrono-formatted content into a string first,
        // then apply the outer alignment/width specification (if any)
        // via format_text_with_spec.
        std::string result;
        result.reserve(64);

        auto val = dur.count();
        using rep_type = decltype(val);
        char buf[64];

        // Walk the chrono_fmt string and expand %-escapes.
        for (std::size_t i = 0; i < chrono_fmt.size(); ++i) {
            if (chrono_fmt[i] == '%' && i + 1 < chrono_fmt.size()) {
                char c = chrono_fmt[i + 1];
                switch (c) {
                case 'Q': {  // %Q — numeric value
                    std::to_chars_result r{};
                    if constexpr (std::is_integral_v<rep_type>) {
                        r = std::to_chars(buf, buf + sizeof(buf), val);
                    } else {
                        // Use double for floating-point Rep types; shortest
                        // representation avoids unnecessary trailing zeros.
                        r = std::to_chars(buf, buf + sizeof(buf),
                                          static_cast<double>(val));
                    }
                    if (r.ec == std::errc{}) {
                        result.append(buf, r.ptr - buf);
                    }
                    ++i;  // consume the escape character
                    break;
                }
                case 'q': {  // %q — unit suffix
                    const char* suffix = period_suffix<Period>();
                    result.append(suffix);
                    ++i;
                    break;
                }
                default:
                    // Unknown escape: emit as-is (percent + character).
                    result.push_back('%');
                    result.push_back(c);
                    ++i;
                    break;
                }
            } else {
                // Regular character, copy through.
                result.push_back(chrono_fmt[i]);
            }
        }

        if (spec) {
            detail::format_text_with_spec(sink, result, *spec);
        } else {
            sink.write(result.data(), result.size());
        }
    }
};

// -----------------------------------------------------------------------
// Formatter for std::chrono::time_point<Clock, Duration>
// -----------------------------------------------------------------------

/// @brief Formatter for std::chrono::time_point values.
///
/// Converts to std::time_t via system_clock and formats with strftime.
/// Only clocks whose time_point can be converted to system_clock::time_point
/// are supported. Default format: "%F %T" (ISO 8601: YYYY-MM-DD HH:MM:SS).
template <typename Clock, typename Duration>
struct formatter<std::chrono::time_point<Clock, Duration>> {
    /// strftime-compatible format string. Default is ISO 8601 date + time.
    /// Uses %Y-%m-%d %H:%M:%S (%F and %T are C99/POSIX extensions not
    /// supported on all C libraries, e.g. MinGW's msvcrt).
    std::string chrono_fmt = "%Y-%m-%d %H:%M:%S";

    /// @brief Parse stub — reserved for future use.
    constexpr std::size_t parse(std::string_view /*spec*/) noexcept {
        return 0;
    }

    /// @brief Formats the time_point and writes the result to the sink.
    template <typename Sink>
    void format(Sink& sink, const std::chrono::time_point<Clock, Duration>& tp,
                const detail::format_spec* spec)
    {
        // Convert via system_clock.  Cast the duration to
        // system_clock::duration if necessary (to_time_t requires a
        // system_clock::time_point whose duration matches the clock's
        // native duration type).
        using sys_tp_t = std::chrono::system_clock::time_point;
        auto sys_tp = std::chrono::time_point_cast<
            std::chrono::system_clock::duration>(
            sys_tp_t(tp.time_since_epoch()));
        std::time_t tt = std::chrono::system_clock::to_time_t(sys_tp);

        // Convert to broken-down time.  We use gmtime (not thread-safe on
        // all platforms) for maximum portability.  A production build could
        // switch to gmtime_r or gmtime_s via feature-test macros.
        std::tm tm{};
        std::tm* tmp = std::gmtime(&tt);
        if (FL_LIKELY(tmp)) {
            tm = *tmp;
        }

        char buf[256];
        std::size_t len = std::strftime(buf, sizeof(buf), chrono_fmt.c_str(), &tm);
        if (len > 0) {
            if (spec) {
                detail::format_text_with_spec(sink,
                    std::string_view(buf, len), *spec);
            } else {
                sink.write(buf, len);
            }
        }
    }
};

}  // namespace fl

#endif  // FL_CHRONO_FORMAT_HPP
