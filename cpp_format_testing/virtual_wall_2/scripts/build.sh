#!/bin/bash

# Simplified Virtual Wall Build Script

set -e  # Exit on any error

echo "╔════════════════════════════════════════════════════════╗"
echo "║   SIMPLIFIED VIRTUAL WALL - BUILD SCRIPT           ║"
echo "╚════════════════════════════════════════════════════════╝"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Project Directory: $PROJECT_DIR${NC}"
echo -e "${BLUE}Build Directory: $BUILD_DIR${NC}"

# Create build directory
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning existing build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Check for required dependencies
echo -e "\n${BLUE}Checking dependencies...${NC}"

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}❌ CMake not found. Please install cmake.${NC}"
    exit 1
fi

# Check for pkg-config
if ! command -v pkg-config &> /dev/null; then
    echo -e "${RED}❌ pkg-config not found. Please install pkg-config.${NC}"
    exit 1
fi

# Check for OpenCV
if ! pkg-config --exists opencv4; then
    if ! pkg-config --exists opencv; then
        echo -e "${RED}❌ OpenCV not found. Please install OpenCV development packages.${NC}"
        exit 1
    fi
fi

# Check for jsoncpp
if ! pkg-config --exists jsoncpp; then
    echo -e "${RED}❌ jsoncpp not found. Please install jsoncpp development packages.${NC}"
    exit 1
fi

# Check for ArducamDepthCamera
if ! pkg-config --exists ArducamDepthCamera; then
    echo -e "${RED}❌ ArducamDepthCamera not found. Please install ArducamDepthCamera library.${NC}"
    exit 1
fi

echo -e "${GREEN}✅ All required dependencies found${NC}"

# Configure with CMake
echo -e "\n${BLUE}Configuring build with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo -e "\n${BLUE}Building applications...${NC}"
make -j$(nproc)

# Check if build was successful
if [ $? -eq 0 ]; then
    echo -e "\n${GREEN}✅ Build completed successfully!${NC}"
    echo ""
    echo -e "${GREEN}Built applications:${NC}"
    echo -e "  📁 simple_virtual_wall_setup  - Setup tool with dimension input"
    echo -e "  📁 simple_virtual_wall_monitor - Monitor tool with X,Y detection"
    echo ""
    echo -e "${BLUE}To run the applications:${NC}"
    echo -e "  ./simple_virtual_wall_setup    - Configure virtual wall"
    echo -e "  ./simple_virtual_wall_monitor  - Start monitoring"
    echo ""
    echo -e "${BLUE}Or use the launcher scripts:${NC}"
    echo -e "  ../scripts/run_setup.sh   - Run setup tool"
    echo -e "  ../scripts/run_monitor.sh - Run monitor tool"
else
    echo -e "${RED}❌ Build failed!${NC}"
    exit 1
fi

# Create necessary directories
echo -e "\n${BLUE}Creating runtime directories...${NC}"
mkdir -p "$PROJECT_DIR/logs"

echo -e "\n${GREEN}🎉 Simplified Virtual Wall System ready!${NC}"
echo -e "${BLUE}This system uses your method:${NC}"
echo -e "  - User-provided ground truth dimensions ✅"
echo -e "  - Simple 2D bilinear mapping ✅"
echo -e "  - Straightforward depth comparison ✅"
echo -e "  - X,Y-only reporting (no Z) ✅"

