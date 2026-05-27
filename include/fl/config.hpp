// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_CONFIG_HPP
#define FL_CONFIG_HPP

/// @file Compile-time configuration and feature-detection.
///
/// fl targets C++20.  The macros below detect C++20 and C++23 for conditional
/// optimisation paths.  This header intentionally contains only preprocessor
/// logic and small helpers — no runtime code.

#ifndef FL_DEBUG_THREAD_SAFETY
/// Enable runtime detection of concurrent access violations on string types.
///
/// When enabled, every read/write/move operation on `fl::string` is tracked
/// through an atomic state machine that aborts with a diagnostic on detecting
/// a data race.  Compiles to a zero-overhead stub when set to 0 (default).
#define FL_DEBUG_THREAD_SAFETY 0
#endif

#ifndef FL_DEBUG_THREAD_SAFETY_HISTORY
/// Number of recent access events retained per object for diagnostics.
/// Set to 0 to disable history recording (still reports basic conflicts).
#define FL_DEBUG_THREAD_SAFETY_HISTORY 32
#endif

#ifndef FL_SYNCHRONISED_STRING_USE_SHARED_MUTEX
/// Prefer std::shared_mutex (multiple concurrent readers) over std::mutex.
#define FL_SYNCHRONISED_STRING_USE_SHARED_MUTEX 1
#endif

#ifndef FL_THREAD_SAFETY_ABORT
/// Callback invoked when a thread-safety violation is detected.
#include <cstdlib>
#define FL_THREAD_SAFETY_ABORT() std::abort()
#endif

// ======================================================================
// Language version detection
// ======================================================================

#if defined(_MSVC_LANG)
#define FL_CPP_LANG _MSVC_LANG
#else
#define FL_CPP_LANG __cplusplus
#endif

/// Defined as 1 when compiling with C++23 or later.
#if FL_CPP_LANG >= 202302L
#define FL_HAS_CPP23 1
#else
#define FL_HAS_CPP23 0
#endif

/// Defined as 1 when compiling with C++20 or later.
#if FL_CPP_LANG >= 202002L
#define FL_HAS_CPP20 1
#else
#define FL_HAS_CPP20 0
#endif

// ======================================================================
// Guaranteed C++20 feature aliases (no fallback needed)
// ======================================================================

/// Always `constexpr` (C++20 baseline guarantees it).
#define FL_CONSTEXPR constexpr

/// Always expands to `[[nodiscard]]`.
#define FL_NODISCARD [[nodiscard]]

/// Always expands to `[[fallthrough]]`.
#define FL_FALLTHROUGH [[fallthrough]]

/// Virtual function specifiers (available since C++11).
#define FL_OVERRIDE override
#define FL_FINAL final

/// Exception specification (available since C++11).
#define FL_NOEXCEPT noexcept

// ------------------------------------------------------------------
// Source-location helper (debug thread-safety diagnostics)
// ------------------------------------------------------------------

#if FL_DEBUG_THREAD_SAFETY
#define FL_STRINGIFY_IMPL(x) #x
#define FL_STRINGIFY(x) FL_STRINGIFY_IMPL(x)
#define FL_LOC (__FILE__ ":" FL_STRINGIFY(__LINE__))
#else
#define FL_LOC nullptr
#endif

// ======================================================================
// ABI / Export Macros
// ======================================================================

/// Public symbol export (DLL on Windows, default visibility on ELF).
#if defined(_WIN32) || defined(_WIN64)
#  ifdef FL_BUILD_SHARED
#    define FL_EXPORT __declspec(dllexport)
#    define FL_IMPORT __declspec(dllimport)
#  else
#    define FL_EXPORT
#    define FL_IMPORT
#  endif
#  define FL_LOCAL
#elif defined(__GNUC__) || defined(__clang__)
#  define FL_EXPORT __attribute__((visibility("default")))
#  define FL_IMPORT __attribute__((visibility("default")))
#  define FL_LOCAL  __attribute__((visibility("hidden")))
#else
#  define FL_EXPORT
#  define FL_IMPORT
#  define FL_LOCAL
#endif

// ======================================================================
// Compiler hints
// ======================================================================

/// Strong inline hint.  Use sparingly — only on hot, small functions.
#if defined(_MSC_VER)
#  define FL_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define FL_INLINE inline __attribute__((always_inline))
#else
#  define FL_INLINE inline
#endif

/// Pointer restrict qualifier.
#if defined(_MSC_VER)
#  define FL_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#  define FL_RESTRICT __restrict__
#else
#  define FL_RESTRICT
#endif

/// Tell the compiler that expression is always true (undefined behaviour if
/// it is not).  Use only where correctness depends on the assumption.
#if defined(_MSC_VER)
#  define FL_ASSUME(expr) __assume(expr)
#elif defined(__GNUC__) && !defined(__clang__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5))
#  define FL_ASSUME(expr) do { if (!(expr)) __builtin_unreachable(); } while(0)
#elif defined(__clang__)
#  define FL_ASSUME(expr) __builtin_assume(expr)
#else
#  define FL_ASSUME(expr) ((void)0)
#endif

/// Mark unreachable code paths (helps optimisation, suppresses warnings).
#if defined(_MSC_VER)
#  define FL_UNREACHABLE() __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
#  define FL_UNREACHABLE() __builtin_unreachable()
#else
#  define FL_UNREACHABLE() ((void)0)
#endif

/// Branch prediction hint: annotate a condition as likely true.
#if defined(__GNUC__) || defined(__clang__)
#  define FL_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define FL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define FL_LIKELY(x)   (x)
#  define FL_UNLIKELY(x) (x)
#endif

/// Mark a declaration as deprecated, with a message suggesting the replacement.
#if defined(__GNUC__) || defined(__clang__)
#  define FL_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
#  define FL_DEPRECATED(msg) __declspec(deprecated(msg))
#else
#  define FL_DEPRECATED(msg)
#endif

#endif  // FL_CONFIG_HPP
