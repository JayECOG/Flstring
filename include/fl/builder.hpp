// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_BUILDER_HPP
#define FL_BUILDER_HPP

// Move-friendly string builder with capacity management and configurable growth
// policies for efficient construction of fl::string instances.

#include "string.hpp"
#include <concepts>
#include <span>
#include "arena.hpp"
#include <cstring>
#include <utility>
#include <algorithm>
#include "fl/profiling.hpp"

namespace fl {

// Growth policy for string builders.
enum class growth_policy {
    linear,      // Grow by constant amount.
    exponential, // Grow by multiplier (1.5x or 2x).
};

// A string builder that accumulates characters into a contiguous buffer and
// produces an fl::string via build(). The builder owns its buffer and supports
// move semantics but not copying. A configurable growth policy controls how
// the internal buffer expands when more space is needed.
class string_builder {
public:
    using size_type = std::size_t;

    string_builder() noexcept : _buffer(nullptr), _capacity(0), _size(0),
                                  _growth_policy(growth_policy::exponential),
                                  _linear_growth(32), _owns_buffer(true) {}

    explicit string_builder(size_type initial_capacity) noexcept
        : _buffer(nullptr), _capacity(0), _size(0),
          _growth_policy(growth_policy::exponential),
          _linear_growth(32), _owns_buffer(true) {
        if (initial_capacity > 0) {
            reserve(initial_capacity);
        }
    }

    string_builder(string_builder&& other) noexcept
        : _buffer(other._buffer), _capacity(other._capacity), _size(other._size),
          _growth_policy(other._growth_policy), _linear_growth(other._linear_growth),
          _owns_buffer(other._owns_buffer) {
        other._buffer = nullptr;
        other._capacity = 0;
        other._size = 0;
        other._owns_buffer = false;
    }

    string_builder& operator=(string_builder&& other) noexcept {
        if (this != &other) {
            if (_owns_buffer && _buffer) {
                fl::deallocate_bytes(_buffer, _capacity);
            }
            _buffer = other._buffer;
            _capacity = other._capacity;
            _size = other._size;
            _growth_policy = other._growth_policy;
            _linear_growth = other._linear_growth;
            _owns_buffer = other._owns_buffer;

            other._buffer = nullptr;
            other._capacity = 0;
            other._size = 0;
            other._owns_buffer = false;
        }
        return *this;
    }

    // Non-copyable because the builder owns a raw buffer.
    string_builder(const string_builder&) = delete;
    string_builder& operator=(const string_builder&) = delete;

    ~string_builder() noexcept {
        if (_owns_buffer && _buffer) {
            fl::deallocate_bytes(_buffer, _capacity);
        }
    }

    string_builder& reserve(size_type cap) noexcept {
        if (cap > _capacity) {
            _grow_to(cap);
        }
        return *this;
    }

    // Reserves capacity assuming the given number of elements, each of
    // avg_element_size bytes. Useful when the element count is known ahead of
    // time but individual sizes vary.
    string_builder& reserve_for_elements(size_type element_count, size_type avg_element_size = 16) noexcept {
        constexpr size_type max_size = static_cast<size_type>(-1);
        if (element_count > max_size / avg_element_size) {
            return reserve(max_size);
        }
        return reserve(element_count * avg_element_size);
    }

    string_builder& set_growth_policy(growth_policy policy) noexcept {
        _growth_policy = policy;
        return *this;
    }

    string_builder& set_linear_growth(size_type increment) noexcept {
        _linear_growth = std::max(size_type(1), increment);
        return *this;
    }

    string_builder& append(const char* cstr) noexcept {
        if (cstr) {
            return append(cstr, std::strlen(cstr));
        }
        return *this;
    }

    string_builder& append(const char* cstr, size_type len) noexcept {
        if (len == 0) return *this;

        size_type new_size = _size + len;
        if (new_size > _capacity) {
            _grow_for_size(new_size);
        }

        std::memcpy(_buffer + _size, cstr, len);
        _size = new_size;
        return *this;
    }

    string_builder& append(const string& str) noexcept {
        return append(str.data(), str.size());
    }

    string_builder& append(std::string_view sv) noexcept {
        return append(sv.data(), sv.size());
    }

