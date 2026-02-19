// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_ARENA_HPP
#define FL_ARENA_HPP

// Arena-based allocation utilities. Provides a bump-pointer arena allocator,
// a growable character buffer backed by an arena, and a thread-local pool of
// reusable temporary buffers.

#include <cstring>
#include "fl/alloc_hooks.hpp"
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <thread>
#include "fl/profiling.hpp"

namespace fl {

namespace detail {
    constexpr std::size_t DEFAULT_ARENA_STACK_SIZE = 4096;
    constexpr std::size_t DEFAULT_BUFFER_INITIAL_CAPACITY = 256;
} // namespace detail

// A bump-pointer allocator that serves small allocations from a fixed-size
// stack-local buffer and falls back to the heap for requests that do not fit.
// All allocations are 8-byte aligned. Heap blocks are freed on destruction or
// when reset() is called. Non-copyable and non-movable because outstanding
// pointers refer into the internal stack buffer.
template <std::size_t StackSize = detail::DEFAULT_ARENA_STACK_SIZE>
class arena_allocator {
public:
    using value_type = std::uint8_t;

    arena_allocator() noexcept : _stack_ptr(_stack_buffer), _stack_used(0), _heap_blocks() {}

    ~arena_allocator() noexcept {
        for (auto& b : _heap_blocks) {
            fl::deallocate_bytes(b.first, b.second);
        }
    }

    arena_allocator(const arena_allocator&) = delete;
    arena_allocator& operator=(const arena_allocator&) = delete;
    arena_allocator(arena_allocator&&) = delete;
    arena_allocator& operator=(arena_allocator&&) = delete;

    void* allocate(std::size_t size) {
        std::size_t aligned_size = (size + 7) & ~7;

        if (_stack_used + aligned_size <= StackSize) {
            void* ptr = _stack_ptr + _stack_used;
            _stack_used += aligned_size;
            return ptr;
        }

        auto* mem = static_cast<std::uint8_t*>(fl::allocate_bytes(aligned_size));
        _heap_blocks.emplace_back(mem, aligned_size);
        return mem;
    }

    void deallocate(void* ptr, std::size_t /*size*/) noexcept {
        if (!ptr) return;

        auto* uptr = static_cast<std::uint8_t*>(ptr);

        if (uptr >= _stack_buffer && uptr < (_stack_buffer + StackSize)) {
            return;
        }

        auto it = std::find_if(_heap_blocks.begin(), _heap_blocks.end(),
            [uptr](const std::pair<std::uint8_t*, std::size_t>& b) { return b.first == uptr; });

        if (it != _heap_blocks.end()) {
            fl::deallocate_bytes(it->first, it->second);
            _heap_blocks.erase(it);
        }
    }

    void reset() noexcept {
        for (auto& b : _heap_blocks) {
            fl::deallocate_bytes(b.first, b.second);
        }
        _heap_blocks.clear();
        _stack_ptr = _stack_buffer;
        _stack_used = 0;
    }

    std::size_t available_stack() const noexcept {
        return StackSize - _stack_used;
    }

    std::size_t total_allocated() const noexcept {
        std::size_t heap_total = 0;
        for (auto const& b : _heap_blocks) {
            heap_total += b.second;
        }
        return _stack_used + heap_total;
    }

private:
    std::uint8_t _stack_buffer[StackSize];
    std::uint8_t* _stack_ptr;
    std::size_t _stack_used;
    std::vector<std::pair<std::uint8_t*, std::size_t>> _heap_blocks;
};

// A growable character buffer backed by an arena_allocator. For typical sizes
// all memory comes from the arena's stack region, avoiding the global heap
// entirely. Non-copyable and non-movable.
template <std::size_t StackSize = detail::DEFAULT_ARENA_STACK_SIZE>
class arena_buffer {
public:
    using arena_type = arena_allocator<StackSize>;
    using size_type = std::size_t;

    arena_buffer() noexcept : _arena(), _buffer(nullptr), _capacity(0), _size(0) {
        _init_buffer(detail::DEFAULT_BUFFER_INITIAL_CAPACITY);
    }

    explicit arena_buffer(size_type initial_capacity) : _arena(), _buffer(nullptr), _capacity(0), _size(0) {
        _init_buffer(initial_capacity);
    }

    ~arena_buffer() noexcept = default;

    arena_buffer(const arena_buffer&) = delete;
    arena_buffer& operator=(const arena_buffer&) = delete;
    arena_buffer(arena_buffer&&) = delete;
    arena_buffer& operator=(arena_buffer&&) = delete;

    arena_buffer& append(const char* cstr) noexcept {
        if (cstr) {
            return append(cstr, std::strlen(cstr));
        }
        return *this;
    }

