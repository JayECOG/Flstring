// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_PROFILING_HPP
#define FL_PROFILING_HPP

// Optional scoped profiler for the fl library.
//
// Define FL_ENABLE_PROFILING before including this header to enable scoped
// timings logged to std::clog.  When the macro is not defined the profiler
// compiles to a zero-cost no-op.

#include <string>
#include <string_view>

#ifdef FL_ENABLE_PROFILING

#include <chrono>
#include <iostream>

namespace fl {
class profiler {
public:
    explicit profiler(std::string_view label)
        : _label(label), _start(std::chrono::high_resolution_clock::now()) {}
    ~profiler() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - _start).count();
        std::clog << "[fl::profiler] " << _label << " took " << duration << " us" << '\n';
    }
private:
    std::string _label;
    std::chrono::high_resolution_clock::time_point _start;
};
}

#else  // FL_ENABLE_PROFILING

namespace fl {
class profiler {
public:
    constexpr explicit profiler(const char*) noexcept {}
    constexpr explicit profiler(std::string_view) noexcept {}
    ~profiler() noexcept = default;
};
}

#endif  // FL_ENABLE_PROFILING

#endif  // FL_PROFILING_HPP
