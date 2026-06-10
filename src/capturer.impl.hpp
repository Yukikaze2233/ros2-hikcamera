#pragma once
#include "hikcamera/capturer.hpp"

#include "errors.hpp"
#include "utility.hpp"

#include <cstring>
#include <expected>
#include <filesystem>
#include <map>
#include <mutex>

using namespace hikcamera;

// =============================================================================
// Helper: MVS enum ↔ public enum mappings
// =============================================================================

namespace {

constexpr auto to_mvs_exposure_auto(auto_mode m) -> unsigned int {
    switch (m) {
    case auto_mode::off: return MV_EXPOSURE_AUTO_MODE_OFF;
    case auto_mode::once: return MV_EXPOSURE_AUTO_MODE_ONCE;
    case auto_mode::continuous: return MV_EXPOSURE_AUTO_MODE_CONTINUOUS;
    }
    return MV_EXPOSURE_AUTO_MODE_OFF;
}

constexpr auto from_mvs_exposure_auto(unsigned int v) -> auto_mode {
    switch (v) {
    case MV_EXPOSURE_AUTO_MODE_ONCE: return auto_mode::once;
    case MV_EXPOSURE_AUTO_MODE_CONTINUOUS: return auto_mode::continuous;
    default: return auto_mode::off;
    }
}

constexpr auto to_mvs_trigger_mode(trigger_mode m) -> unsigned int {
    return (m == trigger_mode::on) ? MV_TRIGGER_MODE_ON : MV_TRIGGER_MODE_OFF;
}

constexpr auto from_mvs_trigger_mode(unsigned int v) -> trigger_mode {
    return (v == MV_TRIGGER_MODE_ON) ? trigger_mode::on : trigger_mode::off;
}

constexpr auto to_mvs_trigger_source(trigger_source s) -> unsigned int {
    switch (s) {
    case trigger_source::line0: return MV_TRIGGER_SOURCE_LINE0;
    case trigger_source::line1: return MV_TRIGGER_SOURCE_LINE1;
    case trigger_source::line2: return MV_TRIGGER_SOURCE_LINE2;
    case trigger_source::line3: return MV_TRIGGER_SOURCE_LINE3;
    case trigger_source::software: return MV_TRIGGER_SOURCE_SOFTWARE;
    }
    return MV_TRIGGER_SOURCE_LINE0;
}

constexpr auto from_mvs_trigger_source(unsigned int v) -> trigger_source {
    switch (v) {
    case MV_TRIGGER_SOURCE_LINE1: return trigger_source::line1;
    case MV_TRIGGER_SOURCE_LINE2: return trigger_source::line2;
    case MV_TRIGGER_SOURCE_LINE3: return trigger_source::line3;
    case MV_TRIGGER_SOURCE_SOFTWARE: return trigger_source::software;
    default: return trigger_source::line0;
    }
}

} // anonymous namespace

// =============================================================================
// Camera::Impl — updated with typed parameter support
// =============================================================================

struct Camera::Impl final {
    using Byte = unsigned char;
    using param_id = detail::parameter_id;
    using param_value = detail::parameter_value;

    constexpr static auto kBufferSize = 5;

    sdk::ConvertParam convert_context;
    sdk::Handler camera_handler;

    std::array<std::vector<Byte>, kBufferSize> buffers;
    std::size_t buffer_size = 0;
    std::size_t buffer_index = 0;

    unsigned int timeout_ms;

    std::optional<Config> config;
    std::optional<DeviceInfo> device_info;
    std::optional<StreamFormat> stream_format;

    // Serialization mutex for connect/disconnect/read/parameter ops
    mutable std::mutex mtx_;

    // Live override cache: after successful set(), cached here; replayed on reconnect()
    std::map<param_id, param_value> live_overrides_;

    ~Impl() noexcept {
        std::ignore = disconnect();
        try {
            std::filesystem::remove_all("./MvSdkLog");
            std::filesystem::remove_all("./MvFGSdkLog");
            std::filesystem::remove_all("./$(ALLUSERSPROFILE)");
        } catch (...) {}
    }

