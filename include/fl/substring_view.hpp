// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_SUBSTRING_VIEW_HPP
#define FL_SUBSTRING_VIEW_HPP

/// @file Lightweight, non-owning substring view.
///
/// Provides an efficient view over a portion of a string without ownership.
/// When constructed from a raw `const char*` or `std::string`, it can
/// optionally track lifetime via a `std::shared_ptr`.  When constructed from
/// an `fl::string` the view does *not* extend the string's lifetime; the
/// caller must ensure the original `fl::string` outlives the view.

#include <cstring>
#include "fl/config.hpp"
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>
#include <cassert>
#include <cstdint>
#include <string_view>
#include <compare>
#include "fl/profiling.hpp"

namespace fl {

// Forward declarations.
class string;

/// Lightweight, non-owning substring view.
///
/// Performance characteristics:
/// - Construction: O(1), constant-time pointer/length setup.
/// - Copy: O(1), shares ownership (if applicable) with original.
/// - Access: O(1) per character.
/// - Search: O(n*m) for substring search.
///
/// Example usage:
/// @code
///   fl::string str("hello world");
///   fl::substring_view view(str.data() + 6, 5);  // "world"
/// @endcode
///
/// @warning `data()` and `c_str()` return a pointer that is NOT
/// null-terminated.  Passing it to a C API that expects a C string is
/// undefined behaviour.
class substring_view {
public:
    using value_type = char;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = const char&;
    using const_reference = const char&;
    using pointer = const char*;
    using const_pointer = const char*;
    using iterator = std::string_view::const_iterator;
    using const_iterator = std::string_view::const_iterator;
    using reverse_iterator = std::reverse_iterator<const_iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    substring_view() noexcept
        : _view(), _owner(nullptr) {}

    explicit substring_view(const char* cstr) noexcept
        : substring_view(cstr, cstr ? std::strlen(cstr) : 0) {}

    /// Constructs a view over [data, data+len).
    /// @param data  Pointer to the start of the substring.
    /// @param len   Length of the substring.
    /// @param owner Optional shared_ptr to keep the backing store alive.
    substring_view(const char* data, size_type len,
                   std::shared_ptr<const void> owner = nullptr) noexcept
        : _view(data ? std::string_view(data, len) : std::string_view()),
          _owner(std::move(owner)) {}

    /// Constructs a view from an fl::string.
    /// @note Does NOT extend the string's lifetime.
    substring_view(const string& str, size_type offset = 0,
                   size_type len = std::string::npos) noexcept;

    /// Constructs a view from a std::string, copying into shared storage.
    /// The view remains valid independently of the original string.
    substring_view(const std::string& str, size_type offset = 0,
                   size_type len = std::string::npos) noexcept;

    substring_view(const substring_view& other) noexcept = default;
    substring_view(substring_view&& other) noexcept = default;
    substring_view& operator=(const substring_view& other) noexcept = default;
    substring_view& operator=(substring_view&& other) noexcept = default;
    ~substring_view() noexcept = default;

    // -- Element access ---------------------------------------------------

    /// Bounds-checked access.  Asserts on out-of-range in debug builds.
    FL_CONSTEXPR reference operator[](size_type pos) const noexcept {
        assert(pos < _view.size());
        return _view[pos];
    }

    /// Throws std::out_of_range on invalid position.
    const_reference at(size_type pos) const {
        if (pos >= _view.size()) {
            throw std::out_of_range("substring_view::at: position out of range");
        }
        return _view[pos];
    }

    FL_CONSTEXPR reference front() const noexcept { assert(!empty()); return _view.front(); }
    FL_CONSTEXPR reference back() const noexcept  { assert(!empty()); return _view.back(); }

    /// @warning NOT null-terminated.  Do not pass to C APIs expecting C strings.
    FL_CONSTEXPR const_pointer data() const noexcept { return _view.data(); }

    /// Identical to data().  @warning NOT null-terminated.
    FL_CONSTEXPR const_pointer c_str() const noexcept { return _view.data(); }

    // -- Capacity ---------------------------------------------------------

    FL_CONSTEXPR size_type size()   const noexcept { return _view.size(); }
    FL_CONSTEXPR size_type length() const noexcept { return _view.size(); }
    FL_CONSTEXPR bool      empty()  const noexcept { return _view.empty(); }

    // -- Iteration --------------------------------------------------------

    FL_CONSTEXPR const_iterator begin()  const noexcept { return _view.begin(); }
    FL_CONSTEXPR const_iterator end()    const noexcept { return _view.end(); }
    FL_CONSTEXPR const_iterator cbegin() const noexcept { return _view.cbegin(); }
    FL_CONSTEXPR const_iterator cend()   const noexcept { return _view.cend(); }

