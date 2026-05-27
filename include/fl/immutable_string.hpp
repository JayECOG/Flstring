// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_IMMUTABLE_STRING_HPP
#define FL_IMMUTABLE_STRING_HPP

/// @file Immutable string types.
///
/// Provides immutable_string_view for lightweight non-owning access and
/// immutable_string for thread-safe shared ownership with atomic reference
/// counting.

#include "fl/config.hpp"
#include "fl/alloc_hooks.hpp"
#include "fl/debug/thread_safety.hpp"
#include <atomic>
#include <cstring>
#include <string>
#include <algorithm>
#include <functional>
#include <ostream>
#include <iterator>
#include <memory>
#include <cassert>
#include "fl/profiling.hpp"

namespace fl {

// Forward declaration.
class immutable_string;

/// Immutable string view optimised for use as map keys.
///
/// Provides an immutable, lightweight string view with a lazily computed
/// FNV-1a hash.  Suitable for use in std::unordered_map and similar
/// associative containers.
class immutable_string_view {
public:
    using value_type = char;
    using size_type = std::size_t;
    using const_reference = const char&;
    using iterator = const char*;
    using const_iterator = const char*;

    static constexpr size_type npos = static_cast<size_type>(-1);

    immutable_string_view() noexcept : _data(nullptr), _length(0), _hash(0), _hash_computed(true) {}

    immutable_string_view(const char* cstr) noexcept
        : immutable_string_view(cstr, cstr ? std::strlen(cstr) : 0) {}

    explicit immutable_string_view(const char* data, size_type len) noexcept
        : _data(data), _length(len), _hash(0), _hash_computed(false) {
        assert(len == 0 || data != nullptr);
    }

    const char* data() const noexcept { return _data; }
    size_type size()   const noexcept { return _length; }
    size_type length() const noexcept { return _length; }
    bool      empty()  const noexcept { return _length == 0; }

    char operator[](size_type pos) const noexcept { assert(pos < _length); return _data[pos]; }

    char at(size_type pos) const {
        if (pos >= _length) throw std::out_of_range("immutable_string_view::at");
        return _data[pos];
    }

    char front() const noexcept { return _data[0]; }
    char back()  const noexcept { return _data[_length - 1]; }

    const char* begin() const noexcept { return _data; }
    const char* end()   const noexcept { return _data + _length; }

    bool operator==(const immutable_string_view& other) const noexcept {
        if (_length != other._length) return false;
        if (_data == other._data) return true;
        return std::memcmp(_data, other._data, _length) == 0;
    }

    /// Returns the cached FNV-1a hash, computing it lazily on first call.
    /// Thread-safe via memory_order_acquire/release on _hash_computed.
    std::size_t hash() const noexcept {
        if (!_hash_computed.load(std::memory_order_acquire)) {
            _hash = fnv1a_hash(_data, _length);
            _hash_computed.store(true, std::memory_order_release);
        }
        return _hash;
    }

private:
    const char* _data;
    size_type _length;
    mutable std::size_t _hash;
    mutable std::atomic<bool> _hash_computed;

    static std::size_t fnv1a_hash(const char* data, size_type len) noexcept {
        std::size_t h = 14695981039346656037ULL;
        for (size_type i = 0; i < len; ++i) {
            h ^= static_cast<unsigned char>(data[i]);
            h *= 1099511628211ULL;
        }
        return h;
    }
};

// -- immutable_string -- atomic RC, cached hash, O(1) copy --------------------

/// Thread-safe immutable string with atomic reference counting.
///
/// Copies are O(1) — they increment the reference count atomically.
/// The control block is cache-line-aligned (alignas(64)) to prevent false
/// sharing.  The FNV-1a hash is computed lazily and cached.
///
/// @note No mutation operations are exposed.  Immutability is enforced at
/// compile time.
class immutable_string {
public:
    using value_type = char;
    using size_type = std::size_t;

    immutable_string() noexcept;
    immutable_string(const char* cstr);
    immutable_string(const char* data, size_type len);
    explicit immutable_string(const std::string& str);
    immutable_string(const immutable_string& other) noexcept;
    immutable_string(immutable_string&& other) noexcept;
    immutable_string& operator=(const immutable_string& other) noexcept;
    immutable_string& operator=(immutable_string&& other) noexcept;
    ~immutable_string();

