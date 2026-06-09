# Hikcamera

海康工业相机 MVS SDK 的 C++23 封装，支持 standalone CMake 和 ROS2 ament 构建。

当前测试型号：MV-CS016-10UC / MV-CS050-10UC

## Standalone 构建（推荐）

依赖：

- CMake ≥ 3.16
- C++23 编译器（GCC 14+ / Clang 18+）
- OpenCV ≥ 4.5
- 海康 MVS SDK（从[海康官网](https://www.hikrobotics.com/machinevision)下载安装）

```bash
# 配置（指定 MVS SDK 安装路径，可选）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMVS_SDK_ROOT=/opt/MVS

# 构建
cmake --build build --parallel
```

## 通过 CMake FetchContent 引入

在项目的 `CMakeLists.txt` 中：

```cmake
include(FetchContent)
FetchContent_Declare(
    hikcamera
    URL https://github.com/Yukikaze2233/ros2-hikcamera/releases/download/v2.1.0/hikcamera-src-2.1.0.zip
    URL_HASH SHA256=<see release notes>
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(hikcamera)

target_link_libraries(your_app PRIVATE hikcamera)
```

使用：

```cpp
#include <hikcamera/capturer.hpp>

auto camera = hikcamera::Camera{};
camera.configure({.exposure_us = 1500, .framerate = 80});
if (auto result = camera.connect()) {
    auto mat = camera.read_image();  // std::expected<cv::Mat, std::string>
}
```

## ROS2 构建（兼容模式）

保留 `package.xml`，仍可通过 ament 构建：

```bash
cd /path/to/rmcs_ws/
git clone https://github.com/Yukikaze2233/ros2-hikcamera.git src/hikcamera
colcon build --packages-select hikcamera --symlink-install --merge-install
```

## 故障排除

- **未找到相机但 `lsusb` 能找到**：配置 udev 规则：
  ```bash
  echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="2bdf", ATTR{idProduct}=="0001", MODE="0666"' \
    | sudo tee /etc/udev/rules.d/99-hikcamera.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```

- **MVS SDK 未找到**：设置环境变量 `MVS_SDK_ROOT` 或 CMake 参数 `-DMVS_SDK_ROOT=/path/to/MVS`。
