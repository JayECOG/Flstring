// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_HPP
#define FL_HPP

// Umbrella header for the fl library.  Including this single header pulls in
// every public component: strings, arenas, sinks, formatting, builders, ropes,
// immutable strings, and synchronised strings.

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
    constexpr int MAJOR_VERSION = 1;
    constexpr int MINOR_VERSION = 0;
    constexpr int PATCH_VERSION = 0;

    // Returns the library version as a human-readable string.
    inline const char* version() noexcept {
        return "1.0.0";
    }
}

#endif  // FL_HPP
