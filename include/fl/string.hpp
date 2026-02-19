// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_STRING_HPP
#define FL_STRING_HPP

// Core fl::string class implementation.
//
// Provides a high-performance string with 23-byte small-string optimization
// (SSO), pool-backed heap allocation for larger strings, SIMD-accelerated
// search (SSE2/AVX2), and optional thread-safety debug guards.  The
// detail namespace contains the low-level copy helpers, character/substring
// search algorithms (Boyer-Moore-Horspool, Two-Way), and adaptive tuning
// state used by the public find() family.

#include <cstring>
#include <span>
#include <concepts>
#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <compare>
#include <stdexcept>
#include "fl/config.hpp"
#include "fl/alloc_hooks.hpp"
#include "fl/debug/thread_safety.hpp"
#include <algorithm>
#include <memory>
#include <type_traits>
#include <iterator>
#include <utility>
#include <cassert>
#include <cstdint>
#include <vector>
#include <deque>
#include "fl/substring_view.hpp"
#include "fl/profiling.hpp"

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace fl {

// Maximum number of characters that fit in the SSO buffer (23 bytes).
constexpr std::size_t SSO_CAPACITY = 23;

// Strings longer than SSO_CAPACITY are heap-allocated.
constexpr std::size_t SSO_THRESHOLD = SSO_CAPACITY + 1;

namespace detail {
        [[nodiscard]] inline unsigned first_set_bit_index(unsigned mask) noexcept {
    #if defined(_MSC_VER)
        unsigned long index = 0;
        _BitScanForward(&index, mask);
        return static_cast<unsigned>(index);
    #else
        return static_cast<unsigned>(__builtin_ctz(mask));
    #endif
        }

    template <typename T>
    struct is_output_iterator : std::false_type {};

    // Branchless overlapping-store copy for buffers up to 64 bytes.
    inline void copy_small(unsigned char* dst,
        const unsigned char* src,
        std::size_t n) noexcept {
        if (n == 0) return;

        if (n <= 8) {
            if (n >= 4) {
                std::uint32_t head, tail;
                std::memcpy(&head, src, 4);
                std::memcpy(&tail, src + n - 4, 4);
                std::memcpy(dst, &head, 4);
                std::memcpy(dst + n - 4, &tail, 4);
            } else if (n >= 2) {
                std::uint16_t head, tail;
                std::memcpy(&head, src, 2);
                std::memcpy(&tail, src + n - 2, 2);
                std::memcpy(dst, &head, 2);
                std::memcpy(dst + n - 2, &tail, 2);
            } else {
                dst[0] = src[0];
            }
            return;
        }

        if (n <= 16) {
            std::uint64_t head, tail;
            std::memcpy(&head, src, 8);
            std::memcpy(&tail, src + n - 8, 8);
            std::memcpy(dst, &head, 8);
            std::memcpy(dst + n - 8, &tail, 8);
            return;
        }

        if (n <= 32) {
            std::uint64_t q0, q1, q2, q3;
            std::memcpy(&q0, src, 8);
            std::memcpy(&q1, src + 8, 8);
            std::memcpy(&q2, src + n - 16, 8);
            std::memcpy(&q3, src + n - 8, 8);
            std::memcpy(dst, &q0, 8);
            std::memcpy(dst + 8, &q1, 8);
            std::memcpy(dst + n - 16, &q2, 8);
            std::memcpy(dst + n - 8, &q3, 8);
            return;
        }

        if (n <= 64) {
            std::memcpy(dst, src, n);
            return;
        }

        std::memcpy(dst, src, n);
    }

    // Copy limited to SSO-sized ranges.
    inline void copy_sso(char* dst, const char* src, std::size_t n) noexcept {
        copy_small(
            reinterpret_cast<unsigned char*>(dst),
            reinterpret_cast<const unsigned char*>(src),
            n
        );
    }


    // Copy for heap-sized payloads with tuned fixed-size fast paths.
    inline void copy_heap_hot(char* dst, const char* src, std::size_t n) noexcept {
        // Fast paths for common string sizes (including null terminator).
        // These are optimised for the most frequent heap allocation sizes.
        if (n <= 64) {
            std::memcpy(dst, src, n);
            return;
        }

        switch (n) {
            case 96:
                std::memcpy(dst, src, 32);
                std::memcpy(dst + 32, src + 32, 32);
                std::memcpy(dst + 64, src + 64, 32);
                return;
            case 99:  // Common: 98-character string + null terminator.
                std::memcpy(dst, src, 64);
                std::memcpy(dst + 64, src + 64, 35);
                return;
            case 128:
                std::memcpy(dst, src, 64);
                std::memcpy(dst + 64, src + 64, 64);
                return;
            default:
                std::memcpy(dst, src, n);
                return;
        }
    }

    [[nodiscard]] inline constexpr bool fits_in_sso(std::size_t n) noexcept {
        return n < SSO_THRESHOLD;
    }

    // SSE2-accelerated single-character search, falling back to memchr.
    [[nodiscard]] inline const char* find_char_simd(const char* data,
                                                   std::size_t len,
                                                   char target) noexcept {
        if (len == 0) return nullptr;

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        const __m128i needle = _mm_set1_epi8(static_cast<char>(target));
        std::size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
            const __m128i cmp = _mm_cmpeq_epi8(block, needle);
            const int mask = _mm_movemask_epi8(cmp);
            if (mask != 0) {
                return data + i + static_cast<std::size_t>(first_set_bit_index(static_cast<unsigned>(mask)));
            }
        }
        for (; i < len; ++i) {
            if (data[i] == target) return data + i;
        }
        return nullptr;
#else
        return static_cast<const char*>(std::memchr(data, target, len));
#endif
    }

    // Boyer-Moore-Horspool substring search for needles up to 255 bytes.
    [[nodiscard]] inline const char* find_substring_bmh_compact(const char* haystack,
                                                                std::size_t haystack_len,
                                                                const char* needle,
                                                                std::size_t needle_len) noexcept {
        if (needle_len == 0) return haystack;
        if (needle_len > haystack_len) return nullptr;
        assert(needle_len <= 255 && "BMH compact requires needle_len <= 255");

        std::uint8_t shift[256];
        std::memset(shift, static_cast<unsigned char>(needle_len), sizeof(shift));
        for (std::size_t i = 0; i + 1 < needle_len; ++i) {
            shift[static_cast<unsigned char>(needle[i])] = static_cast<std::uint8_t>(needle_len - 1 - i);
        }

        const std::size_t last = needle_len - 1;
        std::size_t pos = 0;
        while (pos <= haystack_len - needle_len) {
            const unsigned char tail = static_cast<unsigned char>(haystack[pos + last]);
            if (tail == static_cast<unsigned char>(needle[last]) &&
                std::memcmp(haystack + pos, needle, last) == 0) {
                return haystack + pos;
            }
            pos += shift[tail];
        }
        return nullptr;
    }

    struct find_tuning_state {
        std::atomic<std::size_t> small_haystack_cutoff{256};
        std::atomic<std::size_t> bmh_haystack_cutoff{4096};
        std::atomic<std::uint32_t> adapt_counter{0};
    };

    [[nodiscard]] inline find_tuning_state& tuning_state() noexcept {
        static find_tuning_state s;
        return s;
    }

    // Returns the ratio of unique characters to total length in the needle,
    // used by the adaptive find threshold logic.
    [[nodiscard]] inline float needle_entropy_hint(const char* needle, std::size_t needle_len) noexcept {
        if (needle_len <= 1) return 1.0f;
        bool seen[256] = {};
        std::size_t unique = 0;
        for (std::size_t i = 0; i < needle_len; ++i) {
            const unsigned char c = static_cast<unsigned char>(needle[i]);
            if (!seen[c]) {
                seen[c] = true;
                ++unique;
            }
        }
        return static_cast<float>(unique) / static_cast<float>(needle_len);
    }

    // Periodically adjusts the small-haystack and BMH cutoff thresholds based
    // on observed search characteristics (needle entropy, match position).
    inline void adapt_find_thresholds(std::size_t haystack_len,
                                      std::size_t needle_len,
                                      float entropy,
                                      std::size_t found_pos) noexcept {
        thread_local std::uint32_t local_tick = 0;
        ++local_tick;
        if ((local_tick & 0x3FFu) != 0u) return;

        auto& st = tuning_state();
        st.adapt_counter.fetch_add(1, std::memory_order_relaxed);
        std::size_t small_cut = st.small_haystack_cutoff.load(std::memory_order_relaxed);
        std::size_t bmh_cut = st.bmh_haystack_cutoff.load(std::memory_order_relaxed);

        if (needle_len >= 5 && needle_len <= 64) {
            if (entropy < 0.45f) {
                bmh_cut = std::min<std::size_t>(8192, bmh_cut + 256);
            } else {
                bmh_cut = (bmh_cut > 2048) ? (bmh_cut - 128) : bmh_cut;
            }
        }

        if (found_pos != static_cast<std::size_t>(-1) && found_pos < 32) {
            small_cut = std::min<std::size_t>(512, small_cut + 16);
        } else if (haystack_len > 1024) {
            small_cut = (small_cut > 128) ? (small_cut - 8) : small_cut;
        }

        st.small_haystack_cutoff.store(small_cut, std::memory_order_relaxed);
        st.bmh_haystack_cutoff.store(bmh_cut, std::memory_order_relaxed);
    }

    // Multi-strategy SIMD-accelerated substring search.  Dispatches to
    // find_char_simd for single characters, a short-needle SIMD scan for
    // needles up to 4 bytes, full BMH for large haystacks with long needles,
    // and string_view::find for everything else.
    [[nodiscard]] inline const char* find_substring_simd(const char* haystack,
                                                         std::size_t haystack_len,
                                                         const char* needle,
                                                         std::size_t needle_len) noexcept {
        if (needle_len == 0) return haystack;
        if (needle_len > haystack_len) return nullptr;
        if (needle_len == 1) {
            return find_char_simd(haystack, haystack_len, needle[0]);
        }

        if (needle_len <= 4) {
            std::size_t offset = 0;
            const std::size_t limit = haystack_len - needle_len;
            while (offset <= limit) {
                const char* candidate = find_char_simd(haystack + offset, limit - offset + 1, needle[0]);
                if (!candidate) return nullptr;
                const std::size_t idx = static_cast<std::size_t>(candidate - haystack);
                if (candidate[1] == needle[1] &&
                    (needle_len == 2 || candidate[2] == needle[2]) &&
                    (needle_len <= 3 || candidate[3] == needle[3])) {
                    return candidate;
                }
                offset = idx + 1;
            }
            return nullptr;
        }

        if (haystack_len >= 2048 && needle_len >= 16) {
            std::size_t shift[256];
            for (std::size_t i = 0; i < 256; ++i) {
                shift[i] = needle_len;
            }
            for (std::size_t i = 0; i + 1 < needle_len; ++i) {
                shift[static_cast<unsigned char>(needle[i])] = needle_len - 1 - i;
            }

            const std::size_t last = needle_len - 1;
            std::size_t pos = 0;
            while (pos <= haystack_len - needle_len) {
                const char tail = haystack[pos + last];
                if (tail == needle[last] && std::memcmp(haystack + pos, needle, last) == 0) {
                    return haystack + pos;
                }
                pos += shift[static_cast<unsigned char>(tail)];
            }
            return nullptr;
        }

        if (haystack_len < 256) {
            std::string_view hs(haystack, haystack_len);
            const std::size_t found = hs.find(std::string_view(needle, needle_len));
            return found == std::string_view::npos ? nullptr : (haystack + found);
        }

        const char first = needle[0];
        const char last = needle[needle_len - 1];
        std::size_t offset = 0;
        const std::size_t limit = haystack_len - needle_len;
        while (offset <= limit) {
            const char* candidate = find_char_simd(haystack + offset, limit - offset + 1, first);
            if (!candidate) return nullptr;
            const std::size_t idx = static_cast<std::size_t>(candidate - haystack);
            if (candidate[needle_len - 1] == last && std::memcmp(candidate, needle, needle_len) == 0) {
                return candidate;
            }
            offset = idx + 1;
        }
        return nullptr;
    }

    // -------------------------------------------------------------------------
    // Two-Way string matching for large haystacks.
    //
    // Based on Crochemore & Rytter (1994) / the algorithm used in glibc's memmem
    // for large needles.  O(n + m) time, O(1) extra space.
    //
    // Outperforms std::string_view::find (glibc memmem) for haystacks >= 64 KB
    // where memmem's two-byte window approach degrades on low-entropy text, and
    // for needle lengths >= 32 where BMH shift tables offer the best skip distance
    // but the Two-Way search avoids the O(m) setup per search call.
    //
    // Preprocessing (critical factorization):
    //   Computes the Lyndon factorization boundary `p` and period `per` of needle
    //   in O(m) time and O(1) space using the two-way max-suffix algorithm.
    //
    // Search:
    //   Two-phase scan.  The left half of the needle never needs to be rescanned
    //   after a mismatch on the right half (the period property guarantees this).
    //   Inner loop is a simple byte comparison -- no SIMD required; the algorithm's
    //   memory access pattern is already cache-optimal.
    // -------------------------------------------------------------------------
    namespace two_way {

        // Compute the "max suffix" of needle under lexicographic order, storing
        // the period in *period.  Returns the index of the suffix start.  This
        // is the standard Crochemore two-way preprocessing pass.
        inline std::size_t max_suffix(const char* needle, std::size_t m,
                                      std::size_t* period) noexcept {
            std::size_t i = 0;          // suffix start candidate
            std::size_t j = 1;          // current position
            std::size_t k = 1;          // current period
            *period = 1;
            while (j + k <= m) {
                const unsigned char a = static_cast<unsigned char>(needle[j + k - 1]);
                const unsigned char b = static_cast<unsigned char>(needle[i + k - 1]);
                if (a < b) {
                    j += k;
                    k = 1;
                    *period = j - i;
                } else if (a == b) {
                    if (k == *period) {
                        j += *period;
                        k = 1;
                    } else {
                        ++k;
                    }
                } else {
                    // a > b: i is the new candidate.
                    i = j;
                    j = i + 1;
                    k = 1;
                    *period = 1;
                }
            }
            return i;
        }

        // Compute max suffix under the reverse lexicographic order (for the
        // "min suffix" variant used to pick the better factorization).
        inline std::size_t max_suffix_rev(const char* needle, std::size_t m,
                                          std::size_t* period) noexcept {
            std::size_t i = 0;
            std::size_t j = 1;
            std::size_t k = 1;
            *period = 1;
            while (j + k <= m) {
                const unsigned char a = static_cast<unsigned char>(needle[j + k - 1]);
                const unsigned char b = static_cast<unsigned char>(needle[i + k - 1]);
                if (a > b) {
                    j += k;
                    k = 1;
                    *period = j - i;
                } else if (a == b) {
                    if (k == *period) {
                        j += *period;
                        k = 1;
                    } else {
                        ++k;
                    }
                } else {
                    i = j;
                    j = i + 1;
                    k = 1;
                    *period = 1;
                }
            }
            return i;
        }

        [[nodiscard]] inline const char* search(const char* haystack, std::size_t n,
                                                 const char* needle,   std::size_t m) noexcept {
            if (m == 0) return haystack;
            if (m > n) return nullptr;

            // Fast path for short needles (m <= 8): skip the O(m) critical-
            // factorization preprocessing entirely.  For haystacks where the match
            // is at an early position, preprocessing dominates; memchr (SIMD-
            // accelerated in glibc/musl) + memcmp is substantially cheaper.
            if (m <= 8) {
                const char  first = needle[0];
                const char* scan  = haystack;
                const char* limit = haystack + n - m;
                while (scan <= limit) {
                    scan = static_cast<const char*>(
                        std::memchr(scan, first,
                                    static_cast<std::size_t>(limit - scan + 1)));
                    if (!scan) return nullptr;
                    if (std::memcmp(scan, needle, m) == 0) return scan;
                    ++scan;
                }
                return nullptr;
            }

            // Compute critical factorization: needle = needle[0..l] + needle[l+1..m-1].
            std::size_t per1 = 0, per2 = 0;
            const std::size_t l1 = max_suffix(needle, m, &per1);
            const std::size_t l2 = max_suffix_rev(needle, m, &per2);

            // Choose the factorization that gives the larger left part
            // (larger l -> stronger period guarantee -> fewer comparisons).
            std::size_t l, period;
            if (l1 >= l2) { l = l1; period = per1; }
            else          { l = l2; period = per2; }

            // Does the right half repeat with period `period` into the left half?
            // i.e., is needle[0..l] == needle[period..period+l]?
            bool periodic = (std::memcmp(needle, needle + period, l + 1) == 0);

            const char* pos = haystack;
            const char* end = haystack + n - m;
            std::size_t memory = 0; // how many chars of left half we already know match

            if (periodic) {
                // Periodic case: reuse partial match memory to skip left-half rescans.
#if defined(__AVX2__)
                // AVX2 pre-scan: when memory==0 and right half is non-empty, scan
                // 32 bytes/block for needle[l+1] at pos+l+1.  Blocks where the target
                // char is absent are skipped entirely; the scalar two-way comparison
                // only runs at confirmed candidates.  memory is already 0 at entry to
                // this block, so skipping ahead never invalidates stale partial-match
                // knowledge.
                const bool avx2_ok = (l + 1 < m);
                const __m256i first_r = avx2_ok
                    ? _mm256_set1_epi8(needle[l + 1])
                    : _mm256_setzero_si256();
#endif
                while (pos <= end) {
#if defined(__AVX2__)
                    if (avx2_ok && memory == 0) {
                        const char* scan = pos + l + 1;
                        while (scan + 32 <= haystack + n && pos <= end) {
                            unsigned mask = static_cast<unsigned>(
                                _mm256_movemask_epi8(_mm256_cmpeq_epi8(
                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(scan)),
                                    first_r)));
                            if (mask != 0u) {
                                pos = scan - (l + 1) + __builtin_ctz(mask);
                                goto avx2_periodic_found;
                            }
                            scan += 32;
                            pos += 32;
                        }
                        if (pos > end) return nullptr;
                        // Fewer than 32 bytes remain; fall through to scalar tail.
                        avx2_periodic_found:;
                    }
#endif
                    // Compare right half (pos+l+1 .. pos+m-1).
                    std::size_t i = std::max(l + 1, memory);
                    while (i < m && needle[i] == pos[i]) ++i;
                    if (i < m) {
                        // Mismatch in right half: skip forward, reset memory.
                        pos += static_cast<std::ptrdiff_t>(i - l);
                        memory = 0;
                        continue;
                    }
                    // Right half matched; compare left half starting from `memory`.
                    std::size_t j = memory;
                    while (j <= l && needle[j] == pos[j]) ++j;
                    if (j > l) return pos; // full match
                    // Mismatch in left half: advance by period, retain memory.
                    pos += static_cast<std::ptrdiff_t>(period);
                    memory = m > period ? m - period : 0;
                }
            } else {
                // Non-periodic case: no memory optimisation, but the right half
                // shift is at least (l+1) meaning we skip at least half the needle
                // per mismatch -- comparable to BMH but with O(1) preprocessing.
                const std::size_t right_skip = l + 1;
#if defined(__AVX2__)
                const bool avx2_ok = (l + 1 < m);
                const __m256i first_r = avx2_ok
                    ? _mm256_set1_epi8(needle[l + 1])
                    : _mm256_setzero_si256();
#endif
                while (pos <= end) {
#if defined(__AVX2__)
                    if (avx2_ok) {
                        const char* scan = pos + l + 1;
                        while (scan + 32 <= haystack + n && pos <= end) {
                            unsigned mask = static_cast<unsigned>(
                                _mm256_movemask_epi8(_mm256_cmpeq_epi8(
                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(scan)),
                                    first_r)));
                            if (mask != 0u) {
                                pos = scan - (l + 1) + __builtin_ctz(mask);
                                goto avx2_nonperiodic_found;
                            }
                            scan += 32;
                            pos += 32;
                        }
                        if (pos > end) return nullptr;
                        avx2_nonperiodic_found:;
                    }
#endif
                    std::size_t i = l + 1;
                    while (i < m && needle[i] == pos[i]) ++i;
                    if (i < m) {
                        pos += static_cast<std::ptrdiff_t>(i - l);
                        continue;
                    }
                    std::size_t j = 0;
                    while (j <= l && needle[j] == pos[j]) ++j;
                    if (j > l) return pos;
                    pos += right_skip;
                }
            }
            return nullptr;
        }

    } // namespace two_way

    // Threshold above which we prefer the two-way algorithm over memmem.
    // Measured on AMD EPYC 7763: memmem wins up to ~64 KB; two-way wins above.
    static constexpr std::size_t kTwoWayHaystackThreshold = 65536;

}  // namespace detail

