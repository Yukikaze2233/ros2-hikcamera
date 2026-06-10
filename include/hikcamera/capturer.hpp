#pragma once
#include "hikcamera/parameters.hpp"

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

// =============================================================================
// parameter_ref<Tag> — typed accessor returned by Camera::parameter()
// =============================================================================

// Primary template (for all tags except software_trigger)
template <typename Tag>
class parameter_ref {
public:
    using value_type = typename parameter_traits<Tag>::value_type;

    auto get() -> std::expected<value_type, std::string>;
    auto set(value_type) -> std::expected<void, std::string>;
    auto describe() -> std::expected<parameter_info<value_type>, std::string>;

private:
    friend class Camera;
    Camera& camera_;
    explicit parameter_ref(Camera& cam) noexcept : camera_(cam) {}
};

// Specialization for software_trigger — execute-only, no get/set/describe
template <>
class parameter_ref<param::software_trigger> {
private:
    friend class Camera;
    Camera& camera_;
    explicit parameter_ref(Camera& cam) noexcept : camera_(cam) {}
};

// =============================================================================
// Camera — main camera interface
// =============================================================================

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

    // ---- Typed parameter API ----

    template <typename Tag>
    auto parameter(Tag = {}) -> parameter_ref<Tag> {
        return parameter_ref<Tag>{*this};
    }

    template <typename Tag>
    auto execute(Tag = {}) -> std::expected<void, std::string>;

private:
    template <typename Tag>
    friend class parameter_ref;

    // Non-template dispatch — called by parameter_ref / execute templates
    auto get_parameter_core(detail::parameter_id) const
        -> std::expected<detail::parameter_value, std::string>;
    auto set_parameter_core(detail::parameter_id, detail::parameter_value)
        -> std::expected<void, std::string>;
    auto describe_parameter_core(detail::parameter_id) const -> detail::parameter_metadata;
    auto execute_parameter_core(detail::parameter_id) -> std::expected<void, std::string>;

    struct Impl;
    std::unique_ptr<Impl> pimpl;
};

// =============================================================================
// parameter_ref method definitions (out-of-line templates)
// =============================================================================

template <typename Tag>
auto parameter_ref<Tag>::get() -> std::expected<value_type, std::string> {
    auto result = camera_.get_parameter_core(parameter_traits<Tag>::id);
    if (!result)
        return std::unexpected(result.error());
    return std::get<value_type>(*result);
}

template <typename Tag>
auto parameter_ref<Tag>::set(value_type val) -> std::expected<void, std::string> {
    return camera_.set_parameter_core(parameter_traits<Tag>::id,
                                      detail::parameter_value{val});
}

template <typename Tag>
auto parameter_ref<Tag>::describe() -> std::expected<parameter_info<value_type>, std::string> {
    auto meta = camera_.describe_parameter_core(parameter_traits<Tag>::id);

    parameter_info<value_type> info;
    info.name = std::move(meta.name);
    info.mode = meta.mode;

    info.current = std::get<value_type>(std::move(meta.current));

    if constexpr (std::is_arithmetic_v<value_type> || std::is_enum_v<value_type>) {
        info.min = std::get<value_type>(meta.min);
        info.max = std::get<value_type>(meta.max);
    }
    if constexpr (std::is_integral_v<value_type>) {
        info.step = std::get<value_type>(meta.step);
    }
    if constexpr (std::is_enum_v<value_type>) {
        for (auto& v : meta.supported_values)
            info.supported_values.push_back(std::get<value_type>(v));
        info.supported_symbolics = std::move(meta.supported_symbolics);
    }

    return info;
}

// Camera::execute<Tag> — inline definition
template <typename Tag>
auto Camera::execute(Tag) -> std::expected<void, std::string> {
    return pimpl ? execute_parameter_core(parameter_traits<Tag>::id)
                 : std::unexpected{std::string{"Camera not connected"}};
}

} // namespace hikcamera
