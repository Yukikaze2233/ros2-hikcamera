#include "hikcamera/shared_frame_writer.hpp"
#include "hikcamera/capturer.hpp"
#include "hikcamera/shm_types.hpp"

#include <cstring>
#include <format>
#include <limits>
#include <mutex>

namespace hikcamera {

namespace ring {
auto writer_create(const char* name) -> std::expected<SharedRingHeader*, std::string>;
auto close(SharedRingHeader* ring, const char* name, bool unlink) -> void;
}

struct SharedFrameWriter::WriterState {
    std::string        name;
    SharedRingHeader*  ring{nullptr};
    std::mutex         write_mutex;
};

SharedFrameWriter::SharedFrameWriter(std::unique_ptr<WriterState> state) noexcept
    : state_(std::move(state)) {}
SharedFrameWriter::~SharedFrameWriter() {
    if (state_ && state_->ring != nullptr)
        ring::close(state_->ring, state_->name.c_str(), true);
}
SharedFrameWriter::SharedFrameWriter(SharedFrameWriter&&) noexcept = default;
SharedFrameWriter& SharedFrameWriter::operator=(SharedFrameWriter&&) noexcept = default;

auto SharedFrameWriter::create(const char* name) -> std::expected<SharedFrameWriter, std::string> {
    auto result = ring::writer_create(name);
    if (!result.has_value()) return std::unexpected{result.error()};
    auto state = std::make_unique<WriterState>();
    state->name = name;
    state->ring = result.value();
    return SharedFrameWriter{std::move(state)};
}

static auto validate_metadata(const FrameMetadata& meta, size_t data_sz)
    -> std::expected<void, std::string>
{
    if (meta.width == 0 || meta.height == 0)
        return std::unexpected{"Width and height must be positive"};
    const uint32_t min_stride = meta.width * 3U;
    const uint32_t stride = meta.stride_bytes > 0 ? meta.stride_bytes : min_stride;
    if (stride < min_stride)
        return std::unexpected{"Stride < width*3"};
    const uint64_t needed = static_cast<uint64_t>(stride) * meta.height;
    if (needed > data_sz)
        return std::unexpected{std::format("stride*height ({}) > data ({})", needed, data_sz)};
    if (data_sz > std::numeric_limits<uint32_t>::max())
        return std::unexpected{"Data size exceeds uint32 max"};
    if (data_sz > kShmMaxPixelBytes)
        return std::unexpected{std::format("Frame too large: {} > {}", data_sz, kShmMaxPixelBytes)};
    return {};
}

// RAII slot write-lock guard.
struct SlotGuard { pthread_rwlock_t* lk; ~SlotGuard() { if(lk)pthread_rwlock_unlock(lk); } };

auto SharedFrameWriter::write(FrameMetadata meta,
                              std::span<const unsigned char> bgr_data)
    -> std::expected<void, std::string>
{
    if (!state_ || state_->ring == nullptr) return std::unexpected{"Writer not initialised"};
    if (auto v = validate_metadata(meta, bgr_data.size()); !v.has_value())
        return std::unexpected{v.error()};
    std::lock_guard<std::mutex> lock(state_->write_mutex);
    auto* ring = state_->ring;

    uint64_t seq = ring->latest_sequence.load(std::memory_order_acquire);
    if (seq == std::numeric_limits<uint64_t>::max())
        return std::unexpected{"Sequence overflow"};
    seq += 1;

    const int slot_idx = static_cast<int>((seq - 1) % kShmSlotCount);
    auto& slot = ring->slots[slot_idx];
    int rc = pthread_rwlock_wrlock(&slot.lock);
    if (rc != 0) return std::unexpected{std::format("wrlock: {}", rc)};
    SlotGuard sg{&slot.lock};

    std::memcpy(slot.pixels, bgr_data.data(), bgr_data.size());
    meta.committed_sequence = seq;
    meta.pixel_format       = PixelFormat::RGB8;
    meta.stride_bytes       = meta.stride_bytes > 0 ? meta.stride_bytes : meta.width * 3U;
    meta.committed_bytes    = static_cast<uint32_t>(bgr_data.size());
    slot.metadata = meta;

    rc = pthread_mutex_lock(&ring->mutex);
    if (rc != 0) return std::unexpected{std::format("mutex_lock: {}", rc)};
    ring->latest_sequence.store(seq, std::memory_order_release);
    rc = pthread_cond_broadcast(&ring->cond);
    pthread_mutex_unlock(&ring->mutex);
    if (rc != 0) return std::unexpected{std::format("cond_broadcast: {}", rc)};
    return {};
}

auto SharedFrameWriter::write_from_camera(Camera& camera)
    -> std::expected<void, std::string>
{
    if (!state_ || state_->ring == nullptr) return std::unexpected{"Writer not initialised"};
    std::lock_guard<std::mutex> lock(state_->write_mutex);
    auto* ring = state_->ring;

    uint64_t seq = ring->latest_sequence.load(std::memory_order_acquire);
    if (seq == std::numeric_limits<uint64_t>::max()) return std::unexpected{"Sequence overflow"};
    seq += 1;

    const int slot_idx = static_cast<int>((seq - 1) % kShmSlotCount);
    auto& slot = ring->slots[slot_idx];
    int rc = pthread_rwlock_wrlock(&slot.lock);
    if (rc != 0) return std::unexpected{std::format("wrlock: {}", rc)};
    SlotGuard sg{&slot.lock};

    auto img_result = camera.read_image_with_timestamp(
        std::span<unsigned char>(reinterpret_cast<unsigned char*>(slot.pixels), kShmMaxPixelBytes));
    if (!img_result.has_value()) return std::unexpected{img_result.error()};

    const auto& img = img_result.value();

    // Validate camera output before committing.
    if (img.mat.cols <= 0 || img.mat.rows <= 0)
        return std::unexpected{"Camera returned zero-dimension image"};
    const size_t img_bytes = img.mat.total() * img.mat.elemSize();
    if (img_bytes == 0 || img_bytes > kShmMaxPixelBytes)
        return std::unexpected{std::format("Camera image size {} out of range", img_bytes)};
    if (static_cast<size_t>(img.mat.step[0]) < static_cast<size_t>(img.mat.cols) * img.mat.elemSize())
        return std::unexpected{"Camera stride < width*elemSize"};

    FrameMetadata meta{};
    meta.committed_sequence     = seq;
    meta.frame_id               = img.frame_id;
    meta.device_timestamp_ticks = img.device_timestamp_ticks;
    meta.host_monotonic_ns      = img.host_monotonic_ns;
    meta.exposure_us            = img.exposure_us;
    meta.width                  = static_cast<uint32_t>(img.mat.cols);
    meta.height                 = static_cast<uint32_t>(img.mat.rows);
    meta.stride_bytes           = static_cast<uint32_t>(img.mat.step[0]);
    meta.committed_bytes        = static_cast<uint32_t>(img_bytes);
    meta.pixel_format           = PixelFormat::RGB8;
    slot.metadata = meta;

    rc = pthread_mutex_lock(&ring->mutex);
    if (rc != 0) return std::unexpected{std::format("mutex_lock: {}", rc)};
    ring->latest_sequence.store(seq, std::memory_order_release);
    rc = pthread_cond_broadcast(&ring->cond);
    pthread_mutex_unlock(&ring->mutex);
    if (rc != 0) return std::unexpected{std::format("cond_broadcast: {}", rc)};
    return {};
}

auto SharedFrameWriter::valid() const noexcept -> bool { return state_ && state_->ring != nullptr; }
auto SharedFrameWriter::latest_sequence() const noexcept -> uint64_t {
    if (!state_ || state_->ring == nullptr) return 0;
    return state_->ring->latest_sequence.load(std::memory_order_acquire);
}
auto SharedFrameWriter::name() const noexcept -> const std::string& {
    static const std::string empty;
    return state_ ? state_->name : empty;
}

}  // namespace hikcamera
