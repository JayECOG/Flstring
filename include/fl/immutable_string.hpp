// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_IMMUTABLE_STRING_HPP
#define FL_IMMUTABLE_STRING_HPP

// Immutable string types for the FL library. Provides immutable_string_view for
// lightweight non-owning access and immutable_string for thread-safe shared
// ownership with atomic reference counting.

#include "fl/config.hpp"
#include "fl/alloc_hooks.hpp"
#include "fl/debug/thread_safety.hpp"
#include <atomic>
#include <span>
#include <cstring>
#include <string>
#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <cassert>
#include "fl/profiling.hpp"

namespace fl {

// Forward declarations.
class string;
class substring_view;
class immutable_string;

// Immutable string view optimised for use as map keys.
//
// This type provides an immutable, lightweight string view interface optimised
// for use as keys in associative containers.
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
    size_type size() const noexcept { return _length; }
    size_type length() const noexcept { return _length; }
    bool empty() const noexcept { return _length == 0; }

    char operator[](size_type pos) const noexcept {
        assert(pos < _length);
        return _data[pos];
    }

    char at(size_type pos) const {
        if (pos >= _length) throw std::out_of_range("immutable_string_view::at");
        return _data[pos];
    }

    char front() const noexcept { return _data[0]; }
    char back() const noexcept { return _data[_length - 1]; }

    const char* begin() const noexcept { return _data; }
    const char* end() const noexcept { return _data + _length; }

    bool operator==(const immutable_string_view& other) const noexcept {
        if (_length != other._length) return false;
        if (_data == other._data) return true;
        return std::memcmp(_data, other._data, _length) == 0;
    }

    bool operator!=(const immutable_string_view& other) const noexcept { return !(*this == other); }

    bool operator<(const immutable_string_view& other) const noexcept {
        size_type min_len = std::min(_length, other._length);
        int res = std::memcmp(_data, other._data, min_len);
        if (res != 0) return res < 0;
        return _length < other._length;
    }

    bool operator>(const immutable_string_view& other) const noexcept { return other < *this; }
    bool operator<=(const immutable_string_view& other) const noexcept { return !(*this > other); }
    bool operator>=(const immutable_string_view& other) const noexcept { return !(*this < other); }

    bool contains(char c) const noexcept { return find(c) != npos; }
    bool contains(const immutable_string_view& s) const noexcept { return find(s) != npos; }

    size_type hash() const noexcept {
        if (!_hash_computed) {
            _hash = compute_hash(_data, _length);
            _hash_computed = true;
        }
        return _hash;
    }

    size_type find(char c, size_type pos = 0) const noexcept {
        if (pos >= _length) return npos;
        const char* p = static_cast<const char*>(std::memchr(_data + pos, c, _length - pos));
        return p ? static_cast<size_type>(p - _data) : npos;
    }

    size_type find(const immutable_string_view& needle, size_type pos = 0) const noexcept {
        if (pos > _length) return npos;
        if (needle._length == 0) return pos;
        if (needle._length > _length - pos) return npos;

        auto it = std::search(begin() + pos, end(), needle.begin(), needle.end());
        return it != end() ? static_cast<size_type>(it - begin()) : npos;
    }

    std::string to_string() const { return std::string(_data, _length); }

private:
    // FNV-1a hash for string data.
    static size_type compute_hash(const char* s, size_type len) noexcept {
        size_type h = 0x811c9dc5;
        for (size_type i = 0; i < len; ++i) {
            h ^= static_cast<size_type>(s[i]);
            h *= 0x01000193;
        }
        return h;
    }

    const char* _data;
    size_type _length;
    mutable size_type _hash;
    mutable bool _hash_computed;
};

