// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_ALLOC_HOOKS_HPP
#define FL_ALLOC_HOOKS_HPP

// Allocator hooks, TLS free-list pool, and pool_alloc<T>.
//
// This header provides the allocation layer for fl::string.  It defines
// pluggable allocate/deallocate function pointers, a thread-local free-list
// pool for small blocks (up to 4096 bytes across 7 size classes), and a
// standard-conforming pool_alloc<T> allocator adapter.

#include <cstddef>
#include <cstdlib>
#if defined(_MSC_VER)
# include <malloc.h>  // _aligned_malloc / _aligned_free
#endif
#include <atomic>
#include <new>
#include <vector>
#include <array>
#include <mutex>
#include <fstream>

namespace fl {

using allocate_fn = void*(*)(std::size_t);
using deallocate_fn = void(*)(void*, std::size_t);
using allocate_aligned_fn = void*(*)(std::size_t, std::size_t);
using deallocate_aligned_fn = void(*)(void*, std::size_t, std::size_t);

namespace alloc_hooks {
        // Required boundary for AVX2 loads (32 bytes).  Only relevant when a
        // code path actually issues 32-byte-aligned SIMD loads.
        constexpr std::size_t SIMD_ALIGNMENT = 32;

        // Default alignment for every fl::string heap allocation.
        //
        // Aligning to alignof(std::max_align_t) (16 bytes on x86-64) lets glibc
        // serve requests from its standard per-size bins rather than falling
        // through to aligned_alloc/posix_memalign, which bypass the bins entirely.
        // The SSE2 find_char_simd path uses unaligned 16-byte loads (_mm_loadu_si128)
        // and does not require pointer alignment; AVX2-aligned loads are not issued.
        constexpr std::size_t DEFAULT_ALIGNMENT = alignof(std::max_align_t);

        inline void* default_allocate(std::size_t n) {
        if (n == 0) return nullptr;
    #ifdef _WIN32
        void* p = _aligned_malloc(n, DEFAULT_ALIGNMENT);
        return p;
    #else
        // Prefer aligned_alloc (C11); fall back to posix_memalign on older toolchains.
    #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__APPLE__)
        // aligned_alloc requires size to be a multiple of alignment.
        std::size_t adj = ((n + DEFAULT_ALIGNMENT - 1) / DEFAULT_ALIGNMENT) * DEFAULT_ALIGNMENT;
        void* p = std::aligned_alloc(DEFAULT_ALIGNMENT, adj);
        return p;
    #else
        void* p = nullptr;
        std::size_t adj = ((n + DEFAULT_ALIGNMENT - 1) / DEFAULT_ALIGNMENT) * DEFAULT_ALIGNMENT;
        if (posix_memalign(&p, DEFAULT_ALIGNMENT, adj) != 0) p = nullptr;
        return p;
    #endif
    #endif
        }

        inline void default_deallocate(void* p, std::size_t) {
        if (!p) return;
    #ifdef _WIN32
        _aligned_free(p);
    #else
        std::free(p);
    #endif
        }

    inline std::atomic<bool>& hooks_customised() noexcept {
        static std::atomic<bool> customised{false};
        return customised;
    }

    inline std::atomic<allocate_fn>& get_allocate_ptr() noexcept {
        static std::atomic<allocate_fn> ptr{default_allocate};
        return ptr;
    }

    inline std::atomic<deallocate_fn>& get_deallocate_ptr() noexcept {
        static std::atomic<deallocate_fn> ptr{default_deallocate};
        return ptr;
    }

        // Small-block pool size classes.
        constexpr std::size_t MAX_POOL_SIZE = 4096;
        constexpr std::array<std::size_t,7> POOL_CLASSES = {64,128,256,512,1024,2048,4096};

        // Returns the index into POOL_CLASSES for a given size, or -1 if too large.
        inline int pool_class_index(std::size_t n) noexcept {
            for (size_t i = 0; i < POOL_CLASSES.size(); ++i) if (n <= POOL_CLASSES[i]) return static_cast<int>(i);
            return -1;
        }

