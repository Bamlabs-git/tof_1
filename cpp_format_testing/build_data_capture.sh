#!/bin/bash
# Build script for ToF Data Capture & Labeling Tool

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build"

echo "🔨 Building ToF Data Capture & Labeling Tool..."
echo "📁 Source directory: $SCRIPT_DIR"
echo "📁 Build directory: $BUILD_DIR"
echo ""

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo "📁 Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Change to build directory
cd "$BUILD_DIR"

echo "🔧 Running CMake configuration..."
# Configure with CMake
if ! cmake ..; then
    echo "❌ CMake configuration failed!"
    echo "   Please check that all dependencies are installed:"
    echo "   - ArducamDepthCamera SDK"
    echo "   - OpenCV (libopencv-dev)"
    echo "   - jsoncpp (libjsoncpp-dev)"
    echo "   - Open3D (libopen3d-dev) - optional for 3D viewer"
    exit 1
fi

echo ""
echo "🔨 Compiling project..."
# Build the project
if ! make -j$(nproc); then
    echo "❌ Compilation failed!"
    echo "   Please check the error messages above"
    exit 1
fi

echo ""
echo "✅ Build completed successfully!"
echo ""
echo "📋 Available executables:"
if [ -f "dual_format_tester" ]; then
    echo "   ✅ dual_format_tester - Format testing tool"
else
    echo "   ❌ dual_format_tester - Failed to build"
fi

if [ -f "data_capture_labeling_tool" ]; then
    echo "   ✅ data_capture_labeling_tool - Data capture & labeling tool"
else
    echo "   ❌ data_capture_labeling_tool - Failed to build"
fi

if [ -f "dual_pointcloud_viewer" ]; then
    echo "   ✅ dual_pointcloud_viewer - 3D point cloud viewer"
else
    echo "   ⚠️  dual_pointcloud_viewer - Not built (Open3D not available)"
fi

echo ""
echo "🚀 To run the data capture & labeling tool:"
echo "   ./run_data_capture.sh"
echo ""
echo "📊 Tool features:"
echo "   - Live preview dashboard (Depth, Confidence, Amplitude)"
echo "   - 3D point cloud viewer (separate window)"
echo "   - Action labeling (pickup, touch, return)"
echo "   - Multi-format data recording (PNG, MP4, RAW, PCD, JSON)"
echo "   - Sequential session numbering (data001, data002, ...)"
echo "   - Optimized for machine learning data collection"