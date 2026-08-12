cmake_minimum_required(VERSION 3.16)
project(SimpleVirtualWall)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED)
find_package(jsoncpp REQUIRED)

# Find ArducamDepthCamera
pkg_check_modules(ARDUCAM_DEPTH REQUIRED ArducamDepthCamera)

# Include directories
include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${ARDUCAM_DEPTH_INCLUDE_DIRS})
include_directories(src)

# Link directories
link_directories(${ARDUCAM_DEPTH_LIBRARY_DIRS})

# Add compiler flags for ArducamDepthCamera
add_compile_options(${ARDUCAM_DEPTH_CFLAGS_OTHER})

# Common source files
set(COMMON_SOURCES
    src/common.cpp
    src/simple_virtual_wall_utils.cpp
)

# Setup tool sources
set(SETUP_SOURCES
    src/simple_virtual_wall_setup.cpp
    ${COMMON_SOURCES}
)

# Monitor tool sources
set(MONITOR_SOURCES
    src/simple_virtual_wall_monitor.cpp
    ${COMMON_SOURCES}
)

# Create executables
add_executable(simple_virtual_wall_setup ${SETUP_SOURCES})
add_executable(simple_virtual_wall_monitor ${MONITOR_SOURCES})

# Link libraries for setup tool
target_link_libraries(simple_virtual_wall_setup
    ${OpenCV_LIBS}
    ${ARDUCAM_DEPTH_LIBRARIES}
    jsoncpp_lib
)

# Link libraries for monitor tool
target_link_libraries(simple_virtual_wall_monitor
    ${OpenCV_LIBS}
    ${ARDUCAM_DEPTH_LIBRARIES}
    jsoncpp_lib
)

# Installation
install(TARGETS simple_virtual_wall_setup simple_virtual_wall_monitor
    RUNTIME DESTINATION bin
)

# Install scripts
install(FILES
    scripts/build.sh
    scripts/run_setup.sh
    scripts/run_monitor.sh
    DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Create output directories
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/config)

# Print configuration summary
message(STATUS "")
message(STATUS "╔════════════════════════════════════════════════════════╗")
message(STATUS "║   SIMPLIFIED VIRTUAL WALL BUILD CONFIGURATION      ║")
message(STATUS "╚════════════════════════════════════════════════════════╝")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  OpenCV version: ${OpenCV_VERSION}")
message(STATUS "  ArducamDepthCamera: Found")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "  This simplified system uses:")
message(STATUS "  - User-provided ground truth dimensions")
message(STATUS "  - Simple 2D pixel-to-real-world mapping")
message(STATUS "  - Straightforward depth comparison detection")
message(STATUS "  - X,Y-only coordinate reporting (no Z)")
message(STATUS "")