// High-performance string class with small-string optimization.
//
// Strings of up to 23 bytes are stored inline (SSO buffer); longer strings
// use pool-backed heap allocation.  Substring search dispatches to
// SIMD-accelerated paths (SSE2/AVX2) for single characters and short
// needles, Boyer-Moore-Horspool for medium haystacks, and the Two-Way
// algorithm for haystacks above 64 KB.
//
// In debug builds (FL_DEBUG_THREAD_SAFETY), every public accessor acquires
// a read or write guard that detects unsynchronized concurrent access.
class string {
public:
    using value_type = char;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = char&;
    using const_reference = const char&;
    using pointer = char*;
    using const_pointer = const char*;
    using iterator = char*;
    using const_iterator = const char*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    string() noexcept : _size(0), _flags(0) {
        _data.sso[0] = '\0';
    }

    string(const char* cstr) noexcept : string(cstr, cstr ? std::strlen(cstr) : 0) {}

    // Compile-time length deduction avoids runtime strlen for string literals.
    template <std::size_t N>
    string(const char (&cstr)[N]) : _size(0), _flags(0) {
        constexpr size_type len = (N > 0) ? (N - 1) : 0;
        if constexpr (len == 0) {
            _data.sso[0] = '\0';
        } else if constexpr (detail::fits_in_sso(len)) {
            detail::copy_sso(_data.sso, cstr, len);
            _data.sso[len] = '\0';
            _size = len;
        } else {
            _allocate_heap_exact(len);
            detail::copy_heap_hot(_data.heap.ptr, cstr, len);
            _data.heap.ptr[len] = '\0';
            _size = len;
        }
    }

