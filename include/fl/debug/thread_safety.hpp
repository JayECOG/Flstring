// Copyright (c) 2026 Jayden Emmanuel.
// Licensed under the FL License. See LICENSE.txt for details.

#ifndef FL_DEBUG_THREAD_SAFETY_HPP
#define FL_DEBUG_THREAD_SAFETY_HPP

// Debug-only thread-safety diagnostics for fl string types.
//
// When FL_DEBUG_THREAD_SAFETY is enabled, every read, write, and move operation
// on a string instance is tracked through an atomic state machine embedded in
// the object.  Concurrent access violations (data races) are detected at
// runtime and reported with a full diagnostic before aborting.
//
// In release builds the tracker compiles down to a zero-overhead stub so that
// none of this machinery affects production performance.

#include "fl/config.hpp"

#if FL_DEBUG_THREAD_SAFETY

#include <atomic>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <cstdio>
#include <cstdint>
#include <cassert>

namespace fl::debug {

// Categorises the kind of access a thread is performing on a tracked object.
// Values are chosen as distinct bits so they can be stored in the low byte of
// the packed atomic state word.
enum class AccessType : std::uint8_t {
    None  = 0,
    Read  = 1,
    Write = 2,
    Moved = 4
};

// A snapshot of a single access event, stored in the diagnostic history ring
// buffer.  Each record captures the originating thread, the access kind, a
// monotonic timestamp, and an optional caller-supplied location string.
struct AccessRecord {
    std::thread::id thread_id;
    AccessType access_type;
    std::chrono::steady_clock::time_point timestamp;
    const char* location;

    AccessRecord(AccessType type, const char* loc)
        : thread_id(std::this_thread::get_id()),
          access_type(type),
          timestamp(std::chrono::steady_clock::now()),
          location(loc) {}
};

// Per-instance tracker that detects concurrent access violations at runtime.
//
// The core mechanism is a single atomic uint32_t whose layout is:
//
//     [active thread count : 24 bits][access type : 8 bits]
//
// Read access increments the thread count and sets the type to Read.  Write
// access demands exclusive ownership (state must be zero).  Any transition
// that would create a data race triggers a diagnostic report and aborts.
//
// The tracker is aligned to a cache line to avoid false sharing between
// adjacent string objects in arrays or containers.
//
// An optional bounded history of AccessRecords can be enabled at compile time
// by setting FL_DEBUG_THREAD_SAFETY_HISTORY to a positive value.  The history
// is guarded by its own mutex and is kept off the hot path.
class alignas(64) thread_access_tracker {
    // Packed state word: upper 24 bits hold the active reader/writer count,
    // lower 8 bits hold the current AccessType.
    std::atomic<std::uint32_t> _state{0};

    // Diagnostic history, guarded by its own mutex to avoid contending with
    // the lock-free state machine on the hot path.
    mutable std::mutex _history_mutex;
    mutable std::vector<AccessRecord> _history;

    static constexpr std::uint32_t TYPE_MASK = 0xFF;
    static constexpr std::uint32_t COUNT_SHIFT = 8;
    static constexpr std::uint32_t MAX_THREADS = 0xFFFFFF;

public:
    thread_access_tracker() {
#if FL_DEBUG_THREAD_SAFETY_HISTORY > 0
        _history.reserve(FL_DEBUG_THREAD_SAFETY_HISTORY);
#endif
    }

    // RAII guard returned by begin_read() and begin_write().  When the guard
    // is destroyed it automatically calls end_access() to release the caller's
    // slot in the atomic state word.  Moves transfer ownership; copies are
    // prohibited.
    class AccessGuard {
        thread_access_tracker& _tracker;
        bool _active;

    public:
        AccessGuard(thread_access_tracker& tracker)
            : _tracker(tracker), _active(true) {}

        AccessGuard(const AccessGuard&) = delete;
        AccessGuard& operator=(const AccessGuard&) = delete;

        AccessGuard(AccessGuard&& other) noexcept
            : _tracker(other._tracker), _active(other._active) {
            other._active = false;
        }

        ~AccessGuard() {
            if (_active) {
                _tracker.end_access();
            }
        }
    };

