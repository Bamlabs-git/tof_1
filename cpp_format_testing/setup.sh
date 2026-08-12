#!/bin/bash
# Complete setup script for C++ ToF Format Testing Tool
# This is the ONLY script you need to run after copying the folder!

set -e  # Exit on any error

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

echo "🚀 C++ ToF Format Testing Tool - Complete Setup"
echo "==============================================="
echo "📁 Working directory: $SCRIPT_DIR"
echo ""

# Make scripts executable
echo "🔧 Making all scripts executable..."
chmod +x "$SCRIPT_DIR"/*.sh
echo "✅ Scripts are now executable"

# Install system dependencies
echo ""
echo "📦 Installing system dependencies..."
echo "🔄 Updating package list..."
sudo apt update

echo "🔨 Installing build tools..."
sudo apt install -y build-essential cmake pkg-config git

echo "📷 Installing OpenCV..."
sudo apt install -y libopencv-dev libopencv-contrib-dev

echo "📄 Installing JSON library..."
sudo apt install -y libjsoncpp-dev

echo "🌐 Installing Open3D (optional for 3D point cloud viewer)..."
if sudo apt install -y libopen3d-dev 2>/dev/null; then
    echo "✅ Open3D installed successfully - 3D point cloud viewer will be available"
    OPEN3D_AVAILABLE=true
else
    echo "⚠️  Open3D not available in repositories - 3D point cloud viewer will be disabled"
    echo "   You can still use the main format testing tool"
    OPEN3D_AVAILABLE=false
fi

echo "🛠️  Installing additional useful tools..."
sudo apt install -y htop tree nano vim

# Check Arducam ToF SDK
echo ""
echo "🔍 Checking for Arducam ToF SDK..."
if ldconfig -p | grep -q ArducamDepthCamera; then
    echo "✅ Arducam ToF SDK found"
    SDK_FOUND=true
else
    echo "❌ Arducam ToF SDK not found!"
    echo ""
    echo "⚠️  IMPORTANT: You need to install the Arducam ToF SDK first!"
    echo "   Please run these commands in your main Arducam directory:"
    echo "   cd ~/Arducam_tof_camera"
    echo "   sudo ./Install_dependencies.sh"
    echo ""
    echo "   Then come back and run this setup script again."
    echo ""
    read -p "Do you want to continue anyway? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Setup cancelled. Please install Arducam ToF SDK first."
        exit 1
    fi
    SDK_FOUND=false
fi

# Create saved_data directory
echo ""
echo "📁 Creating saved_data directory..."
mkdir -p "$SCRIPT_DIR/saved_data"

# Set proper permissions
chmod 755 "$SCRIPT_DIR/saved_data"

echo "✅ Save directory created at: $SCRIPT_DIR/saved_data"
echo "📁 Frame-specific subdirectories will be created automatically when saving"

# Build the project
echo ""
echo "🔨 Building C++ ToF Format Testing Tool..."
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "⚙️  Configuring with CMake..."
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17

echo "🔨 Compiling..."
if command -v nproc > /dev/null; then
    JOBS=$(nproc)
else
    JOBS=4
fi
make -j$JOBS

cd "$SCRIPT_DIR"

# Final status report
echo ""
echo "✅ Setup completed successfully!"
echo ""
echo "📋 Installation Summary:"
echo "   ✅ Build tools (gcc, cmake, etc.)"
echo "   ✅ OpenCV development libraries"
echo "   ✅ JSON library (jsoncpp)"
if [ "$OPEN3D_AVAILABLE" = true ]; then
    echo "   ✅ Open3D library - 3D point cloud viewer available"
else
    echo "   ⚠️  Open3D not available - 3D viewer disabled"
fi
if [ "$SDK_FOUND" = true ]; then
    echo "   ✅ Arducam ToF SDK"
else
    echo "   ⚠️  Arducam ToF SDK - please install manually"
fi

echo ""
echo "📋 Built Executables:"
echo "   🔬 build/dual_format_tester - Main format testing tool"
if [ "$OPEN3D_AVAILABLE" = true ] && [ -f "$BUILD_DIR/dual_pointcloud_viewer" ]; then
    echo "   🌐 build/dual_pointcloud_viewer - 3D point cloud viewer"
fi

echo ""
echo "📁 Data Save Location:"
echo "   $SCRIPT_DIR/saved_data/ - Press 's' to save frames here"

echo ""
echo "🎉 Your C++ ToF Format Testing Tool is ready!"
echo ""
echo "🚀 Quick Start Guide:"
echo "   1. Connect both ToF cameras"
echo "   2. Run: ./run.sh"
echo "   3. Press 's' to save frame data"
echo "   4. Press 'q' to quit"
echo ""
echo "📋 Available Commands:"
echo "   ./run.sh                    # Dual camera format testing"
echo "   ./run.sh --single-camera    # Single camera mode"
echo "   ./run.sh --help             # Show all options"
if [ "$OPEN3D_AVAILABLE" = true ]; then
    echo "   ./run_pointcloud.sh         # 3D point cloud viewer"
fi
echo ""
echo "💡 Tips:"
echo "   - Make sure both cameras are connected before running"
echo "   - Saved data goes to: $SCRIPT_DIR/saved_data/"
echo "   - Check README.md for detailed documentation"
echo ""
echo "🎯 Ready to test your ToF cameras! Run './run.sh' to start."