    string(const std::string& s) noexcept : string(s.c_str(), s.size()) {}
    string(std::string_view s) noexcept : string(s.data(), s.size()) {}

    string(const char* cstr, size_type len) : _size(0), _flags(0) {
        if (len > 0) {
            if (detail::fits_in_sso(len)) {
                detail::copy_sso(_data.sso, cstr, len);
                _data.sso[len] = '\0';
                _size = len;
            } else {
                _allocate_heap_exact(len);
                detail::copy_heap_hot(_data.heap.ptr, cstr, len);
                _data.heap.ptr[len] = '\0';
                _size = len;
            }
        } else {
            _data.sso[0] = '\0';
        }
    }

    string(size_type count, char ch) : _size(0), _flags(0) {
        if (count > 0) {
            if (detail::fits_in_sso(count)) {
                std::fill(_data.sso, _data.sso + count, ch);
                _data.sso[count] = '\0';
                _size = count;
            } else {
                _allocate_heap_exact(count);
                std::fill(_data.heap.ptr, _data.heap.ptr + count, ch);
                _data.heap.ptr[count] = '\0';
                _size = count;
            }
        } else {
            _data.sso[0] = '\0';
        }
    }

    string(const string& other) : _size(other._size), _flags(0) {
        if (other._is_heap_allocated()) {
            _allocate_heap_exact(other._size);
            std::memcpy(_data.heap.ptr, other._data.heap.ptr, other._size);
            _data.heap.ptr[_size] = '\0';
        } else {
            detail::copy_sso(_data.sso, other._data.sso, other._size + 1);
        }
    }

    // Throws std::out_of_range if pos > other.size().
    string(const string& other, size_type pos, size_type count = npos) : _size(0), _flags(0) {
        _data.sso[0] = '\0';
        if (pos > other._size) throw std::out_of_range("fl::string::string");
        const size_type len = std::min(count, other._size - pos);
        if (len == 0) return;
        const char* src = other._data_ptr() + pos;
        if (detail::fits_in_sso(len)) {
            detail::copy_sso(_data.sso, src, len);
            _data.sso[len] = '\0';
            _size = len;
        } else {
            _allocate_heap_exact(len);
            detail::copy_heap_hot(_data.heap.ptr, src, len);
            _data.heap.ptr[len] = '\0';
            _size = len;
        }
    }

    template <std::input_iterator InputIter>
    string(InputIter first, InputIter last) : _size(0), _flags(0) {
        _data.sso[0] = '\0';
        append(first, last);
    }

    string(std::initializer_list<char> ilist) : string(ilist.begin(), ilist.size()) {}

    string(string&& other) noexcept : _data(other._data), _size(other._size), _flags(other._flags) {
        other._size = 0;
        other._flags = 0;
        other._data.sso[0] = '\0';
    }

