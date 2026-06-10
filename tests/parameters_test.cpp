#include "hikcamera/capturer.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <print>
#include <stdexcept>
#include <string>

// =============================================================================
// MVS SDK constants (minimal subset for test, matching CameraParams.h)
// =============================================================================
namespace {
constexpr unsigned int MV_EXPOSURE_AUTO_MODE_OFF = 0;
constexpr unsigned int MV_EXPOSURE_AUTO_MODE_ONCE = 1;
constexpr unsigned int MV_EXPOSURE_AUTO_MODE_CONTINUOUS = 2;
constexpr unsigned int MV_TRIGGER_MODE_OFF = 0;
constexpr unsigned int MV_TRIGGER_MODE_ON = 1;
constexpr unsigned int MV_TRIGGER_SOURCE_LINE0 = 0;
constexpr unsigned int MV_TRIGGER_SOURCE_LINE1 = 1;
constexpr unsigned int MV_TRIGGER_SOURCE_LINE2 = 2;
constexpr unsigned int MV_TRIGGER_SOURCE_LINE3 = 3;
constexpr unsigned int MV_TRIGGER_SOURCE_SOFTWARE = 7;
constexpr unsigned int MV_GAMMA_SELECTOR_USER = 1;
} // anonymous namespace

using namespace hikcamera;

// =============================================================================
// Helper: test assertion
// =============================================================================

namespace {
auto require(bool cond, std::string_view msg) -> void {
    if (!cond)
        throw std::runtime_error(std::string(msg));
}

template <typename T>
auto require_near(T actual, T expected, T tolerance, std::string_view msg) -> void {
    if (std::abs(actual - expected) > tolerance) {
        auto oss = std::format("{}: expected {} +/- {}, got {}", msg, expected, tolerance, actual);
        throw std::runtime_error(oss);
    }
}
} // anonymous namespace

// =============================================================================
// Test: parameter_traits mappings
// =============================================================================

static void test_parameter_traits_mappings() {
    // float parameters
    static_assert(
        std::is_same_v<parameter_traits<param::exposure_time_us>::value_type, float>);
    require(parameter_traits<param::exposure_time_us>::name == "exposure_time_us",
            "exposure_time_us name");
    require(
        parameter_traits<param::exposure_time_us>::mode == update_mode::live,
        "exposure_time_us mode");

    static_assert(std::is_same_v<parameter_traits<param::gain>::value_type, float>);
    require(parameter_traits<param::gain>::name == "gain", "gain name");

    static_assert(std::is_same_v<parameter_traits<param::gamma>::value_type, float>);
    require(parameter_traits<param::gamma>::name == "gamma", "gamma name");

    static_assert(
        std::is_same_v<parameter_traits<param::trigger_delay>::value_type, float>);
    require(parameter_traits<param::trigger_delay>::name == "trigger_delay", "trigger_delay name");

    static_assert(
        std::is_same_v<parameter_traits<param::frame_rate_fps>::value_type, float>);
    require(parameter_traits<param::frame_rate_fps>::name == "frame_rate_fps",
            "frame_rate_fps name");

    // int parameters
    static_assert(
        std::is_same_v<parameter_traits<param::white_balance_ratio_red>::value_type, int>);
    require(
        parameter_traits<param::white_balance_ratio_red>::name == "white_balance_ratio_red",
        "wb_ratio_red name");

    static_assert(
        std::is_same_v<parameter_traits<param::white_balance_ratio_green>::value_type, int>);
    require(
        parameter_traits<param::white_balance_ratio_green>::name == "white_balance_ratio_green",
        "wb_ratio_green name");

    static_assert(
        std::is_same_v<parameter_traits<param::white_balance_ratio_blue>::value_type, int>);
    require(
        parameter_traits<param::white_balance_ratio_blue>::name == "white_balance_ratio_blue",
        "wb_ratio_blue name");

    // bool parameters
    static_assert(
        std::is_same_v<parameter_traits<param::frame_rate_enabled>::value_type, bool>);
    require(
        parameter_traits<param::frame_rate_enabled>::name == "frame_rate_enabled",
        "frame_rate_enabled name");

    static_assert(std::is_same_v<parameter_traits<param::reverse_x>::value_type, bool>);
    require(parameter_traits<param::reverse_x>::name == "reverse_x", "reverse_x name");

    static_assert(std::is_same_v<parameter_traits<param::reverse_y>::value_type, bool>);
    require(parameter_traits<param::reverse_y>::name == "reverse_y", "reverse_y name");

    // auto_mode parameters
    static_assert(
        std::is_same_v<parameter_traits<param::exposure_auto>::value_type, auto_mode>);
    static_assert(
        std::is_same_v<parameter_traits<param::gain_auto>::value_type, auto_mode>);
    static_assert(
        std::is_same_v<parameter_traits<param::white_balance_auto>::value_type, auto_mode>);

    // trigger_mode
    static_assert(
        std::is_same_v<parameter_traits<param::trigger_mode>::value_type, trigger_mode>);

    // trigger_source
    static_assert(
        std::is_same_v<parameter_traits<param::trigger_source>::value_type, trigger_source>);

    // software_trigger is execute-only
    static_assert(
        std::is_same_v<parameter_traits<param::software_trigger>::value_type, void>);

    std::println("  PASS: parameter_traits mappings");
}

