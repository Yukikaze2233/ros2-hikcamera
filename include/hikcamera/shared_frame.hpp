#pragma once

#include "hikcamera/shm_types.hpp"

#include <memory>
#include <opencv2/core/mat.hpp>

namespace hikcamera {

/// Copyable RAII read-lease on one SHM slot.
///
/// The slot stays read-locked while at least one SharedFrame copy exists.
/// Copying is cheap (shared_ptr).  The lease survives the SharedFrameReader
/// that produced it — the mapping is reference-counted separately.
///
/// mat() returns a non-owning cv::Mat header directly over the SHM pixels.
///
/// SHORT-LIVED: the underlying slot may be overwritten by the writer once
/// the ring wraps (after 4 subsequent frames).  Do not hold a SharedFrame
/// longer than one processing iteration — copy pixels out if you need them
/// beyond the current frame cycle.
class SharedFrame {
public:
    SharedFrame() = default;
    ~SharedFrame();

    // Copyable — increments shared ownership of the underlying lease.
    SharedFrame(const SharedFrame&)            = default;
    SharedFrame& operator=(const SharedFrame&) = default;

    SharedFrame(SharedFrame&&)            noexcept = default;
    SharedFrame& operator=(SharedFrame&&) noexcept = default;

    [[nodiscard]] auto valid() const noexcept -> bool;
    [[nodiscard]] auto metadata() const noexcept -> const FrameMetadata&;
    [[nodiscard]] auto mat() const noexcept -> cv::Mat;   // BGR8 non-owning view
    [[nodiscard]] auto sequence() const noexcept -> uint64_t;

private:
    friend class SharedFrameReader;
    struct Lease {
        std::shared_ptr<SharedRingHeader> ring_ref;  // keeps mapping alive
        int                               slot_index{-1};
        ~Lease() {
            if (ring_ref && slot_index >= 0)
                pthread_rwlock_unlock(&ring_ref->slots[slot_index].lock);
        }
    };

    explicit SharedFrame(std::shared_ptr<Lease> lease,
                         FrameMetadata meta, cv::Mat view) noexcept;

    std::shared_ptr<Lease> lease_;
    FrameMetadata           metadata_{};
    cv::Mat                 mat_view_{};
};

}  // namespace hikcamera