        // Returns the usable capacity (chars, excluding null terminator) for a raw
        // allocation of raw_size bytes routed through the pool.  When raw_size fits
        // a pool class, the whole class block is available -- using this as the stored
        // capacity avoids premature reallocation (e.g. constructing a 100-char string
        // requests 101 bytes, lands in the 128-byte class, and gets capacity 127
        // instead of 100, giving 27 free bytes before the first grow_to).
        inline std::size_t pool_alloc_usable_capacity(std::size_t raw_size) noexcept {
            int idx = pool_class_index(raw_size);
            if (idx >= 0) return POOL_CLASSES[static_cast<std::size_t>(idx)] - 1;
            return raw_size - 1;
        }

        // -----------------------------------------------------------------------
        // Flat TLS free-list pool.
        //
        // Layout is split into a HOT region (counts) and a COLD region (slots):
        //
        //   offset   0 –  6 : counts[7]  — one byte per class, ALWAYS accessed
        //   offset   7 – 63 : padding    — fills first 64-byte cache line
        //   offset  64 – 127: slots[0]   — 8 pointers for class 64  (cache line 1)
        //   offset 128 – 191: slots[1]   — 8 pointers for class 128 (cache line 2)
        //   ...
        //   offset 448 – 511: slots[6]   — 8 pointers for class 4096(cache line 7)
        //
        // Total: 512 bytes = 8 cache lines (down from 3591 bytes / 56 cache lines
        // at SLAB_DEPTH=64).  On every pool hit/miss the counts cache line is
        // loaded; the relevant slots cache line is only fetched when there is
        // actual work to do there (hit → load ptr, miss → nothing, push → store).
        //
        // POOL_SLAB_DEPTH: per-class slot capacity.  8 is sufficient for typical
        // short-lived string workloads and keeps the cold region to 7 × 64 = 448 B.
        // -----------------------------------------------------------------------
        constexpr int POOL_SLAB_DEPTH = 8;

        // Per-thread free-list structure, aligned to a 64-byte cache line.
        // Separates frequently read count bytes (hot) from the slot pointer arrays
        // (cold) so that a pool lookup that finds an empty class never touches the
        // slots cache line.
        struct alignas(64) TlsFreeLists {
            // Hot cache line: all 7 per-class counts fit here.  Keeping them
            // contiguous and cache-line-aligned means a single load brings in
            // the count for every class simultaneously.
            uint8_t counts[POOL_CLASSES.size()];
            char    _pad[64 - static_cast<int>(POOL_CLASSES.size())];

            // Cold region: one cache-line-sized block of slot pointers per class.
            // Only brought into L1 when the class actually has a pending hit or push.
            void*   slots[POOL_CLASSES.size()][POOL_SLAB_DEPTH];
        };

        // Returns a reference to the per-thread flat pool structure.
        // Zero-initialised on first access per thread.
        inline TlsFreeLists& get_tls_free_lists() noexcept {
            static thread_local TlsFreeLists tls{};
            return tls;
        }

        // Underlying platform allocate/deallocate used when the pool is empty or
        // bypassed.
        //
        // When align <= alignof(std::max_align_t), glibc malloc already guarantees
        // the required alignment, so we skip aligned_alloc/posix_memalign entirely.
        // Those paths bypass the standard tcache/fastbin bins; plain malloc does not.
        inline void* allocate_aligned_unpooled(std::size_t n, std::size_t align) {
            if (n == 0) return nullptr;
    #ifdef _WIN32
            // _aligned_malloc is always required on Windows because HeapAlloc may
            // not guarantee 16-byte alignment, and _aligned_free must pair with it.
            return _aligned_malloc(n, align);
    #else
            if (align <= alignof(std::max_align_t)) {
                // glibc/musl malloc guarantees alignof(max_align_t)-alignment
                // (16 bytes on x86-64).  No padding header required.
                return std::malloc(n);
            }
    #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) && !defined(__APPLE__)
            std::size_t adj = ((n + align - 1) / align) * align;
            return std::aligned_alloc(align, adj);
    #else
            void* p = nullptr;
            std::size_t adj = ((n + align - 1) / align) * align;
            if (posix_memalign(&p, align, adj) != 0) p = nullptr;
            return p;
    #endif
    #endif
        }

        inline void deallocate_aligned_unpooled(void* p, std::size_t, std::size_t) {
            if (!p) return;
#ifdef _WIN32
            _aligned_free(p);
#else
            std::free(p);
#endif
        }

