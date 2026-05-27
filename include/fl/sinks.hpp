// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_SINKS_HPP
#define FL_SINKS_HPP

/// @file Output sink abstractions.
///
/// Directs formatted output to memory buffers, files, and streams without
/// intermediate allocation.  Six sink types are provided, each inheriting
/// from output_sink.

#include "fl/config.hpp"
#include "string.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace fl {

namespace sinks {

/// @cond INTERNAL
namespace detail {

inline void validate_write_input(const char* sink_name, const char* data, std::size_t len) {
    if (len == 0) return;
    if (!data) {
        throw std::invalid_argument(std::string("fl::sinks::") + sink_name + ": data pointer is null with non-zero length");
    }
}

inline void validate_counter_add(const char* sink_name, std::size_t current, std::size_t add) {
    if (add > std::numeric_limits<std::size_t>::max() - current) {
        throw std::overflow_error(std::string("fl::sinks::") + sink_name + ": write counter overflow");
    }
}

}  // namespace detail
/// @endcond

/// Abstract base class for output destinations.
///
/// Subclasses implement write() to direct formatted output to targets such as
/// memory buffers, files, or streams.
class output_sink {
public:
    virtual ~output_sink() = default;
    virtual void write(const char* data, std::size_t len) = 0;

    /// Flushes any buffered data.  Default implementation is a no-op.
    virtual void flush() {}

    void write_char(char ch)       { write(&ch, 1); }
    void write_string(const fl::string& str) { write(str.data(), str.size()); }
    void write_cstring(const char* cstr) {
        if (cstr) write(cstr, std::strlen(cstr));
    }
};

/// Writes to a fixed-size caller-provided buffer.
/// Throws std::overflow_error when the output exceeds capacity.
class buffer_sink final : public output_sink {
public:
    explicit buffer_sink(char* buffer, std::size_t capacity) noexcept
        : _buffer(buffer), _capacity(capacity), _written(0) {}

    void write(const char* data, std::size_t len) override {
        detail::validate_write_input("buffer_sink", data, len);
        detail::validate_counter_add("buffer_sink", _written, len);
        if (_written + len > _capacity) {
            throw std::overflow_error("fl::sinks::buffer_sink: buffer overflow");
        }
        if (len > 0) {
            std::memcpy(_buffer + _written, data, len);
            _written += len;
        }
    }

    /// Writes a null terminator at the current write position.
    /// Does not affect the reported byte count.
    void null_terminate() {
        if (_written < _capacity) {
            _buffer[_written] = '\0';
        }
    }

    std::size_t written() const noexcept { return _written; }
    std::size_t capacity() const noexcept { return _capacity; }
    void reset() noexcept { _written = 0; }

private:
    char* _buffer;
    std::size_t _capacity;
    std::size_t _written;
};

/// Dynamically growing sink backed by a std::vector<char, Alloc>.
///
/// @tparam Alloc Allocator type (defaults to std::allocator<char>).
template <typename Alloc = std::allocator<char>>
class basic_growing_sink final : public output_sink {
public:
    using allocator_type = Alloc;

    basic_growing_sink() = default;
    explicit basic_growing_sink(const Alloc& alloc) : _buffer(alloc) {}
    explicit basic_growing_sink(std::size_t initial_capacity) {
        _buffer.reserve(initial_capacity);
    }
    basic_growing_sink(std::size_t initial_capacity, const Alloc& alloc)
        : _buffer(alloc) {
        _buffer.reserve(initial_capacity);
    }

    void write(const char* data, std::size_t len) override {
        detail::validate_write_input("basic_growing_sink", data, len);
        _buffer.insert(_buffer.end(), data, data + len);
    }

    /// Writes a null terminator without affecting the reported size.
    void null_terminate() { _buffer.push_back('\0'); }

    std::size_t written() const noexcept { return _buffer.size(); }
    const char* data() const noexcept { return _buffer.data(); }
    void reset() noexcept { _buffer.clear(); }

