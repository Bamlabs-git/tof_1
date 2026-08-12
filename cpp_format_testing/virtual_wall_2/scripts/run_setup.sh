#!/bin/bash

# Simplified Virtual Wall Setup Launcher Script

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   SIMPLIFIED VIRTUAL WALL SETUP                    ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
EXECUTABLE="$BUILD_DIR/simple_virtual_wall_setup"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}❌ Build directory not found: $BUILD_DIR${NC}"
    echo -e "${YELLOW}💡 Please run build.sh first to compile the applications.${NC}"
    exit 1
fi

# Check if executable exists
if [ ! -f "$EXECUTABLE" ]; then
    echo -e "${RED}❌ Setup executable not found: $EXECUTABLE${NC}"
    echo -e "${YELLOW}💡 Please run build.sh first to compile the applications.${NC}"
    exit 1
fi

# Check if executable is executable
if [ ! -x "$EXECUTABLE" ]; then
    echo -e "${YELLOW}⚠️ Making executable file executable...${NC}"
    chmod +x "$EXECUTABLE"
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

# Display instructions
echo -e "\n${GREEN}📋 SETUP WORKFLOW:${NC}"
echo -e "══════════════════════════════════════════════════════"
echo -e "1. POSITION CORNERS: Drag the 4 corner markers in the"
echo -e "   camera view to match your virtual wall boundaries"
echo -e ""
echo -e "2. PRESS 'S': When all corners are positioned correctly"
echo -e ""
echo -e "3. ENTER DIMENSIONS: You'll be prompted to enter:"
echo -e "   • Stand width (e.g., 95 cm)"
echo -e "   • Stand height (e.g., 190 cm)"
echo -e "   ** IMPORTANT: Measure with tape measure! **"
echo -e ""
echo -e "4. SAVE: Configuration will be saved automatically"
echo -e "══════════════════════════════════════════════════════"

# Ask for confirmation
echo ""
read -p "Press Enter to start the setup tool, or Ctrl+C to cancel..."

echo -e "\n${GREEN}🚀 Starting Simplified Virtual Wall Setup...${NC}"

# Change to build directory and run
cd "$BUILD_DIR"

# Run the setup tool with UVC warning filtering
"$EXECUTABLE" 2>&1 | grep -v -E "\[WARN\] UVC: ioctl \(-?[0-9]+\) failed: Invalid argument" | grep -v "TOFCamera: Could not get current selection" | grep -v "UVC: ioctl.*failed"

# Check exit status
EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]; then
    echo -e "\n${GREEN}✅ Setup completed successfully!${NC}"
    
    # Check if configuration was created
    CONFIG_FILE="$BUILD_DIR/simple_virtual_wall_config.json"
    if [ -f "$CONFIG_FILE" ]; then
        echo -e "${GREEN}📄 Configuration saved to: $CONFIG_FILE${NC}"
        echo -e "${BLUE}🎉 You can now run the monitor tool: ../scripts/run_monitor.sh${NC}"
    else
        echo -e "${YELLOW}⚠️ Configuration file not found. Make sure you saved the configuration.${NC}"
    fi
else
    echo -e "${RED}❌ Setup tool exited with error code: $EXIT_CODE${NC}"
    exit $EXIT_CODE
fi

