// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_COLOR_HPP
#define FL_COLOR_HPP

/// @file fl/color.hpp
/// Optional extension: terminal colour (ANSI) support via styled_text.
///
/// Include this header after fl/format.hpp to enable formatting of
/// styled text with ANSI escape sequences. The fl::styled() function
/// wraps any value with a text_style, and the formatter emits SGR
/// (Select Graphic Rendition) codes around the formatted value.
///
/// Example:
/// @code
///   fl::print("{}", fl::styled("Hello", fl::fg(fl::ansi::red) | fl::bold));
/// @endcode
///
/// All style combinators return text_style objects so they compose
/// naturally with operator| (or chained function calls).

#include "fl/format.hpp"
#include <string>

namespace fl {

// -----------------------------------------------------------------------
// text_style — represents a set of ANSI SGR attributes.
// -----------------------------------------------------------------------

/// @brief Describes terminal text appearance: foreground colour, background
/// colour, and text attributes (bold, dim, italic, underline, etc.).
///
/// Each field uses -1 to mean "not set" (leave the terminal attribute
/// unchanged).  The formatter only emits codes for attributes that have
/// been explicitly enabled.
struct text_style {
    int fg = -1;           ///< Foreground ANSI colour code (-1 = default).
    int bg = -1;           ///< Background ANSI colour code (-1 = default).
    bool bold = false;
    bool dim = false;
    bool italic = false;
    bool underline = false;
    bool blink = false;
    bool reverse = false;  ///< Reverse video (swap fg/bg).
    bool strikethrough = false;
};

/// @brief Combines two text_style objects by OR-ing their attributes.
/// The right-hand style's non-default values override the left-hand side.
inline text_style operator|(text_style lhs, const text_style& rhs) noexcept {
    if (rhs.fg >= 0) lhs.fg = rhs.fg;
    if (rhs.bg >= 0) lhs.bg = rhs.bg;
    lhs.bold          = lhs.bold          || rhs.bold;
    lhs.dim           = lhs.dim           || rhs.dim;
    lhs.italic        = lhs.italic        || rhs.italic;
    lhs.underline     = lhs.underline     || rhs.underline;
    lhs.blink         = lhs.blink         || rhs.blink;
    lhs.reverse       = lhs.reverse       || rhs.reverse;
    lhs.strikethrough = lhs.strikethrough || rhs.strikethrough;
    return lhs;
}

// -----------------------------------------------------------------------
// Named ANSI colour constants.
// -----------------------------------------------------------------------

namespace ansi {
    /// Foreground colour codes (standard 8 + bright variants).
    constexpr int black        = 30;
    constexpr int red          = 31;
    constexpr int green        = 32;
    constexpr int yellow       = 33;
    constexpr int blue         = 34;
    constexpr int magenta      = 35;
    constexpr int cyan         = 36;
    constexpr int white        = 37;
    constexpr int bright_black   = 90;
    constexpr int bright_red     = 91;
    constexpr int bright_green   = 92;
    constexpr int bright_yellow  = 93;
    constexpr int bright_blue    = 94;
    constexpr int bright_magenta = 95;
    constexpr int bright_cyan    = 96;
    constexpr int bright_white   = 97;
}  // namespace ansi

// -----------------------------------------------------------------------
// Style combinator functions.
// -----------------------------------------------------------------------

/// @brief Creates a text_style with only the foreground colour set.
inline text_style fg(int color) noexcept { text_style s; s.fg = color; return s; }

/// @brief Creates a text_style with only the background colour set.
inline text_style bg(int color) noexcept { text_style s; s.bg = color; return s; }

/// @brief Creates or augments a style with bold attribute.
inline text_style bold(text_style s = {}) noexcept { s.bold = true; return s; }

/// @brief Creates or augments a style with italic attribute.
inline text_style italic(text_style s = {}) noexcept { s.italic = true; return s; }

/// @brief Creates or augments a style with underline attribute.
inline text_style underline(text_style s = {}) noexcept { s.underline = true; return s; }

/// @brief Creates or augments a style with dim attribute.
inline text_style dim(text_style s = {}) noexcept { s.dim = true; return s; }

/// @brief Creates or augments a style with blink attribute.
inline text_style blink(text_style s = {}) noexcept { s.blink = true; return s; }

/// @brief Creates or augments a style with reverse-video attribute.
inline text_style reverse(text_style s = {}) noexcept { s.reverse = true; return s; }

/// @brief Creates or augments a style with strikethrough attribute.
inline text_style strikethrough(text_style s = {}) noexcept { s.strikethrough = true; return s; }

// -----------------------------------------------------------------------
// styled_text<T> — pairs a value with its display style.
// -----------------------------------------------------------------------

/// @brief Wrapper that associates a display style with a value.
///
/// Constructed via fl::styled(value, style).  The formatter specialisation
/// emits an ANSI SGR prefix, the formatted value, then a reset sequence.
template <typename T>
struct styled_text {
    const T& value;
    text_style style;
};

/// @brief Wraps a value with a text_style for ANSI-coloured formatting.
template <typename T>
styled_text<T> styled(const T& value, text_style style) noexcept {
    return {value, style};
}

// -----------------------------------------------------------------------
// Formatter specialisation for styled_text<T>.
// -----------------------------------------------------------------------

template <typename T>
struct formatter<styled_text<T>> {
    /// @brief Parses the format specification (no custom spec handled yet).
    constexpr std::size_t parse(std::string_view /*spec*/) noexcept {
        return 0;
    }

    /// @brief Writes the styled value to the sink, wrapped in ANSI codes.
    template <typename Sink>
    void format(Sink& sink, const styled_text<T>& st, const detail::format_spec* spec) {
        // Build the ANSI SGR escape sequence.
        char ansi_buf[64];
        int pos = 0;
        ansi_buf[pos++] = '\033';
        ansi_buf[pos++] = '[';

        bool first = true;
        auto add_code = [&](int code) {
            if (!first) ansi_buf[pos++] = ';';
            first = false;
            if (code >= 10) {
                ansi_buf[pos++] = static_cast<char>('0' + (code / 10));
            }
            ansi_buf[pos++] = static_cast<char>('0' + (code % 10));
        };

        // Text attribute codes (SGR parameters).
        if (st.style.bold)          add_code(1);
        if (st.style.dim)           add_code(2);
        if (st.style.italic)        add_code(3);
        if (st.style.underline)     add_code(4);
        if (st.style.blink)         add_code(5);
        if (st.style.reverse)       add_code(7);
        if (st.style.strikethrough) add_code(9);
        if (st.style.fg >= 0)       add_code(st.style.fg);
        if (st.style.bg >= 0)       add_code(st.style.bg + 10);  // bg = fg + 10

        ansi_buf[pos++] = 'm';

        // Write the opening SGR sequence.
        sink.write(ansi_buf, static_cast<std::size_t>(pos));

        // Format the underlying value using the standard dispatcher.
        // This handles all built-in types and any user-defined formatters.
        detail::format_argument(sink, st.value, spec);

        // Reset all attributes back to terminal defaults.
        const char reset[] = "\033[0m";
        sink.write(reset, 4);
    }
};

}  // namespace fl

#endif  // FL_COLOR_HPP