    /// Returns a fl::string copy of the accumulated output.
    fl::string to_fl_string() const {
        return fl::string(_buffer.data(), _buffer.size());
    }

private:
    std::vector<char, Alloc> _buffer;
};

/// Default growing sink using std::allocator<char> (backward-compatible).
using growing_sink = basic_growing_sink<>;

/// Writes to a C FILE* handle.
/// Supports owned handles (fclose on destruction) and borrowed handles.
class file_sink final : public output_sink {
public:
    explicit file_sink(const char* filename, const char* mode = "w") {
        if (!filename || !(*filename)) {
            throw std::invalid_argument("fl::sinks::file_sink: filename is null or empty");
        }
        _file = std::fopen(filename, mode ? mode : "w");
        if (!_file) {
            throw std::runtime_error(std::string("fl::sinks::file_sink: failed to open file: ") + filename);
        }
        _owned = true;
    }

    explicit file_sink(std::FILE* file, bool owned = false) noexcept
        : _file(file), _owned(owned) {}

    ~file_sink() override {
        if (_owned && _file) {
            std::fclose(_file);
        }
    }

    file_sink(const file_sink&) = delete;
    file_sink& operator=(const file_sink&) = delete;
    file_sink(file_sink&& other) noexcept : _file(other._file), _owned(other._owned) {
        other._file = nullptr;
        other._owned = false;
    }

    void write(const char* data, std::size_t len) override {
        detail::validate_write_input("file_sink", data, len);
        if (_file && len > 0) {
            if (std::fwrite(data, 1, len, _file) != len) {
                throw std::runtime_error("fl::sinks::file_sink: write failed");
            }
        }
    }

    void flush() override { if (_file) std::fflush(_file); }

private:
    std::FILE* _file;
    bool _owned;
};

/// Writes to a std::ostream reference.
class stream_sink final : public output_sink {
public:
    explicit stream_sink(std::ostream& os) noexcept : _os(&os) {}
    void write(const char* data, std::size_t len) override {
        detail::validate_write_input("stream_sink", data, len);
        _os->write(data, static_cast<std::streamsize>(len));
    }
    void flush() override { _os->flush(); }

private:
    std::ostream* _os;
};

/// Discards all output.  Maintains a byte count of discarded data.
/// Useful for benchmarking formatting overhead.
class null_sink final : public output_sink {
public:
    null_sink() noexcept = default;
    void write(const char* /*data*/, std::size_t len) override { _written += len; }
    std::size_t written() const noexcept { return _written; }
    void reset() noexcept { _written = 0; }

private:
    std::size_t _written = 0;
};

/// Fan-out sink that writes to multiple output_sink targets.
class multi_sink final : public output_sink {
public:
    void add_sink(std::shared_ptr<output_sink> sink) { _sinks.push_back(std::move(sink)); }

    void write(const char* data, std::size_t len) override {
        detail::validate_write_input("multi_sink", data, len);
        for (auto& s : _sinks) { s->write(data, len); }
    }

    void flush() override { for (auto& s : _sinks) { s->flush(); } }

private:
    std::vector<std::shared_ptr<output_sink>> _sinks;
};

}  // namespace sinks

// -- Factory helpers ----------------------------------------------------------

inline sinks::buffer_sink make_buffer_sink(char* buffer, std::size_t capacity) noexcept {
    return sinks::buffer_sink(buffer, capacity);
}

template <typename Alloc = std::allocator<char>>
inline sinks::basic_growing_sink<Alloc> make_growing_sink(const Alloc& alloc = Alloc()) {
    return sinks::basic_growing_sink<Alloc>(alloc);
}

template <typename Alloc = std::allocator<char>>
inline sinks::basic_growing_sink<Alloc> make_growing_sink(std::size_t initial_capacity, const Alloc& alloc = Alloc()) {
    return sinks::basic_growing_sink<Alloc>(initial_capacity, alloc);
}

inline sinks::file_sink make_file_sink(const char* filename, const char* mode = "w") {
    return sinks::file_sink(filename, mode);
}
inline sinks::file_sink make_file_sink(std::FILE* file, bool owned = false) noexcept {
    return sinks::file_sink(file, owned);
}

inline sinks::stream_sink make_stream_sink(std::ostream& os) noexcept {
    return sinks::stream_sink(os);
}

inline sinks::null_sink make_null_sink() noexcept { return sinks::null_sink(); }

}  // namespace fl

#endif  // FL_SINKS_HPP
