#pragma once

#include <atomic>
#include <cstdint>
#include <pthread.h>

namespace hikcamera {

// ---- protocol identity ----

inline constexpr uint32_t kShmMagic   = 0x48494B53;  // "HIKS"
inline constexpr uint32_t kShmVersion = 2U;

// ---- dimensions ----

inline constexpr uint32_t kShmSlotCount      = 4U;
inline constexpr size_t   kShmMaxPixelBytes  = 60'000'000U;
static_assert(5472U * 3648U * 3U <= kShmMaxPixelBytes,
               "Slot capacity must hold full-resolution BGR8 frame");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "SHM protocol requires lock-free 64-bit atomics");
static_assert(std::atomic<bool>::is_always_lock_free,
              "SHM protocol requires lock-free bool atomics");

// ---- pixel format ----

enum class PixelFormat : uint32_t {
    BGR8 = 0x42475238U,  // "BGR8" — little-endian packed
};

// ---- per-frame metadata ----

/// All-zero metadata means "no frame committed".
struct FrameMetadata {
    uint64_t committed_sequence{0};    // 0 = empty slot; >=1 = committed
    uint64_t frame_id{0};              // device frame counter (nFrameCounter)
    uint64_t device_timestamp_ticks{0}; // (nDevTimeStampHigh << 32) | nDevTimeStampLow
    uint64_t host_monotonic_ns{0};     // steady_clock nanoseconds at conversion time
    uint32_t exposure_us{0};           // device exposure in microseconds
    uint32_t width{0};
    uint32_t height{0};
    uint32_t stride_bytes{0};          // row stride of BGR8 pixels
    uint32_t committed_bytes{0};       // valid pixel bytes written
    PixelFormat pixel_format{PixelFormat::BGR8};
};

// ---- one slot of the ring ----

/// Protected by a process-shared rwlock.
/// Readers hold a shared (read) lock; the writer an exclusive (write) lock.
struct FrameSlot {
    pthread_rwlock_t lock{};
    FrameMetadata    metadata{};
    char             pixels[kShmMaxPixelBytes]{};
};

// ---- SHM segment header ----

/// Placed at the start of the POSIX SHM segment.
/// `latest_sequence` is atomically published; 0 = no frame committed.
struct SharedRingHeader {
    // --- identity block (validated by readers on open) ---
    uint32_t magic{kShmMagic};
    uint32_t version{kShmVersion};
    uint32_t layout_size{sizeof(SharedRingHeader)};
    uint32_t slot_count{kShmSlotCount};
    uint32_t max_pixel_bytes{kShmMaxPixelBytes};
    uint32_t _pad8{0};  // 8-byte alignment

    // --- process-shared synchronization ---
    pthread_mutex_t mutex{};
    pthread_cond_t  cond{};
    std::atomic<uint64_t> latest_sequence{0};  // 0 = no frame; >=1 = committed

    // --- initialized by writer, validated by readers ---
    std::atomic<bool> ready{false};

    // --- data slots ---
    FrameSlot slots[kShmSlotCount];
};

// ===================================================================
//  FAIL-STOP BEHAVIOUR
//
//  Process-shared pthread mutex/rwlock are NOT robust (PTHREAD_MUTEX_ROBUST).
//  If a participant crashes while holding a lock, that lock deadlocks
//  permanently.  Recovery requires segment recreation (shm_unlink +
//  SharedFrameWriter::create).
//
//  Writer crash: readers observe bounded-timeout (not infinite hang)
//    because all wait_next() / try_next() calls have a 2 s default.
//  Reader crash: writer eventually blocks on the held slot's rwlock
//    when the ring wraps around after 4 slots.
// ===================================================================

}  // namespace hikcamera