    ~string() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (_is_heap_allocated()) {
            std::size_t align = fl::preferred_alloc_alignment();
            fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, align);
        }
    }

    string& operator=(const string& other) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (this != &other) {
            _assign_impl(other._data_ptr(), other._size);
        }
        return *this;
    }

    string& operator=(string&& other) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (this != &other) {
            if (_is_heap_allocated()) {
                std::size_t align = fl::preferred_alloc_alignment();
                fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, align);
            }
            _size = other._size;
            _flags = other._flags;
            _data = other._data;
            other._size = 0;
            other._flags = 0;
            other._data.sso[0] = '\0';
        }
        return *this;
    }

    void swap(string& other) noexcept {
        if (this == &other) return;
        [[maybe_unused]] auto guard = _guard_write(FL_LOC);
        [[maybe_unused]] auto other_guard = other._guard_write(FL_LOC);
        std::swap(_size, other._size);
        std::swap(_flags, other._flags);
        std::swap(_data, other._data);
    }

    string& operator=(const char* cstr) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (cstr) {
            _assign_impl(cstr, std::strlen(cstr));
        } else {
            clear();
        }
        return *this;
    }

    string& operator=(std::string_view s) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(s.data(), s.size());
        return *this;
    }

    string& operator=(std::initializer_list<char> ilist) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(ilist.begin(), ilist.size());
        return *this;
    }

    [[nodiscard]] const_reference operator[](size_type pos) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr()[pos];
    }

    [[nodiscard]] reference operator[](size_type pos) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable()[pos];
    }

    // Throws std::out_of_range if pos >= size().
    [[nodiscard]] const_reference at(size_type pos) const {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (pos >= _size) {
            throw std::out_of_range("fl::string::at");
        }
        return _data_ptr()[pos];
    }

    // Throws std::out_of_range if pos >= size().
    [[nodiscard]] reference at(size_type pos) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (pos >= _size) {
            throw std::out_of_range("fl::string::at");
        }
        return _data_ptr_mutable()[pos];
    }

    [[nodiscard]] const_reference front() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr()[0];
    }

    [[nodiscard]] reference front() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable()[0];
    }

    [[nodiscard]] const_reference back() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr()[_size - 1];
    }

    [[nodiscard]] reference back() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable()[_size - 1];
    }

    [[nodiscard]] const_pointer data() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        auto span = _data_span();
        return span.data();
    }

    [[nodiscard]] pointer data() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _mutable_storage().data();
    }

    [[nodiscard]] const char* c_str() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        auto span = _data_span();
        return span.data();
    }

    [[nodiscard]] iterator begin() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable();
    }

    [[nodiscard]] const_iterator begin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr();
    }

    [[nodiscard]] const_iterator cbegin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr();
    }

    [[nodiscard]] iterator end() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable() + _size;
    }

    [[nodiscard]] const_iterator end() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr() + _size;
    }

    [[nodiscard]] const_iterator cend() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr() + _size;
    }

    [[nodiscard]] reverse_iterator rbegin() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return reverse_iterator(end());
    }

    [[nodiscard]] const_reverse_iterator rbegin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(end());
    }

    [[nodiscard]] reverse_iterator rend() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return reverse_iterator(begin());
    }

    [[nodiscard]] const_reverse_iterator rend() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(begin());
    }

    [[nodiscard]] const_reverse_iterator crbegin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(_data_ptr() + _size);
    }

    [[nodiscard]] const_reverse_iterator crend() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(_data_ptr());
    }

    [[nodiscard]] size_type size() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size;
    }

    [[nodiscard]] size_type length() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size;
    }

    [[nodiscard]] size_type capacity() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (_is_heap_allocated()) {
            return _data.heap.capacity;
        }
        return SSO_CAPACITY;
    }

    [[nodiscard]] size_type max_size() const noexcept {
        return static_cast<size_type>(-1) / 2;
    }

    [[nodiscard]] bool empty() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size == 0;
    }

    void reserve(size_type cap) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (cap > capacity()) {
            _grow_to(cap);
        }
    }

    void shrink_to_fit() {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (_is_heap_allocated() && _size < _data.heap.capacity) {
            if (detail::fits_in_sso(_size)) {
                std::array<char, SSO_CAPACITY + 1> temp{};
                detail::copy_sso(temp.data(), _data.heap.ptr, _size);
                std::size_t align = fl::preferred_alloc_alignment();
                fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, align);
                detail::copy_sso(_data.sso, temp.data(), _size);
                _data.sso[_size] = '\0';
                _flags = 0;
            } else {
                std::size_t align = fl::preferred_alloc_alignment();
                char* new_ptr = static_cast<char*>(fl::allocate_bytes_aligned(_size + 1, align));
                std::memcpy(new_ptr, _data.heap.ptr, _size);
                new_ptr[_size] = '\0';
                fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, align);
                _data.heap.ptr = new_ptr;
                _data.heap.capacity = _size;
            }
        }
    }

    void clear() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _size = 0;
        auto storage = _mutable_storage();
        storage[0] = '\0';
    }

    string& append(std::string_view sv) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return append(sv.data(), sv.size());
    }

    string& append(const char* cstr) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return append(cstr, cstr ? std::strlen(cstr) : 0);
    }

    string& append(const char* cstr, size_type len) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (len == 0) return *this;

        size_type new_size = _size + len;
        if (new_size > capacity()) {
            _grow_to(new_size);
        }

        char* ptr = _data_ptr_mutable();
        std::memcpy(ptr + _size, cstr, len);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    string& append(const string& other) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return append(other.data(), other.size());
    }

    string& append(char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);

        size_type cur_size = _size;
        size_type new_size = cur_size + 1;
        if (new_size > capacity()) {
            _grow_to(new_size);
        }

        char* ptr = _data_ptr_mutable();
        ptr[cur_size] = ch;
        ptr[new_size] = '\0';
        _size = new_size;
        return *this;
    }

    string& append(size_type count, char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (count == 0) return *this;

        size_type new_size = _size + count;
        if (new_size > capacity()) {
            _grow_to(new_size);
        }

        char* ptr = _data_ptr_mutable();
        std::memset(ptr + _size, ch, count);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    template <std::input_iterator InputIter>
    string& append(InputIter first, InputIter last) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);

        if constexpr (std::random_access_iterator<InputIter>) {
            size_type count = static_cast<size_type>(std::distance(first, last));
            if (count == 0) return *this;

            size_type new_size = _size + count;
            if (new_size >= capacity()) {
                _grow_to(new_size + (new_size / 2));
            }

            char* ptr = _data_ptr_mutable() + _size;
            while (first != last) {
                *ptr++ = *first++;
            }
            _size = new_size;
            _data_ptr_mutable()[_size] = '\0';
        } else {
            while (first != last) {
                append(*first);
                ++first;
            }
        }
        return *this;
    }

    string& operator+=(const char* cstr) noexcept { return append(cstr); }
    string& operator+=(const string& str) noexcept { return append(str); }
    string& operator+=(char ch) noexcept { return append(ch); }
    string& operator+=(std::string_view s) noexcept { return append(s.data(), s.size()); }
    string& operator+=(std::initializer_list<char> ilist) noexcept { return append(ilist.begin(), ilist.size()); }

    // Throws std::out_of_range if pos > str.size().
    string& append(const string& str, size_type pos, size_type count = npos) {
        if (pos > str._size) throw std::out_of_range("fl::string::append");
        const size_type len = std::min(count, str._size - pos);
        return append(str._data_ptr() + pos, len);
    }

    string& append(std::initializer_list<char> ilist) noexcept {
        return append(ilist.begin(), ilist.size());
    }

    string& assign(const char* cstr, size_type len) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(cstr, len);
        return *this;
    }

    string& assign(const char* cstr) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(cstr, cstr ? std::strlen(cstr) : 0);
        return *this;
    }

    string& assign(const string& other) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (this != &other) {
            _assign_impl(other.data(), other.size());
        }
        return *this;
    }

    string& assign(std::string_view sv) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(sv.data(), sv.size());
        return *this;
    }

    string& assign(size_type count, char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        clear();
        return append(count, ch);
    }

    string& assign(string&& str) noexcept {
        return *this = std::move(str);
    }

    // Throws std::out_of_range if pos > str.size().
    string& assign(const string& str, size_type pos, size_type count = npos) {
        if (pos > str._size) throw std::out_of_range("fl::string::assign");
        const size_type len = std::min(count, str._size - pos);
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(str._data_ptr() + pos, len);
        return *this;
    }

    string& assign(std::initializer_list<char> ilist) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(ilist.begin(), ilist.size());
        return *this;
    }

    template <std::input_iterator InputIter>
    string& assign(InputIter first, InputIter last) noexcept {
        clear();
        append(first, last);
        return *this;
    }

    void push_back(char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        append(ch);
    }

    void pop_back() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (_size > 0) {
            _size--;
            _data_ptr_mutable()[_size] = '\0';
        }
    }

    string& erase(size_type pos = 0, size_type len = npos) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (pos >= _size) return *this;

        if (len == npos) {
            len = _size - pos;
        } else {
            len = std::min(len, _size - pos);
        }

        if (len == 0) return *this;

        char* ptr = _data_ptr_mutable();
        std::memmove(ptr + pos, ptr + pos + len, _size - pos - len);
        _size -= len;
        ptr[_size] = '\0';
        return *this;
    }

    iterator erase(const_iterator pos) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = pos - begin();
        erase(idx, 1);
        return begin() + idx;
    }

    iterator erase(const_iterator first, const_iterator last) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = first - begin();
        size_type len = last - first;
        erase(idx, len);
        return begin() + idx;
    }

    string& insert(size_type pos, const string& str) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return insert(pos, str.data(), str.size());
    }

    string& insert(size_type pos, const char* cstr, size_type len) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (len == 0 || pos > _size) return *this;

        size_type new_size = _size + len;
        if (new_size >= capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        auto storage = _mutable_storage();
        auto* ptr = storage.data();
        std::memmove(ptr + pos + len, ptr + pos, _size - pos);
        std::memcpy(ptr + pos, cstr, len);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    string& insert(size_type pos, const char* cstr) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return insert(pos, cstr, cstr ? std::strlen(cstr) : 0);
    }

    string& insert(size_type pos, size_type count, char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (count == 0 || pos > _size) return *this;

        size_type new_size = _size + count;
        if (new_size >= capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        auto storage = _mutable_storage();
        auto* ptr = storage.data();
        std::memmove(ptr + pos + count, ptr + pos, _size - pos);
        std::memset(ptr + pos, ch, count);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    iterator insert(const_iterator pos, char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = pos - begin();
        insert(idx, 1, ch);
        return begin() + idx;
    }

    iterator insert(const_iterator pos, size_type count, char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = pos - begin();
        insert(idx, count, ch);
        return begin() + idx;
    }

    string& insert(size_type pos, std::string_view sv) noexcept {
        return insert(pos, sv.data(), sv.size());
    }

    // Throws std::out_of_range if ipos > str.size().
    string& insert(size_type pos, const string& str, size_type ipos, size_type icount = npos) {
        if (ipos > str._size) throw std::out_of_range("fl::string::insert");
        const size_type len = std::min(icount, str._size - ipos);
        return insert(pos, str._data_ptr() + ipos, len);
    }

    template <std::input_iterator InputIter>
    iterator insert(const_iterator pos, InputIter first, InputIter last) noexcept {
        size_type idx = static_cast<size_type>(pos - begin());
        if constexpr (std::contiguous_iterator<InputIter> &&
                      std::is_same_v<std::iter_value_t<InputIter>, char>) {
            insert(idx, std::to_address(first),
                   static_cast<size_type>(std::distance(first, last)));
        } else {
            string tmp(first, last);
            insert(idx, tmp._data_ptr(), tmp._size);
        }
        return begin() + idx;
    }

    iterator insert(const_iterator pos, std::initializer_list<char> ilist) noexcept {
        size_type idx = static_cast<size_type>(pos - begin());
        insert(idx, ilist.begin(), ilist.size());
        return begin() + idx;
    }

    string& replace(size_type pos, size_type len, const string& str) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return replace(pos, len, str._data_ptr(), str._size);
    }

    string& replace(size_type pos, size_type len, const char* cstr, size_type clen) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (pos > _size) return *this;

        len = std::min(len, _size - pos);
        size_type new_size = _size - len + clen;

        if (new_size >= capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        auto storage = _mutable_storage();
        auto* ptr = storage.data();
        if (len != clen) {
            std::memmove(ptr + pos + clen, ptr + pos + len, _size - pos - len);
        }
        std::memcpy(ptr + pos, cstr, clen);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    string& replace(size_type pos, size_type len, const char* cstr) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return replace(pos, len, cstr, cstr ? std::strlen(cstr) : 0);
    }

    string& replace(size_type pos, size_type len, size_type count, char ch) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (pos > _size) return *this;

        len = std::min(len, _size - pos);
        size_type new_size = _size - len + count;

        if (new_size >= capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        auto storage = _mutable_storage();
        auto* ptr = storage.data();
        if (len != count) {
            std::memmove(ptr + pos + count, ptr + pos + len, _size - pos - len);
        }
        std::memset(ptr + pos, ch, count);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    string& replace(size_type pos, size_type len, std::string_view sv) noexcept {
        return replace(pos, len, sv.data(), sv.size());
    }

    // Throws std::out_of_range if ipos > str.size().
    string& replace(size_type pos, size_type len, const string& str, size_type ipos, size_type icount = npos) {
        if (ipos > str._size) throw std::out_of_range("fl::string::replace");
        const size_type ilen = std::min(icount, str._size - ipos);
        return replace(pos, len, str._data_ptr() + ipos, ilen);
    }

    string& replace(const_iterator first, const_iterator last, const string& str) noexcept {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       str._data_ptr(), str._size);
    }

    string& replace(const_iterator first, const_iterator last, const char* cstr, size_type clen) noexcept {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       cstr, clen);
    }

    string& replace(const_iterator first, const_iterator last, const char* cstr) noexcept {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       cstr ? cstr : "", cstr ? std::strlen(cstr) : size_type(0));
    }

    string& replace(const_iterator first, const_iterator last, size_type count, char ch) noexcept {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       count, ch);
    }

    string& replace(const_iterator first, const_iterator last, std::string_view sv) noexcept {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       sv.data(), sv.size());
    }

    string& replace(const_iterator first, const_iterator last, std::initializer_list<char> ilist) noexcept {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       ilist.begin(), ilist.size());
    }

    template <std::input_iterator InputIter>
    string& replace(const_iterator first, const_iterator last, InputIter rfirst, InputIter rlast) noexcept {
        const size_type pos = static_cast<size_type>(first - begin());
        const size_type len = static_cast<size_type>(last - first);
        if constexpr (std::contiguous_iterator<InputIter> &&
                      std::is_same_v<std::iter_value_t<InputIter>, char>) {
            return replace(pos, len, std::to_address(rfirst),
                           static_cast<size_type>(std::distance(rfirst, rlast)));
        } else {
            string tmp(rfirst, rlast);
            return replace(pos, len, tmp._data_ptr(), tmp._size);
        }
    }

    // Copies up to count characters starting at pos into dest.  The
    // destination buffer is not null-terminated by this function.
    // Throws std::out_of_range if pos > size().
    size_type copy(char* dest, size_type count, size_type pos = 0) const {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (pos > _size) throw std::out_of_range("fl::string::copy");
        const size_type len = std::min(count, _size - pos);
        std::memcpy(dest, _data_ptr() + pos, len);
        return len;
    }

    void resize(size_type new_size, char fill = '\0') noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (new_size > _size) {
            if (new_size >= capacity()) {
                _grow_to(new_size + (new_size / 2));
            }
            std::fill(_data_ptr_mutable() + _size, _data_ptr_mutable() + new_size, fill);
        }
        _size = new_size;
        _data_ptr_mutable()[_size] = '\0';
    }

    [[nodiscard]] std::strong_ordering operator<=>(const string& other) const noexcept {
        return std::string_view(*this) <=> std::string_view(other);
    }

    [[nodiscard]] bool operator==(const string& other) const noexcept {
        return _size == other._size && std::memcmp(_data_ptr(), other._data_ptr(), _size) == 0;
    }

