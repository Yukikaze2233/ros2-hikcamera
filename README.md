# Hikcamera

海康工业相机 MVS SDK 的 C++23 封装，提供采集接口和 typed parameter API。

当前测试型号：MV-CS016-10UC / MV-CS050-10UC

## Build

依赖：

- CMake >= 3.16
- C++23 编译器
- OpenCV >= 4.5
- 海康 MVS SDK

默认构建模式为 `HIKCAMERA_SDK_MODE=AUTO`：

- 如果 `src/sdk/include/MvCameraControl.h` 和 `src/sdk/lib/libMvCameraControl.so` 都存在，则使用仓库内 vendored SDK
- 否则回退到 `system` 模式，从 `MVS_SDK_ROOT` 或 `/opt/MVS` 读取 `include` 和 `lib/64`
- 这个判断由子模块自身完成；上层工程只需在需要固定来源时显式传 `HIKCAMERA_SDK_MODE=system|vendor`
- 主仓库的运行脚本会跟随 `build/` 里生成的 Hik mode 选择，避免运行期与编译期不一致

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

显式使用 vendored SDK：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DHIKCAMERA_SDK_MODE=vendor
```

显式使用系统 SDK：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DHIKCAMERA_SDK_MODE=system \
  -DMVS_SDK_ROOT=/opt/MVS
```

也可以用环境变量：

```bash
HIKCAMERA_SDK_MODE=system MVS_SDK_ROOT=/opt/MVS cmake -S . -B build
```

## Public API

核心接口：

- `hikcamera::Camera`
- `hikcamera::Camera::parameter<Tag>()`
- `hikcamera::Camera::execute<Tag>()`
- `hikcamera/parameters.hpp` 中的 tag / traits

示例：

```cpp
#include <hikcamera/capturer.hpp>
#include <hikcamera/parameters.hpp>

hikcamera::Camera camera;
camera.configure({.exposure_us = 1500, .framerate = 80});
if (auto connected = camera.connect()) {
    auto exposure = camera.parameter<hikcamera::param::exposure_time_us>().get();
    auto set_gain = camera.parameter<hikcamera::param::gain>().set(6.0F);
    auto trigger = camera.execute<hikcamera::param::software_trigger>();
}
```

## Notes

- `vendor` 模式要求 `src/sdk/include` 和 `src/sdk/lib` 完整存在；缺失时会直接报错
- `system` 模式要求 `MVS_SDK_ROOT` 或 `/opt/MVS` 下存在 `include/MvCameraControl.h` 和 `lib/64/libMvCameraControl.so`
- 运行时动态库搜索路径、环境注入和上层 RPATH 策略由集成仓库负责
