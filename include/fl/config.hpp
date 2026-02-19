// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_CONFIG_HPP
#define FL_CONFIG_HPP

// Configuration and feature-detection for fl.
//
// Baseline: C++17
// Optional: C++20 / C++23 optimisation paths
//
// This header intentionally contains only preprocessor logic and small helpers.

#ifndef FL_DEBUG_THREAD_SAFETY
// When enabled, fl instruments selected types with runtime assertions that
// detect unsafe concurrent access patterns (read/write and write/write).
//
// This is a *debugging aid* only. It does not make fl::string thread-safe.
#define FL_DEBUG_THREAD_SAFETY 0
#endif

#ifndef FL_DEBUG_THREAD_SAFETY_HISTORY
// Number of recent access events retained per object for diagnostics.
// Set to 0 to disable history recording (still reports basic conflicts).
#define FL_DEBUG_THREAD_SAFETY_HISTORY 32
#endif

#ifndef FL_SYNCHRONISED_STRING_USE_SHARED_MUTEX
// Prefer shared_mutex (multiple concurrent readers) where available.
// C++17 provides <shared_mutex> on mainstream standard libraries.
#define FL_SYNCHRONISED_STRING_USE_SHARED_MUTEX 1
#endif

#ifndef FL_THREAD_SAFETY_ABORT
#include <cstdlib>
#define FL_THREAD_SAFETY_ABORT() std::abort()
#endif

// -------- Language version detection --------

#if defined(_MSVC_LANG)
#define FL_CPP_LANG _MSVC_LANG
#else
#define FL_CPP_LANG __cplusplus
#endif

#if FL_CPP_LANG >= 202302L
#define FL_HAS_CPP23 1
#else
#define FL_HAS_CPP23 0
#endif

#if FL_CPP_LANG >= 202002L
#define FL_HAS_CPP20 1
#else
#define FL_HAS_CPP20 0
#endif

#if FL_CPP_LANG >= 201703L
#define FL_HAS_CPP17 1
#else
#define FL_HAS_CPP17 0
#endif

// Attributes / hints
#if FL_HAS_CPP20
#define FL_NODISCARD [[nodiscard]]
#else
#define FL_NODISCARD
#endif

// Source-location-ish helper (C++17).
#if FL_DEBUG_THREAD_SAFETY
#define FL_STRINGIFY_IMPL(x) #x
#define FL_STRINGIFY(x) FL_STRINGIFY_IMPL(x)
#define FL_LOC (__FILE__ ":" FL_STRINGIFY(__LINE__))
#else
#define FL_LOC nullptr
#endif

#endif  // FL_CONFIG_HPP