        // Forward declarations for pool instrumentation counters.
        inline std::atomic<std::uint64_t>& pool_hits() noexcept;
        inline std::atomic<std::uint64_t>& pool_misses() noexcept;
        inline std::atomic<std::uint64_t>& pool_pushes() noexcept;
        inline std::atomic<std::uint64_t>& pool_evictions() noexcept;
        inline std::array<std::atomic<std::uint64_t>, POOL_CLASSES.size()>& pool_class_hits() noexcept;
        inline std::array<std::atomic<std::uint64_t>, POOL_CLASSES.size()>& pool_class_pushes() noexcept;

        // A single allocation request record used by the optional request log.
        struct RequestEntry { std::size_t size; int class_idx; };

        // Set to true to enable allocation request logging.  Guarded by
        // if-constexpr so the logging code is compiled out when disabled.
        constexpr bool ENABLE_POOL_REQUEST_LOG = false;

        inline std::vector<RequestEntry>& pool_request_log_storage() noexcept {
            static std::vector<RequestEntry> log;
            return log;
        }

        inline std::mutex& pool_request_log_mutex() noexcept {
            static std::mutex m;
            return m;
        }

        inline void reset_pool_request_log() noexcept {
            std::lock_guard<std::mutex> lk(pool_request_log_mutex());
            pool_request_log_storage().clear();
        }

        inline void dump_pool_request_log_to_file(const char* path) noexcept {
            std::lock_guard<std::mutex> lk(pool_request_log_mutex());
            std::ofstream os(path, std::ios::out | std::ios::trunc);
            if (!os) return;
            for (auto &e : pool_request_log_storage()) os << e.size << ',' << e.class_idx << '\n';
            os.close();
        }

        inline void* default_allocate_aligned(std::size_t n, std::size_t align) {
            if (n == 0) return nullptr;

            // Bypass the pool for sizes larger than any class to avoid TLS access
            // and statistics overhead on allocations that will never be recycled.
            int idx = pool_class_index(n);
            if (idx < 0) return allocate_aligned_unpooled(n, align);

            // Flat TLS free-list: compare one byte (count), then index the slot
            // array.  No vector metadata, no pop_back overhead.
            TlsFreeLists& tls = get_tls_free_lists();
            if (tls.counts[idx] > 0) {
                void* p = tls.slots[idx][--tls.counts[idx]];
                #ifndef NDEBUG
                pool_hits().fetch_add(1, std::memory_order_relaxed);
                pool_class_hits()[idx].fetch_add(1, std::memory_order_relaxed);
                #endif
                if constexpr (ENABLE_POOL_REQUEST_LOG) {
                    std::lock_guard<std::mutex> lk(pool_request_log_mutex());
                    auto &L = pool_request_log_storage();
                    if (L.size() < 200000) L.push_back({n, idx});
                }
                return p;
            }
            // Pool miss: allocate a full class-sized block so the returned memory
            // can later be recycled into the correct class slab on deallocation.
            std::size_t alloc_size = POOL_CLASSES[idx];
            #ifndef NDEBUG
            pool_misses().fetch_add(1, std::memory_order_relaxed);
            #endif
            return allocate_aligned_unpooled(alloc_size, align);
        }

        inline void default_deallocate_aligned(void* p, std::size_t n, std::size_t align) {
            if (!p) return;
            int idx = pool_class_index(n);
            if (idx < 0) { deallocate_aligned_unpooled(p, n, align); return; }

            TlsFreeLists& tls = get_tls_free_lists();
            if (tls.counts[idx] < static_cast<uint8_t>(POOL_SLAB_DEPTH)) {
                tls.slots[idx][tls.counts[idx]++] = p;
                #ifndef NDEBUG
                pool_pushes().fetch_add(1, std::memory_order_relaxed);
                pool_class_pushes()[idx].fetch_add(1, std::memory_order_relaxed);
                #endif
                if constexpr (ENABLE_POOL_REQUEST_LOG) {
                    std::lock_guard<std::mutex> lk(pool_request_log_mutex());
                    auto &L = pool_request_log_storage();
                    if (L.size() < 200000) L.push_back({n, idx});
                }
            } else {
                // Slab full: fall through to the system allocator and record
                // the eviction for diagnostics.
                deallocate_aligned_unpooled(p, n, align);
                #ifndef NDEBUG
                pool_evictions().fetch_add(1, std::memory_order_relaxed);
                #endif
            }
        }

