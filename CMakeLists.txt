cmake_minimum_required(VERSION 3.16)
project(hikcamera VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc)

# ---- MVS SDK ---------------------------------------------------------------
if(DEFINED ENV{HIKCAMERA_SDK_MODE} AND NOT "$ENV{HIKCAMERA_SDK_MODE}" STREQUAL "")
    set(_default_hikcamera_sdk_mode "$ENV{HIKCAMERA_SDK_MODE}")
else()
    set(_default_hikcamera_sdk_mode "AUTO")
endif()

set(HIKCAMERA_SDK_MODE "${_default_hikcamera_sdk_mode}" CACHE STRING
    "Select Hik SDK source: AUTO (default), system, or vendor")
set_property(CACHE HIKCAMERA_SDK_MODE PROPERTY STRINGS AUTO system vendor)

string(TOLOWER "${HIKCAMERA_SDK_MODE}" _requested_hikcamera_sdk_mode)
if(NOT _requested_hikcamera_sdk_mode STREQUAL "auto"
    AND NOT _requested_hikcamera_sdk_mode STREQUAL "system"
    AND NOT _requested_hikcamera_sdk_mode STREQUAL "vendor")
    message(FATAL_ERROR
        "Unsupported HIKCAMERA_SDK_MODE='${HIKCAMERA_SDK_MODE}'. Expected 'AUTO', 'system', or 'vendor'.")
endif()

if(DEFINED ENV{MVS_SDK_ROOT} AND NOT "$ENV{MVS_SDK_ROOT}" STREQUAL "")
    set(_default_mvs_sdk_root "$ENV{MVS_SDK_ROOT}")
else()
    set(_default_mvs_sdk_root "/opt/MVS")
endif()
set(MVS_SDK_ROOT "${_default_mvs_sdk_root}" CACHE PATH
    "Path to the system Hikrobot MVS SDK root used when HIKCAMERA_SDK_MODE=system")
if(NOT MVS_SDK_ROOT)
    set(MVS_SDK_ROOT "/opt/MVS" CACHE PATH
        "Path to the system Hikrobot MVS SDK root used when HIKCAMERA_SDK_MODE=system" FORCE)
endif()

set(_vendor_sdk_root "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk")
set(_vendor_sdk_include_dir "${_vendor_sdk_root}/include")
set(_vendor_sdk_library "${_vendor_sdk_root}/lib/libMvCameraControl.so")
set(_vendor_sdk_runtime_dir "${_vendor_sdk_root}/lib")

get_filename_component(_system_sdk_root "${MVS_SDK_ROOT}" ABSOLUTE)
set(_system_sdk_include_dir "${_system_sdk_root}/include")
set(_system_sdk_library "${_system_sdk_root}/lib/64/libMvCameraControl.so")
set(_system_sdk_runtime_dir "${_system_sdk_root}/lib/64")

if(_requested_hikcamera_sdk_mode STREQUAL "auto")
    if(EXISTS "${_vendor_sdk_include_dir}/MvCameraControl.h" AND EXISTS "${_vendor_sdk_library}")
        set(_selected_hikcamera_sdk_mode "vendor")
        set(HIKCAMERA_SDK_ROOT "${_vendor_sdk_root}")
        set(MVS_INCLUDE_DIR "${_vendor_sdk_include_dir}")
        set(MVS_LIBRARY "${_vendor_sdk_library}")
        set(MVS_RUNTIME_DIR "${_vendor_sdk_runtime_dir}")
    else()
        set(_selected_hikcamera_sdk_mode "system")
        set(HIKCAMERA_SDK_ROOT "${_system_sdk_root}")
        set(MVS_INCLUDE_DIR "${_system_sdk_include_dir}")
        set(MVS_LIBRARY "${_system_sdk_library}")
        set(MVS_RUNTIME_DIR "${_system_sdk_runtime_dir}")
    endif()
elseif(_requested_hikcamera_sdk_mode STREQUAL "vendor")
    set(_selected_hikcamera_sdk_mode "vendor")
    set(HIKCAMERA_SDK_ROOT "${_vendor_sdk_root}")
    set(MVS_INCLUDE_DIR "${_vendor_sdk_include_dir}")
    set(MVS_LIBRARY "${_vendor_sdk_library}")
    set(MVS_RUNTIME_DIR "${_vendor_sdk_runtime_dir}")
elseif(_requested_hikcamera_sdk_mode STREQUAL "system")
    set(_selected_hikcamera_sdk_mode "system")
    set(HIKCAMERA_SDK_ROOT "${_system_sdk_root}")
    set(MVS_INCLUDE_DIR "${_system_sdk_include_dir}")
    set(MVS_LIBRARY "${_system_sdk_library}")
    set(MVS_RUNTIME_DIR "${_system_sdk_runtime_dir}")
endif()

if(_selected_hikcamera_sdk_mode STREQUAL "vendor")
    if(NOT EXISTS "${MVS_INCLUDE_DIR}/MvCameraControl.h")
        message(FATAL_ERROR
            "HIKCAMERA_SDK_MODE=vendor expects '${MVS_INCLUDE_DIR}/MvCameraControl.h'. "
            "Populate vendor/hikcamera/src/sdk explicitly before configuring.")
    endif()
    if(NOT EXISTS "${MVS_LIBRARY}")
        message(FATAL_ERROR
            "HIKCAMERA_SDK_MODE=vendor expects '${MVS_LIBRARY}'. "
            "Populate vendor/hikcamera/src/sdk explicitly before configuring.")
    endif()
else()
    if(NOT EXISTS "${MVS_INCLUDE_DIR}/MvCameraControl.h")
        message(FATAL_ERROR
            "HIKCAMERA_SDK_MODE=system expects '${MVS_INCLUDE_DIR}/MvCameraControl.h'. "
            "Install MVS under /opt/MVS or pass -DMVS_SDK_ROOT=/path/to/MVS.")
    endif()
    if(NOT EXISTS "${MVS_LIBRARY}")
        message(FATAL_ERROR
            "HIKCAMERA_SDK_MODE=system expects '${MVS_LIBRARY}'. "
            "Install MVS under /opt/MVS or pass -DMVS_SDK_ROOT=/path/to/MVS.")
    endif()
endif()

message(STATUS "HIKCAMERA_SDK_MODE=${_selected_hikcamera_sdk_mode}")
message(STATUS "HIKCAMERA_SDK_ROOT=${HIKCAMERA_SDK_ROOT}")
message(STATUS "MVS_SDK_RUNTIME_DIR=${MVS_RUNTIME_DIR}")

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
    HIKCAMERA_RUNTIME_DIR "${MVS_RUNTIME_DIR}"
    HIKCAMERA_SDK_MODE "${_selected_hikcamera_sdk_mode}"
    HIKCAMERA_SDK_ROOT "${HIKCAMERA_SDK_ROOT}"
    )