    const_reverse_iterator rbegin()  const noexcept { return const_reverse_iterator(_view.end()); }
    const_reverse_iterator rend()    const noexcept { return const_reverse_iterator(_view.begin()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(_view.cend()); }
    const_reverse_iterator crend()   const noexcept { return const_reverse_iterator(_view.cbegin()); }

    // -- Comparison -------------------------------------------------------

    [[nodiscard]] bool operator==(const substring_view& other) const noexcept { return _view == other._view; }

    /// C++20 spaceship; auto-generates <, <=, >, >=, !=.
    [[nodiscard]] std::strong_ordering operator<=>(const substring_view& other) const noexcept {
        return _view <=> other._view;
    }

    [[nodiscard]] bool operator==(const char* cstr) const noexcept {
        if (!cstr) return empty();
        return _view == cstr;
    }
    [[nodiscard]] bool operator!=(const char* cstr) const noexcept { return !(*this == cstr); }

    // -- Search -----------------------------------------------------------

    [[nodiscard]] size_type find(char ch, size_type offset = 0) const noexcept {
        auto pos = _view.find(ch, offset);
        return pos == std::string::npos ? npos : pos;
    }

    [[nodiscard]] size_type find(const substring_view& substr, size_type offset = 0) const noexcept {
        if (offset > _view.size()) return npos;
        if (substr.empty()) return offset <= _view.size() ? offset : npos;
        auto pos = _view.find(substr._view, offset);
        return pos == std::string::npos ? npos : pos;
    }

    [[nodiscard]] size_type find(const char* substr, size_type offset = 0) const noexcept {
        if (!substr || !*substr) return offset <= _view.size() ? offset : npos;
        auto pos = _view.find(substr, offset);
        return pos == std::string::npos ? npos : pos;
    }

    [[nodiscard]] size_type rfind(char ch) const noexcept {
        auto pos = _view.rfind(ch);
        return pos == std::string::npos ? npos : pos;
    }

    [[nodiscard]] size_type rfind(const substring_view& substr) const noexcept {
        if (substr.empty()) return _view.size();
        auto pos = _view.rfind(substr._view);
        return pos == std::string::npos ? npos : pos;
    }

    // -- Substring ops ----------------------------------------------------

    /// Returns a new view over a portion of this view.
    [[nodiscard]] substring_view substr(size_type offset = 0,
                          size_type len = std::string::npos) const noexcept {
        if (offset >= _view.size()) return substring_view();
        auto fragment = _view.substr(offset, len);
        return substring_view(fragment.data(), fragment.size(), _owner);
    }

    [[nodiscard]] bool starts_with(const substring_view& prefix) const noexcept {
        return _view.size() >= prefix._view.size() &&
               std::memcmp(_view.data(), prefix._view.data(), prefix._view.size()) == 0;
    }

    [[nodiscard]] bool ends_with(const substring_view& suffix) const noexcept {
        return _view.size() >= suffix._view.size() &&
               std::memcmp(_view.data() + (_view.size() - suffix._view.size()),
                           suffix._view.data(), suffix._view.size()) == 0;
    }

    [[nodiscard]] bool contains(const substring_view& substr) const noexcept {
        return _view.find(substr._view) != std::string::npos;
    }

    // -- Conversion -------------------------------------------------------

    /// Allocates a new std::string copy of the viewed data.
    [[nodiscard]] std::string to_string() const { return std::string(_view.data(), _view.size()); }

    /// Allocates a new fl::string copy of the viewed data.
    string to_fl_string() const;

    /// Sentinel value for "not found", matching std::string::npos.
    static constexpr size_type npos = std::string::npos;

private:
    std::string_view _view;
    std::shared_ptr<const void> _owner;  // Shared ownership of backing data.
};

// ============================================================================
// Deduction guides and helper functions
// ============================================================================

/// Creates a substring_view from a string literal with automatic length deduction.
template<std::size_t N>
inline substring_view make_substring_view(const char (&arr)[N]) noexcept {
    return substring_view(arr, N - 1);  // N-1 excludes the null terminator.
}

/// FNV-1a hash functor for substring_view, for use in std::unordered_map.
struct substring_view_hash {
    std::size_t operator()(const substring_view& view) const noexcept {
        // FNV-1a hash
        std::size_t hash = 14695981039346656037ULL;
        for (char c : view) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }
};

/// Equality functor for substring_view, for use in std::unordered_map.
struct substring_view_equal {
    bool operator()(const substring_view& lhs, const substring_view& rhs) const noexcept {
        return lhs == rhs;
    }
};

inline substring_view::substring_view(const std::string& str, size_type offset,
                                      size_type len) noexcept
    : _view(), _owner(nullptr) {
    if (offset < str.size()) {
        size_type actual_len = std::min(len, str.size() - offset);
        auto owned = std::make_shared<std::string>(str);
        _owner = std::static_pointer_cast<const void>(owned);
        _view = std::string_view(owned->data() + offset, actual_len);
    }
}

inline std::ostream& operator<<(std::ostream& os, const substring_view& sv) {
    return os.write(sv.data(), static_cast<std::streamsize>(sv.size()));
}

}  // namespace fl

#endif  // FL_SUBSTRING_VIEW_HPP