        // Snapshot of pool instrumentation counters for external reporting.
        struct PoolStats {
            std::uint64_t hits = 0;
            std::uint64_t misses = 0;
            std::uint64_t pushes = 0;
            std::uint64_t evictions = 0;
            std::array<std::uint64_t, POOL_CLASSES.size()> class_hits{};
            std::array<std::uint64_t, POOL_CLASSES.size()> class_pushes{};
        };

        inline std::atomic<std::uint64_t>& pool_hits() noexcept {
            static std::atomic<std::uint64_t> a{0};
            return a;
        }
        inline std::atomic<std::uint64_t>& pool_misses() noexcept {
            static std::atomic<std::uint64_t> a{0};
            return a;
        }
        inline std::atomic<std::uint64_t>& pool_pushes() noexcept {
            static std::atomic<std::uint64_t> a{0};
            return a;
        }
        inline std::atomic<std::uint64_t>& pool_evictions() noexcept {
            static std::atomic<std::uint64_t> a{0};
            return a;
        }
        inline std::array<std::atomic<std::uint64_t>, POOL_CLASSES.size()>& pool_class_hits() noexcept {
            static std::array<std::atomic<std::uint64_t>, POOL_CLASSES.size()> a{};
            return a;
        }
        inline std::array<std::atomic<std::uint64_t>, POOL_CLASSES.size()>& pool_class_pushes() noexcept {
            static std::array<std::atomic<std::uint64_t>, POOL_CLASSES.size()> a{};
            return a;
        }

        inline void reset_pool_stats() noexcept {
            pool_hits().store(0, std::memory_order_relaxed);
            pool_misses().store(0, std::memory_order_relaxed);
            pool_pushes().store(0, std::memory_order_relaxed);
            pool_evictions().store(0, std::memory_order_relaxed);
            for (size_t i = 0; i < POOL_CLASSES.size(); ++i) {
                pool_class_hits()[i].store(0, std::memory_order_relaxed);
                pool_class_pushes()[i].store(0, std::memory_order_relaxed);
            }
        }

        inline PoolStats get_pool_stats() noexcept {
            PoolStats s;
            s.hits = pool_hits().load(std::memory_order_relaxed);
            s.misses = pool_misses().load(std::memory_order_relaxed);
            s.pushes = pool_pushes().load(std::memory_order_relaxed);
            s.evictions = pool_evictions().load(std::memory_order_relaxed);
            for (size_t i = 0; i < POOL_CLASSES.size(); ++i) {
                s.class_hits[i] = pool_class_hits()[i].load(std::memory_order_relaxed);
                s.class_pushes[i] = pool_class_pushes()[i].load(std::memory_order_relaxed);
            }
            return s;
        }

        inline std::atomic<allocate_aligned_fn>& get_allocate_aligned_ptr() noexcept {
        static std::atomic<allocate_aligned_fn> ptr{default_allocate_aligned};
        return ptr;
        }

        inline std::atomic<deallocate_aligned_fn>& get_deallocate_aligned_ptr() noexcept {
        static std::atomic<deallocate_aligned_fn> ptr{default_deallocate_aligned};
        return ptr;
        }

    // FL_HOOKS_ALWAYS_DEFAULT: define this macro (e.g. -DFL_HOOKS_ALWAYS_DEFAULT)
    // to hard-wire the "no custom hooks" branch at compile time.  The atomic load
    // on hooks_customised() then disappears entirely, allowing the compiler to
    // inline and dead-code-eliminate fl::string construction in benchmark builds
    // at -O2 (matching the treatment std::string receives for constant literals).
    //
    // WARNING: when this macro is defined, calling set_alloc_hooks() after program
    // start has NO effect.  Use only in test/benchmark translation units where
    // you are certain custom hooks are never installed.
#ifdef FL_HOOKS_ALWAYS_DEFAULT
    inline constexpr bool _hooks_active() noexcept { return false; }
#else
    inline bool _hooks_active() noexcept {
        return hooks_customised().load(std::memory_order_relaxed);
    }
#endif

    inline void* allocate_bytes(std::size_t n) noexcept {
        if (!_hooks_active()) {
            return default_allocate(n);
        }
        return get_allocate_ptr().load(std::memory_order_relaxed)(n);
    }

    inline void deallocate_bytes(void* p, std::size_t n) noexcept {
        if (!_hooks_active()) {
            default_deallocate(p, n);
            return;
        }
        get_deallocate_ptr().load(std::memory_order_relaxed)(p, n);
    }