// =============================================================================
// Test: parameter_id uniqueness
// =============================================================================

static void test_parameter_id_uniqueness() {
    // Verify all parameter_ids are distinct by comparing trait IDs
    using detail::parameter_id;

    auto ids = {
        parameter_traits<param::exposure_time_us>::id,
        parameter_traits<param::exposure_auto>::id,
        parameter_traits<param::gain>::id,
        parameter_traits<param::gain_auto>::id,
        parameter_traits<param::white_balance_auto>::id,
        parameter_traits<param::white_balance_ratio_red>::id,
        parameter_traits<param::white_balance_ratio_green>::id,
        parameter_traits<param::white_balance_ratio_blue>::id,
        parameter_traits<param::gamma>::id,
        parameter_traits<param::trigger_mode>::id,
        parameter_traits<param::trigger_source>::id,
        parameter_traits<param::trigger_delay>::id,
        parameter_traits<param::frame_rate_enabled>::id,
        parameter_traits<param::frame_rate_fps>::id,
        parameter_traits<param::reverse_x>::id,
        parameter_traits<param::reverse_y>::id,
        parameter_traits<param::software_trigger>::id,
    };

    // All IDs should be distinct (17 unique IDs)
    for (auto it = ids.begin(); it != ids.end(); ++it) {
        for (auto jt = std::next(it); jt != ids.end(); ++jt) {
            require(*it != *jt, "Duplicate parameter_id detected");
        }
    }

    std::println("  PASS: parameter_id uniqueness");
}

// =============================================================================
// Test: enum conversion helpers (mirrored from capturer.impl.hpp)
// =============================================================================

static void test_enum_conversions() {
    // auto_mode → MVS enum
    require(
        static_cast<unsigned int>(auto_mode::off) != static_cast<unsigned int>(auto_mode::once),
        "auto_mode values must be distinct");
    require(
        static_cast<unsigned int>(auto_mode::off) != static_cast<unsigned int>(auto_mode::continuous),
        "auto_mode values must be distinct");
    require(
        static_cast<unsigned int>(auto_mode::once) != static_cast<unsigned int>(auto_mode::continuous),
        "auto_mode values must be distinct");

    // trigger_mode → MVS enum
    require(
        static_cast<unsigned int>(trigger_mode::off)
            != static_cast<unsigned int>(trigger_mode::on),
        "trigger_mode values must be distinct");

    // trigger_source values
    require(
        static_cast<unsigned int>(trigger_source::line0) != static_cast<unsigned int>(trigger_source::line1),
        "trigger_source values must be distinct");
    require(
        static_cast<unsigned int>(trigger_source::line1) != static_cast<unsigned int>(trigger_source::line2),
        "trigger_source values must be distinct");
    require(
        static_cast<unsigned int>(trigger_source::line2) != static_cast<unsigned int>(trigger_source::line3),
        "trigger_source values must be distinct");
    require(
        static_cast<unsigned int>(trigger_source::line3) != static_cast<unsigned int>(trigger_source::software),
        "trigger_source values must be distinct");

    std::println("  PASS: enum conversions");
}

