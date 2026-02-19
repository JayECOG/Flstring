// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_SINKS_HPP
#define FL_SINKS_HPP

// Output sink abstractions for directing formatted output to various
// destinations (memory buffers, files, streams) without allocation overhead.

#include "string.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace fl {

namespace sinks {

// Abstract base class for output destinations. Subclasses implement write()
// to direct formatted output to different targets such as memory buffers,
// files, or streams.
class output_sink {
public:
    virtual ~output_sink() = default;

    virtual void write(const char* data, std::size_t len) = 0;

    // Flushes any buffered data. The default implementation is a no-op.
    virtual void flush() {}

    void write_char(char ch) {
        write(&ch, 1);
    }

    void write_string(const fl::string& str) {
        write(str.data(), str.size());
    }

    void write_cstring(const char* cstr) {
        if (cstr) {
            write(cstr, std::strlen(cstr));
        }
    }
};

// Writes to a caller-provided fixed-size buffer. Throws std::overflow_error
// if data would exceed the buffer capacity.
class buffer_sink : public output_sink {
public:
    buffer_sink(char* buffer, std::size_t capacity) noexcept
        : _buffer(buffer), _capacity(capacity), _written(0) {}

    void write(const char* data, std::size_t len) override {
        if (_written + len > _capacity) {
            throw std::overflow_error("fl::sinks::buffer_sink: buffer overflow");
        }
        std::memcpy(_buffer + _written, data, len);
        _written += len;
    }

    std::size_t written() const noexcept { return _written; }
    std::size_t available() const noexcept { return _capacity - _written; }

    void null_terminate() {
        if (_written < _capacity) {
            _buffer[_written] = '\0';
        }
    }

    char* buffer() noexcept { return _buffer; }
    const char* buffer() const noexcept { return _buffer; }

    void reset() noexcept { _written = 0; }

private:
    char* _buffer;
    std::size_t _capacity;
    std::size_t _written;
};

// Writes to a C FILE handle. When constructed with a filename the sink owns
// the handle and closes it on destruction. When constructed with an existing
// FILE pointer, ownership is controlled by the caller via the owns flag.
class file_sink : public output_sink {
public:
    explicit file_sink(const char* filename, bool append = false)
        : _file(nullptr), _owns_file(true) {
        const char* mode = append ? "ab" : "wb";
    #if defined(_MSC_VER)
        if (fopen_s(&_file, filename, mode) != 0 || !_file) {
            throw std::runtime_error(std::string("fl::sinks::file_sink: cannot open file: ") + filename);
        }
    #else
        _file = std::fopen(filename, mode);
        if (!_file) {
            throw std::runtime_error(std::string("fl::sinks::file_sink: cannot open file: ") + filename);
        }
    #endif
    }

    explicit file_sink(std::FILE* file, bool owns = false) noexcept
        : _file(file), _owns_file(owns) {}

    ~file_sink() noexcept override {
        if (_owns_file && _file) {
            std::fclose(_file);
        }
    }

    void write(const char* data, std::size_t len) override {
        if (_file && std::fwrite(data, 1, len, _file) != len) {
            throw std::runtime_error("fl::sinks::file_sink: write failed");
        }
    }

    void flush() override {
        if (_file) {
            std::fflush(_file);
        }
    }

private:
    std::FILE* _file;
    bool _owns_file;
};

// Writes to a std::ostream reference.
class stream_sink : public output_sink {
public:
    explicit stream_sink(std::ostream& stream) noexcept : _stream(stream) {}

    void write(const char* data, std::size_t len) override {
        _stream.write(data, static_cast<std::streamsize>(len));
    }

    void flush() override {
        _stream.flush();
    }

private:
    std::ostream& _stream;
};

// Writes to an automatically growing std::vector<char> buffer. Useful when
// the total output size is not known in advance.
class growing_sink : public output_sink {
public:
    explicit growing_sink(std::size_t initial_capacity = 256) : _buffer(), _written(0) {
        _buffer.reserve(initial_capacity);
    }

    void write(const char* data, std::size_t len) override {
        _buffer.insert(_buffer.end(), data, data + len);
        _written += len;
    }

    // Null-terminates the buffer without affecting the reported size.
    void null_terminate() {
        if (_buffer.size() <= _written) {
            _buffer.resize(_written + 1);
        }
        _buffer[_written] = '\0';
    }

    fl::string to_fl_string() const {
        return fl::string(_buffer.data(), _buffer.size());
    }

    const std::vector<char>& buffer() const noexcept { return _buffer; }
    std::vector<char>& buffer() noexcept { return _buffer; }

    std::size_t size() const noexcept { return _written; }
    const char* data() const noexcept { return _buffer.data(); }

    void reset() noexcept {
        _buffer.clear();
        _written = 0;
    }

private:
    std::vector<char> _buffer;
    std::size_t _written;
};

// Discards all output. Useful for benchmarking formatting overhead without
// any I/O cost.
class null_sink : public output_sink {
public:
    null_sink() noexcept : _written(0) {}

    void write(const char* data, std::size_t len) override {
        (void)data;
        _written += len;
    }

    std::size_t bytes_written() const noexcept { return _written; }

    void reset() noexcept { _written = 0; }

private:
    std::size_t _written;
};

// Fans out writes to multiple sinks simultaneously, allowing output to be
// directed to several destinations at once.
class multi_sink : public output_sink {
public:
    multi_sink() : _sinks() {}

    void add_sink(std::shared_ptr<output_sink> sink) {
        _sinks.push_back(sink);
    }

    void write(const char* data, std::size_t len) override {
        for (auto& sink : _sinks) {
            sink->write(data, len);
        }
    }

    void flush() override {
        for (auto& sink : _sinks) {
            sink->flush();
        }
    }

private:
    std::vector<std::shared_ptr<output_sink>> _sinks;
};

}  // namespace sinks

// Factory helpers for creating sinks.

template <std::size_t N>
sinks::buffer_sink make_buffer_sink(char (&buffer)[N]) noexcept {
    return sinks::buffer_sink(buffer, N);
}

inline std::shared_ptr<sinks::file_sink> make_file_sink(const char* filename, bool append = false) {
    return std::make_shared<sinks::file_sink>(filename, append);
}

inline std::shared_ptr<sinks::stream_sink> make_stream_sink(std::ostream& stream) noexcept {
    return std::make_shared<sinks::stream_sink>(stream);
}

inline std::shared_ptr<sinks::growing_sink> make_growing_sink(std::size_t initial_capacity = 256) {
    return std::make_shared<sinks::growing_sink>(initial_capacity);
}

inline std::shared_ptr<sinks::null_sink> make_null_sink() noexcept {
    return std::make_shared<sinks::null_sink>();
}

}  // namespace fl

#endif  // FL_SINKS_HPP
