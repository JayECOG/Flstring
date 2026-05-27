// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_HPP
#define FL_HPP

/// Umbrella header for the fl library.
///
/// Including this single header pulls in every public component: strings,
/// arenas, sinks, formatting, builders, ropes, immutable strings, and
/// synchronised strings.  If you need only a subset of components, include
/// the individual headers from `fl/` instead.
///
/// Optional headers (not included by default, include separately):
/// - fl/chrono_format.hpp  —  std::chrono duration/time_point formatting
/// - fl/color.hpp          —  terminal colour/style support (ANSI escape codes)

#include "fl/config.hpp"
#include "fl/string.hpp"
#include "fl/arena.hpp"
#include "fl/sinks.hpp"
#include "fl/format.hpp"
#include "fl/builder.hpp"
#include "fl/substring_view.hpp"
#include "fl/rope.hpp"
#include "fl/immutable_string.hpp"
#include "fl/synchronised_string.hpp"

namespace fl {
    /// Major version number, incremented on breaking changes.
    constexpr int MAJOR_VERSION = 2;

    /// Minor version number, incremented on feature additions.
    constexpr int MINOR_VERSION = 0;

    /// Patch version number, incremented on bug fixes.
    constexpr int PATCH_VERSION = 0;

    /// Returns the library version as a human-readable string (e.g. "2.0.0").
    inline const char* version() noexcept {
        return "2.0.0";
    }
}

#endif  // FL_HPP
