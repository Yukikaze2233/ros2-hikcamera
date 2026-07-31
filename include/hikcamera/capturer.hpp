#pragma once
#include <chrono>
#include <cstdint>
#include <expected>
#include <span>
#include <opencv2/core/mat.hpp>

namespace hikcamera {
static constexpr auto kMaxGain = float { 16.9807 };

struct Config {
    int timeout_ms = 2000;

    float exposure_us       = 2000.;
    float framerate         = 80;
    float gain              = kMaxGain;
    int auto_white_balance  = 0;
    int white_balance_red   = 512;
    int white_balance_green = 512;
    int white_balance_blue  = 512;

    int width  = 1920;
    int height = 1080;

    bool invert_image  = false;
    bool software_sync = false;

    bool trigger_mode    = false;
    bool fixed_framerate = true;

    // 相机序列号 (SN)。非空时按序列号在多台相机中挑选目标设备；
    // 为空则保持旧行为：仅当恰好枚举到 1 台时使用它。
    std::string device_serial;
};

class Camera {
public:
    struct Image {
        using Clock = std::chrono::steady_clock;
        using Stamp = Clock::time_point;

        cv::Mat  mat;
        Stamp    timestamp;             // host monotonic time at conversion

        uint64_t frame_id{0};            // device frame counter
        uint64_t device_timestamp_ticks{0};
        uint64_t host_monotonic_ns{0};   // steady_clock ns at conversion
        uint32_t exposure_us{0};         // device exposure, microseconds
    };

    explicit Camera() noexcept;
    ~Camera() noexcept;

    Camera(const Camera&)            = delete;
    Camera& operator=(const Camera&) = delete;

    auto configure(const Config&) noexcept -> void;

    auto connect() noexcept -> std::expected<void, std::string>;

    auto disconnect() noexcept -> std::expected<void, std::string>;

    auto connected() const noexcept -> bool;

    /// @note: It takes time and will return before timeout.
    auto read_image() noexcept -> std::expected<cv::Mat, std::string>;

    /// Convert Bayer→BGR8 directly into dst_buffer (zero-copy for the caller).
    /// Returns Image with cv::Mat wrapping dst_buffer and host-side timestamp.
    auto read_image_with_timestamp(std::span<unsigned char> dst_buffer) noexcept
        -> std::expected<Image, std::string>;

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