    string_builder& append(std::span<const char> s) noexcept {
        return append(s.data(), s.size());
    }

    // Appends a range given by iterators. For random-access iterators the
    // required space is reserved in one shot; for input iterators each element
    // is appended individually.
    template <std::input_iterator InputIter>
    string_builder& append(InputIter first, InputIter last) noexcept {
        if constexpr (std::random_access_iterator<InputIter>) {
            size_type count = static_cast<size_type>(std::distance(first, last));
            if (count == 0) return *this;

            size_type new_size = _size + count;
            if (new_size > _capacity) {
                _grow_for_size(new_size);
            }

            char* ptr = _buffer + _size;
            while (first != last) {
                *ptr++ = *first++;
            }
            _size = new_size;
        } else {
            while (first != last) {
                append(*first);
                ++first;
            }
        }
        return *this;
    }

    string_builder& append(char ch) noexcept {
        if (_size >= _capacity) {
            _grow_for_size(_size + 1);
        }
        _buffer[_size++] = ch;
        return *this;
    }

    string_builder& operator+=(const char* cstr) noexcept { return append(cstr); }
    string_builder& operator+=(const string& str) noexcept { return append(str); }
    string_builder& operator+=(char ch) noexcept { return append(ch); }
    string_builder& operator+=(std::string_view sv) noexcept { return append(sv); }

    string_builder& append_repeat(char ch, size_type count) noexcept {
        if (count == 0) return *this;

        size_type new_size = _size + count;
        if (new_size > _capacity) {
            _grow_for_size(new_size);
        }

        std::fill(_buffer + _size, _buffer + new_size, ch);
        _size = new_size;
        return *this;
    }

    // Appends a formatted string by replacing the first "{}" placeholder with
    // the string representation of the given value. Supports integral,
    // floating-point, and string_view-convertible types.
    template <typename T>
    requires (std::integral<T> || std::floating_point<T> || std::convertible_to<T, std::string_view>)
    string_builder& append_formatted(const char* fmt, T value) noexcept {
        // Find and replace first {} with value representation.
        const char* p = fmt;
        while (*p) {
            if (*p == '{' && *(p + 1) == '}') {
                size_type prefix_len = p - fmt;
                append(fmt, prefix_len);

                char temp[64];
                size_type len = 0;

                if constexpr (std::convertible_to<T, std::string_view>) {
                    std::string_view sv = value;
                    append(sv.data(), sv.size());
                } else if constexpr (std::integral<T>) {
                    if constexpr (std::signed_integral<T>) {
                        len = _format_int64(temp, sizeof(temp), static_cast<int64_t>(value));
                    } else {
                        len = _format_uint64(temp, sizeof(temp), static_cast<uint64_t>(value));
                    }
                    append(temp, len);
                } else if constexpr (std::floating_point<T>) {
                    len = static_cast<size_type>(std::snprintf(temp, sizeof(temp), "%g", static_cast<double>(value)));
                    if (len > 0) append(temp, len);
                }

                append(p + 2);
                return *this;
            }
            ++p;
        }
        // No placeholder found; append the format string as-is.
        append(fmt);
        return *this;
    }

    // Builds the final fl::string from the accumulated content. Transfers
    // buffer ownership to the returned string for large results and uses SSO
    // for small ones. The builder is left in an empty, valid state. Must be
    // called on an rvalue (e.g., std::move(builder).build()).
    [[nodiscard]] string build() && noexcept {
        if (_size == 0) {
            return string();
        }

        if (_size < SSO_THRESHOLD) {
            string result(_buffer, _size);
            if (_owns_buffer && _buffer) {
                fl::deallocate_bytes(_buffer, _capacity);
            }
            _buffer = nullptr;
            _capacity = 0;
            _size = 0;
            return result;
        }

        // Transfer heap buffer ownership to the new string.
        string result;
        result._size = _size;
        result._flags = 0x01;
        result._data.heap.ptr = _buffer;
        result._data.heap.capacity = _capacity;

        _owns_buffer = false;
        _buffer = nullptr;
        _capacity = 0;
        _size = 0;

        return result;
    }

