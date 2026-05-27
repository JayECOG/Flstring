// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_SYNCHRONISED_STRING_HPP
#define FL_SYNCHRONISED_STRING_HPP

/// @file Thread-safe mutable string wrapper.
///
/// Synchronisation is provided via `std::shared_mutex`, and callback-based
/// access prevents raw reference leaks that could bypass the lock.

#include "fl/config.hpp"
#include "fl/string.hpp"
#include "fl/debug/thread_safety.hpp"
#include <shared_mutex>
#include <mutex>
#include <functional>
#include <concepts>
#include <utility>
#include <string_view>

namespace fl {

/// Explicitly synchronised mutable string with internal locking.
///
/// Thread-safety guarantees:
/// - All public methods are fully thread-safe via internal mutex.
/// - Concurrent mutations from multiple threads: safe (serialised).
/// - Concurrent reads while writing: safe (readers block on mutex).
/// - Exception guarantee: strong (mutations are transactional relative to
///   storage).
///
/// Performance characteristics:
/// - Mutation operations: O(string_op) + mutex lock overhead.
/// - Read operations: O(string_op) + shared lock overhead.
/// - Memory overhead: sizeof(std::shared_mutex).
///
/// Design rationale:
/// - Uses std::shared_mutex to permit concurrent readers when no writer is
///   active.
/// - Access is primarily through callbacks (read/write) to prevent raw
///   reference leaks.
class synchronised_string {
public:
    using value_type = char;
    using size_type = std::size_t;

    synchronised_string() = default;
    explicit synchronised_string(const char* cstr) : _data(cstr) {}
    explicit synchronised_string(const fl::string& s) : _data(s) {}

    /// Copy constructor: copies protected state under the source's shared lock.
    synchronised_string(const synchronised_string& other) {
        other.read([this](const fl::string& s) { _data = s; });
    }

    /// Move constructor: takes exclusive access to the source and steals its
    /// storage.  Uses std::scoped_lock to avoid deadlock when the source is
    /// under a concurrent shared lock.
    synchronised_string(synchronised_string&& other) noexcept {
        std::scoped_lock lock(_mutex, other._mutex);
        _data = std::move(other._data);
    }

    /// Acquires both locks in a deadlock-free manner via std::scoped_lock.
    synchronised_string& operator=(const synchronised_string& other) {
        if (this == &other) return *this;
        std::scoped_lock lock(_mutex, other._mutex);
        _data = other._data;
        return *this;
    }

    /// Move-assigns the underlying storage under both locks.
    synchronised_string& operator=(synchronised_string&& other) noexcept {
        if (this == &other) return *this;
        std::scoped_lock lock(_mutex, other._mutex);
        _data = std::move(other._data);
        return *this;
    }

    ~synchronised_string() = default;

    // -- Observers -------------------------------------------------------

    /// Returns the number of characters.
    [[nodiscard]] size_type size() const noexcept {
        return read([](const fl::string& s) noexcept { return s.size(); });
    }

    /// Returns true when the string contains no characters.
    [[nodiscard]] bool empty() const noexcept {
        return read([](const fl::string& s) noexcept { return s.empty(); });
    }

    /// Returns a copy of the underlying fl::string.
    [[nodiscard]] fl::string to_fl_string() const noexcept {
        return read([](const fl::string& s) noexcept { return s; });
    }

    // -- Modifiers -------------------------------------------------------

    synchronised_string& operator+=(const char* cstr) {
        write([&](fl::string& s) { s += cstr; }); return *this;
    }
    synchronised_string& operator+=(const fl::string& other) {
        write([&](fl::string& s) { s += other; }); return *this;
    }
    synchronised_string& operator+=(std::string_view sv) {
        write([&](fl::string& s) { s += sv; }); return *this;
    }
    synchronised_string& operator+=(char ch) {
        write([&](fl::string& s) { s += ch; }); return *this;
    }

    /// Swaps contents with another synchronised_string.
    /// Locks both mutexes in a deadlock-free manner via std::scoped_lock.
    void swap(synchronised_string& other) noexcept {
        if (this == &other) return;
        std::scoped_lock lock(_mutex, other._mutex);
        _data.swap(other._data);
    }