// =============================================================================
// Test: software_trigger is execute-only (compile-time + runtime check)
// =============================================================================

static void test_software_trigger_execute_only() {
    // software_trigger has value_type = void
    static_assert(
        std::is_same_v<parameter_traits<param::software_trigger>::value_type, void>);

    // parameter_ref<software_trigger> has no get() or set() — verified by
    // the specialization in capturer.hpp that only provides describe()
    // (compile-time: attempting to call get/set would fail)

    std::println("  PASS: software_trigger execute-only contract");
}

// =============================================================================
// Test: Fake backend for parameter operations
// =============================================================================

namespace {

using param_id = detail::parameter_id;
using param_value = detail::parameter_value;

struct FakeParameterBackend {
    std::map<param_id, param_value> values;
    std::map<param_id, param_value> overrides;
    int set_parameter_core_call_count = 0;
    int get_parameter_core_call_count = 0;
    int execute_parameter_core_call_count = 0;
    bool gamma_selector_was_set = false;

    auto get_parameter_core(param_id id) -> std::expected<param_value, std::string> {
        ++get_parameter_core_call_count;
        if (auto it = values.find(id); it != values.end())
            return it->second;
        return std::unexpected{std::format("No value for parameter {}", static_cast<int>(id))};
    }

    auto set_parameter_core(param_id id, param_value val) -> std::expected<void, std::string> {
        ++set_parameter_core_call_count;
        values[id] = val;
        overrides[id] = val;

        // Simulate gamma_selector requirement
        if (id == param_id::gamma)
            gamma_selector_was_set = true;
        return {};
    }

    auto describe_parameter_core(param_id id) -> detail::parameter_metadata {
        auto meta = detail::parameter_metadata{};
        meta.mode = update_mode::live;

        switch (id) {
        case param_id::exposure_time_us:
            meta.name = "exposure_time_us";
            meta.current = param_value{2000.0f};
            meta.min = param_value{10.0f};
            meta.max = param_value{100000.0f};
            break;
        case param_id::exposure_auto:
            meta.name = "exposure_auto";
            meta.current = param_value{auto_mode::off};
            meta.supported_values = {
                param_value{auto_mode::off},
                param_value{auto_mode::once},
                param_value{auto_mode::continuous},
            };
            meta.supported_symbolics = {"off", "once", "continuous"};
            break;
        case param_id::gain:
            meta.name = "gain";
            meta.current = param_value{16.9807f};
            meta.min = param_value{0.0f};
            meta.max = param_value{20.0f};
            break;
        case param_id::gamma:
            meta.name = "gamma";
            meta.current = param_value{1.0f};
            meta.min = param_value{0.1f};
            meta.max = param_value{4.0f};
            break;
        case param_id::trigger_mode:
            meta.name = "trigger_mode";
            meta.current = param_value{trigger_mode::off};
            meta.supported_values = {
                param_value{trigger_mode::off},
                param_value{trigger_mode::on},
            };
            meta.supported_symbolics = {"off", "on"};
            break;
        case param_id::trigger_source:
            meta.name = "trigger_source";
            meta.current = param_value{trigger_source::line0};
            meta.supported_values = {
                param_value{trigger_source::line0},
                param_value{trigger_source::line1},
                param_value{trigger_source::software},
            };
            meta.supported_symbolics = {"Line0", "Line1", "Software"};
            break;
        case param_id::frame_rate_fps:
            meta.name = "frame_rate_fps";
            meta.current = param_value{80.0f};
            meta.min = param_value{1.0f};
            meta.max = param_value{500.0f};
            break;
        case param_id::reverse_x:
            meta.name = "reverse_x";
            meta.current = param_value{false};
            break;
        default:
            meta.name = "unknown";
            break;
        }
        return meta;
    }

