#include "capturer.impl.hpp"

namespace hikcamera {

auto select_device_index(const std::vector<DeviceInfo>& devices, std::string_view device_id)
    -> std::expected<std::size_t, std::string> {
    if (devices.empty())
        return std::unexpected("No device was found");

    if (device_id.empty()) {
        if (devices.size() == 1)
            return std::size_t{0};
        return std::unexpected(
            std::format("Found {} devices; device_id is required to disambiguate", devices.size()));
    }

    for (std::size_t index = 0; index < devices.size(); ++index) {
        if (util::compare(devices[index], device_id))
            return index;
    }

    return std::unexpected(std::format("No device matches '{}'", device_id));
}

auto Camera::configure(const Config& config) noexcept -> void { pimpl->configure(config); }

auto Camera::connect() noexcept -> std::expected<void, std::string> { return pimpl->connect(); }

auto Camera::connected() const noexcept -> bool { return pimpl->camera_handler != nullptr; }

auto Camera::device_info() const noexcept -> const std::optional<DeviceInfo>& {
    return pimpl->device_info;
}

auto Camera::stream_format() const noexcept -> const std::optional<StreamFormat>& {
    return pimpl->stream_format;
}

auto Camera::disconnect() noexcept -> std::expected<void, std::string> {
    return pimpl->disconnect();
}

auto Camera::read_image() noexcept -> std::expected<cv::Mat, std::string> {
    return pimpl->read_image();
}
auto Camera::read_image_with_timestamp() noexcept -> std::expected<Image, std::string> {
    return pimpl->read_image_with_timestamp();
}

Camera::Camera() noexcept
    : pimpl{std::make_unique<Impl>()} {}

Camera::~Camera() noexcept = default;

} // namespace hikcamera