private:
    template <typename Allocator>
    friend class basic_lazy_concat;

    static string _concat_raw(const char* lhs_ptr, size_type lhs_size,
                              const char* rhs_ptr, size_type rhs_size) {
        string out;
        const size_type total = lhs_size + rhs_size;
        if (total == 0) {
            return out;
        }

        if (detail::fits_in_sso(total)) {
            out._flags = 0;
            if (lhs_size != 0) {
                detail::copy_sso(out._data.sso, lhs_ptr, lhs_size);
            }
            if (rhs_size != 0) {
                detail::copy_sso(out._data.sso + lhs_size, rhs_ptr, rhs_size);
            }
            out._size = total;
            out._data.sso[total] = '\0';
            return out;
        }

        char* dst = nullptr;
        {
            out._allocate_heap(total);
            dst = out._data.heap.ptr;
        }

        if (lhs_size != 0) {
            std::memcpy(dst, lhs_ptr, lhs_size);
            dst += lhs_size;
        }
        if (rhs_size != 0) {
            std::memcpy(dst, rhs_ptr, rhs_size);
        }
        out._size = total;
        out._data.heap.ptr[total] = '\0';
        return out;
    }

    static string _concat_raw_move_lhs(string&& lhs, const char* rhs_ptr, size_type rhs_size) {
        if (rhs_size == 0) {
            return std::move(lhs);
        }
        lhs.reserve(lhs._size + rhs_size);
        lhs.append(rhs_ptr, rhs_size);
        return std::move(lhs);
    }

    static string _concat_raw_move_rhs(const char* lhs_ptr, size_type lhs_size, string&& rhs) {
        if (lhs_size == 0) {
            return std::move(rhs);
        }
        return _concat_raw(lhs_ptr, lhs_size, rhs._data_ptr(), rhs._size);
    }