    auto read_image() noexcept -> std::expected<cv::Mat, std::string> {
        std::lock_guard lock{mtx_};

        if (camera_handler == nullptr)
            return std::unexpected{"Attempted to read from an uninitialized camera"};

        auto info = sdk::FrameOut{};
        auto code = std::uint32_t{};

        code = MV_CC_GetImageBuffer(camera_handler, &info, timeout_ms);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to get image buffer", code);
        auto guard =
            util::scope_exit{[&] { std::ignore = MV_CC_FreeImageBuffer(camera_handler, &info); }};

        if (buffer_size == 0) {
            if (auto result = update_convert_context(info); !result) {
                return util::make_unexpected(
                    "Failed to update convert context: {}", result.error());
            }
        }

        convert_context.pSrcData = info.pBufAddr;
        convert_context.pDstBuffer = buffers[fetch_and_update_buffer_index()].data();
        code = MV_CC_ConvertPixelType(camera_handler, &convert_context);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to convert image", code);

        // clone() 确保返回的 cv::Mat 拥有独立内存，不依赖内部循环缓冲区
        return generate_mat(info).clone();
    }

    auto read_image_with_timestamp() noexcept -> std::expected<Image, std::string> {
        if (auto result = read_image()) {
            return Image{
                .mat = result.value(),
                .timestamp = Image::Clock::now(),
            };
        } else {
            return std::unexpected{result.error()};
        }
    }

    auto configure(const Config& _config) noexcept { config = _config; }

    auto connect() -> std::expected<void, std::string> {
        std::lock_guard lock{mtx_};

        if (camera_handler != nullptr) {
            std::ignore = disconnect_impl();
        }
        device_info.reset();
        stream_format.reset();

        if (!config.has_value()) {
            return std::unexpected{"Need configure"};
        }

        auto code = std::uint32_t{};

        timeout_ms = config->timeout_ms;
        auto guard_handler = util::scope_exit{[this] { camera_handler = nullptr; }};

        auto devices = util::enumerate_devices();
        if (!devices)
            return std::unexpected{devices.error()};

        auto device_result = util::select_device_pointer(*devices, config->device_id);
        if (!device_result)
            return std::unexpected{device_result.error()};

        auto* device = *device_result;
        if (device == nullptr)
            return std::unexpected{"Null device was got by searching"};
        device_info = util::device_info_from_sdk(*device);

        // Create and open device
        if (sdk::OK != (code = MV_CC_CreateHandleWithoutLog(&camera_handler, device)))
            return util::make_unexpected_with_error("Failed to create handler", code);
        auto guard_destroy = util::scope_exit{[this] { MV_CC_DestroyHandle(camera_handler); }};

        if (sdk::OK != (code = MV_CC_OpenDevice(camera_handler)))
            return util::make_unexpected_with_error("Failed to open device", code);
        auto guard_close = util::scope_exit{[this] { MV_CC_CloseDevice(camera_handler); }};

        if (device->nTLayerType == MV_GIGE_DEVICE) {
            auto size = MV_CC_GetOptimalPacketSize(camera_handler);
            if (size <= 0)
                return std::unexpected{"Invalid packet size"};

            if (auto ret = set(sdk::key::GevSCPSPacketSize, size); !ret)
                return std::unexpected{"Failed to set packet size | " + ret.error()};
        }

        // Fixed initialize method
        {
            if (sdk::OK != (code = MV_CC_SetBayerCvtQuality(camera_handler, 2)))
                return util::make_unexpected_with_error("Failed to set bayer cvt quality", code);

            if (auto ret = set(sdk::key::ExposureAuto, sdk::ExposureAutoMode::OFF); !ret)
                return std::unexpected{ret.error()};
        }

        // initialize using config
        {
            if (auto ret = set(sdk::key::ReverseX, config->invert_image); !ret)
                return std::unexpected{ret.error()};

            if (auto ret = set(sdk::key::ReverseY, config->invert_image); !ret)
                return std::unexpected{ret.error()};

            if (auto ret = set(sdk::key::ExposureTime, config->exposure_us); !ret)
                return std::unexpected{ret.error()};

            if (auto ret = set(sdk::key::Gain, config->gain); !ret)
                return std::unexpected{ret.error()};

            auto trigger_mode_val =
                config->trigger_mode ? sdk::TriggerMode::ON : sdk::TriggerMode::OFF;
            if (auto ret = set(sdk::key::TriggerMode, trigger_mode_val); !ret)
                return std::unexpected{ret.error()};

            if (config->software_sync)
                if (auto ret = set(sdk::key::TriggerSource, sdk::TriggerSource::SOFTWARE); !ret)
                    return std::unexpected{ret.error()};

            if (config->fixed_framerate) {
                if (auto ret = set(sdk::key::AcquisitionFrameRateEnable, true); !ret)
                    return std::unexpected{ret.error()};

                if (auto ret = set(sdk::key::AcquisitionFrameRate, config->framerate); !ret)
                    return std::unexpected{ret.error()};
            } else {
                if (auto ret = set(sdk::key::AcquisitionFrameRateEnable, false); !ret)
                    return std::unexpected{ret.error()};
            }
        }

        // Replay live overrides in fixed order
        if (!live_overrides_.empty()) {
            constexpr param_id replay_order[] = {
                param_id::reverse_x,
                param_id::reverse_y,
                param_id::exposure_auto,
                param_id::exposure_time_us,
                param_id::gain_auto,
                param_id::gain,
                param_id::white_balance_auto,
                param_id::white_balance_ratio_red,
                param_id::white_balance_ratio_green,
                param_id::white_balance_ratio_blue,
                param_id::gamma,
                param_id::trigger_mode,
                param_id::trigger_source,
                param_id::trigger_delay,
                param_id::frame_rate_enabled,
                param_id::frame_rate_fps,
            };
            for (auto id : replay_order) {
                if (auto it = live_overrides_.find(id); it != live_overrides_.end()) {
                    if (auto ret = set_parameter_core(id, it->second); !ret) {
                        // Log but don't fail the connect
                    }
                }
            }
        }

        if (auto format = query_stream_format(); !format)
            return std::unexpected{"Failed to query stream format | " + format.error()};
        else
            stream_format = std::move(*format);

        // Start grabbing image
        if (sdk::OK != (code = MV_CC_StartGrabbing(camera_handler)))
            return util::make_unexpected_with_error("Failed to start grabbing", code);

        guard_close.release();
        guard_destroy.release();
        guard_handler.release();

        return {};
    }

