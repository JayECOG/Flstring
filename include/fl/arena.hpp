// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_ARENA_HPP
#define FL_ARENA_HPP

/// @file Arena-based allocation utilities.
///
/// Provides a bump-pointer arena allocator, a growable character buffer backed
/// by an arena, and a thread-local pool of reusable temporary buffers.

#include "fl/config.hpp"
#include <cstring>
#include "fl/alloc_hooks.hpp"
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <thread>
#include <string_view>
#include <limits>
#include <cassert>
#include "fl/profiling.hpp"

namespace fl {

namespace detail {
    constexpr std::size_t DEFAULT_ARENA_STACK_SIZE = 4096;
    constexpr std::size_t DEFAULT_BUFFER_INITIAL_CAPACITY = 512;
} // namespace detail

/// A bump-pointer allocator that serves small allocations from a fixed-size
/// stack-local buffer and falls back to the heap for requests that do not fit.
///
/// All allocations are 8-byte aligned.  Heap blocks are freed on destruction or
/// when reset() is called.  Non-copyable and non-movable because outstanding
/// pointers refer into the internal stack buffer.
///
/// @warning This allocator is not thread-safe.  Allocate and deallocate from
/// the same thread that created the allocator.  Cross-thread usage of
/// temp_buffer (which uses thread-local pooling) is unsafe and will cause data
/// corruption.
template <std::size_t StackSize = detail::DEFAULT_ARENA_STACK_SIZE>
class arena_allocator {
public:
    using value_type = std::uint8_t;

    arena_allocator() noexcept : _stack_buffer(), _stack_used(0), _heap_blocks() {}

    ~arena_allocator() noexcept {
        for (auto& b : _heap_blocks) {
            fl::deallocate_bytes(b.first, b.second);
        }
    }

    arena_allocator(const arena_allocator&) = delete;
    arena_allocator& operator=(const arena_allocator&) = delete;
    arena_allocator(arena_allocator&&) = delete;
    arena_allocator& operator=(arena_allocator&&) = delete;

    /// Allocates a block of the given size.
    /// Serves from the stack buffer if possible; otherwise falls back to heap.
    FL_INLINE void* allocate(std::size_t size) {
        // Guard against overflow in the alignment arithmetic.
        if (FL_UNLIKELY(size > std::numeric_limits<std::size_t>::max() - 7)) {
            throw std::bad_alloc();
        }
        std::size_t aligned_size = (size + 7) & ~7;

        if (FL_LIKELY(_stack_used <= StackSize && aligned_size <= StackSize - _stack_used)) {
            void* ptr = _stack_buffer + _stack_used;
            _stack_used += aligned_size;
            return ptr;
        }
        return _allocate_heap(aligned_size);
    }

    /// Deallocate is a no-op for stack allocations (bump-pointer reset).
    /// For heap allocations, the block is freed on arena destruction or reset().
    void deallocate(void* /*ptr*/, std::size_t /*size*/) noexcept {}

    /// Returns the number of bytes used from the stack buffer.
    std::size_t used() const noexcept { return _stack_used; }

    /// Returns true if all allocations fit within the stack buffer.
    bool stack_only() const noexcept { return _heap_blocks.empty(); }

    /// Resets the allocator, freeing all heap blocks and rewinding the stack.
    void reset() noexcept {
        _stack_used = 0;
        for (auto& b : _heap_blocks) {
            fl::deallocate_bytes(b.first, b.second);
        }
        _heap_blocks.clear();
    }

private:
    FL_INLINE void* _allocate_heap(std::size_t size) {
        auto* ptr = fl::allocate_bytes(size);
        _heap_blocks.emplace_back(ptr, size);
        return ptr;
    }

    alignas(std::max_align_t) char _stack_buffer[StackSize];
    std::size_t _stack_used;
    std::vector<std::pair<void*, std::size_t>> _heap_blocks;
};

/// An append-only character buffer backed by an arena_allocator.
///
/// For typical sizes, all memory comes from the arena's stack region, avoiding
/// the global heap entirely.  Growth beyond the stack region falls back to the
/// arena's heap allocation path.
template <std::size_t StackSize = detail::DEFAULT_ARENA_STACK_SIZE>
class arena_buffer {
public:
    using value_type = char;
    using size_type = std::size_t;

    arena_buffer() noexcept { _buf[0] = '\0'; }

    ~arena_buffer() noexcept { reset(); }

    explicit arena_buffer(std::size_t initial_capacity) {
        if (initial_capacity <= StackSize) {
            _buf[0] = '\0';
            cur_size = 0;
        } else {
            _grow(initial_capacity);
        }
    }

    /// Appends data.  Null pointer with len > 0 is a precondition violation
    /// (asserts in debug builds; no-op in release).
    arena_buffer& append(const char* cstr, size_type len) {
        if (!cstr) {
            assert(false && "append(ptr, len): cstr is null but len > 0");
            return *this;
        }
        if (len == 0) return *this;
        _ensure_capacity(len);
        std::memcpy(_ptr() + cur_size, cstr, len);
        cur_size += len;
        _ptr()[cur_size] = '\0';
        return *this;
    }

    arena_buffer& append(char ch) {
        _ensure_capacity(1);
        _ptr()[cur_size++] = ch;
        _ptr()[cur_size] = '\0';
        return *this;
    }