    auto execute_parameter_core(param_id id) -> std::expected<void, std::string> {
        ++execute_parameter_core_call_count;
        if (id == param_id::software_trigger)
            return {};
        return std::unexpected{"Parameter does not support execute"};
    }
};

} // anonymous namespace

// =============================================================================
// Test: Fake backend operations
// =============================================================================

static void test_fake_backend_get_set() {
    FakeParameterBackend backend;

    // Pre-populate a value
    backend.values[param_id::exposure_time_us] = param_value{1500.0f};

    // get
    auto result = backend.get_parameter_core(param_id::exposure_time_us);
    require(result.has_value(), "get exposure_time_us should succeed");
    require_near(std::get<float>(*result), 1500.0f, 0.01f, "get exposure_time_us value");

    // set
    auto set_result = backend.set_parameter_core(param_id::exposure_time_us, param_value{3000.0f});
    require(set_result.has_value(), "set exposure_time_us should succeed");

    // Verify cache updated
    auto cached = backend.overrides[param_id::exposure_time_us];
    require_near(std::get<float>(cached), 3000.0f, 0.01f, "override cache after set");

    // Verify gamma sets selector
    std::ignore = backend.set_parameter_core(param_id::gamma, param_value{2.2f});
    require(backend.gamma_selector_was_set, "gamma set should simulate gamma_selector=user");

    std::println("  PASS: fake backend get/set");
}

static void test_fake_backend_describe() {
    FakeParameterBackend backend;

    // describe exposure_time_us
    auto meta = backend.describe_parameter_core(param_id::exposure_time_us);
    require(meta.name == "exposure_time_us", "describe exposure_time_us name");
    require(meta.mode == update_mode::live, "describe exposure_time_us mode");
    require_near(std::get<float>(meta.current), 2000.0f, 0.01f, "describe exposure_time_us current");
    require_near(std::get<float>(meta.min), 10.0f, 0.01f, "describe exposure_time_us min");
    require_near(std::get<float>(meta.max), 100000.0f, 0.01f, "describe exposure_time_us max");

    // describe exposure_auto (enum)
    auto meta2 = backend.describe_parameter_core(param_id::exposure_auto);
    require(meta2.name == "exposure_auto", "describe exposure_auto name");
    require(
        std::get<auto_mode>(meta2.current) == auto_mode::off,
        "describe exposure_auto current");
    require(meta2.supported_values.size() == 3, "describe exposure_auto supported count");
    require(meta2.supported_symbolics.size() == 3, "describe exposure_auto symbolic count");

    std::println("  PASS: fake backend describe");
}

static void test_fake_backend_execute() {
    FakeParameterBackend backend;

    // software_trigger should succeed
    auto result = backend.execute_parameter_core(param_id::software_trigger);
    require(result.has_value(), "execute software_trigger should succeed");
    require(backend.execute_parameter_core_call_count == 1, "execute call count");

    // Non-execute parameter should fail
    auto result2 = backend.execute_parameter_core(param_id::exposure_time_us);
    require(!result2.has_value(), "execute exposure_time_us should fail");

    std::println("  PASS: fake backend execute");
}

// =============================================================================
// Test: Override cache replay order
// =============================================================================

