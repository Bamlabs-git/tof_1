#!/bin/bash
# Launcher script for C++ ToF Data Capture & Labeling Tool

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "❌ Build directory not found!"
    echo "   Please run ./build.sh first to compile the project"
    exit 1
fi

# Check if executable exists
if [ ! -f "$BUILD_DIR/data_capture_labeling_tool" ]; then
    echo "❌ data_capture_labeling_tool executable not found!"
    echo "   Please run ./build.sh to compile the project"
    exit 1
fi

# Change to build directory
cd "$BUILD_DIR"

echo "🏷️  Starting ToF Data Capture & Labeling Tool..."
echo "📁 Working directory: $(pwd)"

# Show header
echo ""
echo "======================================================"
echo "🏷️  ToF Data Capture & Labeling Tool (C++)"
echo "  📷 Depth Format (Rainbow colormap)"
echo "  🎯 Confidence Format (Grayscale)"
echo "  📶 Amplitude Format (Grayscale)"
echo "  🌐 3D Point Cloud Viewer (Separate window)"
echo "  🏷️  Action Labeling (pickup, touch, return)"
echo "  📹 Multi-format Data Recording"
echo "======================================================"
echo ""
echo "⌨️  Quick Controls:"
echo "   1/2/3 - Set label (pickup/touch/return)"
echo "   4/5 - Set label (no_gesture/hand_present) [NEW - negative classes]"
echo "   SPACE - Start/Stop recording"
echo "   Q - Quit"
echo "   P - Pause/Resume preview"
echo "   +/- - Adjust confidence threshold"
echo "   </> - Adjust max distance"
echo ""
echo "💾 Data will be saved to:"
echo "   📁 /home/dev/Arducam_tof_camera/cpp_format_testing/labeled_data/"
echo "   📊 Formats: PNG frames, MP4 videos, RAW data, PCD point clouds, JSON metadata"
echo "   ⏱️  Frame timing data with nanosecond precision [P0 CRITICAL ENHANCEMENT]"
echo ""

# Set OpenGL version for compatibility (for point cloud viewer)
export MESA_GL_VERSION_OVERRIDE=4.5

# Run the tool with UVC warning filtering
# Filter out the persistent UVC ioctl warnings while keeping important messages
./data_capture_labeling_tool "$@" 2>&1 | grep -v "UVC: ioctl.*failed: Invalid argument" | grep -v "TOFCamera: Could not get current selection"

echo ""
echo "✅ Data Capture & Labeling Tool session completed"
echo "📁 Check the labeled_data directory for your recorded sessions"