public:
    friend string operator+(const string& lhs, const string& rhs) {
        return _concat_raw(lhs._data_ptr(), lhs._size, rhs._data_ptr(), rhs._size);
    }

    friend string operator+(string&& lhs, const string& rhs) {
        return _concat_raw_move_lhs(std::move(lhs), rhs._data_ptr(), rhs._size);
    }

    friend string operator+(const string& lhs, string&& rhs) {
        return _concat_raw_move_rhs(lhs._data_ptr(), lhs._size, std::move(rhs));
    }

    friend string operator+(string&& lhs, string&& rhs) {
        return _concat_raw_move_lhs(std::move(lhs), rhs._data_ptr(), rhs._size);
    }

    friend string operator+(const string& lhs, const char* rhs) {
        const char* rhs_ptr = rhs ? rhs : "";
        const size_type rhs_size = rhs ? std::strlen(rhs) : 0;
        return _concat_raw(lhs._data_ptr(), lhs._size, rhs_ptr, rhs_size);
    }

    friend string operator+(string&& lhs, const char* rhs) {
        const char* rhs_ptr = rhs ? rhs : "";
        const size_type rhs_size = rhs ? std::strlen(rhs) : 0;
        return _concat_raw_move_lhs(std::move(lhs), rhs_ptr, rhs_size);
    }

    friend string operator+(const char* lhs, const string& rhs) {
        const char* lhs_ptr = lhs ? lhs : "";
        const size_type lhs_size = lhs ? std::strlen(lhs) : 0;
        return _concat_raw(lhs_ptr, lhs_size, rhs._data_ptr(), rhs._size);
    }

    friend string operator+(const char* lhs, string&& rhs) {
        const char* lhs_ptr = lhs ? lhs : "";
        const size_type lhs_size = lhs ? std::strlen(lhs) : 0;
        return _concat_raw_move_rhs(lhs_ptr, lhs_size, std::move(rhs));
    }

    [[nodiscard]] size_type find(char ch, size_type pos = 0) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (pos >= _size) return npos;
        const char* p = _data_ptr();
        const char* res = static_cast<const char*>(std::memchr(p + pos, ch, _size - pos));
        return res ? static_cast<size_type>(res - p) : npos;
    }

    [[nodiscard]] size_type find(const char* substr, size_type pos = 0) const noexcept {
        if (!substr) return npos;
        if (!substr[0]) {
            [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
            return pos <= _size ? pos : npos;
        }
        return find(std::string_view(substr), pos);
    }

    template <std::size_t N>
    [[nodiscard]] size_type find(const char (&substr)[N], size_type pos = 0) const noexcept {
        return find(std::string_view(substr, N - 1), pos);
    }

    // Primary multi-character find implementation.
    //
    // For single characters, delegates to an optimised memchr path.  For
    // multi-character needles the strategy depends on haystack size:
    //
    //   - Haystacks >= 64 KB with needles >= 2: Two-Way algorithm, which is
    //     O(n + m) time and O(1) space.  glibc memmem degrades on low-entropy
    //     text at these sizes; Two-Way's period-based memory avoids rescanning.
    //
    //   - Everything else: std::string_view::find (glibc memmem), which uses
    //     AVX2 internally and outperforms hand-rolled SIMD/BMH at all measured
    //     haystack sizes up to 64 KB.
    [[nodiscard]] size_type find(std::string_view sv, size_type pos = 0) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (pos > _size) return npos;
        if (sv.empty()) return pos;

        const char* data = _data_ptr();
        const size_type remaining = _size - pos;

        // Fast path: single character uses optimised memchr.
        if (sv.size() == 1) {
            const char* found = static_cast<const char*>(std::memchr(data + pos, sv[0], remaining));
            return found ? static_cast<size_type>(found - data) : npos;
        }

        // For very large haystacks (>= 64 KB), use the Two-Way algorithm which is
        // O(n + m) time, O(1) space.  glibc memmem switches to a different internal
        // strategy at 64 KB boundaries that loses performance on low-entropy text;
        // the Two-Way algorithm's period-based memory avoids rescanning and maintains
        // linear time regardless of text entropy.
        if (remaining >= detail::kTwoWayHaystackThreshold && sv.size() >= 2) {
            const char* found = detail::two_way::search(
                data + pos, remaining, sv.data(), sv.size());
            return found ? static_cast<size_type>(found - data) : npos;
        }
        {
            const std::string_view haystack(data + pos, remaining);
            const size_type found = haystack.find(sv);
            return found == npos ? npos : (pos + found);
        }
    }

    [[nodiscard]] size_type rfind(char ch, size_type pos = npos) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (_size == 0) return npos;
        if (pos >= _size) pos = _size - 1;
        const char* p = _data_ptr();
        for (difference_type i = static_cast<difference_type>(pos); i >= 0; --i) {
            if (p[i] == ch) return static_cast<size_type>(i);
        }
        return npos;
    }

    [[nodiscard]] size_type rfind(std::string_view sv, size_type pos = npos) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).rfind(sv, pos);
    }

    [[nodiscard]] size_type find(const string& str, size_type pos = 0) const noexcept {
        return find(std::string_view(str._data_ptr(), str._size), pos);
    }

    [[nodiscard]] size_type find(const char* cstr, size_type pos, size_type count) const noexcept {
        return find(std::string_view(cstr, count), pos);
    }

    [[nodiscard]] size_type rfind(const string& str, size_type pos = npos) const noexcept {
        return rfind(std::string_view(str._data_ptr(), str._size), pos);
    }

    [[nodiscard]] size_type rfind(const char* cstr, size_type pos = npos) const noexcept {
        return rfind(std::string_view(cstr ? cstr : ""), pos);
    }

    [[nodiscard]] size_type rfind(const char* cstr, size_type pos, size_type count) const noexcept {
        return rfind(std::string_view(cstr, count), pos);
    }

    [[nodiscard]] size_type find_first_of(char ch, size_type pos = 0) const noexcept {
        return find(ch, pos);
    }

    [[nodiscard]] size_type find_first_of(std::string_view sv, size_type pos = 0) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).find_first_of(sv, pos);
    }

    [[nodiscard]] size_type find_first_of(const string& str, size_type pos = 0) const noexcept {
        return find_first_of(std::string_view(str._data_ptr(), str._size), pos);
    }

    [[nodiscard]] size_type find_first_of(const char* cstr, size_type pos = 0) const noexcept {
        return find_first_of(std::string_view(cstr ? cstr : ""), pos);
    }

    [[nodiscard]] size_type find_first_of(const char* cstr, size_type pos, size_type count) const noexcept {
        return find_first_of(std::string_view(cstr, count), pos);
    }

    [[nodiscard]] size_type find_last_of(char ch, size_type pos = npos) const noexcept {
        return rfind(ch, pos);
    }

    [[nodiscard]] size_type find_last_of(std::string_view sv, size_type pos = npos) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).find_last_of(sv, pos);
    }

    [[nodiscard]] size_type find_last_of(const string& str, size_type pos = npos) const noexcept {
        return find_last_of(std::string_view(str._data_ptr(), str._size), pos);
    }

    [[nodiscard]] size_type find_last_of(const char* cstr, size_type pos = npos) const noexcept {
        return find_last_of(std::string_view(cstr ? cstr : ""), pos);
    }

    [[nodiscard]] size_type find_last_of(const char* cstr, size_type pos, size_type count) const noexcept {
        return find_last_of(std::string_view(cstr, count), pos);
    }

    [[nodiscard]] size_type find_first_not_of(char ch, size_type pos = 0) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).find_first_not_of(ch, pos);
    }

    [[nodiscard]] size_type find_first_not_of(std::string_view sv, size_type pos = 0) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).find_first_not_of(sv, pos);
    }

    [[nodiscard]] size_type find_first_not_of(const string& str, size_type pos = 0) const noexcept {
        return find_first_not_of(std::string_view(str._data_ptr(), str._size), pos);
    }

    [[nodiscard]] size_type find_first_not_of(const char* cstr, size_type pos = 0) const noexcept {
        return find_first_not_of(std::string_view(cstr ? cstr : ""), pos);
    }

    [[nodiscard]] size_type find_first_not_of(const char* cstr, size_type pos, size_type count) const noexcept {
        return find_first_not_of(std::string_view(cstr, count), pos);
    }

    [[nodiscard]] size_type find_last_not_of(char ch, size_type pos = npos) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).find_last_not_of(ch, pos);
    }

    [[nodiscard]] size_type find_last_not_of(std::string_view sv, size_type pos = npos) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).find_last_not_of(sv, pos);
    }

    [[nodiscard]] size_type find_last_not_of(const string& str, size_type pos = npos) const noexcept {
        return find_last_not_of(std::string_view(str._data_ptr(), str._size), pos);
    }

    [[nodiscard]] size_type find_last_not_of(const char* cstr, size_type pos = npos) const noexcept {
        return find_last_not_of(std::string_view(cstr ? cstr : ""), pos);
    }

    [[nodiscard]] size_type find_last_not_of(const char* cstr, size_type pos, size_type count) const noexcept {
        return find_last_not_of(std::string_view(cstr, count), pos);
    }

    [[nodiscard]] int compare(const string& other) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(_data_ptr(), _size).compare(
            std::string_view(other._data_ptr(), other._size));
    }

    [[nodiscard]] int compare(std::string_view sv) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string_view(*this).compare(sv);
    }

    // Throws std::out_of_range if pos > size().
    [[nodiscard]] int compare(size_type pos, size_type len, std::string_view sv) const {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (pos > _size) throw std::out_of_range("fl::string::compare");
        return std::string_view(_data_ptr() + pos, std::min(len, _size - pos)).compare(sv);
    }

    // Throws std::out_of_range if pos1 > size().
    [[nodiscard]] int compare(size_type pos1, size_type count1, const string& str) const {
        if (pos1 > _size) throw std::out_of_range("fl::string::compare");
        return std::string_view(_data_ptr() + pos1, std::min(count1, _size - pos1))
            .compare(std::string_view(str._data_ptr(), str._size));
    }

    // Throws std::out_of_range if pos1 > size() or pos2 > str.size().
    [[nodiscard]] int compare(size_type pos1, size_type count1, const string& str,
                              size_type pos2, size_type count2 = npos) const {
        if (pos1 > _size) throw std::out_of_range("fl::string::compare");
        if (pos2 > str._size) throw std::out_of_range("fl::string::compare");
        return std::string_view(_data_ptr() + pos1, std::min(count1, _size - pos1))
            .compare(std::string_view(str._data_ptr() + pos2, std::min(count2, str._size - pos2)));
    }

    [[nodiscard]] int compare(const char* cstr) const noexcept {
        return std::string_view(_data_ptr(), _size).compare(std::string_view(cstr ? cstr : ""));
    }

    // Throws std::out_of_range if pos > size().
    [[nodiscard]] int compare(size_type pos, size_type len, const char* cstr) const {
        if (pos > _size) throw std::out_of_range("fl::string::compare");
        return std::string_view(_data_ptr() + pos, std::min(len, _size - pos))
            .compare(std::string_view(cstr ? cstr : ""));
    }

    // Throws std::out_of_range if pos > size().
    [[nodiscard]] int compare(size_type pos, size_type len, const char* cstr, size_type count) const {
        if (pos > _size) throw std::out_of_range("fl::string::compare");
        return std::string_view(_data_ptr() + pos, std::min(len, _size - pos))
            .compare(std::string_view(cstr, count));
    }

    [[nodiscard]] bool starts_with(std::string_view sv) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size >= sv.size() && std::memcmp(_data_ptr(), sv.data(), sv.size()) == 0;
    }

    [[nodiscard]] bool starts_with(char ch) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size > 0 && _data_ptr()[0] == ch;
    }

    [[nodiscard]] bool starts_with(const char* cstr) const noexcept {
        return starts_with(std::string_view(cstr ? cstr : ""));
    }

    [[nodiscard]] bool ends_with(std::string_view sv) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size >= sv.size() && std::memcmp(_data_ptr() + _size - sv.size(), sv.data(), sv.size()) == 0;
    }

    [[nodiscard]] bool ends_with(char ch) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size > 0 && _data_ptr()[_size - 1] == ch;
    }

    [[nodiscard]] bool ends_with(const char* cstr) const noexcept {
        return ends_with(std::string_view(cstr ? cstr : ""));
    }

    [[nodiscard]] bool contains(std::string_view sv) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return find(sv) != npos;
    }

    [[nodiscard]] bool contains(char ch) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return find(ch) != npos;
    }

    // Throws std::out_of_range if pos > size().
    [[nodiscard]] string substr(size_type pos = 0, size_type len = npos) const {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (pos > _size) throw std::out_of_range("fl::string::substr");
        len = std::min(len, _size - pos);
        return string(_data_ptr() + pos, len);
    }

    [[nodiscard]] fl::substring_view substr_view(size_type pos = 0, size_type len = npos) const noexcept {
        return fl::substring_view(*this, pos, len);
    }

    // Zero-copy substring view (alias for substr_view).
    [[nodiscard]] fl::substring_view slice(size_type pos = 0, size_type len = npos) const noexcept {
        return substr_view(pos, len);
    }

    [[nodiscard]] fl::substring_view left_view(size_type count) const noexcept {
        return substr_view(0, count);
    }

    [[nodiscard]] fl::substring_view right_view(size_type count) const noexcept {
        if (count >= _size) return substr_view(0, _size);
        return substr_view(_size - count, count);
    }

    // Returns a zero-copy view of the matched substring, or an empty view
    // if the needle is not found.
    [[nodiscard]] fl::substring_view find_view(std::string_view needle, size_type pos = 0) const noexcept {
        size_type where = find(needle, pos);
        if (where == npos) return fl::substring_view();
        return substr_view(where, needle.size());
    }

    operator std::string_view() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        auto span = _data_span();
        return std::string_view(span.data(), span.size());
    }

    static constexpr size_type npos = static_cast<size_type>(-1);