// Thread-safe immutable string with atomic reference counting.
//
// Thread-safety guarantees:
// - All operations are thread-safe and lock-free.
// - Concurrent reads from multiple threads: safe.
// - Concurrent copies across threads: safe.
// - No mutation operations exist; immutability is enforced at compile time.
//
// Performance characteristics:
// - Copy: O(1) atomic increment, lock-free.
// - Destruction: O(1) atomic decrement plus conditional O(n) deallocation.
// - Memory overhead: sizeof(std::atomic<size_t>) + 2*sizeof(size_t) + overhead.
// - Cache-line considerations: control block is aligned to avoid false sharing.
// - Hash computation: cached in control block for O(1) map lookups after first
//   call to hash().
class immutable_string {
    struct control_block {
        alignas(64) std::atomic<std::size_t> refcount;
        std::size_t size;
        mutable std::size_t cached_hash;
        mutable std::atomic<bool> hash_computed;
        char buf[1];  // Flexible array member.

        const char* data() const noexcept { return buf; }
    };

    control_block* _ctrl;
#if FL_DEBUG_THREAD_SAFETY
    mutable debug::thread_access_tracker _tracker;
#endif

public:
    using value_type = char;
    using size_type = std::size_t;
    using const_reference = const char&;
    using const_iterator = const char*;

    immutable_string() noexcept : _ctrl(nullptr) {}

    explicit immutable_string(const char* str) : _ctrl(nullptr) {
        if (str) {
            allocate_and_init(str, std::strlen(str));
        }
    }

    immutable_string(const char* str, size_type len) : _ctrl(nullptr) {
        if (str || len == 0) {
            allocate_and_init(str, len);
        }
    }

    immutable_string(immutable_string_view view) : _ctrl(nullptr) {
        if (!view.empty()) {
            allocate_and_init(view.data(), view.size());
        }
    }

    explicit immutable_string(const std::string& str) : _ctrl(nullptr) {
        allocate_and_init(str.data(), str.size());
    }

    // Thread-safety: safe to copy concurrently from multiple threads.
    // Relaxed ordering suffices because the caller already holds a live reference.
    immutable_string(const immutable_string& other) noexcept : _ctrl(other._ctrl) {
        if (_ctrl) {
            _ctrl->refcount.fetch_add(1, std::memory_order_relaxed);
        }
    }

    immutable_string(immutable_string&& other) noexcept : _ctrl(other._ctrl) {
        other._ctrl = nullptr;
#if FL_DEBUG_THREAD_SAFETY
        other._tracker.mark_moved(FL_LOC);
#endif
    }

    // The acq_rel decrement synchronises with the acquire fence so that all
    // prior accesses in other threads are visible before deallocation.
    ~immutable_string() noexcept {
        if (_ctrl) {
            if (_ctrl->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                destroy_control_block(_ctrl);
            }
        }
    }

    immutable_string& operator=(const immutable_string& other) noexcept {
        if (this != &other) {
            if (other._ctrl) {
                other._ctrl->refcount.fetch_add(1, std::memory_order_relaxed);
            }
            if (_ctrl) {
                if (_ctrl->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    destroy_control_block(_ctrl);
                }
            }
            _ctrl = other._ctrl;
        }
        return *this;
    }

    immutable_string& operator=(immutable_string&& other) noexcept {
        if (this != &other) {
            if (_ctrl) {
                if (_ctrl->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    destroy_control_block(_ctrl);
                }
            }
            _ctrl = other._ctrl;
            other._ctrl = nullptr;
#if FL_DEBUG_THREAD_SAFETY
            other._tracker.mark_moved(FL_LOC);
#endif
        }
        return *this;
    }

    // Observers.

    [[nodiscard]] const char* data() const noexcept {
#if FL_DEBUG_THREAD_SAFETY
        auto g = _tracker.begin_read(FL_LOC);
#endif
        return _ctrl ? _ctrl->data() : "";
    }

    [[nodiscard]] size_type size() const noexcept {
#if FL_DEBUG_THREAD_SAFETY
        auto g = _tracker.begin_read(FL_LOC);
#endif
        return _ctrl ? _ctrl->size : 0;
    }

