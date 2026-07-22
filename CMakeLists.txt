cmake_minimum_required(VERSION 3.22)
project(hikcamera VERSION 2.0.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

find_package(ament_cmake REQUIRED)
find_package(OpenCV 4.5 REQUIRED)

# ---- library ----
add_library(${PROJECT_NAME} SHARED
    src/capturer.cpp
    src/shm_ring.cpp
    src/shared_frame.cpp
    src/shared_frame_reader.cpp
    src/shared_frame_writer.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)

target_link_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/lib>
    $<INSTALL_INTERFACE:lib>
)

target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    MvCameraControl
)

# ---- tests (registered with CTest) ----
enable_testing()

add_executable(test_shm test/test_shm.cpp)
target_include_directories(test_shm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/test
)
target_link_libraries(test_shm
    ${PROJECT_NAME}
    ${OpenCV_LIBS}
    pthread
    rt
)

# Register tests individually so failures are isolated.
add_test(NAME shm_two_readers
    COMMAND test_shm two_readers)
add_test(NAME shm_lease_tear
    COMMAND test_shm lease_tear)
add_test(NAME shm_lagged_reader
    COMMAND test_shm lagged_reader)
add_test(NAME shm_metadata_roundtrip
    COMMAND test_shm metadata_roundtrip)
add_test(NAME shm_creator_exclusive
    COMMAND test_shm creator_exclusive)
add_test(NAME shm_lease_lifetime
    COMMAND test_shm lease_lifetime)
add_test(NAME shm_concurrent_writes
    COMMAND test_shm concurrent_writes)
add_test(NAME shm_boundary_reject
    COMMAND test_shm boundary_reject)

# ---- install ----
install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}_export
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY src/sdk/lib/ DESTINATION lib)

ament_export_targets(${PROJECT_NAME}_export HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_package()