    auto disconnect() noexcept -> std::expected<void, std::string> {
        std::lock_guard lock{mtx_};
        return disconnect_impl();
    }

    // =========================================================================
    // Parameter core methods
    // =========================================================================

    auto get_parameter_core(param_id id) const -> std::expected<param_value, std::string> {
        std::lock_guard lock{mtx_};
        if (camera_handler == nullptr)
            return std::unexpected{"Camera not connected"};

        switch (id) {
        case param_id::exposure_time_us: return get_generic_float("ExposureTime");
        case param_id::exposure_auto: return get_exposure_auto();
        case param_id::gain: return get_gain();
        case param_id::gain_auto: return get_gain_auto();
        case param_id::white_balance_auto: return get_white_balance_auto();
        case param_id::white_balance_ratio_red: return get_balance_ratio_red();
        case param_id::white_balance_ratio_green: return get_balance_ratio_green();
        case param_id::white_balance_ratio_blue: return get_balance_ratio_blue();
        case param_id::gamma: return get_gamma();
        case param_id::trigger_mode: return get_trigger_mode();
        case param_id::trigger_source: return get_trigger_source();
        case param_id::trigger_delay: return get_trigger_delay();
        case param_id::frame_rate_enabled: return get_generic_bool("AcquisitionFrameRateEnable");
        case param_id::frame_rate_fps: return get_generic_float("AcquisitionFrameRate");
        case param_id::reverse_x: return get_generic_bool("ReverseX");
        case param_id::reverse_y: return get_generic_bool("ReverseY");
        case param_id::software_trigger: return std::unexpected{"software_trigger is execute-only"};
        }
        return std::unexpected{"Unknown parameter"};
    }

    auto set_parameter_core(param_id id, param_value val) -> std::expected<void, std::string> {
        std::lock_guard lock{mtx_};
        if (camera_handler == nullptr)
            return std::unexpected{"Camera not connected"};

        auto result = set_parameter_core_impl(id, val);
        if (result) {
            live_overrides_[id] = val;

            // Refresh frame rate cache if frame_rate_fps or frame_rate_enabled was set
            if (id == param_id::frame_rate_fps || id == param_id::frame_rate_enabled) {
                if (auto fmt = query_stream_format()) {
                    stream_format = std::move(*fmt);
                }
            }
        }
        return result;
    }

