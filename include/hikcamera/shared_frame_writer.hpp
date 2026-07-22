#pragma once

#include "hikcamera/shm_types.hpp"

#include <expected>
#include <memory>
#include <span>
#include <string>

namespace hikcamera {

class Camera;  // fwd

/// RAII exclusive writer for a four-slot broadcast SHM ring.
///
/// Only the writer creates the SHM segment (O_CREAT|O_EXCL) and initialises
/// the header with magic/version/layout.  The segment is unlinked when the
/// writer is destroyed.
///
/// write() and write_from_camera() are internally serialized. The camera
/// driver normally still uses one capture thread.
class SharedFrameWriter {
public:
    SharedFrameWriter() = default;
    ~SharedFrameWriter();

    SharedFrameWriter(const SharedFrameWriter&)            = delete;
    SharedFrameWriter& operator=(const SharedFrameWriter&) = delete;

    SharedFrameWriter(SharedFrameWriter&&)            noexcept;
    SharedFrameWriter& operator=(SharedFrameWriter&&) noexcept;

    /// Create a new SHM ring named `name`.  Fails if the segment already exists.
    [[nodiscard]] static auto create(const char* name)
        -> std::expected<SharedFrameWriter, std::string>;

    /// Write BGR8 pixel data + metadata into the next slot.
    /// The ring's latest_sequence is advanced atomically.
    [[nodiscard]] auto write(FrameMetadata meta,
                             std::span<const unsigned char> bgr_data)
        -> std::expected<void, std::string>;

    /// Capture a frame from `camera` (Bayer→BGR8), package metadata, publish.
    [[nodiscard]] auto write_from_camera(Camera& camera)
        -> std::expected<void, std::string>;

    [[nodiscard]] auto valid() const noexcept -> bool;
    [[nodiscard]] auto latest_sequence() const noexcept -> uint64_t;
    [[nodiscard]] auto name() const noexcept -> const std::string&;

private:
    struct WriterState;
    explicit SharedFrameWriter(std::unique_ptr<WriterState> state) noexcept;
    std::unique_ptr<WriterState> state_;
};

}  // namespace hikcamera
