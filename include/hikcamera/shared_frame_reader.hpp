#pragma once

#include "hikcamera/shared_frame.hpp"
#include "hikcamera/shm_types.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <optional>

namespace hikcamera {

// fwd — defined in shm_ring.cpp
namespace ring {
auto reader_open(const char* name) -> std::expected<SharedRingHeader*, std::string>;
}

/// Latest-frame broadcast reader.
///
/// Opens an *existing* SHM ring (never creates or resizes), validates the
/// magic/version/layout, then maps the segment once.  Each wait_next() /
/// try_next() blocks until a new committed sequence is available, skips
/// directly to the latest, and returns a SharedFrame lease.
///
/// Sequences are delivered at most once and in strictly increasing order.
/// Intervening frames are dropped if the reader cannot keep up.
///
/// The reader is move-only.  Outstanding SharedFrame leases survive
/// reader destruction/reopen/move because they hold independent
/// shared ownership of the mapping.
class SharedFrameReader {
public:
    SharedFrameReader();
    ~SharedFrameReader();

    SharedFrameReader(const SharedFrameReader&)            = delete;
    SharedFrameReader& operator=(const SharedFrameReader&) = delete;

    SharedFrameReader(SharedFrameReader&&)            noexcept;
    SharedFrameReader& operator=(SharedFrameReader&&) noexcept;

    /// Open an existing SHM ring.  Fails if the segment does not exist,
    /// has wrong magic/version, or the layout size mismatches.
    [[nodiscard]] auto open(const char* name)
        -> std::expected<void, std::string>;

    /// Reopen: close current mapping (if any) and reopen the same name.
    [[nodiscard]] auto reopen() -> std::expected<void, std::string>;

    /// Block until `latest_sequence > last_delivered_sequence`.
    /// Returns the frame at the latest sequence; drops any intermediates.
    /// `timeout` uses CLOCK_MONOTONIC.  0 = no timeout (infinite).
    [[nodiscard]] auto wait_next(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{2000})
        -> std::expected<SharedFrame, std::string>;

    /// Non-blocking poll — returns latest frame if a new one exists.
    [[nodiscard]] auto try_next() -> std::optional<SharedFrame>;

    [[nodiscard]] auto is_open() const noexcept -> bool;

    /// Last sequence delivered by this reader (0 if none).
    [[nodiscard]] auto last_delivered_sequence() const noexcept -> uint64_t;

private:
    struct ReaderState;
    std::shared_ptr<ReaderState> state_;
};

}  // namespace hikcamera
