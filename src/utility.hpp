#pragma once
#include "hikcamera/capturer.hpp"
#include "errors.hpp"
#include "MvCameraControl.h"

#include <experimental/scope>

#include <algorithm>
#include <cstring>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace hikcamera::sdk {

constexpr auto OK = MV_OK;

enum class ExposureAutoMode {
    OFF = MV_EXPOSURE_AUTO_MODE_OFF,
    ONCE = MV_EXPOSURE_AUTO_MODE_ONCE,
    CONTINUOUS = MV_EXPOSURE_AUTO_MODE_CONTINUOUS,
};
enum class TriggerMode {
    ON = MV_TRIGGER_MODE_ON,
    OFF = MV_TRIGGER_MODE_OFF,
};
enum class TriggerSource {
    LINE0 = MV_TRIGGER_SOURCE_LINE0,
    LINE1 = MV_TRIGGER_SOURCE_LINE1,
    LINE2 = MV_TRIGGER_SOURCE_LINE2,
    LINE3 = MV_TRIGGER_SOURCE_LINE3,

    SOFTWARE = MV_TRIGGER_SOURCE_SOFTWARE,
};

using Handler = void*;

using DeviceInfo = MV_CC_DEVICE_INFO;
using DeviceInfoList = MV_CC_DEVICE_INFO_LIST;

using FrameOut = MV_FRAME_OUT;

using ConvertParam = MV_CC_PIXEL_CONVERT_PARAM;

namespace key {
constexpr auto GevSCPSPacketSize = "GevSCPSPacketSize";
constexpr auto ExposureAuto = "ExposureAuto";
constexpr auto AcquisitionFrameRateEnable = "AcquisitionFrameRateEnable";
constexpr auto AcquisitionFrameRate = "AcquisitionFrameRate";
constexpr auto ReverseX = "ReverseX";
constexpr auto ReverseY = "ReverseY";
constexpr auto ExposureTime = "ExposureTime";
constexpr auto Gain = "Gain";
constexpr auto TriggerMode = "TriggerMode";
constexpr auto TriggerSource = "TriggerSource";
constexpr auto Width = "Width";
constexpr auto Height = "Height";
constexpr auto PixelFormat = "PixelFormat";
} // namespace key

} // namespace hikcamera::sdk

namespace hikcamera::util {

template <typename Ef>
using scope_exit = std::experimental::scope_exit<Ef>;

template <typename... Args>
constexpr auto make_unexpected(std::format_string<Args...> fmt, Args&&... args) noexcept {
    return std::unexpected{std::format(fmt, std::forward<Args>(args)...)};
}
constexpr auto make_unexpected_with_error(std::string_view msg, std::uint32_t error) noexcept {
    return std::unexpected{std::format("{}: {}", msg, translate_error(error))};
}

constexpr auto is_rgb_pixel_type(MvGvspPixelType type) noexcept -> bool {
    switch (type) {
    case PixelType_Gvsp_BGR8_Packed:
    case PixelType_Gvsp_YUV422_Packed:
    case PixelType_Gvsp_YUV422_YUYV_Packed:
    case PixelType_Gvsp_BayerGR8:
    case PixelType_Gvsp_BayerRG8:
    case PixelType_Gvsp_BayerGB8:
    case PixelType_Gvsp_BayerBG8:
    case PixelType_Gvsp_BayerGB10:
    case PixelType_Gvsp_BayerGB10_Packed:
    case PixelType_Gvsp_BayerBG10:
    case PixelType_Gvsp_BayerBG10_Packed:
    case PixelType_Gvsp_BayerRG10:
    case PixelType_Gvsp_BayerRG10_Packed:
    case PixelType_Gvsp_BayerGR10:
    case PixelType_Gvsp_BayerGR10_Packed:
    case PixelType_Gvsp_BayerGB12:
    case PixelType_Gvsp_BayerGB12_Packed:
    case PixelType_Gvsp_BayerBG12:
    case PixelType_Gvsp_BayerBG12_Packed:
    case PixelType_Gvsp_BayerRG12:
    case PixelType_Gvsp_BayerRG12_Packed:
    case PixelType_Gvsp_BayerGR12:
    case PixelType_Gvsp_BayerGR12_Packed: return true;
    default: return false;
    }
}

inline auto string_from_buffer(const unsigned char* raw) -> std::string {
    if (raw == nullptr)
        return {};
    return reinterpret_cast<const char*>(raw);
}

inline auto transport_layer_name(const unsigned int layer_type) -> std::string {
    switch (layer_type) {
    case MV_GIGE_DEVICE: return "GigE";
    case MV_USB_DEVICE: return "USB";
    case MV_CAMERALINK_DEVICE: return "CameraLink";
    case MV_GENTL_CXP_DEVICE: return "CXP";
    case MV_GENTL_XOF_DEVICE: return "XOF";
    default: return "Unknown";
    }
}

inline auto device_info_from_sdk(const sdk::DeviceInfo& info) -> DeviceInfo {
    DeviceInfo result;
    result.transport_layer = transport_layer_name(info.nTLayerType);

    switch (info.nTLayerType) {
    case MV_GIGE_DEVICE: {
        const auto& gige = info.SpecialInfo.stGigEInfo;
        result.user_defined_name = string_from_buffer(gige.chUserDefinedName);
        result.serial_number = string_from_buffer(gige.chSerialNumber);
        result.model_name = string_from_buffer(gige.chModelName);
        result.device_id = result.user_defined_name.empty() ? result.serial_number
                                                            : result.user_defined_name;
        break;
    }
    case MV_USB_DEVICE: {
        const auto& usb = info.SpecialInfo.stUsb3VInfo;
        result.user_defined_name = string_from_buffer(usb.chUserDefinedName);
        result.serial_number = string_from_buffer(usb.chSerialNumber);
        result.model_name = string_from_buffer(usb.chModelName);
        result.device_id = result.user_defined_name.empty() ? result.serial_number
                                                            : result.user_defined_name;
        break;
    }
    case MV_GENTL_CXP_DEVICE: {
        const auto& cxp = info.SpecialInfo.stCXPInfo;
        result.device_id = string_from_buffer(cxp.chDeviceID);
        result.user_defined_name = string_from_buffer(cxp.chUserDefinedName);
        result.serial_number = string_from_buffer(cxp.chSerialNumber);
        result.model_name = string_from_buffer(cxp.chModelName);
        break;
    }
    case MV_GENTL_XOF_DEVICE: {
        const auto& xof = info.SpecialInfo.stXoFInfo;
        result.device_id = string_from_buffer(xof.chDeviceID);
        result.user_defined_name = string_from_buffer(xof.chUserDefinedName);
        result.serial_number = string_from_buffer(xof.chSerialNumber);
        result.model_name = string_from_buffer(xof.chModelName);
        break;
    }
    default: break;
    }

    if (result.device_id.empty())
        result.device_id = result.user_defined_name.empty() ? result.serial_number
                                                            : result.user_defined_name;

    return result;
}

inline auto compare(const DeviceInfo& info, std::string_view other) noexcept -> bool {
    return other == info.device_id || other == info.user_defined_name || other == info.serial_number
        || other == info.model_name;
}

inline auto enumerate_devices() noexcept -> std::expected<std::vector<sdk::DeviceInfo*>, std::string> {

    auto devices = sdk::DeviceInfoList{};
    std::memset(&devices, 0, sizeof(devices));

    const auto result = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devices);
    if (result != MV_OK) {
        const auto msg = translate_error(result);
        return std::unexpected{std::format("Failed to enum device: {}", msg)};
    }

