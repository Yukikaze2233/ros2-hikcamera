#include "hikcamera/shared_frame_reader.hpp"

#include <cerrno>
#include <format>
#include <sys/mman.h>

namespace hikcamera {

namespace ring {
auto reader_open(const char* name) -> std::expected<SharedRingHeader*, std::string>;
}

struct SharedFrameReader::ReaderState {
    std::string                         shm_name;
    std::shared_ptr<SharedRingHeader>   ring_ref;
    uint64_t                            last_delivered{0};
    bool                                is_open{false};
};

// Shared slot-metadata validation used by wait_next and try_next.
// Caller holds rdlock; returns nullopt/error and does NOT unlock.
static auto validate_slot(const FrameMetadata& meta, uint64_t target_seq)
    -> std::expected<void, std::string>
{
    if (meta.committed_sequence != target_seq)
        return std::unexpected{"stale"};  // special marker: retry
    if (meta.pixel_format != PixelFormat::BGR8)
        return std::unexpected{"Pixel format is not BGR8"};
    if (meta.width == 0 || meta.height == 0)
        return std::unexpected{"Dimensions are zero"};
    const uint32_t min_stride = meta.width * 3U;
    const uint32_t stride = meta.stride_bytes > 0 ? meta.stride_bytes : min_stride;
    if (stride < min_stride)
        return std::unexpected{"Stride < width*3"};
    const uint64_t needed = static_cast<uint64_t>(stride) * meta.height;
    if (meta.committed_bytes == 0 || needed > meta.committed_bytes
        || meta.committed_bytes > kShmMaxPixelBytes)
        return std::unexpected{"committed_bytes inconsistent"};
    return {};
}

SharedFrameReader::SharedFrameReader() = default;
SharedFrameReader::~SharedFrameReader() = default;
SharedFrameReader::SharedFrameReader(SharedFrameReader&&) noexcept = default;
SharedFrameReader& SharedFrameReader::operator=(SharedFrameReader&&) noexcept = default;

auto SharedFrameReader::open(const char* name) -> std::expected<void, std::string> {
    state_ = std::make_shared<ReaderState>();
    state_->shm_name = name;
    auto result = ring::reader_open(name);
    if (!result.has_value()) return std::unexpected{result.error()};
    auto* raw = result.value();
    state_->ring_ref = std::shared_ptr<SharedRingHeader>(raw, [](SharedRingHeader* r) {
        if (r) munmap(r, sizeof(SharedRingHeader));
    });
    state_->last_delivered = 0;
    state_->is_open = true;
    return {};
}

auto SharedFrameReader::reopen() -> std::expected<void, std::string> {
    if (!state_ || state_->shm_name.empty()) return std::unexpected{"No previous open"};
    return open(state_->shm_name.c_str());
}

// RAII mutex unlocker for hot paths.
struct MutexGuard { pthread_mutex_t* m; ~MutexGuard() { if(m)pthread_mutex_unlock(m); } };

auto SharedFrameReader::wait_next(std::chrono::milliseconds timeout)
    -> std::expected<SharedFrame, std::string>
{
    if (!state_ || !state_->is_open || !state_->ring_ref)
        return std::unexpected{"Reader not open"};
    auto* ring = state_->ring_ref.get();

    for (;;) {
        int lr = pthread_mutex_lock(&ring->mutex);
        if (lr != 0) return std::unexpected{std::format("mutex_lock: {}", lr)};
        MutexGuard mg{&ring->mutex};

        uint64_t latest = ring->latest_sequence.load(std::memory_order_acquire);
        const uint64_t last = state_->last_delivered;

        if (timeout.count() == 0) {
            while (latest == last) {
                int wr = pthread_cond_wait(&ring->cond, &ring->mutex);
                if (wr != 0) return std::unexpected{std::format("cond_wait: {}", wr)};
                latest = ring->latest_sequence.load(std::memory_order_acquire);
            }
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            auto total_ns = static_cast<long>(ts.tv_nsec)
                          + static_cast<long>(timeout.count()) * 1'000'000L;
            ts.tv_sec  += static_cast<time_t>(total_ns / 1'000'000'000L);
            ts.tv_nsec  = total_ns % 1'000'000'000L;

            while (latest == last) {
                int wr = pthread_cond_timedwait(&ring->cond, &ring->mutex, &ts);
                if (wr == ETIMEDOUT) {
                    latest = ring->latest_sequence.load(std::memory_order_acquire);
                    break;
                }
                if (wr != 0) return std::unexpected{std::format("cond_timedwait: {}", wr)};
                latest = ring->latest_sequence.load(std::memory_order_acquire);
            }

            if (latest == last)
                return std::unexpected{"Timeout waiting for next frame"};
        }

        const uint64_t target_seq = latest;
        const int slot_idx = static_cast<int>((target_seq - 1) % kShmSlotCount);
        auto& slot = ring->slots[slot_idx];

        // Release mutex before rdlock to avoid holding both.
        mg.m = nullptr;
        pthread_mutex_unlock(&ring->mutex);

        int rr = pthread_rwlock_rdlock(&slot.lock);
        if (rr != 0) return std::unexpected{std::format("rwlock_rdlock: {}", rr)};

        const auto& meta = slot.metadata;
        auto v = validate_slot(meta, target_seq);
        if (!v.has_value()) {
            pthread_rwlock_unlock(&slot.lock);
            if (v.error() == "stale") continue;
            return std::unexpected{v.error()};
        }

        const size_t stride = meta.stride_bytes > 0
            ? static_cast<size_t>(meta.stride_bytes) : static_cast<size_t>(meta.width) * 3U;
        cv::Mat view(static_cast<int>(meta.height), static_cast<int>(meta.width),
                     CV_8UC3, slot.pixels, stride);

        auto lease = std::make_shared<SharedFrame::Lease>();
        lease->ring_ref = state_->ring_ref;
        lease->slot_index = slot_idx;
        state_->last_delivered = target_seq;

        return SharedFrame{std::move(lease), meta, view};
    }
}

auto SharedFrameReader::try_next() -> std::optional<SharedFrame> {
    if (!state_ || !state_->is_open || !state_->ring_ref) return std::nullopt;
    auto* ring = state_->ring_ref.get();

    int lr = pthread_mutex_lock(&ring->mutex);
    if (lr != 0) return std::nullopt;
    uint64_t latest = ring->latest_sequence.load(std::memory_order_acquire);
    const uint64_t last = state_->last_delivered;
    if (latest == last) { pthread_mutex_unlock(&ring->mutex); return std::nullopt; }

    const uint64_t target_seq = latest;
    const int slot_idx = static_cast<int>((target_seq - 1) % kShmSlotCount);
    auto& slot = ring->slots[slot_idx];
    pthread_mutex_unlock(&ring->mutex);

    int rr = pthread_rwlock_rdlock(&slot.lock);
    if (rr != 0) return std::nullopt;

    const auto& meta = slot.metadata;
    auto v = validate_slot(meta, target_seq);
    if (!v.has_value()) { pthread_rwlock_unlock(&slot.lock); return std::nullopt; }

    const size_t stride = meta.stride_bytes > 0
        ? static_cast<size_t>(meta.stride_bytes) : static_cast<size_t>(meta.width) * 3U;
    cv::Mat view(static_cast<int>(meta.height), static_cast<int>(meta.width),
                 CV_8UC3, slot.pixels, stride);

    auto lease = std::make_shared<SharedFrame::Lease>();
    lease->ring_ref = state_->ring_ref;
    lease->slot_index = slot_idx;
    state_->last_delivered = target_seq;
    return SharedFrame{std::move(lease), meta, view};
}

auto SharedFrameReader::is_open() const noexcept -> bool { return state_ && state_->is_open; }
auto SharedFrameReader::last_delivered_sequence() const noexcept -> uint64_t {
    return state_ ? state_->last_delivered : 0;
}

}  // namespace hikcamera