    arena_buffer& append(const char* cstr, size_type len) noexcept {
        if (len == 0) return *this;

        size_type new_size = _size + len;
        if (new_size > _capacity) {
            _grow(new_size);
        }

        std::memcpy(_buffer + _size, cstr, len);
        _size = new_size;
        return *this;
    }

    arena_buffer& append(char ch) noexcept {
        if (_size >= _capacity) {
            _grow(_capacity * 2);
        }
        _buffer[_size++] = ch;
        return *this;
    }

    arena_buffer& append_repeat(char ch, size_type count) noexcept {
        if (count == 0) return *this;

        size_type new_size = _size + count;
        if (new_size > _capacity) {
            _grow(new_size);
        }

        std::fill(_buffer + _size, _buffer + new_size, ch);
        _size = new_size;
        return *this;
    }

    void clear() noexcept {
        _size = 0;
    }

    void reset() noexcept {
        _arena.reset();
        _buffer = nullptr;
        _capacity = 0;
        _size = 0;
        _init_buffer(detail::DEFAULT_BUFFER_INITIAL_CAPACITY);
    }

    fl::string to_string() const {
        return fl::string(_buffer, _size);
    }

private:
    arena_type _arena;
    char* _buffer;
    size_type _capacity;
    size_type _size;

    void _init_buffer(size_type initial_capacity) {
        _capacity = (initial_capacity == 0) ? 1 : initial_capacity;
        _buffer = static_cast<char*>(_arena.allocate(_capacity + 1));
        _buffer[0] = '\0';
    }

    void _grow(size_type min_capacity) {
        size_type new_capacity = _capacity;
        if (new_capacity == 0) new_capacity = detail::DEFAULT_BUFFER_INITIAL_CAPACITY;
        while (new_capacity <= min_capacity) {
            new_capacity *= 2;
        }

        char* old_buffer = _buffer;
        [[maybe_unused]] size_type old_capacity = _capacity;

        _buffer = static_cast<char*>(_arena.allocate(new_capacity + 1));
        std::memcpy(_buffer, old_buffer, _size);
        _capacity = new_capacity;
    }
};

namespace detail {

constexpr std::size_t MAX_ARENA_BUFFER_POOL_SIZE = 8;

template <std::size_t StackSize_>
struct arena_buffer_pool_details_ {
    std::vector<std::unique_ptr<arena_buffer<StackSize_>>> pool;

    ~arena_buffer_pool_details_() {
        for (auto& buf_ptr : pool) {
            buf_ptr->reset();
        }
    }
};

static thread_local arena_buffer_pool_details_<DEFAULT_ARENA_STACK_SIZE> g_arena_buffer_pool_details;

} // namespace detail

// Custom deleter that returns arena buffers to the thread-local pool instead
// of destroying them, up to MAX_ARENA_BUFFER_POOL_SIZE.
template <std::size_t StackSize_>
struct pooled_temp_buffer_deleter_ {
    void operator()(arena_buffer<StackSize_>* buf) const noexcept {
        if (buf) {
            buf->reset();
            if (fl::detail::g_arena_buffer_pool_details.pool.size() < fl::detail::MAX_ARENA_BUFFER_POOL_SIZE) {
                fl::detail::g_arena_buffer_pool_details.pool.push_back(std::unique_ptr<arena_buffer<StackSize_>>(buf));
            } else {
                delete buf;
            }
        }
    }
};

using temp_buffer = std::unique_ptr<arena_buffer<fl::detail::DEFAULT_ARENA_STACK_SIZE>,
                                     pooled_temp_buffer_deleter_<fl::detail::DEFAULT_ARENA_STACK_SIZE>>;

// Returns an arena buffer from the thread-local pool, or creates a new one if
// the pool is empty. The returned unique_ptr uses a custom deleter that
// recycles the buffer back into the pool on release.
inline temp_buffer get_pooled_temp_buffer() {
    if (!fl::detail::g_arena_buffer_pool_details.pool.empty()) {
        temp_buffer buf(fl::detail::g_arena_buffer_pool_details.pool.back().release(),
                        pooled_temp_buffer_deleter_<fl::detail::DEFAULT_ARENA_STACK_SIZE>());
        fl::detail::g_arena_buffer_pool_details.pool.pop_back();
        return buf;
    }
    return temp_buffer(new arena_buffer<fl::detail::DEFAULT_ARENA_STACK_SIZE>(),
                       pooled_temp_buffer_deleter_<fl::detail::DEFAULT_ARENA_STACK_SIZE>());
}

} // namespace fl

#endif // FL_ARENA_HPP
