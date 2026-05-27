// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_PROFILING_HPP
#define FL_PROFILING_HPP

/// @file Optional scoped profiler.
///
/// Define `FL_ENABLE_PROFILING` before including this header to activate
/// scoped timing output to `std::clog`.  When the macro is not defined the
/// entire profiler compiles away to a zero-cost empty class.

#include <string>
#include <string_view>

#ifdef FL_ENABLE_PROFILING

#include <chrono>
#include <iostream>

namespace fl {

/// Scoped profiler that logs elapsed time to std::clog.
///
/// Usage:
/// @code
///     {
///         fl::profiler p("parse_input");
///         // ... work ...
///     }
///     // Output: [fl::profiler] parse_input took 1234 us
/// @endcode
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

}  // namespace fl

#else  // FL_ENABLE_PROFILING — compile to nothing

namespace fl {

/// Zero-cost stub.  The constructor and destructor are constexpr no-ops when
/// `FL_ENABLE_PROFILING` is not defined.
class profiler {
public:
    constexpr explicit profiler(const char*) noexcept {}
    ~profiler() noexcept = default;
};

}  // namespace fl

#endif  // FL_ENABLE_PROFILING

#endif  // FL_PROFILING_HPP