    arena_buffer& append(const char* cstr) {
        if (cstr) append(cstr, std::strlen(cstr));
        return *this;
    }

    arena_buffer& append(std::string_view sv) {
        if (!sv.empty())
            append(sv.data(), sv.size());
        return *this;
    }

    arena_buffer& append_repeat(char ch, size_type count) {
        if (count == 0) return *this;
        _ensure_capacity(count);
        std::memset(_ptr() + cur_size, ch, count);
        cur_size += count;
        _ptr()[cur_size] = '\0';
        return *this;
    }

    std::string_view view() const noexcept {
        return std::string_view(data(), size());
    }

    /// Returns a view of the accumulated data.  Null-terminated.
    const char* data()  const noexcept { return _buf; }
    char*       data()        noexcept { return _buf; }

    size_type size()     const noexcept { return cur_size; }
    bool      empty()    const noexcept { return cur_size == 0; }
    size_type capacity() const noexcept { return _heap_capacity ? _heap_capacity : StackSize; }

    void clear() noexcept { cur_size = 0; _buf[0] = '\0'; }

    /// Releases heap memory and resets to empty.
    void reset() noexcept {
        if (_heap_ptr) {
            fl::deallocate_bytes(_heap_ptr, _heap_capacity);
            _heap_ptr = nullptr;
            _heap_capacity = 0;
        }
        cur_size = 0;
        _buf[0] = '\0';
    }

    /// Ensures at least min_capacity bytes of storage are available.
    void reserve(size_type min_capacity) {
        if (min_capacity > capacity()) {
            _grow(min_capacity - cur_size);
        }
    }

    /// Returns an fl::string copy of the accumulated data.
    fl::string to_string() const { return fl::string(data(), size()); }

private:
    char* _ptr() noexcept { return _heap_ptr ? _heap_ptr : _buf; }

    void _ensure_capacity(size_type needed) {
        if (cur_size + needed >= capacity()) {
            _grow(needed);
        }
    }

    void _grow(size_type min_extra) {
        // Prevent overflow in growth calculation
        if (cur_size > std::numeric_limits<size_type>::max() - min_extra) {
            throw std::overflow_error("arena_buffer::_grow: size overflow");
        }
        size_type min_cap = cur_size + min_extra + 1;  // +1 for NUL
        // Check that min_cap didn't wrap (it shouldn't given the check above, but
        // be defensive against edge cases around SIZE_MAX).
        if (min_cap < min_extra) {
            throw std::overflow_error("arena_buffer::_grow: capacity wraparound");
        }
        size_type new_cap = (std::max)(capacity() * 2, min_cap);
        // Reject unreasonable allocation sizes before they reach the allocator.
        // ASan flags allocation-size-too-big for requests >= SIZE_MAX/2.
        constexpr size_type max_reasonable = std::numeric_limits<size_type>::max() / 2;
        if (new_cap >= max_reasonable) {
            throw std::bad_alloc();
        }
        if (new_cap < min_cap) {
            // Wraparound: clamp
            new_cap = min_cap;
        }
        auto* new_ptr = static_cast<char*>(fl::allocate_bytes(new_cap));
        if (!new_ptr) {
            throw std::bad_alloc();
        }
        if (cur_size > 0) {
            std::memcpy(new_ptr, _ptr(), cur_size);
        }
        new_ptr[cur_size] = '\0';
        if (_heap_ptr) {
            fl::deallocate_bytes(_heap_ptr, _heap_capacity);
        }
        _heap_ptr = new_ptr;
        _heap_capacity = new_cap;
    }

    char _buf[StackSize];
    size_type cur_size = 0;
    char* _heap_ptr = nullptr;
    size_type _heap_capacity = 0;
};

/// A std::unique_ptr<arena_buffer<4096>> with a custom deleter that returns the
/// buffer to a thread-local pool (capacity 8) instead of destroying it.
using temp_buffer = std::unique_ptr<arena_buffer<4096>, void(*)(arena_buffer<4096>*)>;

/// Retrieves a temp_buffer from the thread-local pool, or allocates a new one.
/// The buffer is automatically returned to the pool when the unique_ptr goes
/// out of scope.
inline temp_buffer get_pooled_temp_buffer() {
    // Thread-local pool: simple freelist of 8 pointers
    struct TempBufferPool {
        arena_buffer<4096>* slots[8];
        int count = 0;
    };
    thread_local TempBufferPool pool{};

    if (pool.count > 0) {
        --pool.count;
        return temp_buffer(pool.slots[pool.count], [](arena_buffer<4096>* buf) {
            buf->reset();
            thread_local TempBufferPool& local_pool = pool;
            if (local_pool.count < 8) {
                local_pool.slots[local_pool.count++] = buf;
            } else {
                delete buf;
            }
        });
    }
    return temp_buffer(new arena_buffer<4096>(), [](arena_buffer<4096>* buf) {
        buf->reset();
        thread_local TempBufferPool& local_pool = pool;
        if (local_pool.count < 8) {
            local_pool.slots[local_pool.count++] = buf;
        } else {
            delete buf;
        }
    });
}

}  // namespace fl

#endif  // FL_ARENA_HPP
