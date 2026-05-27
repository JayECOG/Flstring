// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_STRING_HPP
#define FL_STRING_HPP

/// @file Core fl::string class implementation.
///
/// Provides a high-performance string with 23-byte small-string optimisation
/// (SSO), pool-backed heap allocation for larger strings, SIMD-accelerated
/// search (SSE2/AVX2), and optional thread-safety debug guards.  The
/// detail namespace contains the low-level copy helpers, character/substring
/// search algorithms (Boyer-Moore-Horspool, Two-Way), and adaptive tuning
/// state used by the public find() family.

#include <cstring>
#include <array>
#include <atomic>
#include <istream>
#include <string>
#include "fl/config.hpp"
#include "fl/compat/string_view.hpp"
#if FL_HAS_CPP20
#include <compare>
#endif
#include <stdexcept>
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

/// Maximum number of characters that fit in the SSO buffer (23 bytes).
constexpr std::size_t SSO_CAPACITY = 23;

/// Strings longer than SSO_CAPACITY are heap-allocated.
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

    template <typename T, typename = void>
    struct is_input_iterator : std::false_type {};

    template <typename T>
    struct is_input_iterator<T, typename std::enable_if<!std::is_same<
        typename std::iterator_traits<T>::iterator_category,
        void>::value>::type>
        : std::integral_constant<bool,
              std::is_base_of<std::input_iterator_tag,
                              typename std::iterator_traits<T>::iterator_category>::value> {};

    // Overlapping tail-copy for buffers up to 64 bytes.
    //
    // Copies the head and tail of each segment in word-sized chunks so that
    // every byte is stored at least once.  This avoids branches proportional
    // to n and compiles to a small number of inline mov instructions.
    //
    // For n <= 8: zero-initialise a qword, copy n bytes from src, write n bytes.
    // For n <= 16 and n <= 32: copy head 8 bytes + tail 8 bytes.
    // Above 32: delegates to std::memcpy.
    //
    // IMPORTANT: always reads exactly n bytes from src, never more.  String
    // literals are not padded to 8 bytes, and ASan detects over-reads.
    FL_INLINE void copy_small(unsigned char* FL_RESTRICT dst,
        const unsigned char* FL_RESTRICT src,
        std::size_t n) noexcept {
        if (n == 0) return;

        if (n <= 8) {
            std::uint64_t chunk = 0;
            std::memcpy(&chunk, src, n);
            std::memcpy(dst, &chunk, n);
            return;
        }

        if (n <= 16) {
            std::uint64_t head = 0, tail = 0;
            std::memcpy(&head, src, 8);
            std::memcpy(&tail, src + n - 8, 8);
            std::memcpy(dst, &head, 8);
            std::memcpy(dst + n - 8, &tail, 8);
            return;
        }

        if (n <= 32) {
            std::uint64_t q0 = 0, q1 = 0, q2 = 0, q3 = 0;
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
    FL_INLINE void copy_heap_hot(char* FL_RESTRICT dst, const char* FL_RESTRICT src, std::size_t n) noexcept {
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
    [[nodiscard]] FL_INLINE const char* find_char_simd(const char* data,
                                                       std::size_t len,
                                                       char target) noexcept {
        if (FL_UNLIKELY(len == 0)) return nullptr;

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        const __m128i needle = _mm_set1_epi8(static_cast<char>(target));
        std::size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            const __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
            const __m128i cmp = _mm_cmpeq_epi8(block, needle);
            const int mask = _mm_movemask_epi8(cmp);
            if (FL_LIKELY(mask != 0)) {
                return data + i + static_cast<std::size_t>(first_set_bit_index(static_cast<unsigned>(mask)));
            }
        }
        for (; i < len; ++i) {
            if (FL_UNLIKELY(data[i] == target)) return data + i;
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
    [[nodiscard]] FL_INLINE const char* find_substring_simd(const char* FL_RESTRICT haystack,
                                                             std::size_t haystack_len,
                                                             const char* FL_RESTRICT needle,
                                                             std::size_t needle_len) noexcept {
        if (FL_UNLIKELY(needle_len == 0)) return haystack;
        if (FL_UNLIKELY(needle_len > haystack_len)) return nullptr;
        if (needle_len == 1) {
            return find_char_simd(haystack, haystack_len, needle[0]);
        }

        if (FL_LIKELY(needle_len <= 4)) {
            std::size_t offset = 0;
            const std::size_t limit = haystack_len - needle_len;
            while (offset <= limit) {
                const char* candidate = find_char_simd(haystack + offset, limit - offset + 1, needle[0]);
                if (FL_UNLIKELY(!candidate)) return nullptr;
                const std::size_t idx = static_cast<std::size_t>(candidate - haystack);
                if (FL_LIKELY(candidate[1] == needle[1] &&
                    (needle_len == 2 || candidate[2] == needle[2]) &&
                    (needle_len <= 3 || candidate[3] == needle[3]))) {
                    return candidate;
                }
                offset = idx + 1;
            }
            return nullptr;
        }

        if (FL_UNLIKELY(haystack_len >= 2048 && needle_len >= 16)) {
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
                if (FL_UNLIKELY(tail == needle[last] && std::memcmp(haystack + pos, needle, last) == 0)) {
                    return haystack + pos;
                }
                pos += shift[static_cast<unsigned char>(tail)];
            }
            return nullptr;
        }

        if (FL_LIKELY(haystack_len < 256)) {
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
            if (FL_UNLIKELY(!candidate)) return nullptr;
            const std::size_t idx = static_cast<std::size_t>(candidate - haystack);
            if (FL_UNLIKELY(candidate[needle_len - 1] == last && std::memcmp(candidate, needle, needle_len) == 0)) {
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

        [[nodiscard]] FL_INLINE const char* search(const char* FL_RESTRICT haystack, std::size_t n,
                                                    const char* FL_RESTRICT needle,   std::size_t m) noexcept {
            if (FL_UNLIKELY(m == 0)) return haystack;
            if (FL_UNLIKELY(m > n)) return nullptr;

            // Fast path for short needles (m <= 8): skip the O(m) critical-
            // factorization preprocessing entirely.  For haystacks where the match
            // is at an early position, preprocessing dominates; memchr (SIMD-
            // accelerated in glibc/musl) + memcmp is substantially cheaper.
            if (FL_LIKELY(m <= 8)) {
                const char  first = needle[0];
                const char* scan  = haystack;
                const char* limit = haystack + n - m;
                while (scan <= limit) {
                    scan = static_cast<const char*>(
                        std::memchr(scan, first,
                                    static_cast<std::size_t>(limit - scan + 1)));
                    if (FL_UNLIKELY(!scan)) return nullptr;
                    if (FL_UNLIKELY(std::memcmp(scan, needle, m) == 0)) return scan;
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
            if (FL_LIKELY(l1 >= l2)) { l = l1; period = per1; }
            else                     { l = l2; period = per2; }

            // Does the right half repeat with period `period` into the left half?
            // i.e., is needle[0..l] == needle[period..period+l]?
            bool periodic = (std::memcmp(needle, needle + period, l + 1) == 0);

            const char* pos = haystack;
            const char* end = haystack + n - m;
            std::size_t memory = 0; // how many chars of left half we already know match

            if (FL_LIKELY(periodic)) {
                // Periodic case: reuse partial match memory to skip left-half rescans.
                // Local restrict pointer helps GCC 15 generate better addressing code
                // for the inner comparison loops (avoids reloading from memory).
                const char* FL_RESTRICT pos_r = pos;
                const char* FL_RESTRICT const end_r = end;
#if defined(__AVX2__)
                // AVX2 pre-scan: when memory==0 and right half is non-empty, scan
                // 32 bytes/block for needle[l+1] at pos+l+1.  Blocks where the target
                // char is absent are skipped entirely; the scalar two-way comparison
                // only runs at confirmed candidates.  memory is already 0 at entry to
                // this block, so skipping ahead never invalidates stale partial-match
                // knowledge.
                //
                // Restructured to avoid goto labels: GCC 15 generates significantly
                // worse code when goto crosses the AVX2 pre-scan / scalar comparison
                // boundary, likely due to register spillage across the label.
                const bool avx2_ok = (l + 1 < m);
                const __m256i first_r = avx2_ok
                    ? _mm256_set1_epi8(needle[l + 1])
                    : _mm256_setzero_si256();
#endif
                while (pos_r <= end_r) {
#if defined(__AVX2__)
                    if (avx2_ok && memory == 0) {
                        // AVX2 pre-scan: skip 32-byte blocks where needle[l+1] is absent.
                        // When found, pos is updated to the candidate position and we fall
                        // through to the scalar comparison below (no goto needed).
                        const char* scan = pos_r + l + 1;
                        while (scan + 32 <= haystack + n && pos_r <= end_r) {
                            unsigned mask = static_cast<unsigned>(
                                _mm256_movemask_epi8(_mm256_cmpeq_epi8(
                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(scan)),
                                    first_r)));
                            if (FL_UNLIKELY(mask != 0u)) {
                                pos_r = scan - (l + 1) + __builtin_ctz(mask);
                                break; // candidate found; fall through to scalar compare
                            }
                            scan += 32;
                            pos_r += 32;
                        }
                        if (FL_UNLIKELY(pos_r > end_r)) { pos = pos_r; return nullptr; }
                    }
#endif
                    // Compare right half (pos+l+1 .. pos+m-1) using memcmp.
                    // GCC 15 inlines memcmp for small sizes into SIMD block
                    // comparisons (movdqu + pcmpeqb + pmovmskb), which is
                    // substantially faster than byte-by-byte loops.
                    {
                        const std::size_t i = std::max(l + 1, memory);
                        const std::size_t right_len = m - i;
                        if (FL_UNLIKELY(right_len > 0 &&
                            std::memcmp(needle + i, pos_r + i, right_len) != 0)) {
                            // Find mismatch position for skip calculation.
                            std::size_t k = i;
                            while (k < m && needle[k] == pos_r[k]) ++k;
                            pos_r += static_cast<std::ptrdiff_t>(k - l);
                            memory = 0;
                            continue;
                        }
                    }
                    // Right half matched; compare left half starting from `memory`.
                    {
                        const std::size_t j = memory;
                        const std::size_t left_len = l + 1 - j;
                        if (FL_LIKELY(left_len == 0 ||
                            std::memcmp(needle + j, pos_r + j, left_len) == 0)) {
                            pos = pos_r; return pos_r; // full match
                        }
                        // Mismatch in left half: advance by period, retain memory.
                        pos_r += static_cast<std::ptrdiff_t>(period);
                        memory = m > period ? m - period : 0;
                    }
                }
                pos = pos_r;
            } else {
                // Non-periodic case: no memory optimisation, but the right half
                // shift is at least (l+1) meaning we skip at least half the needle
                // per mismatch -- comparable to BMH but with O(1) preprocessing.
                const std::size_t right_skip = l + 1;
                const char* FL_RESTRICT pos_r = pos;
                const char* FL_RESTRICT const end_r = end;
#if defined(__AVX2__)
                const bool avx2_ok = (l + 1 < m);
                const __m256i first_r = avx2_ok
                    ? _mm256_set1_epi8(needle[l + 1])
                    : _mm256_setzero_si256();
#endif
                while (pos_r <= end_r) {
#if defined(__AVX2__)
                    if (avx2_ok) {
                        const char* scan = pos_r + l + 1;
                        while (scan + 32 <= haystack + n && pos_r <= end_r) {
                            unsigned mask = static_cast<unsigned>(
                                _mm256_movemask_epi8(_mm256_cmpeq_epi8(
                                    _mm256_loadu_si256(reinterpret_cast<const __m256i*>(scan)),
                                    first_r)));
                            if (FL_UNLIKELY(mask != 0u)) {
                                pos_r = scan - (l + 1) + __builtin_ctz(mask);
                                break;
                            }
                            scan += 32;
                            pos_r += 32;
                        }
                        if (FL_UNLIKELY(pos_r > end_r)) { pos = pos_r; return nullptr; }
                    }
#endif
                    // Compare right half using memcmp (GCC 15 inlines to SIMD).
                    {
                        const std::size_t i = l + 1;
                        const std::size_t right_len = m - i;
                        if (FL_UNLIKELY(right_len > 0 &&
                            std::memcmp(needle + i, pos_r + i, right_len) != 0)) {
                            std::size_t k = i;
                            while (k < m && needle[k] == pos_r[k]) ++k;
                            pos_r += static_cast<std::ptrdiff_t>(k - l);
                            continue;
                        }
                    }
                    // Compare left half using memcmp.
                    {
                        const std::size_t left_len = l + 1;
                        if (FL_LIKELY(left_len == 0 ||
                            std::memcmp(needle, pos_r, left_len) == 0)) {
                            pos = pos_r; return pos_r;
                        }
                        pos_r += right_skip;
                    }
                }
                pos = pos_r;
            }
            return nullptr;
        }

    } // namespace two_way

    // Threshold above which we prefer the two-way algorithm over memmem.
    // Measured on AMD EPYC 7763: memmem wins up to ~64 KB; two-way wins above.
    static constexpr std::size_t kTwoWayHaystackThreshold = 65536;

}  // namespace detail

/** High-performance string class with small-string optimisation.
 *
 * Strings of up to 23 bytes are stored inline (SSO buffer); longer strings
 * use pool-backed heap allocation.  Substring search dispatches to
 * SIMD-accelerated paths (SSE2/AVX2) for single characters and short
 * needles, Boyer-Moore-Horspool for medium haystacks, and the Two-Way
 * algorithm for haystacks above 64 KB.
 *
 * In debug builds (FL_DEBUG_THREAD_SAFETY), every public accessor acquires
 * a read or write guard that detects unsynchronised concurrent access.
 */
class string {
public:
    using value_type = char;
    using allocator_type = std::allocator<char>;
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

    static constexpr size_type npos = static_cast<size_type>(-1);

    string() noexcept : _size(0), _flags(0) {
        _data.sso[0] = '\0';
    }

    // C-string constructor: single std::strlen call eliminates the duplicate
    // scan (24-byte inline probe + full strlen for heap strings) present in
    // the previous version.  For SSO-sized strings the result is used directly;
    // for longer strings it feeds the heap path.
    string(const char* cstr) : _size(0), _flags(0) {
        if (FL_UNLIKELY(!cstr)) {
            _data.sso[0] = '\0';
            return;
        }
        const size_t len = std::strlen(cstr);
        if (FL_LIKELY(len <= SSO_CAPACITY)) {
            std::memcpy(_data.sso, cstr, len);
            _data.sso[len] = '\0';
            _size = static_cast<size_type>(len);
        } else {
            _allocate_heap_exact(static_cast<size_type>(len));
            detail::copy_heap_hot(_data.heap.ptr, cstr, len);
            _data.heap.ptr[len] = '\0';
            _size = static_cast<size_type>(len);
        }
    }

    // Compile-time length deduction avoids runtime strlen for string literals.
    template <std::size_t N>
    string(const char (&cstr)[N]) : _size(0), _flags(0) {
        constexpr size_type len = (N > 0) ? (N - 1) : 0;
        if (len == 0) {
            _data.sso[0] = '\0';
        } else if (detail::fits_in_sso(len)) {
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

    // std::string interop: extracts the pointer and length directly.
    // s.c_str() is guaranteed non-null (C++11+), so we skip the null check
    // that the (cstr, len) constructor performs.
    string(const std::string& s) : _size(0), _flags(0) {
        const size_type len = static_cast<size_type>(s.size());
        if (detail::fits_in_sso(len)) {
            detail::copy_sso(_data.sso, s.c_str(), len);
            _data.sso[len] = '\0';
            _size = len;
        } else {
            _allocate_heap_exact(len);
            detail::copy_heap_hot(_data.heap.ptr, s.c_str(), len);
            _data.heap.ptr[len] = '\0';
            _size = len;
        }
    }

    // std::string_view interop: s.data() may be null when s.size() == 0,
    // which the SSO path handles naturally (zero-length copy is a no-op).
    string(std::string_view s) : _size(0), _flags(0) {
        const size_type len = static_cast<size_type>(s.size());
        if (detail::fits_in_sso(len)) {
            if (FL_LIKELY(len > 0)) {
                detail::copy_sso(_data.sso, s.data(), len);
            }
            _data.sso[len] = '\0';
            _size = len;
        } else {
            _allocate_heap_exact(len);
            detail::copy_heap_hot(_data.heap.ptr, s.data(), len);
            _data.heap.ptr[len] = '\0';
            _size = len;
        }
    }

    string(const char* cstr, size_type len) : _size(0), _flags(0) {
        if (len > 0 && FL_LIKELY(cstr != nullptr)) {
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

    template <typename InputIter,
              typename std::enable_if<detail::is_input_iterator<InputIter>::value, int>::type = 0>
    string(InputIter first, InputIter last) : _size(0), _flags(0) {
        _data.sso[0] = '\0';
        append(first, last);
    }

    string(std::initializer_list<char> ilist) : string(ilist.begin(), ilist.size()) {}

    string(string&& other) noexcept
        : _data(other._data), _size(other._size), _flags(other._flags) {
        other._size = 0;
        other._flags = 0;
        other._data.sso[0] = '\0';
    }

    ~string() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (_is_heap_allocated()) {
            fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, kAllocAlignment);
        }
    }

    string& operator=(const string& other) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (this != &other) {
            _assign_impl(other._data_ptr(), other._size);
        }
        return *this;
    }

    string& operator=(string&& other) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (this != &other) {
            // Free existing heap allocation if any.
            if (_is_heap_allocated()) {
                fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, kAllocAlignment);
            }
            // Steal other's state via memcpy of the data members.
            // Using memcpy prevents the compiler from recognizing this pattern
            // and replacing it with a call to _assign_impl (which is slower
            // for SSO strings since it does a full SSO copy via copy_sso).
            std::memcpy(&_size, &other._size, sizeof(_size));
            std::memcpy(&_flags, &other._flags, sizeof(_flags));
            std::memcpy(&_data, &other._data, sizeof(_data));
            // Reset other to empty SSO state.
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

    string& operator=(const char* cstr) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (cstr) {
            _assign_impl(cstr, std::strlen(cstr));
        } else {
            clear();
        }
        return *this;
    }

    string& operator=(std::string_view s) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(s.data(), s.size());
        return *this;
    }

    string& operator=(std::initializer_list<char> ilist) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(ilist.begin(), ilist.size());
        return *this;
    }

    [[nodiscard]] FL_INLINE const_reference operator[](size_type pos) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr()[pos];
    }

    [[nodiscard]] FL_INLINE reference operator[](size_type pos) noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable()[pos];
    }

    // Throws std::out_of_range if pos >= size().
    [[nodiscard]] FL_INLINE const_reference at(size_type pos) const {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (FL_UNLIKELY(pos >= _size)) {
            throw std::out_of_range("fl::string::at");
        }
        return _data_ptr()[pos];
    }

    // Throws std::out_of_range if pos >= size().
    [[nodiscard]] FL_INLINE reference at(size_type pos) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (FL_UNLIKELY(pos >= _size)) {
            throw std::out_of_range("fl::string::at");
        }
        return _data_ptr_mutable()[pos];
    }

    [[nodiscard]] FL_INLINE const_reference front() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr()[0];
    }

    [[nodiscard]] FL_INLINE reference front() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable()[0];
    }

    [[nodiscard]] FL_INLINE const_reference back() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr()[_size - 1];
    }

    [[nodiscard]] FL_INLINE reference back() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable()[_size - 1];
    }

    [[nodiscard]] FL_INLINE const_pointer data() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr();
    }

    [[nodiscard]] FL_INLINE pointer data() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable();
    }

    [[nodiscard]] FL_INLINE const char* c_str() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr();
    }

    [[nodiscard]] FL_INLINE iterator begin() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable();
    }

    [[nodiscard]] FL_INLINE const_iterator begin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr();
    }

    [[nodiscard]] FL_INLINE const_iterator cbegin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr();
    }

    [[nodiscard]] FL_INLINE iterator end() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return _data_ptr_mutable() + _size;
    }

    [[nodiscard]] FL_INLINE const_iterator end() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr() + _size;
    }

    [[nodiscard]] FL_INLINE const_iterator cend() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _data_ptr() + _size;
    }

    [[nodiscard]] FL_INLINE reverse_iterator rbegin() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return reverse_iterator(end());
    }

    [[nodiscard]] FL_INLINE const_reverse_iterator rbegin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(end());
    }

    [[nodiscard]] FL_INLINE reverse_iterator rend() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return reverse_iterator(begin());
    }

    [[nodiscard]] FL_INLINE const_reverse_iterator rend() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(begin());
    }

    [[nodiscard]] FL_INLINE const_reverse_iterator crbegin() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(_data_ptr() + _size);
    }

    [[nodiscard]] FL_INLINE const_reverse_iterator crend() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return const_reverse_iterator(_data_ptr());
    }

    [[nodiscard]] FL_INLINE size_type size() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size;
    }

    [[nodiscard]] FL_INLINE size_type length() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size;
    }

    [[nodiscard]] FL_INLINE size_type capacity() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (_is_heap_allocated()) {
            return _data.heap.capacity;
        }
        return SSO_CAPACITY;
    }

    [[nodiscard]] FL_INLINE size_type max_size() const noexcept {
        return static_cast<size_type>(-1) / 2;
    }

    [[nodiscard]] FL_INLINE allocator_type get_allocator() const noexcept {
        return allocator_type{};
    }

    [[nodiscard]] FL_INLINE bool empty() const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return _size == 0;
    }

    void reserve(size_type cap) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (cap > capacity()) {
            _grow_to(cap);
        }
    }

    // Kept for C++11/14/17 compatibility.  Deprecated in C++20
    // (use shrink_to_fit() instead, which is semantically identical).
    void reserve() noexcept { shrink_to_fit(); }


    void shrink_to_fit() {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (_is_heap_allocated() && _size < _data.heap.capacity) {
            if (detail::fits_in_sso(_size)) {
                std::array<char, SSO_CAPACITY + 1> temp{};
                detail::copy_sso(temp.data(), _data.heap.ptr, _size);
                fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, kAllocAlignment);
                detail::copy_sso(_data.sso, temp.data(), _size);
                _data.sso[_size] = '\0';
                _flags = 0;
            } else {
                char* new_ptr = static_cast<char*>(fl::allocate_bytes_aligned(_size + 1, kAllocAlignment));
                std::memcpy(new_ptr, _data.heap.ptr, _size);
                new_ptr[_size] = '\0';
                fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, kAllocAlignment);
                _data.heap.ptr = new_ptr;
                _data.heap.capacity = _size;
            }
        }
    }

    void clear() noexcept {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _size = 0;
        _data_ptr_mutable()[0] = '\0';
    }

    string& append(std::string_view sv) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return append(sv.data(), sv.size());
    }

    string& append(const char* cstr) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return append(cstr, cstr ? std::strlen(cstr) : 0);
    }

    string& append(const char* cstr, size_type len) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (len == 0 || FL_UNLIKELY(!cstr)) return *this;

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

    string& append(const string& other) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return append(other.data(), other.size());
    }

    string& append(char ch) {
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

    string& append(size_type count, char ch) {
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

    template <typename InputIter,
              typename std::enable_if<detail::is_input_iterator<InputIter>::value, int>::type = 0>
    string& append(InputIter first, InputIter last) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        using cat = typename std::iterator_traits<InputIter>::iterator_category;
        if constexpr (std::is_base_of_v<std::random_access_iterator_tag, cat>) {
            auto count = static_cast<size_type>(std::distance(first, last));
            if (count == 0) return *this;
            reserve(_size + count);
            char* ptr = _data_ptr_mutable();
            for (size_type i = 0; i < count; ++i) {
                ptr[_size + i] = static_cast<char>(*first++);
            }
            _size += count;
            ptr[_size] = '\0';
        } else {
            while (first != last) {
                append(*first);
                ++first;
            }
        }
        return *this;
    }

    string& operator+=(const char* cstr) { return append(cstr); }
    string& operator+=(const string& str) { return append(str); }
    string& operator+=(char ch) { return append(ch); }
    string& operator+=(std::string_view s) { return append(s.data(), s.size()); }
    string& operator+=(std::initializer_list<char> ilist) { return append(ilist.begin(), ilist.size()); }

    // Throws std::out_of_range if pos > str.size().
    string& append(const string& str, size_type pos, size_type count = npos) {
        if (pos > str._size) throw std::out_of_range("fl::string::append");
        const size_type len = std::min(count, str._size - pos);
        return append(str._data_ptr() + pos, len);
    }

    string& append(std::initializer_list<char> ilist) {
        return append(ilist.begin(), ilist.size());
    }

    string& assign(const char* cstr, size_type len) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(cstr, len);
        return *this;
    }

    string& assign(const char* cstr) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(cstr, cstr ? std::strlen(cstr) : 0);
        return *this;
    }

    string& assign(const string& other) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (this != &other) {
            _assign_impl(other.data(), other.size());
        }
        return *this;
    }

    string& assign(std::string_view sv) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(sv.data(), sv.size());
        return *this;
    }

    string& assign(size_type count, char ch) {
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

    string& assign(std::initializer_list<char> ilist) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        _assign_impl(ilist.begin(), ilist.size());
        return *this;
    }

    template <typename InputIter,
              typename std::enable_if<detail::is_input_iterator<InputIter>::value, int>::type = 0>
    string& assign(InputIter first, InputIter last) {
        clear();
        append(first, last);
        return *this;
    }

    void push_back(char ch) {
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

    string& erase(size_type pos = 0, size_type len = npos) {
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

    iterator erase(const_iterator pos) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = pos - begin();
        erase(idx, 1);
        return begin() + idx;
    }

    iterator erase(const_iterator first, const_iterator last) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = first - begin();
        size_type len = last - first;
        erase(idx, len);
        return begin() + idx;
    }

    string& insert(size_type pos, const string& str) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return insert(pos, str.data(), str.size());
    }

    string& insert(size_type pos, const char* cstr, size_type len) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (len == 0 || pos > _size || FL_UNLIKELY(!cstr)) return *this;

        size_type new_size = _size + len;
        if (new_size > capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        char* ptr = _data_ptr_mutable();
        std::memmove(ptr + pos + len, ptr + pos, _size - pos);
        std::memcpy(ptr + pos, cstr, len);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    string& insert(size_type pos, const char* cstr) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return insert(pos, cstr, cstr ? std::strlen(cstr) : 0);
    }

    string& insert(size_type pos, size_type count, char ch) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (count == 0 || pos > _size) return *this;

        size_type new_size = _size + count;
        if (new_size > capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        char* ptr = _data_ptr_mutable();
        std::memmove(ptr + pos + count, ptr + pos, _size - pos);
        std::memset(ptr + pos, ch, count);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    iterator insert(const_iterator pos, char ch) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = pos - begin();
        insert(idx, 1, ch);
        return begin() + idx;
    }

    iterator insert(const_iterator pos, size_type count, char ch) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        size_type idx = pos - begin();
        insert(idx, count, ch);
        return begin() + idx;
    }

    string& insert(size_type pos, std::string_view sv) {
        return insert(pos, sv.data(), sv.size());
    }

    // Throws std::out_of_range if ipos > str.size().
    string& insert(size_type pos, const string& str, size_type ipos, size_type icount = npos) {
        if (ipos > str._size) throw std::out_of_range("fl::string::insert");
        const size_type len = std::min(icount, str._size - ipos);
        return insert(pos, str._data_ptr() + ipos, len);
    }

    template <typename InputIter,
              typename std::enable_if<detail::is_input_iterator<InputIter>::value, int>::type = 0>
    iterator insert(const_iterator pos, InputIter first, InputIter last) {
        size_type idx = static_cast<size_type>(pos - begin());
        using cat = typename std::iterator_traits<InputIter>::iterator_category;
        if constexpr (std::is_base_of_v<std::random_access_iterator_tag, cat>) {
            [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
            auto count = static_cast<size_type>(std::distance(first, last));
            if (count == 0) return begin() + idx;
            size_type new_size = _size + count;
            if (new_size > capacity()) {
                _grow_to(new_size + (new_size / 2));
            }
            char* ptr = _data_ptr_mutable();
            std::memmove(ptr + idx + count, ptr + idx, _size - idx);
            for (size_type i = 0; i < count; ++i) {
                ptr[idx + i] = static_cast<char>(*first++);
            }
            _size = new_size;
            ptr[_size] = '\0';
        } else {
            string tmp(first, last);
            insert(idx, tmp._data_ptr(), tmp._size);
        }
        return begin() + idx;
    }

    iterator insert(const_iterator pos, std::initializer_list<char> ilist) {
        size_type idx = static_cast<size_type>(pos - begin());
        insert(idx, ilist.begin(), ilist.size());
        return begin() + idx;
    }

    string& replace(size_type pos, size_type len, const string& str) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return replace(pos, len, str._data_ptr(), str._size);
    }

    string& replace(size_type pos, size_type len, const char* cstr, size_type clen) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (pos > _size) return *this;

        len = std::min(len, _size - pos);
        size_type new_size = _size - len + clen;

        if (new_size > capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        char* ptr = _data_ptr_mutable();
        if (len != clen) {
            std::memmove(ptr + pos + clen, ptr + pos + len, _size - pos - len);
        }
        std::memcpy(ptr + pos, cstr, clen);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    string& replace(size_type pos, size_type len, const char* cstr) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        return replace(pos, len, cstr, cstr ? std::strlen(cstr) : 0);
    }

    string& replace(size_type pos, size_type len, size_type count, char ch) {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (pos > _size) return *this;

        len = std::min(len, _size - pos);
        size_type new_size = _size - len + count;

        if (new_size > capacity()) {
            _grow_to(new_size + (new_size / 2));
        }

        char* ptr = _data_ptr_mutable();
        if (len != count) {
            std::memmove(ptr + pos + count, ptr + pos + len, _size - pos - len);
        }
        std::memset(ptr + pos, ch, count);
        _size = new_size;
        ptr[_size] = '\0';
        return *this;
    }

    string& replace(size_type pos, size_type len, std::string_view sv) {
        return replace(pos, len, sv.data(), sv.size());
    }

    // Throws std::out_of_range if ipos > str.size().
    string& replace(size_type pos, size_type len, const string& str, size_type ipos, size_type icount = npos) {
        if (ipos > str._size) throw std::out_of_range("fl::string::replace");
        const size_type ilen = std::min(icount, str._size - ipos);
        return replace(pos, len, str._data_ptr() + ipos, ilen);
    }

    string& replace(const_iterator first, const_iterator last, const string& str) {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       str._data_ptr(), str._size);
    }

    string& replace(const_iterator first, const_iterator last, const char* cstr, size_type clen) {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       cstr, clen);
    }

    string& replace(const_iterator first, const_iterator last, const char* cstr) {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       cstr ? cstr : "", cstr ? std::strlen(cstr) : size_type(0));
    }

    string& replace(const_iterator first, const_iterator last, size_type count, char ch) {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       count, ch);
    }

    string& replace(const_iterator first, const_iterator last, std::string_view sv) {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       sv.data(), sv.size());
    }

    string& replace(const_iterator first, const_iterator last, std::initializer_list<char> ilist) {
        return replace(static_cast<size_type>(first - begin()),
                       static_cast<size_type>(last - first),
                       ilist.begin(), ilist.size());
    }

    template <typename InputIter,
              typename std::enable_if<detail::is_input_iterator<InputIter>::value, int>::type = 0>
    string& replace(const_iterator first, const_iterator last, InputIter rfirst, InputIter rlast) {
        const size_type pos = static_cast<size_type>(first - begin());
        const size_type len = static_cast<size_type>(last - first);
        using cat = typename std::iterator_traits<InputIter>::iterator_category;
        if constexpr (std::is_base_of_v<std::random_access_iterator_tag, cat>) {
            [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
            auto count = static_cast<size_type>(std::distance(rfirst, rlast));
            size_type new_size = _size - len + count;
            if (new_size > capacity()) {
                _grow_to(new_size + (new_size / 2));
            }
            char* ptr = _data_ptr_mutable();
            std::memmove(ptr + pos + count, ptr + pos + len, _size - pos - len);
            for (size_type i = 0; i < count; ++i) {
                ptr[pos + i] = static_cast<char>(*rfirst++);
            }
            _size = new_size;
            ptr[_size] = '\0';
        } else {
            string tmp(rfirst, rlast);
            replace(pos, len, tmp._data_ptr(), tmp._size);
        }
        return *this;
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

    void resize(size_type new_size, char fill = '\0') {
        [[maybe_unused]] auto _guard = _guard_write(FL_LOC);
        if (new_size > _size) {
            if (new_size > capacity()) {
                _grow_to(new_size + (new_size / 2));
            }
            std::fill(_data_ptr_mutable() + _size, _data_ptr_mutable() + new_size, fill);
        }
        _size = new_size;
        _data_ptr_mutable()[_size] = '\0';
    }

#if FL_HAS_CPP20
    [[nodiscard]] std::strong_ordering operator<=>(const string& other) const noexcept {
        return std::string_view(*this) <=> std::string_view(other);
    }
#endif

    // Optimised equality: for SSO strings, compare inline buffers directly
    // avoiding the branch in _data_ptr().  For heap strings, delegates to
    // memcmp on the data pointers.
    [[nodiscard]] bool operator==(const string& other) const noexcept {
        if (_size != other._size) return false;
        if (_size == 0) return true;
        // Both SSO: compare inline buffers directly (branchless pointer resolution)
        if (FL_LIKELY(!_is_heap_allocated() && !other._is_heap_allocated())) {
            return std::memcmp(_data.sso, other._data.sso, _size) == 0;
        }
        // At least one is heap-allocated: use data pointers
        return std::memcmp(_data_ptr(), other._data_ptr(), _size) == 0;
    }

    [[nodiscard]] bool operator!=(const string& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] bool operator<(const string& other) const noexcept {
        return compare(other) < 0;
    }

    [[nodiscard]] bool operator<=(const string& other) const noexcept {
        return compare(other) <= 0;
    }

    [[nodiscard]] bool operator>(const string& other) const noexcept {
        return compare(other) > 0;
    }

    [[nodiscard]] bool operator>=(const string& other) const noexcept {
        return compare(other) >= 0;
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

    friend string operator+(const string& lhs, char rhs) {
        return _concat_raw(lhs._data_ptr(), lhs._size, &rhs, 1);
    }

    friend string operator+(string&& lhs, char rhs) {
        return _concat_raw_move_lhs(std::move(lhs), &rhs, 1);
    }

    friend string operator+(char lhs, const string& rhs) {
        return _concat_raw(&lhs, 1, rhs._data_ptr(), rhs._size);
    }

    friend string operator+(char lhs, string&& rhs) {
        return _concat_raw_move_rhs(&lhs, 1, std::move(rhs));
    }

    [[nodiscard]] FL_INLINE size_type find(char ch, size_type pos = 0) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (FL_UNLIKELY(pos >= _size)) return npos;
        const char* p = _data_ptr();
        const char* res = static_cast<const char*>(std::memchr(p + pos, ch, _size - pos));
        return FL_LIKELY(res != nullptr) ? static_cast<size_type>(res - p) : npos;
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
    //   - Haystacks >= 64 KB with needles >= 2: uses the Two-Way algorithm
    //     for the full scan.  For high-entropy needles, a fast-path early
    //     check via string_view::find (limited to 64 KB) is attempted first
    //     to catch early-position matches without Two-Way's O(m) preprocessing.
    //
    //   - Small haystacks (< 256 B): uses a direct memcmp loop to avoid
    //     string_view wrapper overhead.
    //
    //   - Everything else: std::string_view::find (glibc memmem), which uses
    //     AVX2 internally and outperforms hand-rolled SIMD/BMH at all measured
    //     haystack sizes up to 64 KB.
    [[nodiscard]] FL_INLINE size_type find(std::string_view sv, size_type pos = 0) const noexcept {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        if (FL_UNLIKELY(pos > _size)) return npos;
        if (FL_UNLIKELY(sv.empty())) return pos;

        const char* data = _data_ptr();
        const size_type remaining = _size - pos;
        const size_type nlen = sv.size();

        // Fast path: single character uses optimised memchr.
        if (FL_LIKELY(nlen == 1)) {
            const char* found = static_cast<const char*>(std::memchr(data + pos, sv[0], remaining));
            return FL_LIKELY(found != nullptr) ? static_cast<size_type>(found - data) : npos;
        }

        // For very large haystacks (>= 64 KB), dispatch based on needle entropy.
        //
        // High-entropy needles (first != last char): memmem's AVX2 window scan
        // is 4-60x faster than Two-Way for random text.  Use string_view::find
        // (which calls memmem) for the full scan.
        //
        // Low-entropy / periodic needles (first == last char): memmem degrades to
        // O(n*m) worst-case.  Two-Way's period-based memory avoids rescanning and
        // maintains O(n+m) time regardless of text entropy.
        if (FL_UNLIKELY(remaining >= detail::kTwoWayHaystackThreshold && nlen >= 2)) {
            // Quick entropy heuristic: if first and last char differ, the needle
            // is likely high-entropy and memmem will be fast.  For periodic needles
            // (e.g. all 'a' with one 'b'), first == last and Two-Way excels.
            const bool high_entropy = (sv.front() != sv.back());
            if (high_entropy && nlen <= 32) {
                // High-entropy needle: use memmem for the full scan.
                const std::string_view haystack_full(data + pos, remaining);
                const size_type found = haystack_full.find(sv);
                return found == npos ? npos : (pos + found);
            }
            // Low-entropy needle or long needle: use Two-Way algorithm.
            const char* found = detail::two_way::search(
                data + pos, remaining, sv.data(), nlen);
            return FL_LIKELY(found != nullptr)
                ? static_cast<size_type>(found - data)
                : npos;
        }

        // Medium haystacks (< 64 KB): delegate to string_view::find
        // (platform memmem).  memmem is highly optimized and generally faster
        // than hand-rolled loops for this size range.
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

    [[nodiscard]] bool contains(const char* cstr) const noexcept {
        return contains(std::string_view(cstr ? cstr : ""));
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
        return std::string_view(_data_ptr(), _size);
    }

    // Implicit conversion to std::string for compatibility with
    // standard-library APIs.  Copies the underlying storage.
    operator std::string() const {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string(_data_ptr(), _size);
    }

    // Explicit named conversion to std::string.
    [[nodiscard]] std::string to_std_string() const {
        [[maybe_unused]] auto _guard = _guard_read(FL_LOC);
        return std::string(_data_ptr(), _size);
    }

private:
    friend class rope;
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

    // Compile-time alignment constant used for all pool allocations, replacing
    // the runtime-preferred_alloc_alignment() function call.  Matches
    // fl::alloc_hooks::DEFAULT_ALIGNMENT (= alignof(std::max_align_t)), which
    // is the alignment used by the TLS pool allocator.
    static constexpr std::size_t kAllocAlignment =
#if defined(__STDCPP_DEFAULT_NEW_ALIGNMENT__)
        __STDCPP_DEFAULT_NEW_ALIGNMENT__;
#else
        alignof(std::max_align_t);
#endif

    bool _is_heap_allocated() const noexcept {
        return (_flags & HEAP_ALLOCATED_FLAG) != 0;
    }

    const char* _data_ptr() const noexcept {
        // FL_LIKELY signals that SSO is the common case, helping the compiler
        // generate better code for the hot path (e.g. operator[] in tight loops).
        return FL_LIKELY(!_is_heap_allocated()) ? _data.sso : _data.heap.ptr;
    }

    char* _data_ptr_mutable() noexcept {
        return FL_LIKELY(!_is_heap_allocated()) ? _data.sso : _data.heap.ptr;
    }

    void _allocate_heap(size_type min_capacity) {
        size_type new_capacity = _calculate_new_capacity(min_capacity);
        std::size_t alloc_n = new_capacity + 1;
        _data.heap.ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, kAllocAlignment));
        _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
        _flags |= HEAP_ALLOCATED_FLAG;
    }

    // Allocates heap storage with exactly the requested capacity.  The actual
    // usable capacity may be larger because the pool allocator rounds up to
    // the next pool class size (e.g. requesting 101 bytes lands in the
    // 128-byte pool class, giving capacity 127 instead of 100).
    void _allocate_heap_exact(size_type exact_capacity) {
        std::size_t alloc_n = exact_capacity + 1;
        _data.heap.ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, kAllocAlignment));
        _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
        _flags |= HEAP_ALLOCATED_FLAG;
    }

    void _grow_to(size_type min_capacity) {
        if (min_capacity <= capacity()) return;

        if (!_is_heap_allocated()) {
            size_type new_capacity = _calculate_new_capacity(min_capacity);
            std::size_t alloc_n = new_capacity + 1;
            char* new_ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, kAllocAlignment));

            detail::copy_sso(new_ptr, _data.sso, _size);
            new_ptr[_size] = '\0';

            _data.heap.ptr = new_ptr;
            _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
            _flags |= HEAP_ALLOCATED_FLAG;
        } else {
            size_type new_capacity = _calculate_new_capacity(min_capacity);
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

            char* new_ptr = static_cast<char*>(fl::allocate_bytes_aligned(alloc_n, kAllocAlignment));

            // Copy old data to new buffer BEFORE deallocating old buffer to avoid
            // use-after-free. The order is important for both semantic correctness
            // and AddressSanitizer compliance.
            std::memcpy(new_ptr, old_ptr, _size);
            new_ptr[_size] = '\0';

            // Now deallocate the old buffer after reading from it is complete.
            fl::deallocate_bytes_aligned(old_ptr, old_alloc_n, kAllocAlignment);

            _data.heap.ptr = new_ptr;
            _data.heap.capacity = fl::alloc_hooks::pool_alloc_usable_capacity(alloc_n);
        }
    }

    // Rounds min_capacity up to the next power of two minus one (minimum 32).
    FL_INLINE static constexpr size_type _calculate_new_capacity(size_type min_capacity) noexcept {
        if (FL_UNLIKELY(min_capacity < 32)) return 32;
        size_type cap = min_capacity;
        cap |= cap >> 1;
        cap |= cap >> 2;
        cap |= cap >> 4;
        cap |= cap >> 8;
        cap |= cap >> 16;
#if SIZE_MAX > 0xFFFFFFFFu
        cap |= cap >> 32;
#endif
        return cap;
    }

    void _assign_impl(const char* cstr, size_type len) {
        if (_is_heap_allocated()) {
            if (_data.heap.capacity >= len) {
                detail::copy_heap_hot(_data.heap.ptr, cstr, len);
                _data.heap.ptr[len] = '\0';
                _size = len;
                return;
            }
            fl::deallocate_bytes_aligned(_data.heap.ptr, _data.heap.capacity + 1, kAllocAlignment);
            _data.heap.ptr = nullptr;
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

    // Parts are stored as owning fl::string pointers so that the underlying
    // data addresses are stable — std::vector pointer stability guarantees
    // that shared_ptr->data() remains valid even as the vector grows.
    explicit basic_lazy_concat(const allocator_type& alloc = allocator_type())
        : _parts(alloc),
          _total_size(0) {}

    basic_lazy_concat& append(const string& part) {
        _total_size += part.size();
        _parts.push_back(std::make_shared<fl::string>(part));
        return *this;
    }

    basic_lazy_concat& append(string&& part) {
        _total_size += part.size();
        _parts.push_back(std::make_shared<fl::string>(std::move(part)));
        return *this;
    }

    basic_lazy_concat& append(std::string_view part) {
        _total_size += part.size();
        _parts.push_back(std::make_shared<fl::string>(part));
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
        _parts.reserve(parts);
    }

    // Materializes all appended parts into a single contiguous fl::string.
    //
    // Optimised path: calculates the total size, then constructs the output
    // string with a single allocation (or SSO inline storage) and copies
    // each part into place with std::memcpy — no per-part branch overhead.
    [[nodiscard]] string materialize() const {
        if (_parts.empty()) {
            return string();
        }

        if (_parts.size() == 1) {
            return *_parts.front();
        }

        // SSO fast path: if the total fits inline, construct directly.
        if (detail::fits_in_sso(_total_size)) {
            string out;
            char* dst = out._data.sso;
            for (const auto& p : _parts) {
                const size_type n = p->size();
                if (FL_LIKELY(n > 0)) {
                    std::memcpy(dst, p->data(), n);
                    dst += n;
                }
            }
            out._size = _total_size;
            out._data.sso[_total_size] = '\0';
            return out;
        }

        // Heap path: single allocation, then batch-copy each part.
        string out;
        out._allocate_heap_exact(_total_size);
        char* dst = out._data.heap.ptr;
        for (const auto& p : _parts) {
            const size_type n = p->size();
            if (FL_LIKELY(n > 0)) {
                std::memcpy(dst, p->data(), n);
                dst += n;
            }
        }
        out._size = _total_size;
        out._data.heap.ptr[_total_size] = '\0';
        return out;
    }

private:
    allocator_type _allocator;
    // Using shared_ptr<fl::string> provides stable data pointers — the
    // shared_ptr itself lives in a vector that may reallocate, but the
    // pointed-to fl::string and its data() address never move.
    std::vector<std::shared_ptr<fl::string>, typename std::allocator_traits<allocator_type>::template rebind_alloc<std::shared_ptr<fl::string>>> _parts;
    size_type _total_size;
};

using lazy_concat = basic_lazy_concat<>;

inline lazy_concat make_lazy_concat(const string& lhs, const string& rhs) {
    lazy_concat chain;
    chain.append(lhs);
    chain.append(rhs);
    return chain;
}

inline string operator""_fs(const char* cstr, std::size_t len) {
    return string(cstr, len);
}

inline std::ostream& operator<<(std::ostream& os, const string& s) {
    return os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

inline std::istream& operator>>(std::istream& is, string& s) {
    std::string tmp;
    is >> tmp;
    s.assign(tmp.data(), tmp.size());
    return is;
}

inline void swap(string& lhs, string& rhs) noexcept {
    lhs.swap(rhs);
}

inline std::istream& getline(std::istream& is, string& str, char delim) {
    std::string tmp;
    std::getline(is, tmp, delim);
    str.assign(tmp.data(), tmp.size());
    return is;
}

inline std::istream& getline(std::istream& is, string& str) {
    return fl::getline(is, str, '\n');
}

}  // namespace fl

namespace std {

template <>
struct hash<fl::string> {
    std::size_t operator()(const fl::string& value) const noexcept {
        return std::hash<std::string_view>{}(static_cast<std::string_view>(value));
    }
};

}  // namespace std

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