    auto describe_parameter_core(param_id id) const -> detail::parameter_metadata {
        std::lock_guard lock{mtx_};

        auto meta = detail::parameter_metadata{};
        meta.mode = update_mode::live;

        if (camera_handler == nullptr) {
            meta.name = "unknown";
            return meta;
        }

        switch (id) {
        case param_id::exposure_time_us:
            return describe_generic_float(id, "ExposureTime", "exposure_time_us");
        case param_id::exposure_auto:
            meta.name = "exposure_auto";
            meta.current = param_value{get_exposure_auto_value()};
            meta.supported_values = {
                param_value{auto_mode::off},
                param_value{auto_mode::once},
                param_value{auto_mode::continuous},
            };
            meta.supported_symbolics = {"off", "once", "continuous"};
            return meta;
        case param_id::gain:
            return describe_parameter_gain();
        case param_id::gain_auto:
            meta.name = "gain_auto";
            meta.current = param_value{get_gain_auto_value()};
            meta.supported_values = {
                param_value{auto_mode::off},
                param_value{auto_mode::once},
                param_value{auto_mode::continuous},
            };
            meta.supported_symbolics = {"off", "once", "continuous"};
            return meta;
        case param_id::white_balance_auto:
            meta.name = "white_balance_auto";
            meta.current = param_value{get_white_balance_auto_value()};
            meta.supported_values = {
                param_value{auto_mode::off},
                param_value{auto_mode::once},
                param_value{auto_mode::continuous},
            };
            meta.supported_symbolics = {"off", "once", "continuous"};
            return meta;
        case param_id::white_balance_ratio_red:
            return describe_balance_ratio(
                id, "white_balance_ratio_red",
                [this] { return get_balance_ratio_red_value(); });
        case param_id::white_balance_ratio_green:
            return describe_balance_ratio(
                id, "white_balance_ratio_green",
                [this] { return get_balance_ratio_green_value(); });
        case param_id::white_balance_ratio_blue:
            return describe_balance_ratio(
                id, "white_balance_ratio_blue",
                [this] { return get_balance_ratio_blue_value(); });
        case param_id::gamma:
            return describe_parameter_gamma();
        case param_id::trigger_mode:
            meta.name = "trigger_mode";
            meta.current = param_value{get_trigger_mode_value()};
            meta.supported_values = {param_value{trigger_mode::off}, param_value{trigger_mode::on}};
            meta.supported_symbolics = {"off", "on"};
            return meta;
        case param_id::trigger_source: {
            meta.name = "trigger_source";
            meta.current = param_value{get_trigger_source_value()};
            // Build supported sources from camera
            auto supported = get_supported_trigger_sources();
            for (auto s : supported) {
                meta.supported_values.push_back(param_value{s});
                meta.supported_symbolics.push_back(trigger_source_to_string(s));
            }
            return meta;
        }
        case param_id::trigger_delay:
            return describe_parameter_trigger_delay();
        case param_id::frame_rate_enabled:
            return describe_generic_bool(id, "AcquisitionFrameRateEnable", "frame_rate_enabled");
        case param_id::frame_rate_fps:
            return describe_generic_float(id, "AcquisitionFrameRate", "frame_rate_fps");
        case param_id::reverse_x:
            return describe_generic_bool(id, "ReverseX", "reverse_x");
        case param_id::reverse_y:
            return describe_generic_bool(id, "ReverseY", "reverse_y");
        case param_id::software_trigger:
            meta.name = "software_trigger";
            return meta;
        }
        meta.name = "unknown";
        return meta;
    }

    auto execute_parameter_core(param_id id) -> std::expected<void, std::string> {
        std::lock_guard lock{mtx_};
        if (camera_handler == nullptr)
            return std::unexpected{"Camera not connected"};

        switch (id) {
        case param_id::software_trigger: {
            auto code = MV_CC_TriggerSoftwareExecute(camera_handler);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to execute software trigger", code);
            return {};
        }
        default: return std::unexpected{"Parameter does not support execute"};
        }
    }

private:
    // ---- Disconnect helper (caller holds mtx_) ----

    auto disconnect_impl() noexcept -> std::expected<void, std::string> {
        auto on_exit = util::scope_exit{[this] {
            camera_handler = nullptr;
            stream_format.reset();
        }};

        if (camera_handler == nullptr)
            return {};

        if (auto ret = MV_CC_StopGrabbing(camera_handler); ret != sdk::OK)
            return util::make_unexpected_with_error("Failed to stop grabbing:", ret);

        if (auto ret = MV_CC_CloseDevice(camera_handler); ret != sdk::OK)
            return util::make_unexpected_with_error("Failed to close device:", ret);

        if (auto ret = MV_CC_DestroyHandle(camera_handler); ret != sdk::OK)
            return util::make_unexpected_with_error("Failed to destory handle:", ret);

        return {};
    }

    // ---- set_parameter_core_impl (caller holds mtx_) ----