    [[nodiscard]] size_type size() const noexcept {
        return _size;
    }

    [[nodiscard]] size_type capacity() const noexcept {
        return _capacity;
    }

    [[nodiscard]] bool empty() const noexcept {
        return _size == 0;
    }

    // Clears content but preserves the allocated buffer for reuse.
    void clear() noexcept {
        _size = 0;
    }

    [[nodiscard]] const char* data() const noexcept {
        return _buffer;
    }

    [[nodiscard]] char* data() noexcept {
        return _buffer;
    }

    [[nodiscard]] char& operator[](size_type pos) noexcept {
        return _buffer[pos];
    }

    [[nodiscard]] const char& operator[](size_type pos) const noexcept {
        return _buffer[pos];
    }

    char* begin() noexcept { return _buffer; }
    const char* begin() const noexcept { return _buffer; }
    char* end() noexcept { return _buffer + _size; }
    const char* end() const noexcept { return _buffer + _size; }

private:
    char* _buffer;
    size_type _capacity;
    size_type _size;
    growth_policy _growth_policy;
    size_type _linear_growth;
    bool _owns_buffer;

    void _grow_to(size_type new_capacity) noexcept {
        if (new_capacity <= _capacity) return;

        char* new_buffer = static_cast<char*>(fl::allocate_bytes(new_capacity));
        if (_buffer && _size > 0) {
            std::memcpy(new_buffer, _buffer, _size);
        }

        if (_owns_buffer && _buffer) {
            fl::deallocate_bytes(_buffer, _capacity);
        }

        _buffer = new_buffer;
        _capacity = new_capacity;
        _owns_buffer = true;
    }

    void _grow_for_size(size_type min_size) noexcept {
        if (min_size <= _capacity) return;
        size_type new_capacity = _calculate_growth_capacity(min_size);
        _grow_to(new_capacity);
    }

    size_type _calculate_growth_capacity(size_type min_size) const noexcept {
        constexpr size_type kInitialCapacity = 64;
        constexpr size_type kHalfGrowthThreshold = 256;

        size_type target = std::max(min_size, kInitialCapacity);

        if (_growth_policy == growth_policy::linear) {
            if (_capacity >= target) {
                return _capacity;
            }
            size_type steps = (target - _capacity + _linear_growth - 1) / _linear_growth;
            return std::max(_capacity + steps * _linear_growth, target);
        }

        size_type candidate = std::max(_capacity, kInitialCapacity);
        if (candidate == 0) {
            candidate = kInitialCapacity;
        }

        while (candidate < target) {
            if (candidate < kHalfGrowthThreshold) {
                candidate = std::max(candidate * 2, kInitialCapacity);
            } else {
                candidate = candidate + (candidate / 2);
            }
        }

        return candidate;
    }

    static size_type _format_int64(char* buffer, size_type capacity, int64_t value) noexcept {
        if (capacity == 0) return 0;

        if (value == 0) {
            buffer[0] = '0';
            return 1;
        }

        bool negative = value < 0;
        uint64_t uvalue = negative ? (value == INT64_MIN ? static_cast<uint64_t>(INT64_MAX) + 1 : static_cast<uint64_t>(-value)) : static_cast<uint64_t>(value);

        size_type len = 0;
        uint64_t temp = uvalue;
        while (temp > 0) {
            ++len;
            temp /= 10;
        }

        size_type total_len = negative ? len + 1 : len;
        if (total_len > capacity) return 0;

        size_type pos = total_len;
        while (uvalue > 0) {
            buffer[--pos] = '0' + (uvalue % 10);
            uvalue /= 10;
        }

        if (negative) {
            buffer[0] = '-';
        }

        return total_len;
    }

    static size_type _format_uint64(char* buffer, size_type capacity, uint64_t value) noexcept {
        if (capacity == 0) return 0;

        if (value == 0) {
            buffer[0] = '0';
            return 1;
        }

        char temp[20];
        size_type len = 0;
        while (value > 0) {
            temp[len++] = '0' + (value % 10);
            value /= 10;
        }

        std::reverse_copy(temp, temp + len, buffer);
        return len;
    }
};

}  // namespace fl

#endif  // FL_BUILDER_HPP
