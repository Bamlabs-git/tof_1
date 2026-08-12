#!/bin/bash
# Launcher script for C++ 3D Point Cloud Viewer

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "❌ Build directory not found!"
    echo "   Please run ./build.sh first to compile the project"
    exit 1
fi

# Check if executable exists
if [ ! -f "$BUILD_DIR/dual_pointcloud_viewer" ]; then
    echo "❌ dual_pointcloud_viewer executable not found!"
    echo "   This means Open3D was not available during compilation."
    echo "   Install Open3D and rebuild:"
    echo "   sudo apt install libopen3d-dev"
    echo "   ./build.sh"
    exit 1
fi

# Change to build directory
cd "$BUILD_DIR"

# Set OpenGL version for compatibility
export MESA_GL_VERSION_OVERRIDE=4.5

echo "🌐 Starting C++ 3D Point Cloud Viewer..."
echo "📁 Working directory: $(pwd)"
echo "🎮 Controls:"
echo "   Mouse: Rotate, zoom, pan"
echo "   Close window to exit"
echo ""

# Run the point cloud viewer with all passed arguments
./dual_pointcloud_viewer "$@"