    const auto device_num = devices.nDeviceNum;
    if (device_num == 0) {
        return std::unexpected{"No device was found"};
    }

    std::vector<sdk::DeviceInfo*> result_devices;
    result_devices.reserve(device_num);
    for (unsigned int index = 0; index < device_num; ++index) {
        if (devices.pDeviceInfo[index] != nullptr)
            result_devices.push_back(devices.pDeviceInfo[index]);
    }
    if (result_devices.empty())
        return std::unexpected{"Enum returned zero usable device pointers"};
    return result_devices;
}

inline auto to_public_device_infos(const std::vector<sdk::DeviceInfo*>& devices) -> std::vector<DeviceInfo> {
    std::vector<DeviceInfo> result;
    result.reserve(devices.size());
    for (const auto* device : devices) {
        if (device != nullptr)
            result.push_back(device_info_from_sdk(*device));
    }
    return result;
}

inline auto select_device_pointer(
    const std::vector<sdk::DeviceInfo*>& devices, std::string_view device_id) noexcept
    -> std::expected<sdk::DeviceInfo*, std::string> {
    if (devices.empty())
        return std::unexpected{"No device was found"};

    const auto public_infos = to_public_device_infos(devices);
    const auto selected_index = select_device_index(public_infos, device_id);
    if (!selected_index)
        return std::unexpected(selected_index.error());

    return devices[*selected_index];
}

inline auto make_information(const sdk::DeviceInfo& info) noexcept -> std::string {
    auto result = std::string{};

    if (info.nTLayerType == MV_GIGE_DEVICE) {
        const auto& gige = info.SpecialInfo.stGigEInfo;

        auto ip1 = (gige.nCurrentIp & 0xff000000) >> 24;
        auto ip2 = (gige.nCurrentIp & 0x00ff0000) >> 16;
        auto ip3 = (gige.nCurrentIp & 0x0000ff00) >> 8;
        auto ip4 = (gige.nCurrentIp & 0x000000ff);

        result += std::format("Device Type: GigE\n");
        result += std::format("Device IP: {}.{}.{}.{}\n", ip1, ip2, ip3, ip4);
        result += std::format(
            "User Defined Name: {}\n", reinterpret_cast<const char*>(gige.chUserDefinedName));
    } else if (info.nTLayerType == MV_USB_DEVICE) {
        const auto& usb = info.SpecialInfo.stUsb3VInfo;

        result += std::format("Device Type: USB\n");
        result += std::format(
            "User Defined Name: {}\n", reinterpret_cast<const char*>(usb.chUserDefinedName));
        result +=
            std::format("Serial Number: {}\n", reinterpret_cast<const char*>(usb.chSerialNumber));
        result += std::format("Device Number: {}\n", usb.nDeviceNumber);
    } else {
        result += "Device Type: Unknown\n";
    }
    return result;
}

} // namespace hikcamera::util