    // Register a read access.  Concurrent reads are allowed, but a read while
    // a write or moved state is active constitutes a violation.
    [[nodiscard]] AccessGuard begin_read(const char* location) const {
        std::uint32_t old_state = _state.load(std::memory_order_acquire);

        while (true) {
            AccessType type = static_cast<AccessType>(old_state & TYPE_MASK);
            if (type == AccessType::Write || type == AccessType::Moved) {
                const_cast<thread_access_tracker*>(this)->report_violation(AccessType::Read, old_state, location);
            }

            std::uint32_t thread_count = old_state >> COUNT_SHIFT;
            std::uint32_t new_state = ((thread_count + 1) << COUNT_SHIFT) | static_cast<std::uint32_t>(AccessType::Read);

            if (_state.compare_exchange_weak(old_state, new_state,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                break;
            }
        }

        record_access(AccessType::Read, location);
        return AccessGuard(*const_cast<thread_access_tracker*>(this));
    }

    // Register an exclusive write access.  The state must be completely idle
    // (zero); any existing readers, writers, or a moved flag is a violation.
    [[nodiscard]] AccessGuard begin_write(const char* location) const {
        std::uint32_t old_state = _state.load(std::memory_order_acquire);

        if (old_state != 0) {
            const_cast<thread_access_tracker*>(this)->report_violation(AccessType::Write, old_state, location);
        }

        std::uint32_t new_state = (1 << COUNT_SHIFT) | static_cast<std::uint32_t>(AccessType::Write);

        if (!_state.compare_exchange_strong(old_state, new_state,
                                           std::memory_order_release,
                                           std::memory_order_acquire)) {
            const_cast<thread_access_tracker*>(this)->report_violation(AccessType::Write, old_state, location);
        }

        record_access(AccessType::Write, location);
        return AccessGuard(*const_cast<thread_access_tracker*>(this));
    }

    // Permanently mark the object as moved-from.  Every subsequent access
    // will be reported as a use-after-move violation.
    void mark_moved(const char* location) {
        _state.store(static_cast<std::uint32_t>(AccessType::Moved), std::memory_order_release);
        record_access(AccessType::Moved, location);
    }

private:
    // Decrement the active-thread count in the packed state word.  When the
    // count drops to zero the access-type bits are cleared as well, returning
    // the object to the idle state.
    void end_access() {
        std::uint32_t old_state = _state.load(std::memory_order_acquire);
        while (true) {
            std::uint32_t count = old_state >> COUNT_SHIFT;
            if (count == 0) break;

            std::uint32_t new_count = count - 1;
            std::uint32_t new_state = (new_count << COUNT_SHIFT);
            if (new_count > 0) {
                new_state |= (old_state & TYPE_MASK);
            }

            if (_state.compare_exchange_weak(old_state, new_state,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                break;
            }
        }
    }

    void record_access(AccessType type, const char* location) const {
#if FL_DEBUG_THREAD_SAFETY_HISTORY > 0
        std::lock_guard<std::mutex> lock(_history_mutex);
        _history.emplace_back(type, location);
        if (_history.size() > FL_DEBUG_THREAD_SAFETY_HISTORY) {
            _history.erase(_history.begin());
        }
#else
        (void)type; (void)location;
#endif
    }

    // Print a detailed diagnostic to stderr and abort.  The report includes
    // the attempted access, the conflicting state, and (when history is
    // enabled) recent access records.
    [[noreturn]] void report_violation(AccessType attempted, std::uint32_t state, const char* loc) {
        std::lock_guard<std::mutex> lock(_history_mutex);

        AccessType current_type = static_cast<AccessType>(state & TYPE_MASK);
        std::uint32_t count = state >> COUNT_SHIFT;

        std::fprintf(stderr,
            "\n"
            "════════════════════════════════════════════════════════════════\n"
            "  FL THREAD-SAFETY VIOLATION DETECTED\n"
            "════════════════════════════════════════════════════════════════\n"
            "\n"
            "Attempted illegal access:\n"
            "  Type:      %s\n"
            "  Thread ID: %zu\n"
            "  Location:  %s\n"
            "\n"
            "Conflicting object state:\n"
            "  State type:   %s\n"
            "  Thread count: %u\n"
            "\n",
            access_name(attempted),
            std::hash<std::thread::id>{}(std::this_thread::get_id()),
            loc ? loc : "unknown",
            access_name(current_type),
            count
        );

#if FL_DEBUG_THREAD_SAFETY_HISTORY > 0
        if (!_history.empty()) {
            std::fprintf(stderr, "Recent access history (most recent last):\n");
            for (const auto& rec : _history) {
                std::fprintf(stderr, "  - Thread %zu: %s at %s\n",
                    std::hash<std::thread::id>{}(rec.thread_id),
                    access_name(rec.access_type),
                    rec.location ? rec.location : "unknown"
                );
            }
        }
#endif

        std::fprintf(stderr,
            "\n"
            "RATIONALE: This access violated the thread-safety contract of fl library.\n"
            "Concurrent mutation and read/write operations from multiple threads without\n"
            "explicit synchronisation lead to DATA RACES and UNDEFINED BEHAVIOUR.\n"
            "════════════════════════════════════════════════════════════════\n"
            "\n"
        );

        FL_THREAD_SAFETY_ABORT();
    }

    static const char* access_name(AccessType t) {
        switch (t) {
            case AccessType::None:  return "None";
            case AccessType::Read:  return "Read";
            case AccessType::Write: return "Write";
            case AccessType::Moved: return "Moved";
            default:                return "Unknown";
        }
    }
};

} // namespace fl::debug

#else // !FL_DEBUG_THREAD_SAFETY

namespace fl::debug {

// Stub implementation used when FL_DEBUG_THREAD_SAFETY is disabled.  Every
// method is trivially inlined so the compiler can eliminate all tracking
// overhead entirely.  The interface mirrors the real tracker so that calling
// code does not need any preprocessor conditionals of its own.
class thread_access_tracker {
public:
    struct AccessGuard {
        AccessGuard(thread_access_tracker&) {}
        AccessGuard(const AccessGuard&) = delete;
        AccessGuard(AccessGuard&&) = default;
    };

    thread_access_tracker() = default;

    [[nodiscard]] AccessGuard begin_read(const char*) const { return AccessGuard(*const_cast<thread_access_tracker*>(this)); }
    [[nodiscard]] AccessGuard begin_write(const char*) const { return AccessGuard(*const_cast<thread_access_tracker*>(this)); }
    void mark_moved(const char*) {}
};

} // namespace fl::debug

#endif // FL_DEBUG_THREAD_SAFETY

#endif // FL_DEBUG_THREAD_SAFETY_HPP