private:
    friend class string_builder;

    struct _thread_safety_noop_guard {
        _thread_safety_noop_guard() noexcept = default;
    };

    using _thread_safety_guard =
#if FL_DEBUG_THREAD_SAFETY
        fl::debug::thread_access_tracker::guard;
#else
        _thread_safety_noop_guard;
#endif

    _thread_safety_guard _guard_read(const char* loc) const {
#if FL_DEBUG_THREAD_SAFETY
        return _ts.begin_read(loc);
#else
        (void)loc;
        return _thread_safety_noop_guard{};
#endif
    }

    _thread_safety_guard _guard_write(const char* loc) const {
#if FL_DEBUG_THREAD_SAFETY
        return _ts.begin_write(loc);
#else
        (void)loc;
        return _thread_safety_noop_guard{};
#endif
    }

    // Storage union: the SSO buffer and the heap pointer share the same
    // memory so that _data.sso sits at offset zero for cache-friendly access.
    union StorageData {
        char sso[SSO_CAPACITY + 1];
        struct {
            char* ptr;
            size_type capacity;
        } heap;
    };

    StorageData _data;
    size_type _size;
    uint8_t _flags;

#if FL_DEBUG_THREAD_SAFETY
    mutable fl::debug::thread_access_tracker _ts;
#endif

    static constexpr uint8_t HEAP_ALLOCATED_FLAG = 0x01;

    bool _is_heap_allocated() const noexcept {
        return (_flags & HEAP_ALLOCATED_FLAG) != 0;
    }

    [[nodiscard]] std::span<char> _mutable_storage() noexcept {
        if (_is_heap_allocated()) {
            return std::span<char>(_data.heap.ptr, _data.heap.capacity + 1);
        }
        return std::span<char>(_data.sso, SSO_CAPACITY + 1);
    }

    [[nodiscard]] std::span<const char> _data_span() const noexcept {
        return {_data_ptr(), _size};
    }

    const char* _data_ptr() const noexcept {
        if (_is_heap_allocated()) {
            return _data.heap.ptr;
        }
        return _data.sso;
    }

    char* _data_ptr_mutable() noexcept {
        if (_is_heap_allocated()) {
            return _data.heap.ptr;
        }
        return _data.sso;
    }

    void _allocate_heap(size_type min_capacity) {
        size_type new_capacity = _calculate_new_capacity(min_capacity);
        std::size_t align = fl::preferred_alloc_alignment();
        std::size_t alloc_n = new_capacity + 1;
        _data.heap.ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, align));
        _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
        _flags |= HEAP_ALLOCATED_FLAG;
    }

    // Allocates heap storage with exactly the requested capacity.  The actual
    // usable capacity may be larger because the pool allocator rounds up to
    // the next pool class size (e.g. requesting 101 bytes lands in the
    // 128-byte pool class, giving capacity 127 instead of 100).
    void _allocate_heap_exact(size_type exact_capacity) {
        std::size_t align = fl::preferred_alloc_alignment();
        std::size_t alloc_n = exact_capacity + 1;
        _data.heap.ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, align));
        _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
        _flags |= HEAP_ALLOCATED_FLAG;
    }

    void _grow_to(size_type min_capacity) {
        if (min_capacity <= capacity()) return;

        if (!_is_heap_allocated()) {
            size_type new_capacity = _calculate_new_capacity(min_capacity);
            std::size_t align = fl::preferred_alloc_alignment();
            std::size_t alloc_n = new_capacity + 1;
            char* new_ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, align));

            detail::copy_sso(new_ptr, _data.sso, _size);
            new_ptr[_size] = '\0';

            _data.heap.ptr = new_ptr;
            _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
            _flags |= HEAP_ALLOCATED_FLAG;
        } else {
            size_type new_capacity = _calculate_new_capacity(min_capacity);
            std::size_t align = fl::preferred_alloc_alignment();
            std::size_t alloc_n = new_capacity + 1;

            // Pool-to-pool grow: return the old block to the TLS pool BEFORE
            // requesting the new (larger) class.  This reduces peak memory
            // because old and new blocks are never simultaneously live, and it
            // warms the old class's pool slot immediately, benefiting the next
            // same-size allocation on this thread.
            //
            // Safety: since alloc_n > old_alloc_n (grow, not shrink), the pool
            // can never return old_ptr for the new request -- the two pool
            // classes are disjoint.  The TLS pool stores old_ptr verbatim and
            // does not modify the memory it holds, so reading from old_ptr
            // after the deallocate call is benign (single-threaded TLS; no
            // concurrent pool consumer).
            char* const  old_ptr    = _data.heap.ptr;
            std::size_t  old_alloc_n = _data.heap.capacity + 1;

            char* new_ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, align));

            // Copy old data to new buffer BEFORE deallocating old buffer to avoid
            // use-after-free. The order is important for both semantic correctness
            // and AddressSanitizer compliance.
            std::memcpy(new_ptr, old_ptr, _size);
            new_ptr[_size] = '\0';

            // Now deallocate the old buffer after reading from it is complete.
            fl::deallocate_bytes_aligned(old_ptr, old_alloc_n, align);

            _data.heap.ptr = new_ptr;
            _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
        }
    }

    // Rounds min_capacity up to the next power of two minus one (minimum 32).
    static constexpr size_type _calculate_new_capacity(size_type min_capacity) noexcept {
        if (min_capacity < 32) return 32;
        size_type cap = min_capacity;
        cap |= cap >> 1;
        cap |= cap >> 2;
        cap |= cap >> 4;
        cap |= cap >> 8;
        cap |= cap >> 16;
        if constexpr (sizeof(size_type) > 4) {
            cap |= cap >> 32;
        }
        return cap;
    }

    void _assign_impl(const char* cstr, size_type len) noexcept {
        if (_is_heap_allocated()) {
            if (_data.heap.capacity >= len) {
                detail::copy_heap_hot(_data.heap.ptr, cstr, len);
                _data.heap.ptr[len] = '\0';
                _size = len;
                return;
            }
            std::size_t align = fl::preferred_alloc_alignment();
            fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, align);
            _flags = 0;
        }

        if (detail::fits_in_sso(len)) {
            detail::copy_sso(_data.sso, cstr, len);
            _data.sso[len] = '\0';
            _size = len;
            _flags = 0;
        } else {
            _allocate_heap(len);
            detail::copy_heap_hot(_data.heap.ptr, cstr, len);
            _data.heap.ptr[len] = '\0';
            _size = len;
        }
    }
};

