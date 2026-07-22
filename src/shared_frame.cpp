#include "hikcamera/shared_frame.hpp"

namespace hikcamera {

SharedFrame::SharedFrame(std::shared_ptr<Lease> lease,
                         FrameMetadata meta, cv::Mat view) noexcept
    : lease_(std::move(lease)), metadata_(std::move(meta)), mat_view_(std::move(view)) {}

SharedFrame::~SharedFrame() = default;

auto SharedFrame::valid() const noexcept -> bool {
    return lease_ != nullptr && lease_->ring_ref != nullptr;
}

auto SharedFrame::metadata() const noexcept -> const FrameMetadata& {
    return metadata_;
}

auto SharedFrame::mat() const noexcept -> cv::Mat {
    return mat_view_;
}

auto SharedFrame::sequence() const noexcept -> uint64_t {
    return metadata_.committed_sequence;
}

}  // namespace hikcamera