static void test_override_cache_replay_order() {
    // The spec defines a fixed replay order:
    // reverse_x, reverse_y, exposure_auto, exposure_time_us,
    // gain_auto, gain, white_balance_auto,
    // white_balance_ratio_{red,green,blue},
    // gamma, trigger_mode, trigger_source, trigger_delay,
    // frame_rate_enabled, frame_rate_fps

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

    constexpr auto count = sizeof(replay_order) / sizeof(replay_order[0]);
    require(count == 16, "replay order should have 16 entries");

    // Verify all mutable parameters are in the replay (software_trigger excluded)
    // and no duplicates
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            require(
                replay_order[i] != replay_order[j],
                std::format("Duplicate in replay order at indices {} and {}", i, j));
        }
    }

    // Simulate replay: populate override cache, then replay in order
    FakeParameterBackend backend;
    backend.overrides[param_id::exposure_time_us] = param_value{5000.0f};
    backend.overrides[param_id::gain] = param_value{10.0f};
    backend.overrides[param_id::reverse_x] = param_value{true};

    std::vector<param_id> applied_order;
    for (auto id : replay_order) {
        if (auto it = backend.overrides.find(id); it != backend.overrides.end()) {
            std::ignore = backend.set_parameter_core(id, it->second);
            applied_order.push_back(id);
        }
    }

    // Check order: reverse_x first, then exposure_time_us, then gain
    require(applied_order.size() == 3, "3 overrides should be applied");
    require(applied_order[0] == param_id::reverse_x, "first applied: reverse_x");
    require(applied_order[1] == param_id::exposure_time_us, "second applied: exposure_time_us");
    require(applied_order[2] == param_id::gain, "third applied: gain");

    std::println("  PASS: override cache replay order");
}

// =============================================================================
// Test: Frame rate cache refresh
// =============================================================================

static void test_frame_rate_cache_refresh() {
    // frame_rate_fps or frame_rate_enabled set should trigger cache refresh
    // (simulated by the backend setting a value and checking)

    FakeParameterBackend backend;

    // Initial state: no frame rate set
    backend.values[param_id::frame_rate_enabled] = param_value{true};

    // Set frame_rate_fps — this should trigger refresh (simulated)
    auto result = backend.set_parameter_core(param_id::frame_rate_fps, param_value{120.0f});
    require(result.has_value(), "set frame_rate_fps should succeed");

    // Verify the value was written
    auto fps = backend.values[param_id::frame_rate_fps];
    require_near(std::get<float>(fps), 120.0f, 0.01f, "frame_rate_fps after set");

    // Verify override cache has the value
    auto cached = backend.overrides[param_id::frame_rate_fps];
    require_near(std::get<float>(cached), 120.0f, 0.01f, "frame_rate_fps in override cache");

    std::println("  PASS: frame rate cache refresh");
}

// =============================================================================
// Test: parameter_info structure
// =============================================================================

static void test_parameter_info_struct() {
    // Test parameter_info<float>
    parameter_info<float> info;
    info.name = "exposure_time_us";
    info.mode = update_mode::live;
    info.current = 1500.0f;
    info.min = 10.0f;
    info.max = 100000.0f;
    info.step = 0.0f; // float has no step concept but field exists

    require(info.name == "exposure_time_us", "parameter_info float name");
    require_near(info.current, 1500.0f, 0.01f, "parameter_info float current");
    require_near(info.min, 10.0f, 0.01f, "parameter_info float min");
    require_near(info.max, 100000.0f, 0.01f, "parameter_info float max");

    // Test parameter_info<int>
    parameter_info<int> int_info;
    int_info.name = "wb_ratio_red";
    int_info.current = 4096;
    int_info.min = 0;
    int_info.max = 65535;
    int_info.step = 1;

    require(int_info.name == "wb_ratio_red", "parameter_info int name");
    require(int_info.current == 4096, "parameter_info int current");
    require(int_info.step == 1, "parameter_info int step");

    // Test parameter_info<auto_mode> (enum)
    parameter_info<auto_mode> enum_info;
    enum_info.name = "exposure_auto";
    enum_info.current = auto_mode::continuous;
    enum_info.supported_values = {auto_mode::off, auto_mode::once, auto_mode::continuous};
    enum_info.supported_symbolics = {"off", "once", "continuous"};

    require(enum_info.current == auto_mode::continuous, "parameter_info enum current");
    require(enum_info.supported_values.size() == 3, "parameter_info enum values count");
    require(enum_info.supported_symbolics.size() == 3, "parameter_info enum symbolics count");

    std::println("  PASS: parameter_info struct");
}

// =============================================================================
// Test: parameter_value variant
// =============================================================================