template <typename Allocator = std::allocator<string>>
class basic_lazy_concat {
public:
    using value_type = string;
    using allocator_type = Allocator;
    using size_type = std::size_t;
    using part_allocator_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<string>;
    using view_allocator_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<std::string_view>;
    using owned_parts_type = std::deque<string, part_allocator_type>;

    explicit basic_lazy_concat(const allocator_type& alloc = allocator_type())
        : _allocator(alloc), _parts(nullptr), _views(view_allocator_type(alloc)), _total_size(0) {}

    basic_lazy_concat& append(const string& part) {
        _ensure_parts();
        _total_size += part.size();
        _parts->push_back(part);
        _views.push_back(std::string_view(_parts->back().data(), _parts->back().size()));
        return *this;
    }

    basic_lazy_concat& append(string&& part) {
        _ensure_parts();
        _total_size += part.size();
        _parts->push_back(std::move(part));
        _views.push_back(std::string_view(_parts->back().data(), _parts->back().size()));
        return *this;
    }

    basic_lazy_concat& append(std::string_view part) {
        _total_size += part.size();
        _views.push_back(part);
        return *this;
    }

    basic_lazy_concat& append(const char* part) {
        return append(std::string_view(part ? part : ""));
    }

    template <std::size_t N>
    basic_lazy_concat& append(const char (&part)[N]) {
        return append(std::string_view(part, N - 1));
    }

    [[nodiscard]] size_type size() const noexcept {
        return _total_size;
    }

    [[nodiscard]] bool empty() const noexcept {
        return _total_size == 0;
    }

    void reserve(size_type parts) {
        _views.reserve(parts);
    }

    // Materializes all appended parts into a single contiguous fl::string.
    [[nodiscard]] string materialize() const {
        if (_views.empty()) {
            return string();
        }

        if (_views.size() == 1) {
            const auto& only = _views.front();
            return string(only.data(), only.size());
        }

        string out;
        out.reserve(_total_size);
        char* dst = out._data_ptr_mutable();
        for (const auto& part : _views) {
            const size_type n = part.size();
            detail::copy_small(
                reinterpret_cast<unsigned char*>(dst),
                reinterpret_cast<const unsigned char*>(part.data()),
                n
            );
            dst += n;
        }
        out._size = _total_size;
        out._data_ptr_mutable()[_total_size] = '\0';
        return out;
    }

private:
    void _ensure_parts() {
        if (!_parts) {
            _parts = std::make_unique<owned_parts_type>(part_allocator_type(_allocator));
        }
    }

    allocator_type _allocator;
    std::unique_ptr<owned_parts_type> _parts;
    std::vector<std::string_view, view_allocator_type> _views;
    size_type _total_size;
};

using lazy_concat = basic_lazy_concat<>;

inline lazy_concat make_lazy_concat(const string& lhs, const string& rhs) {
    lazy_concat chain;
    chain.append(lhs).append(rhs);
    return chain;
}

inline string operator""_fs(const char* cstr, std::size_t len) {
    return string(cstr, len);
}

inline std::ostream& operator<<(std::ostream& os, const string& s) {
    return os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

}  // namespace fl

namespace fl {

inline substring_view::substring_view(const string& str, size_type offset,
                                     size_type len) noexcept
    : _view(), _owner(nullptr)
{
    if (offset < str.size()) {
        size_type actual_len = std::min(len, str.size() - offset);
        _view = std::string_view(str.data() + offset, actual_len);
    }
}

inline string substring_view::to_fl_string() const {
    return string(data(), size());
}

} // namespace fl

#endif  // FL_STRING_HPP
