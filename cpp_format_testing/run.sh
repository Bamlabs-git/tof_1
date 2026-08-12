#!/bin/bash
# Launcher script for C++ ToF Format Testing Tool

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "❌ Build directory not found!"
    echo "   Please run ./build.sh first to compile the project"
    exit 1
fi

# Check if executable exists
if [ ! -f "$BUILD_DIR/dual_format_tester" ]; then
    echo "❌ dual_format_tester executable not found!"
    echo "   Please run ./build.sh to compile the project"
    exit 1
fi

# Change to build directory
cd "$BUILD_DIR"

echo "🚀 Starting C++ ToF Format Testing Tool..."
echo "📁 Working directory: $(pwd)"

# Show header
echo ""
echo "==================================================="
echo "🔬 ToF Camera Format Testing Tool (C++)"
echo "  📷 Depth Format"
echo "  🎯 Confidence Format"
echo "  📶 Amplitude Format"
echo "==================================================="

# Run the tool with UVC warning filtering
# Filter out the persistent UVC ioctl warnings while keeping important messages
./dual_format_tester "$@" 2>&1 | grep -v "UVC: ioctl.*failed: Invalid argument" | grep -v "TOFCamera: Could not get current selection"
