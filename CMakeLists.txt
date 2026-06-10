cmake_minimum_required(VERSION 3.16)
project(hikcamera VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc)

# ---- MVS SDK ---------------------------------------------------------------
# 优先使用仓库内 src/sdk 头文件和运行库，系统安装仅作为兜底。
set(MVS_INCLUDE_HINT "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/include")
set(MVS_RUNTIME_HINT "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/lib")

if(EXISTS "${MVS_INCLUDE_HINT}/MvCameraControl.h")
    set(MVS_INCLUDE_DIR "${MVS_INCLUDE_HINT}")
else()
    find_path(MVS_INCLUDE_DIR MvCameraControl.h
        HINTS ENV MVS_SDK_ROOT
        PATHS /opt/MVS/include /usr/local/include
        PATH_SUFFIXES include)
endif()

if(EXISTS "${MVS_RUNTIME_HINT}/libMvCameraControl.so")
    set(MVS_LIBRARY "${MVS_RUNTIME_HINT}/libMvCameraControl.so")
    set(MVS_RUNTIME_DIR "${MVS_RUNTIME_HINT}")
else()
    find_library(MVS_LIBRARY MvCameraControl
        HINTS ENV MVS_SDK_ROOT
        PATHS /opt/MVS/lib /opt/MVS/lib/64 /usr/local/lib
        PATH_SUFFIXES lib lib64)
    if(MVS_LIBRARY)
        get_filename_component(MVS_RUNTIME_DIR "${MVS_LIBRARY}" DIRECTORY)
    endif()
endif()

if(NOT MVS_INCLUDE_DIR)
    message(FATAL_ERROR "MVS SDK headers not found (MvCameraControl.h). "
        "Expected vendor/hikcamera/src/sdk/include or a system MVS SDK.")
endif()
if(NOT MVS_LIBRARY OR NOT MVS_RUNTIME_DIR)
    message(FATAL_ERROR "MVS SDK library not found (libMvCameraControl.so). "
        "Expected vendor/hikcamera/src/sdk/lib or a system MVS SDK.")
endif()

message(STATUS "MVS SDK include: ${MVS_INCLUDE_DIR}")
message(STATUS "MVS SDK library: ${MVS_LIBRARY}")
message(STATUS "MVS SDK runtime dir: ${MVS_RUNTIME_DIR}")

add_library(mvs::camera_control SHARED IMPORTED)
set_target_properties(mvs::camera_control PROPERTIES
    IMPORTED_LOCATION "${MVS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MVS_INCLUDE_DIR}")

# ---- hikcamera library ------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
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
        ${OpenCV_LIBS}
    PRIVATE
        mvs::camera_control
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${MVS_RUNTIME_DIR}"
    HIKCAMERA_RUNTIME_DIR "${MVS_RUNTIME_DIR}")

# 通过 PUBLIC include 路径，让消费者只需 link hikcamera 即可包含头文件
# MVS 头文件不暴露给消费者（pimpl 模式已隔离）