    // -- Observers -------------------------------------------------------

    const char* data()    const noexcept;
    size_type   size()    const noexcept;
    size_type   length()  const noexcept;
    bool        empty()   const noexcept;
    char        operator[](size_type pos) const noexcept;
    char        at(size_type pos) const;

    const char* begin()   const noexcept { return data(); }
    const char* end()     const noexcept { return data() + size(); }

    explicit operator std::string_view() const noexcept;
    explicit operator std::string() const;

    /// Returns the cached FNV-1a hash.  Thread-safe.
    std::size_t hash() const noexcept;

    // -- Friends ---------------------------------------------------------

    friend bool operator==(const immutable_string& lhs, const immutable_string& rhs) noexcept;
    friend bool operator!=(const immutable_string& lhs, const immutable_string& rhs) noexcept;
    friend bool operator<(const immutable_string& lhs, const immutable_string& rhs) noexcept;

    friend void swap(immutable_string& lhs, immutable_string& rhs) noexcept {
        std::swap(lhs._ctrl, rhs._ctrl);
    }

private:
    /// Control block: reference count, size, data, and cached hash.
    /// Aligned to 64 bytes to avoid false sharing between threads.
    struct alignas(64) control_block {
        std::atomic<std::size_t> refs;
        std::size_t size;
        std::size_t hash;
        std::atomic<bool> hash_computed;
        char data[1];  // Flexible-array-like; actual allocation is sizeof(ctrl)+size.
    };

    control_block* _ctrl;

    static control_block* allocate_ctrl(const char* s, size_type len);
    static void          deallocate_ctrl(control_block* ctrl) noexcept;