    inline void* allocate_bytes_aligned(std::size_t n, std::size_t align) noexcept {
        if (!_hooks_active()) {
            return default_allocate_aligned(n, align);
        }
        return get_allocate_aligned_ptr().load(std::memory_order_relaxed)(n, align);
    }

    inline void deallocate_bytes_aligned(void* p, std::size_t n, std::size_t align) noexcept {
        if (!_hooks_active()) {
            default_deallocate_aligned(p, n, align);
            return;
        }
        get_deallocate_aligned_ptr().load(std::memory_order_relaxed)(p, n, align);
    }

    inline void set_hooks(allocate_fn a, deallocate_fn d, allocate_aligned_fn aa = nullptr, deallocate_aligned_fn da = nullptr) noexcept {
        hooks_customised().store(a || d || aa || da, std::memory_order_relaxed);
        get_allocate_ptr().store(a ? a : default_allocate, std::memory_order_relaxed);
        get_deallocate_ptr().store(d ? d : default_deallocate, std::memory_order_relaxed);

        if (aa) get_allocate_aligned_ptr().store(aa, std::memory_order_relaxed);
        else if (a) get_allocate_aligned_ptr().store(
            +[](std::size_t n, std::size_t) -> void* { return get_allocate_ptr().load(std::memory_order_relaxed)(n); },
            std::memory_order_relaxed);
        else get_allocate_aligned_ptr().store(default_allocate_aligned, std::memory_order_relaxed);

        if (da) get_deallocate_aligned_ptr().store(da, std::memory_order_relaxed);
        else if (d) get_deallocate_aligned_ptr().store(
            +[](void* p, std::size_t n, std::size_t) -> void { get_deallocate_ptr().load(std::memory_order_relaxed)(p, n); },
            std::memory_order_relaxed);
        else get_deallocate_aligned_ptr().store(default_deallocate_aligned, std::memory_order_relaxed);
    }
}  // namespace alloc_hooks

// Namespace-level convenience wrappers that forward to alloc_hooks.
inline void* allocate_bytes(std::size_t n) noexcept { return alloc_hooks::allocate_bytes(n); }
inline void deallocate_bytes(void* p, std::size_t n) noexcept { alloc_hooks::deallocate_bytes(p, n); }
inline void* allocate_bytes_aligned(std::size_t n, std::size_t align) noexcept { return alloc_hooks::allocate_bytes_aligned(n, align); }
inline void deallocate_bytes_aligned(void* p, std::size_t n, std::size_t align) noexcept { alloc_hooks::deallocate_bytes_aligned(p, n, align); }
inline void set_alloc_hooks(allocate_fn a, deallocate_fn d, allocate_aligned_fn aa = nullptr, deallocate_aligned_fn da = nullptr) noexcept { alloc_hooks::set_hooks(a, d, aa, da); }

// C++ standard allocator backed by the fl TLS free-list pool.
//
// Use with std::allocate_shared or std::vector where nodes should be recycled
// through the pool rather than going to global malloc/free.  For example,
// std::allocate_shared allocates sizeof(T) + control block in a single call;
// the combined size typically lands in a pool class (e.g. leaf_node ~48 B
// + 16 B control = 64 B, which maps to pool class 64).
template <typename T>
struct pool_alloc {
    using value_type = T;
    pool_alloc() noexcept = default;
    template <typename U> pool_alloc(const pool_alloc<U>&) noexcept {}
    T* allocate(std::size_t n) {
        void* p = alloc_hooks::allocate_bytes_aligned(n * sizeof(T), alignof(T));
        if (!p) throw std::bad_alloc{};
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t n) noexcept {
        alloc_hooks::deallocate_bytes_aligned(p, n * sizeof(T), alignof(T));
    }
    template <typename U> bool operator==(const pool_alloc<U>&) const noexcept { return true; }
    template <typename U> bool operator!=(const pool_alloc<U>&) const noexcept { return false; }
};

}  // namespace fl

namespace fl {

// Returns the preferred allocation alignment based on pool configuration.
inline std::size_t preferred_alloc_alignment() noexcept {
    return alloc_hooks::DEFAULT_ALIGNMENT;
}

}  // namespace fl

#endif  // FL_ALLOC_HOOKS_HPP
