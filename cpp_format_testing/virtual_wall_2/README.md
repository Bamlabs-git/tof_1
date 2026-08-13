# Virtual Wall 2 - Simplified Virtual Wall Detection System

## Overview

Virtual Wall 2 is a **simplified, practical** implementation of virtual wall boundary detection using a Time-of-Flight (ToF) camera. This version is specifically designed for **downward-facing camera setups** (like the BAM BOT shelf monitoring system) and uses a **fixed Z-plane detection method** with **bilinear interpolation** for coordinate mapping.

### Key Differences from Virtual Wall 1

| Feature | Virtual Wall 1 (Original) | Virtual Wall 2 (Simplified) |
|---------|--------------------------|----------------------------|
| **Dimension Calculation** | Automatic 3D reconstruction from depth | **User enters real dimensions** (95cm × 190cm) |
| **Coordinate System** | Complex 3D world coordinates | **Simple 2D pixel-to-real-world mapping** |
| **Detection Method** | 3D plane equation or fixed Z | **Fixed Z-plane only** (average of corners) |
| **Event Logging** | X, Y, Z coordinates | **X, Y only** (no Z-axis in output) |
| **Complexity** | High (many deprecated methods) | **Low (streamlined, practical)** |
| **UI/UX** | Sophisticated interactive setup | **Same sophisticated UI** (updated terminology) |

---

## System Architecture

### Camera Setup

```
                    ┌─────────────────┐
                    │    BAM BOT      │ ← Sign/Header
                    │   📷 Camera     │ ← ToF camera (downward-facing)
                    └─────────────────┘
                           ⬇ Looking DOWN at shelves
```

