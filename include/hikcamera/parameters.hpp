#pragma once
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hikcamera {

// =============================================================================
// Public enums
// =============================================================================

enum class auto_mode { off, once, continuous };
enum class trigger_mode { off, on };
enum class trigger_source { line0, line1, line2, line3, software };
enum class update_mode { live };

// =============================================================================
// Internal types (detail namespace — not part of user-facing API)
// =============================================================================

namespace detail {

enum class parameter_id {
    exposure_time_us,
    exposure_auto,
    gain,
    gain_auto,
    white_balance_auto,
    white_balance_ratio_red,
    white_balance_ratio_green,
    white_balance_ratio_blue,
    gamma,
    trigger_mode,
    trigger_source,
    trigger_delay,
    frame_rate_enabled,
    frame_rate_fps,
    reverse_x,
    reverse_y,
    software_trigger,
};

using parameter_value = std::variant<float, int, bool, auto_mode, trigger_mode, trigger_source>;

struct parameter_metadata {
    std::string name;
    update_mode mode;
    parameter_value current;
    parameter_value min;
    parameter_value max;
    parameter_value step;
    std::vector<parameter_value> supported_values;
    std::vector<std::string> supported_symbolics;
};

} // namespace detail

// =============================================================================
// Tag types (param namespace)
// =============================================================================

namespace param {

struct exposure_time_us {};
struct exposure_auto {};
struct gain {};
struct gain_auto {};
struct white_balance_auto {};
struct white_balance_ratio_red {};
struct white_balance_ratio_green {};
struct white_balance_ratio_blue {};
struct gamma {};
struct trigger_mode {};
struct trigger_source {};
struct trigger_delay {};
struct frame_rate_enabled {};
struct frame_rate_fps {};
struct reverse_x {};
struct reverse_y {};
struct software_trigger {};

} // namespace param

// =============================================================================
// parameter_traits — maps tag types to their value_type, id, name, mode
// =============================================================================

template <typename Tag>
struct parameter_traits;

template <>
struct parameter_traits<param::exposure_time_us> {
    using value_type = float;
    static constexpr auto id = detail::parameter_id::exposure_time_us;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "exposure_time_us";
};

template <>
struct parameter_traits<param::exposure_auto> {
    using value_type = auto_mode;
    static constexpr auto id = detail::parameter_id::exposure_auto;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "exposure_auto";
};

template <>
struct parameter_traits<param::gain> {
    using value_type = float;
    static constexpr auto id = detail::parameter_id::gain;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "gain";
};

template <>
struct parameter_traits<param::gain_auto> {
    using value_type = auto_mode;
    static constexpr auto id = detail::parameter_id::gain_auto;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "gain_auto";
};

template <>
struct parameter_traits<param::white_balance_auto> {
    using value_type = auto_mode;
    static constexpr auto id = detail::parameter_id::white_balance_auto;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "white_balance_auto";
};

template <>
struct parameter_traits<param::white_balance_ratio_red> {
    using value_type = int;
    static constexpr auto id = detail::parameter_id::white_balance_ratio_red;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "white_balance_ratio_red";
};

template <>
struct parameter_traits<param::white_balance_ratio_green> {
    using value_type = int;
    static constexpr auto id = detail::parameter_id::white_balance_ratio_green;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "white_balance_ratio_green";
};

template <>
struct parameter_traits<param::white_balance_ratio_blue> {
    using value_type = int;
    static constexpr auto id = detail::parameter_id::white_balance_ratio_blue;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "white_balance_ratio_blue";
};

template <>
struct parameter_traits<param::gamma> {
    using value_type = float;
    static constexpr auto id = detail::parameter_id::gamma;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "gamma";
};

template <>
struct parameter_traits<param::trigger_mode> {
    using value_type = trigger_mode;
    static constexpr auto id = detail::parameter_id::trigger_mode;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "trigger_mode";
};

template <>
struct parameter_traits<param::trigger_source> {
    using value_type = trigger_source;
    static constexpr auto id = detail::parameter_id::trigger_source;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "trigger_source";
};

template <>
struct parameter_traits<param::trigger_delay> {
    using value_type = float;
    static constexpr auto id = detail::parameter_id::trigger_delay;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "trigger_delay";
};

template <>
struct parameter_traits<param::frame_rate_enabled> {
    using value_type = bool;
    static constexpr auto id = detail::parameter_id::frame_rate_enabled;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "frame_rate_enabled";
};

template <>
struct parameter_traits<param::frame_rate_fps> {
    using value_type = float;
    static constexpr auto id = detail::parameter_id::frame_rate_fps;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "frame_rate_fps";
};

template <>
struct parameter_traits<param::reverse_x> {
    using value_type = bool;
    static constexpr auto id = detail::parameter_id::reverse_x;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "reverse_x";
};

template <>
struct parameter_traits<param::reverse_y> {
    using value_type = bool;
    static constexpr auto id = detail::parameter_id::reverse_y;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "reverse_y";
};

// software_trigger is execute-only — no get/set in parameter_ref
template <>
struct parameter_traits<param::software_trigger> {
    using value_type = void;
    static constexpr auto id = detail::parameter_id::software_trigger;
    static constexpr auto mode = update_mode::live;
    static constexpr std::string_view name = "software_trigger";
};

// =============================================================================
// parameter_info — metadata returned by describe()
// =============================================================================

template <typename T>
struct parameter_info {
    std::string name;
    update_mode mode;
    T current;
    T min{};
    T max{};
    T step{};
    std::vector<T> supported_values;
    std::vector<std::string> supported_symbolics;
};

// =============================================================================
// Forward declarations (definitions in capturer.hpp after Camera)
// =============================================================================

class Camera;

template <typename Tag>
class parameter_ref;

} // namespace hikcamera