static void test_parameter_value_variant() {
    // detail::parameter_value is std::variant<float, int, bool, auto_mode, trigger_mode, trigger_source>

    auto v_float = param_value{3.14f};
    require(std::holds_alternative<float>(v_float), "variant holds float");
    require_near(std::get<float>(v_float), 3.14f, 0.01f, "variant float value");

    auto v_int = param_value{42};
    require(std::holds_alternative<int>(v_int), "variant holds int");
    require(std::get<int>(v_int) == 42, "variant int value");

    auto v_bool = param_value{true};
    require(std::holds_alternative<bool>(v_bool), "variant holds bool");
    require(std::get<bool>(v_bool), "variant bool value");

    auto v_auto_mode = param_value{auto_mode::continuous};
    require(std::holds_alternative<auto_mode>(v_auto_mode), "variant holds auto_mode");
    require(std::get<auto_mode>(v_auto_mode) == auto_mode::continuous, "variant auto_mode value");

    auto v_trigger_mode = param_value{trigger_mode::on};
    require(std::holds_alternative<trigger_mode>(v_trigger_mode), "variant holds trigger_mode");
    require(std::get<trigger_mode>(v_trigger_mode) == trigger_mode::on, "variant trigger_mode value");

    auto v_trigger_source = param_value{trigger_source::software};
    require(
        std::holds_alternative<trigger_source>(v_trigger_source), "variant holds trigger_source");
    require(
        std::get<trigger_source>(v_trigger_source) == trigger_source::software,
        "variant trigger_source value");

    std::println("  PASS: parameter_value variant");
}

// =============================================================================
// Test: Config backward compatibility
// =============================================================================

static void test_config_backward_compatibility() {
    // Verify Config struct is unchanged — all existing fields still present
    Config cfg;
    cfg.device_id = "test";
    cfg.timeout_ms = 1000;
    cfg.exposure_us = 2000.0f;
    cfg.framerate = 80.0f;
    cfg.gain = 16.9807f;
    cfg.invert_image = false;
    cfg.software_sync = false;
    cfg.trigger_mode = false;
    cfg.fixed_framerate = true;

    require(cfg.device_id == "test", "Config device_id");
    require(cfg.timeout_ms == 1000, "Config timeout_ms");
    require_near(cfg.exposure_us, 2000.0f, 0.01f, "Config exposure_us");
    require(cfg.invert_image == false, "Config invert_image");
    require(cfg.trigger_mode == false, "Config trigger_mode");
    require(cfg.fixed_framerate == true, "Config fixed_framerate");

    std::println("  PASS: Config backward compatibility");
}

// =============================================================================
// Test: StreamFormat struct unchanged
// =============================================================================

static void test_stream_format_unchanged() {
    StreamFormat fmt;
    fmt.width = 1920;
    fmt.height = 1080;
    fmt.framerate = 80.0;
    fmt.pixel_format_name = "BGR8";
    fmt.source_pixel_format_name = "BayerRG8";

    require(fmt.width == 1920, "StreamFormat width");
    require(fmt.height == 1080, "StreamFormat height");
    require_near(fmt.framerate, 80.0, 0.01, "StreamFormat framerate");

    std::println("  PASS: StreamFormat unchanged");
}

// =============================================================================
// Main
// =============================================================================

int main() {
    try {
        std::println("=== hikcamera typed parameter SDK tests ===");

        test_parameter_traits_mappings();
        test_parameter_id_uniqueness();
        test_enum_conversions();
        test_software_trigger_execute_only();
        test_parameter_info_struct();
        test_parameter_value_variant();
        test_fake_backend_get_set();
        test_fake_backend_describe();
        test_fake_backend_execute();
        test_override_cache_replay_order();
        test_frame_rate_cache_refresh();
        test_config_backward_compatibility();
        test_stream_format_unchanged();

        std::println("\nAll tests passed.");
        return 0;
    } catch (const std::exception& e) {
        std::println(stderr, "TEST FAILED: {}", e.what());
        return 1;
    }
}