    [[nodiscard]] size_type length() const noexcept { return size(); }
    [[nodiscard]] bool empty() const noexcept { return _ctrl == nullptr; }

    [[nodiscard]] char operator[](std::size_t pos) const noexcept {
        return view()[pos];
    }

    [[nodiscard]] immutable_string_view view() const noexcept {
#if FL_DEBUG_THREAD_SAFETY
        auto g = _tracker.begin_read(FL_LOC);
#endif
        return _ctrl ? immutable_string_view(_ctrl->data(), _ctrl->size) : immutable_string_view();
    }

    [[nodiscard]] std::string to_string() const { return view().to_string(); }

    [[nodiscard]] operator immutable_string_view() const noexcept { return view(); }

    // Hash computation is cached in the control block. The acquire/release pair
    // on hash_computed ensures the cached_hash write is visible to all
    // subsequent readers.
    [[nodiscard]] size_type hash() const noexcept {
        if (!_ctrl) return immutable_string_view().hash();

        if (!_ctrl->hash_computed.load(std::memory_order_acquire)) {
            _ctrl->cached_hash = immutable_string_view(_ctrl->data(), _ctrl->size).hash();
            _ctrl->hash_computed.store(true, std::memory_order_release);
        }
        return _ctrl->cached_hash;
    }

private:
    void allocate_and_init(const char* s, size_type len) {
        size_type bytes = sizeof(control_block) + len;
        void* mem = fl::allocate_bytes(bytes);
        if (!mem) throw std::bad_alloc();

        _ctrl = static_cast<control_block*>(mem);
        new (&_ctrl->refcount) std::atomic<std::size_t>(1);
        _ctrl->size = len;
        _ctrl->cached_hash = 0;
        new (&_ctrl->hash_computed) std::atomic<bool>(false);
        if (s && len > 0) {
            std::memcpy(_ctrl->buf, s, len);
        }
        _ctrl->buf[len] = '\0';
    }

    void destroy_control_block(control_block* cb) {
        cb->hash_computed.~atomic();
        cb->refcount.~atomic();
        fl::deallocate_bytes(cb, sizeof(control_block) + cb->size);
    }
};

// Alias for compatibility with previous versions.
using owning_immutable_string = immutable_string;

// Operators and functors.

inline bool operator==(const immutable_string& lhs, const immutable_string& rhs) noexcept {
    if (&lhs == &rhs) return true;
    return lhs.view() == rhs.view();
}

inline bool operator!=(const immutable_string& lhs, const immutable_string& rhs) noexcept {
    return !(lhs == rhs);
}

inline bool operator==(const immutable_string_view& lhs, const char* rhs) noexcept {
    return lhs == immutable_string_view(rhs);
}

inline bool operator==(const char* lhs, const immutable_string_view& rhs) noexcept {
    return immutable_string_view(lhs) == rhs;
}

inline bool operator==(const immutable_string& lhs, const immutable_string_view& rhs) noexcept {
    return lhs.view() == rhs;
}

inline bool operator==(const immutable_string_view& lhs, const immutable_string& rhs) noexcept {
    return lhs == rhs.view();
}

struct immutable_string_hash {
    std::size_t operator()(const immutable_string_view& v) const noexcept { return v.hash(); }
    std::size_t operator()(const immutable_string& s) const noexcept { return s.hash(); }
};

struct immutable_string_equal {
    using is_transparent = void;
    bool operator()(const immutable_string_view& lhs, const immutable_string_view& rhs) const noexcept { return lhs == rhs; }
    bool operator()(const immutable_string& lhs, const immutable_string& rhs) const noexcept { return lhs == rhs; }
};

inline std::ostream& operator<<(std::ostream& os, const immutable_string_view& v) {
    if (v.empty()) return os;
    return os.write(v.data(), static_cast<std::streamsize>(v.size()));
}

inline std::ostream& operator<<(std::ostream& os, const immutable_string& s) {
    return os << s.view();
}

} // namespace fl

#endif // FL_IMMUTABLE_STRING_HPP
