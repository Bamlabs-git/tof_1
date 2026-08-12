cmake_minimum_required(VERSION 3.10)
project(ArducamToFFormatTesting)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags to reduce warnings and notes
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")  # Suppress ABI change notes
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")  # Suppress deprecated warnings

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED)

# Find jsoncpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)
if(NOT JSONCPP_FOUND)
    message(FATAL_ERROR "jsoncpp library not found. Install with: sudo apt install libjsoncpp-dev")
endif()

# Find ArducamDepthCamera
find_library(ARDUCAM_DEPTH_CAMERA ArducamDepthCamera PATHS /usr/local/lib /usr/lib)
if(NOT ARDUCAM_DEPTH_CAMERA)
    message(FATAL_ERROR "ArducamDepthCamera library not found. Please install Arducam ToF SDK.")
endif()

# Find Open3D (optional for point cloud viewer)
find_package(Open3D QUIET)
if(Open3D_FOUND)
    message(STATUS "Open3D found - Point cloud viewer will be built")
    add_definitions(-DWITH_OPEN3D)
else()
    message(STATUS "Open3D not found - Point cloud viewer will be disabled")
    message(STATUS "To enable 3D viewer: sudo apt install libopen3d-dev")
endif()

# Include directories
include_directories(/usr/local/include/ArducamDepthCamera)
include_directories(${OpenCV_INCLUDE_DIRS})

# Source files
set(COMMON_SOURCES
    src/common.cpp
    src/format_analyzer.cpp
    src/data_saver.cpp
)

# Main dual camera format testing tool
add_executable(dual_format_tester
    src/dual_format_tester.cpp
    ${COMMON_SOURCES}
)

target_link_libraries(dual_format_tester
    ${ARDUCAM_DEPTH_CAMERA}
    ${OpenCV_LIBS}
    ${JSONCPP_LIBRARIES}
    pthread
)

target_include_directories(dual_format_tester PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_compile_options(dual_format_tester PRIVATE 
    ${JSONCPP_CFLAGS_OTHER}
    -Wno-psabi
    -Wno-deprecated-declarations
)

# Point cloud viewer (if Open3D is available)
if(Open3D_FOUND)
    add_executable(dual_pointcloud_viewer
        src/dual_pointcloud_viewer.cpp
        ${COMMON_SOURCES}
    )
    
    target_link_libraries(dual_pointcloud_viewer
        ${ARDUCAM_DEPTH_CAMERA}
        ${OpenCV_LIBS}
        ${Open3D_LIBRARIES}
        ${JSONCPP_LIBRARIES}
        pthread
    )
    
    target_include_directories(dual_pointcloud_viewer PRIVATE 
        ${Open3D_INCLUDE_DIRS}
        ${JSONCPP_INCLUDE_DIRS}
    )
    target_compile_options(dual_pointcloud_viewer PRIVATE 
        ${JSONCPP_CFLAGS_OTHER}
        -Wno-psabi  # Suppress ABI change notes for Open3D
        -Wno-deprecated-declarations
    )
endif()

# Data capture and labeling tool
add_executable(data_capture_labeling_tool
    src/data_capture_labeling_tool.cpp
    ${COMMON_SOURCES}
)

target_link_libraries(data_capture_labeling_tool
    ${ARDUCAM_DEPTH_CAMERA}
    ${OpenCV_LIBS}
    ${JSONCPP_LIBRARIES}
    pthread
)

target_include_directories(data_capture_labeling_tool PRIVATE ${JSONCPP_INCLUDE_DIRS})
target_compile_options(data_capture_labeling_tool PRIVATE 
    ${JSONCPP_CFLAGS_OTHER}
    -Wno-psabi
    -Wno-deprecated-declarations
)

# Add Open3D support if available
if(Open3D_FOUND)
    target_link_libraries(data_capture_labeling_tool ${Open3D_LIBRARIES})
    target_include_directories(data_capture_labeling_tool PRIVATE ${Open3D_INCLUDE_DIRS})
endif()

# Install targets
install(TARGETS dual_format_tester DESTINATION bin)
install(TARGETS data_capture_labeling_tool DESTINATION bin)
if(Open3D_FOUND)
    install(TARGETS dual_pointcloud_viewer DESTINATION bin)
endif()