    static std::size_t fnv1a_hash(const char* s, size_type len) noexcept {
        std::size_t h = 14695981039346656037ULL;
        for (size_type i = 0; i < len; ++i) {
            h ^= static_cast<unsigned char>(s[i]);
            h *= 1099511628211ULL;
        }
        return h;
    }
};

// -- immutable_string implementation -----------------------------------------

inline immutable_string::immutable_string() noexcept : _ctrl(nullptr) {}

inline immutable_string::immutable_string(const char* cstr)
    : _ctrl(cstr ? allocate_ctrl(cstr, std::strlen(cstr)) : nullptr) {}

inline immutable_string::immutable_string(const char* data, size_type len)
    : _ctrl(data ? allocate_ctrl(data, len) : nullptr) {}

inline immutable_string::immutable_string(const std::string& str)
    : _ctrl(allocate_ctrl(str.data(), str.size())) {}

inline immutable_string::immutable_string(const immutable_string& other) noexcept
    : _ctrl(other._ctrl) {
    if (_ctrl) {
        _ctrl->refs.fetch_add(1, std::memory_order_relaxed);
    }
}

inline immutable_string::immutable_string(immutable_string&& other) noexcept
    : _ctrl(other._ctrl) {
    other._ctrl = nullptr;
}

inline immutable_string& immutable_string::operator=(const immutable_string& other) noexcept {
    if (this != &other) {
        // Decrement current
        if (_ctrl && _ctrl->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            deallocate_ctrl(_ctrl);
        }
        _ctrl = other._ctrl;
        if (_ctrl) {
            _ctrl->refs.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return *this;
}

inline immutable_string& immutable_string::operator=(immutable_string&& other) noexcept {
    if (this != &other) {
        if (_ctrl && _ctrl->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            deallocate_ctrl(_ctrl);
        }
        _ctrl = other._ctrl;
        other._ctrl = nullptr;
    }
    return *this;
}

inline immutable_string::~immutable_string() {
    if (_ctrl && _ctrl->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        deallocate_ctrl(_ctrl);
    }
}

// -- Observers ---------------------------------------------------------------

inline const char* immutable_string::data()   const noexcept { return _ctrl ? _ctrl->data : ""; }
inline auto immutable_string::size()            const noexcept -> size_type { return _ctrl ? _ctrl->size : 0; }
inline auto immutable_string::length()          const noexcept -> size_type { return size(); }
inline bool immutable_string::empty()           const noexcept { return size() == 0; }

inline char immutable_string::operator[](size_type pos) const noexcept {
    return data()[pos];
}

inline char immutable_string::at(size_type pos) const {
    if (pos >= size()) throw std::out_of_range("immutable_string::at");
    return data()[pos];
}

// -- Conversion --------------------------------------------------------------

inline immutable_string::operator std::string_view() const noexcept {
    return std::string_view(data(), size());
}

inline immutable_string::operator std::string() const {
    return std::string(data(), size());
}

// -- Hash --------------------------------------------------------------------

inline std::size_t immutable_string::hash() const noexcept {
    if (!_ctrl) return 0;
    if (!_ctrl->hash_computed.load(std::memory_order_acquire)) {
        _ctrl->hash = fnv1a_hash(_ctrl->data, _ctrl->size);
        _ctrl->hash_computed.store(true, std::memory_order_release);
    }
    return _ctrl->hash;
}

// -- Allocation helpers ------------------------------------------------------

inline immutable_string::control_block*
immutable_string::allocate_ctrl(const char* s, size_type len) {
    const size_type alloc_size = sizeof(control_block) + len;  // +1 for NUL is included in data[1]
    void* mem = fl::allocate_bytes_aligned(alloc_size, alignof(control_block));
    auto* ctrl = ::new (mem) control_block;
    ctrl->refs.store(1, std::memory_order_relaxed);
    ctrl->size = len;
    ctrl->hash = 0;
    ctrl->hash_computed.store(false, std::memory_order_release);
    std::memcpy(ctrl->data, s, len);
    ctrl->data[len] = '\0';
    return ctrl;
}

inline void immutable_string::deallocate_ctrl(control_block* ctrl) noexcept {
    ctrl->~control_block();
    fl::deallocate_bytes_aligned(ctrl, sizeof(control_block) + ctrl->size, alignof(control_block));
}

// -- Comparison operators ----------------------------------------------------

inline bool operator==(const immutable_string& lhs, const immutable_string& rhs) noexcept {
    if (lhs._ctrl == rhs._ctrl) return true;
    if (!lhs._ctrl || !rhs._ctrl) return false;
    return lhs._ctrl->size == rhs._ctrl->size &&
           std::memcmp(lhs._ctrl->data, rhs._ctrl->data, lhs._ctrl->size) == 0;
}

inline bool operator!=(const immutable_string& lhs, const immutable_string& rhs) noexcept {
    return !(lhs == rhs);
}

inline bool operator<(const immutable_string& lhs, const immutable_string& rhs) noexcept {
    if (!lhs._ctrl) return rhs._ctrl != nullptr;
    if (!rhs._ctrl) return false;
    const std::size_t min_len = (std::min)(lhs._ctrl->size, rhs._ctrl->size);
    int cmp = std::memcmp(lhs._ctrl->data, rhs._ctrl->data, min_len);
    if (cmp != 0) return cmp < 0;
    return lhs._ctrl->size < rhs._ctrl->size;
}

// -- Hash / equality functors for associative containers --------------------

/// FNV-1a hash functor for immutable_string, for use in std::unordered_map.
struct immutable_string_hash {
    using is_transparent = void;
    std::size_t operator()(const immutable_string& s) const noexcept { return s.hash(); }
    std::size_t operator()(const immutable_string_view& v) const noexcept { return v.hash(); }
    std::size_t operator()(std::string_view sv) const noexcept {
        std::size_t h = 14695981039346656037ULL;
        for (char c : sv) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
        return h;
    }
};

/// Equality functor for immutable_string, for use in std::unordered_map.
struct immutable_string_equal {
    using is_transparent = void;
    bool operator()(const immutable_string& a, const immutable_string& b) const noexcept { return a == b; }
    bool operator()(const immutable_string& a, std::string_view b) const noexcept {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
    }
    bool operator()(std::string_view a, const immutable_string& b) const noexcept {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
    }
};

/// Non-owning immutable string key with cached hash.
/// Compatible alias kept for API stability.
using owning_immutable_string = immutable_string;

}  // namespace fl

#endif  // FL_IMMUTABLE_STRING_HPP
