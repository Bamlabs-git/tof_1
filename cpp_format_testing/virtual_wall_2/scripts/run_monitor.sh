#!/bin/bash

# Simplified Virtual Wall Monitor Launcher Script

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   SIMPLIFIED VIRTUAL WALL MONITOR                  ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
EXECUTABLE="$BUILD_DIR/simple_virtual_wall_monitor"
CONFIG_FILE="$BUILD_DIR/simple_virtual_wall_config.json"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}❌ Build directory not found: $BUILD_DIR${NC}"
    echo -e "${YELLOW}💡 Please run build.sh first to compile the applications.${NC}"
    exit 1
fi

# Check if executable exists
if [ ! -f "$EXECUTABLE" ]; then
    echo -e "${RED}❌ Monitor executable not found: $EXECUTABLE${NC}"
    echo -e "${YELLOW}💡 Please run build.sh first to compile the applications.${NC}"
    exit 1
fi

# Check if executable is executable
if [ ! -x "$EXECUTABLE" ]; then
    echo -e "${YELLOW}⚠️ Making executable file executable...${NC}"
    chmod +x "$EXECUTABLE"
fi

# Check if configuration exists
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}❌ Virtual wall configuration not found: $CONFIG_FILE${NC}"
    echo -e "${YELLOW}💡 Please run the setup tool first: ../scripts/run_setup.sh${NC}"
    exit 1
fi

# Set up environment
export LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH"

# Check camera permissions
echo -e "\n${BLUE}🔍 Checking camera permissions...${NC}"
if [ ! -r /dev/video0 ] && [ ! -r /dev/video1 ]; then
    echo -e "${YELLOW}⚠️ Camera devices may not be accessible.${NC}"
    echo -e "${YELLOW}   Make sure cameras are connected and you have proper permissions.${NC}"
    echo -e "${YELLOW}   You may need to run: sudo usermod -a -G video \$USER${NC}"
fi

# Display configuration info
echo -e "\n${GREEN}📋 Configuration Status:${NC}"
echo -e "══════════════════════════════════════════════════════"
echo -e "Configuration file: $CONFIG_FILE"
if command -v jq &> /dev/null; then
    echo -e "Stand width:  $(jq -r '.dimensions.actual_width_cm' "$CONFIG_FILE") cm"
    echo -e "Stand height: $(jq -r '.dimensions.actual_height_cm' "$CONFIG_FILE") cm"
    echo -e "Wall depth:   $(jq -r '.dimensions.wall_depth_mm' "$CONFIG_FILE") mm"
else
    echo -e "${YELLOW}💡 Install 'jq' to see configuration details${NC}"
fi
echo -e "══════════════════════════════════════════════════════"

echo -e "\n${GREEN}🎮 MONITOR CONTROLS:${NC}"
echo -e "══════════════════════════════════════════════════════"
echo -e "   SPACE - Toggle monitoring on/off"
echo -e "   R     - Reset interference counter"
echo -e "   Q     - Quit monitor"
echo -e "══════════════════════════════════════════════════════"

echo -e "\n${BLUE}📊 DETECTION METHOD:${NC}"
echo -e "══════════════════════════════════════════════════════"
echo -e "   Simple depth comparison (your improved method!)"
echo -e "   Reports X,Y coordinates only (no Z)"
echo -e "   Based on ground truth dimensions you provided"
echo -e "══════════════════════════════════════════════════════"

# Ask for confirmation
echo ""
read -p "Press Enter to start the monitor, or Ctrl+C to cancel..."

echo -e "\n${GREEN}🚀 Starting Simplified Virtual Wall Monitor...${NC}"

# Change to build directory and run
cd "$BUILD_DIR"

# Run the monitor tool with UVC warning filtering
"$EXECUTABLE" 2>&1 | grep -v -E "\[WARN\] UVC: ioctl \(-?[0-9]+\) failed: Invalid argument" | grep -v "TOFCamera: Could not get current selection" | grep -v "UVC: ioctl.*failed"

# Check exit status
EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]; then
    echo -e "\n${GREEN}✅ Monitor completed successfully!${NC}"
    
    # Show log files
    LOG_DIR="$BUILD_DIR/logs"
    if [ -d "$LOG_DIR" ]; then
        echo -e "${BLUE}📝 Event logs saved in: $LOG_DIR${NC}"
        echo -e "${BLUE}Recent log files:${NC}"
        ls -lt "$LOG_DIR"/*.json 2>/dev/null | head -5 || echo "No log files found"
    fi
else
    echo -e "${RED}❌ Monitor exited with error code: $EXIT_CODE${NC}"
    exit $EXIT_CODE
fi

