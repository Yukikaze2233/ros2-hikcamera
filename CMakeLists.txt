cmake_minimum_required(VERSION 3.16)
project(hikcamera VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 复用父目录已有的 OpenCV targets（add_subdirectory 场景）
if(NOT TARGET OpenCV::core)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
endif()

# ---- MVS SDK ---------------------------------------------------------------
# 优先从 src/sdk 查找（仓库自带头文件），其次从系统路径查找
# .so 文件必须由用户从海康官网安装或通过 MVS_SDK_ROOT 指定
set(MVS_INCLUDE_HINT "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/include")

find_path(MVS_INCLUDE_DIR MvCameraControl.h
    HINTS ${MVS_INCLUDE_HINT}
    PATHS /opt/MVS/include /usr/local/include
)

find_library(MVS_LIBRARY MvCameraControl
    HINTS ENV MVS_SDK_ROOT
    PATHS /opt/MVS/lib /opt/MVS/lib/64 /usr/local/lib
    PATH_SUFFIXES lib lib64
)

if(NOT MVS_INCLUDE_DIR)
    message(FATAL_ERROR "MVS SDK headers not found (MvCameraControl.h). "
        "Install Hikvision MVS SDK or set MVS_SDK_ROOT.")
endif()
if(NOT MVS_LIBRARY)
    message(FATAL_ERROR "MVS SDK library not found (libMvCameraControl.so). "
        "Install Hikvision MVS SDK or set MVS_SDK_ROOT.")
endif()

message(STATUS "MVS SDK include: ${MVS_INCLUDE_DIR}")
message(STATUS "MVS SDK library: ${MVS_LIBRARY}")

# ---- hikcamera library ------------------------------------------------------
add_library(${PROJECT_NAME} STATIC
    src/capturer.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${MVS_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        OpenCV::core
        OpenCV::imgproc
    PRIVATE
        ${MVS_LIBRARY}
)

# 通过 PUBLIC include 路径，让消费者只需 link hikcamera 即可包含头文件
# MVS 头文件不暴露给消费者（pimpl 模式已隔离）