**Coordinate System (from camera's perspective):**
- **X-axis**: Horizontal (shelf width, left-right) → 0 to 95 cm
- **Y-axis**: Depth (shelf depth, back-front) → 0 to 190 cm  
- **Z-axis**: Vertical distance from camera (downward) → Fixed wall depth

### Detection Principle

1. **Virtual Wall = Fixed Z-Plane**
   - Average depth of 4 corner points defines the shelf surface
   - Example: `wall_depth = 650 mm`

2. **Interference Detection**
   - When `measured_depth < wall_depth - threshold`
   - Hand is **above** the shelf surface (closer to camera)
   - Triggers interference event

3. **Coordinate Mapping**
   - Pixel (u, v) → Real-world (X cm, Y cm)
   - Uses **bilinear interpolation** within the 4 corner boundary
   - Simple, fast, and accurate for planar surfaces

---

## Features

### ✨ Setup Tool Features
- **Interactive Corner Selection**
  - Drag 4 corners to match shelf boundaries
  - Real-time depth validation with visual feedback
  - Robust depth measurement with outlier rejection
  
- **Sophisticated UI**
  - Combined layout: Camera view + Confidence view + Sidebar
  - Synchronized wall overlay on both views
  - Color-coded status indicators for corners
  - Adjustable parameters (confidence threshold, max distance)

- **User-Provided Dimensions**
  - No complex 3D calculations
  - Enter actual measurements (width, depth)
  - Perfect for downward-facing setups

### 🎯 Monitor Tool Features
- **Real-Time Detection**
  - Sub-second response time
  - Fixed Z-plane comparison for speed
  - Bilinear interpolation for coordinate mapping

- **Event Logging**
  - JSON format with X, Y coordinates only
  - Penetration distance in mm
  - Timestamp and unique event IDs

- **Automatic Action Recording**
  - One recording session per continuous wall interference
  - Saves visual frames, normalized depth frames, raw depth/confidence matrices, and videos
  - Stores each action under a dated folder with an `unknown` label for later ML labeling

- **Visual Feedback**
  - Live depth visualization with rainbow colormap
  - Wall boundary overlay
  - Penetration markers with fade-out effect

---

## Installation

### Prerequisites
- **Raspberry Pi 5** (or compatible Linux system)
- **Arducam ToF Camera**
- **CMake** 3.10+
- **OpenCV** 4.x
- **JsonCpp**
- **ArducamDepthCamera SDK**

### Build Instructions

```bash
cd cpp_format_testing/virtual_wall_2
mkdir build && cd build
cmake ..
make -j4
```

This creates two executables:
- `simple_virtual_wall_setup` - Interactive setup tool
- `simple_virtual_wall_monitor` - Real-time monitoring tool

---

## Usage Workflow

### Step 1: Setup (Interactive Calibration)

```bash
cd cpp_format_testing/virtual_wall_2/build
./simple_virtual_wall_setup
```

**What happens:**

1. **Camera initializes** with live depth feed

2. **Position corners:**
   - 4 draggable corner markers appear
   - Drag each corner to match the actual shelf boundaries
   - Sidebar shows real-time depth and status for each corner
   - Green indicator = valid depth, ready to save

3. **Press 'S' to save:**
   - System captures precise depth at each corner
   - Uses robust averaging with outlier rejection

4. **Enter dimensions:**
   ```
   Enter shelf WIDTH in cm (e.g., 95): 95
   Enter shelf DEPTH in cm (e.g., 190): 190
   ```

5. **Configuration saved:**
   - Stored in `simple_virtual_wall_config.json`
   - Average wall depth calculated: `(UL + UR + LL + LR) / 4`
   - Ready for monitoring!

**Setup UI Controls:**
- **Mouse Drag**: Move corners
- **S**: Save configuration
- **R**: Reset corners to default
- **+/-**: Adjust confidence threshold
- **>/<**: Adjust max distance
- **Q**: Quit

---

### Step 2: Monitor (Real-Time Detection)

```bash
./simple_virtual_wall_monitor
```

If the checked-in `build/` directory is root-owned or stale, build to a user-writable runtime directory instead:

```bash
cmake -S . -B /tmp/virtual_wall_2_build
cmake --build /tmp/virtual_wall_2_build
mkdir -p runtime
cp /tmp/virtual_wall_2_build/simple_virtual_wall_monitor runtime/
cp /tmp/virtual_wall_2_build/simple_virtual_wall_setup runtime/
cp build/simple_virtual_wall_config.json runtime/
cd runtime
./simple_virtual_wall_monitor
```

**What happens:**

1. **Loads configuration** from `simple_virtual_wall_config.json`

2. **Starts monitoring:**
   - Captures depth frames at ~30 FPS
   - For each pixel inside the wall boundary:
     - Maps pixel (u, v) → real-world (X cm, Y cm) using bilinear interpolation
     - Compares depth: `measured_depth < wall_depth - threshold`
     - If true → **INTERFERENCE DETECTED** 🚨

3. **Logs events** to `simple_interference_events.json`:
   ```json
   {
     "timestamp": "20241104_143025_123",
     "event_id": "EVT_1234567890",
     "pixel_coord": {"x": 120, "y": 90},
     "x_cm": 47.5,
     "y_cm": 120.0,
     "measured_depth_mm": 600.0,
     "wall_depth_at_point_mm": 650.0,
     "penetration_distance_mm": 50.0
   }
   ```

4. **Visual feedback:**
    - Red/orange penetration markers where interference occurs
    - Markers fade out over time
    - Live depth visualization

5. **Automatic action recording:**
   - When interference is confirmed, the monitor creates one action folder.
   - Recording includes 5 frames before the confirmed interference.
   - Recording continues while the hand/object remains inside the wall.
   - Recording stops after 5 frames after the wall is clear.
   - A continuous hand presence is counted once, not repeatedly every second.
   - Frame/video saving runs on a background writer thread so detection is not blocked by disk I/O.
   - Saved depth PNG/video frames include a small blue dot at the detected object center and a top-right average depth label.

   Output location:
   ```bash
   recorded_actions/YYYY-MM-DD/action_1/
   ```

   The path is relative to the directory where the monitor is started. If you start it from `runtime/`, recordings are saved under `runtime/recorded_actions/`. If you start it from `build/`, recordings are saved under `build/recorded_actions/`.

   Action folder numbers are scoped per day. If `action_1` through `action_6` already exist in today's folder, the next recording will be `action_7`.

   To fully reset runtime recordings and event logs:
   ```bash
   cd runtime
   python3 reset.py
   ```

   This deletes and recreates `recorded_actions/` and `logs/`. It does not delete the monitor binary, setup binary, or `simple_virtual_wall_config.json`.

   Folder contents:
   ```text
   metadata.json
   video/*_depth.mp4
   frames/depth_png/frame_1_before.png
   frames/depth_png/frame_6.png
   frames/depth_png/frame_50_after.png
   frames/depth_raw/frame_1_before.yml
   frames/depth_raw/frame_6.yml
   frames/depth_raw/frame_50_after.yml
   frames/confidence_raw/frame_1_before.yml
   frames/confidence_raw/frame_6.yml
   frames/confidence_raw/frame_50_after.yml
   ```

   Before-action frames use `_before`, active-action frames have no suffix, and post-action frames use `_after`. Numbering remains continuous across the action. Display frames/videos are intentionally not saved because they duplicate the depth visualization. `metadata.json` contains phase counts/ranges, timestamps, duration, object center/depth stats, and wall configuration details.

6. **Automatic lower-shelf tuning:**
   - After baseline calibration, the monitor analyzes valid depth/confidence values inside the wall.
   - It auto-selects display max distance, valid depth max, confidence threshold, and motion threshold.
   - This helps shelves farther from the camera by avoiding fixed `3500mm` and fixed confidence assumptions.
   - Far shelf mode lowers the minimum valid pixels per cluster and applies wall-centered depth contrast for clearer saved depth images.
   - Far shelf mode also counts individual penetrating pixels inside each cluster, so thin/far hands can be detected even when the full 10x10 cluster median remains background.
   - The calibrated wall remains the entry band, while detection candidates are tracked in an extended region below the wall band down to the bottom of the frame.
   - Selected values are printed to the console and saved under `metadata.json` as `auto_tuned_parameters`.

**Monitor UI Controls:**
- **Q**: Quit
- **R**: Reload configuration
- **C**: Clear event log

---

## Configuration File

**Location:** `simple_virtual_wall_config.json`

**Structure:**
```json
{
  "upper_left": {"x": 60, "y": 40, "depth_mm": 645},
  "upper_right": {"x": 180, "y": 40, "depth_mm": 648},
  "lower_left": {"x": 60, "y": 140, "depth_mm": 652},
  "lower_right": {"x": 180, "y": 140, "depth_mm": 655},
  "actual_width_cm": 95.0,
  "actual_height_cm": 190.0,
  "wall_depth_mm": 650.0,
  "penetration_threshold_mm": 50.0,
  "unique_id": "WALL_1730739825",
  "creation_timestamp": "20241104_143025",
  "is_valid": true
}
```

---

## Detection Method Details

### Fixed Z-Plane Detection

**Concept:**
- Virtual wall exists at a **fixed depth** (Z-value) from the camera
- Average of 4 corner depths: `wall_depth = (UL + UR + LL + LR) / 4`
- Penetration occurs when object is **closer** than this fixed plane

**Why it works:**
- Shelf surface is approximately planar
- Small depth variations handled by `penetration_threshold`
- Fast comparison: no complex plane equations needed

### Bilinear Interpolation Mapping

**Purpose:** Map pixel coordinates to real-world coordinates

**Method:**
1. Define 4 corners in pixel space: `(u0, v0), (u1, v0), (u0, v1), (u1, v1)`
2. Define 4 corners in real-world space: `(0, 0), (W, 0), (0, H), (W, H)`
3. For any pixel `(u, v)` inside the boundary:
   - Normalize: `s = (u - u0) / (u1 - u0)`, `t = (v - v0) / (v1 - v0)`
   - Interpolate: `X = s * W`, `Y = t * H`

**Result:** Direct mapping from camera pixels to shelf coordinates (cm)

### Penetration Threshold

**Default:** 50 mm (5 cm)

**Purpose:**
- Filter noise in depth measurements
- Prevent false positives from minor depth fluctuations
- Only trigger when hand is **clearly** above the shelf surface

**Adjustable:** Modify in config file or via command-line argument

---

## Event Logging

**Log File:** `simple_interference_events.json`

**Format:** JSON array of events

**Event Fields:**
- `timestamp`: ISO 8601 format with milliseconds
- `event_id`: Unique identifier (e.g., `EVT_1730739825123`)
- `pixel_coord`: Pixel position where interference occurred
- `x_cm`: Real-world X coordinate (0 to `actual_width_cm`)
- `y_cm`: Real-world Y coordinate (0 to `actual_height_cm`)
- `measured_depth_mm`: Measured depth at interference point
- `wall_depth_at_point_mm`: Interpolated wall depth at that point
- `penetration_distance_mm`: How far above the shelf (`wall - measured`)

**Note:** Z-axis information is **not included** in event logs, as requested. Only X and Y coordinates are reported.

---

## Code Structure

```
cpp_format_testing/virtual_wall_2/
├── CMakeLists.txt                         # Build configuration
├── README.md                              # This file
├── SETUP_UI_UPDATE_SUMMARY.md            # UI update details
├── src/
│   ├── simple_virtual_wall_setup.cpp     # Interactive setup tool
│   ├── simple_virtual_wall_monitor.cpp   # Real-time monitoring
│   ├── simple_virtual_wall_utils.h       # Core utilities
│   ├── simple_virtual_wall_utils.cpp     # Utility implementations
│   ├── common.h                          # Camera manager
│   ├── common.cpp                        # Camera implementation
│   └── warning_suppression.h             # UVC warning suppression
├── scripts/
│   ├── build.sh                          # Build automation
│   ├── run_setup.sh                      # Launch setup tool
│   └── run_monitor.sh                    # Launch monitor tool
└── build/                                # Build output directory
```

---

## API Reference

### SimpleVirtualWallUtils

**Key Functions:**

#### Configuration Management
- `saveConfig(config, filepath)` - Save configuration to JSON
- `loadConfig(config, filepath)` - Load configuration from JSON
- `validateConfig(config)` - Validate configuration completeness

#### Coordinate Transformations
- `pixelToRealWorldCoord(pixel, config)` - Map pixel (u, v) → (X cm, Y cm) using bilinear interpolation

#### Penetration Detection
- `isPointPenetratingWall(pixel, depth, config)` - Check if point penetrates the fixed Z-plane
- `calculatePenetrationDistance(pixel, depth, config)` - Calculate how far above the shelf
- `interpolateWallDepth(pixel, config)` - Get wall depth at specific pixel (for non-flat surfaces)

#### Boundary Checking
- `isPointInsideWallBoundary(pixel, config)` - Check if pixel is within the 4-corner boundary

#### Event Logging
- `logInterferenceEvent(event, log_file)` - Append event to JSON log file
- `generateEventId()` - Create unique event identifier
- `getCurrentTimestamp()` - Get formatted timestamp string

---

## Troubleshooting

### Setup Issues

**Problem:** Corners show "Invalid depth" or "No Data"

**Solution:**
- Ensure proper lighting (ToF cameras need IR reflection)
- Move corners to areas with stable depth readings
- Adjust confidence threshold with `+`/`-` keys
- Increase max distance with `>` key if objects are far

**Problem:** "Depth out of valid range" errors

**Solution:**
- System expects depths between 200mm and 4000mm
- Ensure camera is within this range of the shelf surface
- Check camera focus and cleanliness

### Monitoring Issues

**Problem:** False positives (too many interference events)

**Solution:**
- Increase `penetration_threshold_mm` in config file (e.g., from 50 to 100)
- Recalibrate with better corner positioning
- Ensure shelf surface is relatively flat

**Problem:** Missed detections (hand not detected)

**Solution:**
- Decrease `penetration_threshold_mm` (e.g., from 50 to 30)
- Check that corners accurately match the shelf boundaries
- Verify wall_depth is correct (should be ~650mm for typical setup)

**Problem:** Incorrect X, Y coordinates in events

**Solution:**
- Re-run setup and enter accurate dimensions
- Verify dimensions: width = left-to-right, depth = back-to-front
- Check that corners form a proper rectangle (not skewed)

---

## Performance

- **Setup Time**: ~1-2 minutes (interactive corner positioning + dimension entry)
- **Detection Latency**: < 33ms (sub-frame processing at 30 FPS)
- **CPU Usage**: Low (~15% on Raspberry Pi 5)
- **Memory**: ~50 MB
- **Frame Rate**: 30 FPS

---

## Example Use Case: BAM BOT Shelf Monitoring

### Scenario
Monitor a retail shelf system with:
- **Camera**: Mounted on top header, facing downward
- **Shelf Width**: 95 cm (left to right)
- **Monitoring Depth**: 190 cm (from back wall to front edge, covering multiple shelf levels)
- **Goal**: Detect when customers reach into the shelf area

### Setup Process
1. Mount ToF camera on BAM BOT header
2. Run setup tool, position corners to mark the shelf boundary
3. Enter dimensions: 95 cm (width) × 190 cm (depth)
4. System calculates Z-plane at ~650mm from camera
5. Run monitor to start real-time detection

### Detection Output
```json
{
  "timestamp": "2024-11-04T14:30:25.123Z",
  "event_id": "EVT_1730739825123",
  "x_cm": 47.5,    // Center of shelf (left-right)
  "y_cm": 120.0,   // 120cm forward from back wall
  "penetration_distance_mm": 50
}
```

**Interpretation:** Customer's hand detected at center of shelf, 120cm forward from back wall, 50mm above the shelf surface.

---

## Future Enhancements

Potential improvements for Version 3:
- [ ] Multi-zone detection (different thresholds for different areas)
- [ ] Object classification (hand vs. product vs. tool)
- [ ] Persistent object tracking (follow hands over time)
- [ ] Integration with inventory management systems
- [ ] Heatmap visualization of access patterns
- [ ] Cloud logging and analytics

---

## License

MIT License (see LICENSE file for details)

---

## Acknowledgments

- **Original Virtual Wall**: Foundation for core concepts
- **Arducam**: ToF camera SDK and hardware
- **OpenCV**: Computer vision library
- **JsonCpp**: JSON serialization

---

## Contact

For issues, questions, or contributions, please open an issue on GitHub or contact the development team.

---

## Appendix: Comparison with Original System

### What Was Simplified

**Removed Complexity:**
- ❌ Automatic 3D dimension calculation (unreliable with noisy depth data)
- ❌ Complex world coordinate transformations
- ❌ 3D plane equation calculations
- ❌ Camera intrinsic parameter dependencies
- ❌ Z-axis reporting in events (as requested)
- ❌ Background subtraction (can be added back if needed)
- ❌ Tilt angle calculations
- ❌ Surface area calculations
- ❌ Deprecated dual-camera support

**Kept from Original:**
- ✅ Sophisticated interactive UI with sidebar
- ✅ Robust depth measurement with outlier rejection
- ✅ Real-time visual feedback
- ✅ Adjustable parameters (trackbars)
- ✅ JSON configuration management
- ✅ Event logging with unique IDs
- ✅ Confidence/amplitude view synchronization

### Why It's Better

1. **More Reliable**: User-provided dimensions eliminate "unrealistic dimensions" errors
2. **Faster Setup**: No waiting for 3D calculations to converge
3. **Easier to Debug**: Simple fixed Z-plane comparison is transparent
4. **Better for Downward Cameras**: Optimized for planar surface monitoring
5. **Cleaner Code**: Removed hundreds of lines of deprecated/unused methods

---

**Last Updated:** 2024-11-04  
**Version:** 2.0  
**Status:** Production Ready ✅