    auto set_parameter_core_impl(param_id id, param_value val)
        -> std::expected<void, std::string> {
        auto code = std::uint32_t{};

        switch (id) {
        case param_id::exposure_time_us: {
            auto v = std::get<float>(val);
            code = MV_CC_SetFloatValue(camera_handler, "ExposureTime", v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set ExposureTime", code);
            return {};
        }
        case param_id::exposure_auto: {
            auto v = to_mvs_exposure_auto(std::get<auto_mode>(val));
            code = MV_CC_SetExposureAutoMode(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set ExposureAutoMode", code);
            return {};
        }
        case param_id::gain: {
            auto v = std::get<float>(val);
            code = MV_CC_SetGain(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set Gain", code);
            return {};
        }
        case param_id::gain_auto: {
            auto v = to_mvs_exposure_auto(std::get<auto_mode>(val));
            code = MV_CC_SetGainMode(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set GainMode", code);
            return {};
        }
        case param_id::white_balance_auto: {
            auto v = to_mvs_exposure_auto(std::get<auto_mode>(val));
            code = MV_CC_SetBalanceWhiteAuto(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set BalanceWhiteAuto", code);
            return {};
        }
        case param_id::white_balance_ratio_red: {
            auto v = static_cast<unsigned int>(std::get<int>(val));
            code = MV_CC_SetBalanceRatioRed(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set BalanceRatioRed", code);
            return {};
        }
        case param_id::white_balance_ratio_green: {
            auto v = static_cast<unsigned int>(std::get<int>(val));
            code = MV_CC_SetBalanceRatioGreen(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set BalanceRatioGreen", code);
            return {};
        }
        case param_id::white_balance_ratio_blue: {
            auto v = static_cast<unsigned int>(std::get<int>(val));
            code = MV_CC_SetBalanceRatioBlue(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set BalanceRatioBlue", code);
            return {};
        }
        case param_id::gamma: {
            // Must set gamma selector to User before writing gamma value
            code = MV_CC_SetGammaSelector(camera_handler, MV_GAMMA_SELECTOR_USER);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set GammaSelector(User)", code);
            code = MV_CC_SetGamma(camera_handler, std::get<float>(val));
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set Gamma", code);
            return {};
        }
        case param_id::trigger_mode: {
            auto v = to_mvs_trigger_mode(std::get<trigger_mode>(val));
            code = MV_CC_SetTriggerMode(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set TriggerMode", code);
            return {};
        }
        case param_id::trigger_source: {
            auto v = to_mvs_trigger_source(std::get<trigger_source>(val));
            code = MV_CC_SetTriggerSource(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set TriggerSource", code);
            return {};
        }
        case param_id::trigger_delay: {
            auto v = std::get<float>(val);
            code = MV_CC_SetTriggerDelay(camera_handler, v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set TriggerDelay", code);
            return {};
        }
        case param_id::frame_rate_enabled: {
            auto v = std::get<bool>(val);
            code = MV_CC_SetBoolValue(camera_handler, "AcquisitionFrameRateEnable", v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error(
                    "Failed to set AcquisitionFrameRateEnable", code);
            return {};
        }
        case param_id::frame_rate_fps: {
            auto v = std::get<float>(val);
            code = MV_CC_SetFloatValue(camera_handler, "AcquisitionFrameRate", v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set AcquisitionFrameRate", code);
            return {};
        }
        case param_id::reverse_x: {
            auto v = std::get<bool>(val);
            code = MV_CC_SetBoolValue(camera_handler, "ReverseX", v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set ReverseX", code);
            return {};
        }
        case param_id::reverse_y: {
            auto v = std::get<bool>(val);
            code = MV_CC_SetBoolValue(camera_handler, "ReverseY", v);
            if (code != sdk::OK)
                return util::make_unexpected_with_error("Failed to set ReverseY", code);
            return {};
        }
        case param_id::software_trigger:
            return std::unexpected{"software_trigger uses execute, not set"};
        }
        return std::unexpected{"Unknown parameter"};
    }

    // ---- Generic get helpers ----

    auto get_generic_float(const char* key) const -> std::expected<param_value, std::string> {
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetFloatValue(camera_handler, key, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error(
                std::format("Failed to get float '{}'", key), code);
        return param_value{static_cast<float>(val.fCurValue)};
    }

    auto get_generic_bool(const char* key) const -> std::expected<param_value, std::string> {
        bool val = false;
        auto code = MV_CC_GetBoolValue(camera_handler, key, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error(
                std::format("Failed to get bool '{}'", key), code);
        return param_value{val};
    }

    // ---- Exposure auto ----

    auto get_exposure_auto() const -> std::expected<param_value, std::string> {
        return param_value{get_exposure_auto_value()};
    }

    auto get_exposure_auto_value() const -> auto_mode {
        auto val = MVCC_ENUMVALUE{};
        if (MV_CC_GetExposureAutoMode(const_cast<Impl*>(this)->camera_handler, &val) != sdk::OK)
            return auto_mode::off;
        return from_mvs_exposure_auto(val.nCurValue);
    }

    // ---- Gain ----

    auto get_gain() const -> std::expected<param_value, std::string> {
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetGain(const_cast<Impl*>(this)->camera_handler, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to get Gain", code);
        return param_value{static_cast<float>(val.fCurValue)};
    }

    // ---- Gain auto ----

    auto get_gain_auto() const -> std::expected<param_value, std::string> {
        return param_value{get_gain_auto_value()};
    }

    auto get_gain_auto_value() const -> auto_mode {
        auto val = MVCC_ENUMVALUE{};
        if (MV_CC_GetGainMode(const_cast<Impl*>(this)->camera_handler, &val) != sdk::OK)
            return auto_mode::off;
        return from_mvs_exposure_auto(val.nCurValue);
    }

    // ---- White balance auto ----

    auto get_white_balance_auto() const -> std::expected<param_value, std::string> {
        return param_value{get_white_balance_auto_value()};
    }

    auto get_white_balance_auto_value() const -> auto_mode {
        auto val = MVCC_ENUMVALUE{};
        if (MV_CC_GetBalanceWhiteAuto(const_cast<Impl*>(this)->camera_handler, &val) != sdk::OK)
            return auto_mode::off;
        return from_mvs_exposure_auto(val.nCurValue);
    }

    // ---- Balance ratios ----

    auto get_balance_ratio_red() const -> std::expected<param_value, std::string> {
        auto result = get_balance_ratio_red_value();
        if (!result) return std::unexpected(result.error());
        return param_value{*result};
    }

    auto get_balance_ratio_red_value() const -> std::expected<int, std::string> {
        auto val = MVCC_INTVALUE{};
        auto code =
            MV_CC_GetBalanceRatioRed(const_cast<Impl*>(this)->camera_handler, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to get BalanceRatioRed", code);
        return static_cast<int>(val.nCurValue);
    }

    auto get_balance_ratio_green() const -> std::expected<param_value, std::string> {
        auto result = get_balance_ratio_green_value();
        if (!result) return std::unexpected(result.error());
        return param_value{*result};
    }

    auto get_balance_ratio_green_value() const -> std::expected<int, std::string> {
        auto val = MVCC_INTVALUE{};
        auto code =
            MV_CC_GetBalanceRatioGreen(const_cast<Impl*>(this)->camera_handler, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to get BalanceRatioGreen", code);
        return static_cast<int>(val.nCurValue);
    }

    auto get_balance_ratio_blue() const -> std::expected<param_value, std::string> {
        auto result = get_balance_ratio_blue_value();
        if (!result) return std::unexpected(result.error());
        return param_value{*result};
    }

    auto get_balance_ratio_blue_value() const -> std::expected<int, std::string> {
        auto val = MVCC_INTVALUE{};
        auto code =
            MV_CC_GetBalanceRatioBlue(const_cast<Impl*>(this)->camera_handler, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to get BalanceRatioBlue", code);
        return static_cast<int>(val.nCurValue);
    }

    // ---- Gamma ----

    auto get_gamma() const -> std::expected<param_value, std::string> {
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetGamma(const_cast<Impl*>(this)->camera_handler, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to get Gamma", code);
        return param_value{static_cast<float>(val.fCurValue)};
    }

    // ---- Trigger mode ----

    auto get_trigger_mode() const -> std::expected<param_value, std::string> {
        return param_value{get_trigger_mode_value()};
    }

    auto get_trigger_mode_value() const -> trigger_mode {
        auto val = MVCC_ENUMVALUE{};
        if (MV_CC_GetTriggerMode(const_cast<Impl*>(this)->camera_handler, &val) != sdk::OK)
            return trigger_mode::off;
        return from_mvs_trigger_mode(val.nCurValue);
    }

    // ---- Trigger source ----

    auto get_trigger_source() const -> std::expected<param_value, std::string> {
        return param_value{get_trigger_source_value()};
    }

    auto get_trigger_source_value() const -> trigger_source {
        auto val = MVCC_ENUMVALUE{};
        if (MV_CC_GetTriggerSource(const_cast<Impl*>(this)->camera_handler, &val) != sdk::OK)
            return trigger_source::line0;
        return from_mvs_trigger_source(val.nCurValue);
    }

    // ---- Trigger delay ----

    auto get_trigger_delay() const -> std::expected<param_value, std::string> {
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetTriggerDelay(const_cast<Impl*>(this)->camera_handler, &val);
        if (code != sdk::OK)
            return util::make_unexpected_with_error("Failed to get TriggerDelay", code);
        return param_value{static_cast<float>(val.fCurValue)};
    }

    // ---- Supported trigger sources ----

    auto get_supported_trigger_sources() const -> std::vector<trigger_source> {
        // v1: returns static enumeration of known trigger sources.
        // Future: query camera via MV_CC_GetEnumValue("TriggerSource") for
        // actual hardware-supported set.
        std::vector<trigger_source> result;
        constexpr trigger_source all[] = {
            trigger_source::line0, trigger_source::line1,
            trigger_source::line2, trigger_source::line3,
            trigger_source::software,
        };
        for (auto s : all) {
            result.push_back(s);
        }
        return result;
    }

    static auto trigger_source_to_string(trigger_source s) -> std::string {
        switch (s) {
        case trigger_source::line0: return "Line0";
        case trigger_source::line1: return "Line1";
        case trigger_source::line2: return "Line2";
        case trigger_source::line3: return "Line3";
        case trigger_source::software: return "Software";
        }
        return "Unknown";
    }

    // ---- Describe helpers ----

    template <typename F>
    auto describe_balance_ratio(param_id /*id*/, std::string name, F getter) const
        -> detail::parameter_metadata {
        auto meta = detail::parameter_metadata{};
        meta.name = std::move(name);
        auto cur = getter();
        if (cur) {
            meta.current = param_value{*cur};
        } else {
            meta.current = param_value{0};
        }
        meta.min = param_value{0};
        meta.max = param_value{65535};
        meta.step = param_value{1};
        return meta;
    }

    auto describe_generic_float(param_id /*id*/, const char* key, std::string name) const
        -> detail::parameter_metadata {
        auto meta = detail::parameter_metadata{};
        meta.name = std::move(name);
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetFloatValue(const_cast<Impl*>(this)->camera_handler, key, &val);
        if (code == sdk::OK) {
            meta.current = param_value{static_cast<float>(val.fCurValue)};
            meta.min = param_value{static_cast<float>(val.fMin)};
            meta.max = param_value{static_cast<float>(val.fMax)};
        }
        return meta;
    }

    auto describe_generic_bool(param_id /*id*/, const char* key, std::string name) const
        -> detail::parameter_metadata {
        auto meta = detail::parameter_metadata{};
        meta.name = std::move(name);
        bool val = false;
        auto code = MV_CC_GetBoolValue(const_cast<Impl*>(this)->camera_handler, key, &val);
        if (code == sdk::OK) {
            meta.current = param_value{val};
        }
        return meta;
    }

    auto describe_parameter_gain() const -> detail::parameter_metadata {
        auto meta = detail::parameter_metadata{};
        meta.name = "gain";
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetGain(const_cast<Impl*>(this)->camera_handler, &val);
        if (code == sdk::OK) {
            meta.current = param_value{static_cast<float>(val.fCurValue)};
            meta.min = param_value{static_cast<float>(val.fMin)};
            meta.max = param_value{static_cast<float>(val.fMax)};
        }
        return meta;
    }

    auto describe_parameter_gamma() const -> detail::parameter_metadata {
        auto meta = detail::parameter_metadata{};
        meta.name = "gamma";
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetGamma(const_cast<Impl*>(this)->camera_handler, &val);
        if (code == sdk::OK) {
            meta.current = param_value{static_cast<float>(val.fCurValue)};
            meta.min = param_value{static_cast<float>(val.fMin)};
            meta.max = param_value{static_cast<float>(val.fMax)};
        }
        return meta;
    }

    auto describe_parameter_trigger_delay() const -> detail::parameter_metadata {
        auto meta = detail::parameter_metadata{};
        meta.name = "trigger_delay";
        auto val = MVCC_FLOATVALUE{};
        auto code = MV_CC_GetTriggerDelay(const_cast<Impl*>(this)->camera_handler, &val);
        if (code == sdk::OK) {
            meta.current = param_value{static_cast<float>(val.fCurValue)};
            meta.min = param_value{static_cast<float>(val.fMin)};
            meta.max = param_value{static_cast<float>(val.fMax)};
        }
        return meta;
    }

    // ---- Existing helpers (unchanged) ----

    auto fetch_and_update_buffer_index() noexcept -> std::size_t {
        const auto current = buffer_index;
        buffer_index = (buffer_index + 1) % kBufferSize;
        return current;
    }

    template <typename T>
    auto set(char const* key, T value) noexcept -> std::expected<void, std::string> {
        if (camera_handler == nullptr) {
            return std::unexpected{"Camera has not been initialized"};
        }

        auto result = std::uint32_t{};
        auto printable = std::string{};
        /*  */ if constexpr (std::same_as<T, bool>) {
            result = MV_CC_SetBoolValue(camera_handler, key, value);
            printable = std::to_string(value);
        } else if constexpr (std::same_as<T, int>) {
            result = MV_CC_SetIntValue(camera_handler, key, value);
            printable = std::to_string(value);
        } else if constexpr (std::floating_point<T>) {
            result = MV_CC_SetFloatValue(camera_handler, key, value);
            printable = std::to_string(value);
        } else if constexpr (std::is_enum_v<T>) {
            result = MV_CC_SetEnumValue(camera_handler, key, std::to_underlying(value));
            printable = "enum underlying " + std::to_string(std::to_underlying(value));
        } else {
            static_assert(false, "Unknown type of value");
        }

        if (result != sdk::OK) {
            const auto translated = translate_error(result);
            return util::make_unexpected(
                "Failed to set '{}' with {}: {}", key, printable, translated);
        }
        return {};
    }

    auto get_int(const char* key) const noexcept -> std::expected<std::int64_t, std::string> {
        if (camera_handler == nullptr)
            return std::unexpected{"Camera has not been initialized"};

        auto value = MVCC_INTVALUE_EX{};
        if (const auto code = MV_CC_GetIntValueEx(camera_handler, key, &value); code != sdk::OK)
            return util::make_unexpected_with_error(
                std::format("Failed to get int '{}'", key), code);
        return value.nCurValue;
    }

    auto get_float(const char* key) const noexcept -> std::expected<double, std::string> {
        if (camera_handler == nullptr)
            return std::unexpected{"Camera has not been initialized"};

        auto value = MVCC_FLOATVALUE{};
        if (const auto code = MV_CC_GetFloatValue(camera_handler, key, &value); code != sdk::OK)
            return util::make_unexpected_with_error(
                std::format("Failed to get float '{}'", key), code);
        return value.fCurValue;
    }

    auto get_enum_symbolic(const char* key) const noexcept -> std::expected<std::string, std::string> {
        if (camera_handler == nullptr)
            return std::unexpected{"Camera has not been initialized"};

        auto value = MVCC_ENUMVALUE{};
        if (const auto code = MV_CC_GetEnumValue(camera_handler, key, &value); code != sdk::OK)
            return util::make_unexpected_with_error(
                std::format("Failed to get enum '{}'", key), code);

        auto entry = MVCC_ENUMENTRY{};
        entry.nValue = value.nCurValue;
        if (const auto code = MV_CC_GetEnumEntrySymbolic(camera_handler, key, &entry);
            code != sdk::OK) {
            return util::make_unexpected_with_error(
                std::format("Failed to get enum symbolic '{}'", key), code);
        }
        return std::string(entry.chSymbolic);
    }

    auto query_stream_format() const noexcept -> std::expected<StreamFormat, std::string> {
        const auto width = get_int(sdk::key::Width);
        if (!width)
            return std::unexpected(width.error());

        const auto height = get_int(sdk::key::Height);
        if (!height)
            return std::unexpected(height.error());

        const auto framerate = get_float(sdk::key::AcquisitionFrameRate);
        if (!framerate)
            return std::unexpected(framerate.error());

        const auto source_pixel_format = get_enum_symbolic(sdk::key::PixelFormat);
        if (!source_pixel_format)
            return std::unexpected(source_pixel_format.error());

        return StreamFormat{
            .width = static_cast<int>(*width),
            .height = static_cast<int>(*height),
            .framerate = *framerate,
            .pixel_format_name = "BGR8",
            .source_pixel_format_name = *source_pixel_format,
        };
    }

    auto update_convert_context(sdk::FrameOut const& info) noexcept
        -> std::expected<void, std::string_view> {

        if (!util::is_rgb_pixel_type(info.stFrameInfo.enPixelType)) {
            return std::unexpected{"Camera must has RGB channel"};
        }

        auto& frame_info = info.stFrameInfo;
        buffer_size = frame_info.nWidth * frame_info.nHeight * 3;
        std::ranges::for_each(buffers, [this](auto& buffer) { buffer.resize(buffer_size); });

        convert_context.nWidth = frame_info.nWidth;
        convert_context.nHeight = frame_info.nHeight;
        convert_context.nSrcDataLen = frame_info.nFrameLen;

        convert_context.enSrcPixelType = frame_info.enPixelType;
        convert_context.enDstPixelType = PixelType_Gvsp_BGR8_Packed;

        convert_context.nDstBufferSize = buffer_size;

        return {};
    }

    auto generate_mat(const sdk::FrameOut& source_info) const -> cv::Mat {
        return {
            source_info.stFrameInfo.nHeight,
            source_info.stFrameInfo.nWidth,
            CV_8UC3,
            convert_context.pDstBuffer,
        };
    }
};
