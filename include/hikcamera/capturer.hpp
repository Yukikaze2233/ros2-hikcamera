#pragma once
#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <opencv2/core/mat.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace hikcamera {
static constexpr auto kMaxGain = float{16.9807};

struct Config {
    std::string device_id{};
    unsigned int timeout_ms = 2000;

    float exposure_us = 2000.;
    float framerate = 80;
    float gain = kMaxGain;

    bool invert_image = false;
    bool software_sync = false;

    bool trigger_mode = false;
    bool fixed_framerate = true;
};

struct DeviceInfo {
    std::string device_id{};
    std::string user_defined_name{};
    std::string serial_number{};
    std::string model_name{};
    std::string transport_layer{};
};

struct StreamFormat {
    int width = 0;
    int height = 0;
    double framerate = 0.0;
    std::string pixel_format_name{};
    std::string source_pixel_format_name{};
};

auto select_device_index(const std::vector<DeviceInfo>& devices, std::string_view device_id)
    -> std::expected<std::size_t, std::string>;

class Camera {
public:
    struct Image {
        using Clock = std::chrono::steady_clock;
        using Stamp = Clock::time_point;

        cv::Mat mat;
        Stamp timestamp;
    };

    explicit Camera() noexcept;
    ~Camera() noexcept;

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    auto configure(const Config&) noexcept -> void;

    auto connect() noexcept -> std::expected<void, std::string>;

    auto disconnect() noexcept -> std::expected<void, std::string>;

    auto connected() const noexcept -> bool;
    [[nodiscard]] auto device_info() const noexcept -> const std::optional<DeviceInfo>&;
    [[nodiscard]] auto stream_format() const noexcept -> const std::optional<StreamFormat>&;

    /// @note: It takes time and will return before timeout.
    auto read_image() noexcept -> std::expected<cv::Mat, std::string>;
    auto read_image_with_timestamp() noexcept -> std::expected<Image, std::string>;

    /// Alias
    ///
    auto initialize(const Config& config) noexcept -> std::expected<void, std::string> {
        configure(config);
        return connect();
    }
    auto deinitialize() noexcept -> std::expected<void, std::string> { return disconnect(); }

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace hikcamera
