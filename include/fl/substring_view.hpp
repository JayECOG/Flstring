// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_SUBSTRING_VIEW_HPP
#define FL_SUBSTRING_VIEW_HPP

// Lightweight, non-owning substring view for efficient string operations.

#include <cstring>
#include <span>
#include <concepts>
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

// Lightweight, non-owning substring view.
//
// Provides an efficient view over a portion of a string without ownership or
// allocation. When constructed from a raw `const char*` or `std::string`, it
// can optionally track lifetime via a `std::shared_ptr`. However, when
// constructed from an `fl::string`, the `fl::substring_view` does NOT manage
// the lifetime of the underlying `fl::string`'s data. In such cases, the user
// is responsible for ensuring the original `fl::string` outlives the
// `fl::substring_view`.
//
// Performance characteristics:
//   - Construction: O(1), constant-time pointer/length setup.
//   - Copy: O(1), shares ownership (if applicable) with original.
//   - Access: O(1) per character.
//   - Search: O(n*m) for substring search.
//
// Example usage:
//   fl::string str("hello world");
//   fl::substring_view view(str.data() + 6, 5);  // "world"
//   std::cout << view.data() << std::endl;        // Not null-terminated.
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

    // The owner shared_ptr keeps the underlying data alive for the lifetime
    // of this view.
    substring_view(const char* data, size_type len,
                   std::shared_ptr<const void> owner = nullptr) noexcept
        : _view(data ? std::string_view(data, len) : std::string_view()),
          _owner(std::move(owner)) {}

    // Stores shared ownership of the fl::string's internal data so the view
    // remains valid independently.
    substring_view(const string& str, size_type offset = 0,
                   size_type len = std::string::npos) noexcept;

    // Copies the std::string into shared storage so the view remains valid
    // independently of the original string's lifetime.
    substring_view(const std::string& str, size_type offset = 0,
                   size_type len = std::string::npos) noexcept;

    substring_view(const substring_view& other) noexcept = default;
    substring_view(substring_view&& other) noexcept = default;
    substring_view& operator=(const substring_view& other) noexcept = default;
    substring_view& operator=(substring_view&& other) noexcept = default;
    ~substring_view() noexcept = default;

    // ========== Element Access ==========

    constexpr reference operator[](size_type pos) const noexcept {
        assert(pos < _view.size());
        return _view[pos];
    }

    const_reference at(size_type pos) const {
        if (pos >= _view.size()) {
            throw std::out_of_range("substring_view::at: position out of range");
        }
        return _view[pos];
    }

    constexpr reference front() const noexcept {
        assert(!empty());
        return _view.front();
    }

    constexpr reference back() const noexcept {
        assert(!empty());
        return _view.back();
    }

    // The returned pointer is NOT null-terminated.
    constexpr const_pointer data() const noexcept {
        return _view.data();
    }

    // Unlike std::string::c_str(), the returned pointer is NOT null-terminated.
    constexpr const_pointer c_str() const noexcept {
        return _view.data();
    }

    // ========== Capacity Queries ==========

    constexpr size_type size() const noexcept {
        return _view.size();
    }

    constexpr size_type length() const noexcept {
        return _view.size();
    }

    constexpr bool empty() const noexcept {
        return _view.empty();
    }

    // ========== Iteration ==========

    constexpr const_iterator begin() const noexcept {
        return _view.begin();
    }

    constexpr const_iterator end() const noexcept {
        return _view.end();
    }

    constexpr const_iterator cbegin() const noexcept {
        return _view.cbegin();
    }

    constexpr const_iterator cend() const noexcept {
        return _view.cend();
    }

    const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(_view.end());
    }

    const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(_view.begin());
    }

    const_reverse_iterator crbegin() const noexcept {
        return const_reverse_iterator(_view.cend());
    }

    const_reverse_iterator crend() const noexcept {
        return const_reverse_iterator(_view.cbegin());
    }

    // ========== Comparison Operations ==========

    [[nodiscard]] std::strong_ordering operator<=>(const substring_view& other) const noexcept {
        return _view <=> other._view;
    }

    [[nodiscard]] bool operator==(const substring_view& other) const noexcept {
        return _view == other._view;
    }

    [[nodiscard]] bool operator!=(const substring_view& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] bool operator==(const char* cstr) const noexcept {
        if (!cstr) return empty();
        return _view == cstr;
    }

    [[nodiscard]] bool operator!=(const char* cstr) const noexcept {
        return !(*this == cstr);
    }

    // ========== Search Operations ==========

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
        if (!substr) return offset <= _view.size() ? offset : npos;
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

    // ========== Substring Operations ==========

    [[nodiscard]] substring_view substr(size_type offset = 0,
                          size_type len = std::string::npos) const noexcept {
        if (offset >= _view.size()) return substring_view();
        auto fragment = _view.substr(offset, len);
        return substring_view(fragment.data(), fragment.size(), _owner);
    }

    [[nodiscard]] bool starts_with(const substring_view& prefix) const noexcept {
        return _view.starts_with(prefix._view);
    }

    [[nodiscard]] bool ends_with(const substring_view& suffix) const noexcept {
        return _view.ends_with(suffix._view);
    }

    [[nodiscard]] bool contains(const substring_view& substr) const noexcept {
        return find(substr) != npos;
    }

    // ========== Conversion ==========

    // Allocates a new std::string copy of the viewed data.
    [[nodiscard]] std::string to_string() const {
        return std::string(_view);
    }

    // Allocates a new fl::string copy of the viewed data.
    string to_fl_string() const;

    // ========== Static Members ==========

    // Sentinel value for "not found", matching std::string::npos.
    static constexpr size_type npos = std::string::npos;

private:
    std::string_view _view;
    std::shared_ptr<const void> _owner;  // Shared ownership of underlying data.
};

// ============================================================================
// Deduction guides and helper functions.
// ============================================================================

// Creates a substring_view from a string literal with automatic length
// deduction.
template<std::size_t N>
inline substring_view make_substring_view(const char (&arr)[N]) noexcept {
    return substring_view(arr, N - 1);  // N-1 to exclude null terminator.
}

// Hash functor for substring_view, suitable for use in std::unordered_map.
struct substring_view_hash {
    std::size_t operator()(const substring_view& view) const noexcept {
        // Uses the FNV-1a hash algorithm.
        std::size_t hash = 14695981039346656037ULL;  // FNV-1a offset basis.
        for (char c : view) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ULL;  // FNV-1a prime.
        }
        return hash;
    }
};

// Equality functor for substring_view, suitable for use in std::unordered_map.
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
