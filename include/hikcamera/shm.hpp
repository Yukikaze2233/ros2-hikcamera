#pragma once

#include "hikcamera/capturer.hpp"
#include "hikcamera/shared_frame_writer.hpp"

#include <expected>
#include <string>

namespace hikcamera {

/// Convenience: create a SharedFrameWriter and return it.
/// The writer owns the SHM segment; when it goes out of scope the
/// segment is unmapped and unlinked.
[[nodiscard]] inline auto SHMInit(const char* name)
    -> std::expected<SharedFrameWriter, std::string>
{
    return SharedFrameWriter::create(name);
}

/// Convenience: capture a frame from `camera` and publish into `writer`.
[[nodiscard]] inline auto SHMWrite(SharedFrameWriter& writer, Camera& camera)
    -> std::expected<void, std::string>
{
    return writer.write_from_camera(camera);
}

}  // namespace hikcamera