    /// Three-way comparison against another synchronised_string.
    /// Returns negative, zero, or positive.
    int compare(const synchronised_string& other) const noexcept {
        if (this == &other) return 0;
        std::scoped_lock lock(_mutex, other._mutex);
        std::string_view lhs(_data.data(), _data.size());
        std::string_view rhs(other._data.data(), other._data.size());
        auto cmp = lhs <=> rhs;
        return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
    }

    /// Three-way comparison against a std::string_view.
    int compare(std::string_view sv) const noexcept {
        return read([&](const fl::string& s) noexcept {
            std::string_view lhs(s.data(), s.size());
            auto cmp = lhs <=> sv;
            return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
        });
    }

    synchronised_string& append(const char* buf, size_type len) {
        write([&](fl::string& dest) { dest.append(buf, len); }); return *this;
    }
    synchronised_string& append(const fl::string& other) {
        write([&](fl::string& dest) { dest.append(other); }); return *this;
    }
    synchronised_string& append(std::string_view sv) {
        write([&](fl::string& dest) { dest.append(sv.data(), sv.size()); }); return *this;
    }
    void push_back(char ch) { write([&](fl::string& dest) { dest.push_back(ch); }); }
    void pop_back() { write([&](fl::string& dest) { dest.pop_back(); }); }
    void clear() { write([](fl::string& s) { s.clear(); }); }

    // -- Callback-based access -------------------------------------------

    /// Acquires a shared lock (permitting concurrent readers) and invokes the
    /// callback with a const reference to the underlying string.
    template <std::invocable<const fl::string&> Func>
    auto read(Func&& f) const
        noexcept(std::is_nothrow_invocable_v<Func, const fl::string&>)
        -> std::invoke_result_t<Func, const fl::string&>
    {
#if FL_DEBUG_THREAD_SAFETY
        auto g = _tracker.begin_read(FL_LOC);
#endif
        std::shared_lock lock(_mutex);
        return std::invoke(std::forward<Func>(f), _data);
    }

    /// Acquires an exclusive lock (blocking all readers and writers) and
    /// invokes the callback with a mutable reference to the underlying string.
    template <std::invocable<fl::string&> Func>
    auto write(Func&& f)
        noexcept(std::is_nothrow_invocable_v<Func, fl::string&>)
        -> std::invoke_result_t<Func, fl::string&>
    {
#if FL_DEBUG_THREAD_SAFETY
        auto g = _tracker.begin_write(FL_LOC);
#endif
        std::unique_lock lock(_mutex);
        return std::invoke(std::forward<Func>(f), _data);
    }

    /// Returns a snapshot copy of the underlying string.
    [[nodiscard]] fl::string snapshot() const noexcept {
        return read([](const fl::string& s) noexcept { return s; });
    }

private:
#if FL_DEBUG_THREAD_SAFETY
    mutable debug::thread_access_tracker _tracker;
#endif
    mutable std::shared_mutex _mutex;
    fl::string _data;
};

// -- Comparison operators ------------------------------------------------

inline bool operator==(const synchronised_string& lhs, const synchronised_string& rhs) noexcept {
    return lhs.compare(rhs) == 0;
}
inline bool operator!=(const synchronised_string& lhs, const synchronised_string& rhs) noexcept {
    return lhs.compare(rhs) != 0;
}
inline bool operator<(const synchronised_string& lhs, const synchronised_string& rhs) noexcept {
    return lhs.compare(rhs) < 0;
}
inline bool operator<=(const synchronised_string& lhs, const synchronised_string& rhs) noexcept {
    return lhs.compare(rhs) <= 0;
}
inline bool operator>(const synchronised_string& lhs, const synchronised_string& rhs) noexcept {
    return lhs.compare(rhs) > 0;
}
inline bool operator>=(const synchronised_string& lhs, const synchronised_string& rhs) noexcept {
    return lhs.compare(rhs) >= 0;
}

/// US-spelling alias.
using synchronized_string = synchronised_string;

} // namespace fl

#endif // FL_SYNCHRONISED_STRING_HPP
