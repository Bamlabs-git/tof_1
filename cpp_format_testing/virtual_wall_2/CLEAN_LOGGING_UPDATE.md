# Clean Logging System - Monitoring Script 📝

## 🎯 Problem
The monitoring script was generating too much debug output:
- Cluster statistics printed every frame with motion
- "Filtered out" messages cluttering terminal
- Difficult to see actual detection events
- Hard to understand what was being detected

## ✅ Solution Applied

### **1. Added Debug Mode Toggle**
- Press `D` to enable/disable detailed debug logs
- **Default: OFF** (clean logs only)
- **When enabled**: Shows detailed cluster statistics

### **2. Restructured Detection Logs**
**Before:** Verbose, multiple lines, confusing format

**After:** Clean, structured, informative format

---

## 📊 New Log Format

### **Normal Mode (Default - Clean Logs)**

**Only shows when actual detection occurs:**

```
🚨 ══════════════════════════════════════════════════════
   INTERFERENCE DETECTED - Event #1
══════════════════════════════════════════════════════
📋 ID: evt_20251112_143025_123456
🕐 Time: 20251112_143025_123
📍 Position: 47.5 cm (X), 95.3 cm (Y)
📏 Depth: 1630 mm (measured) vs 2150 mm (wall)
⚠️  Penetration: 520 mm
📊 Clusters: 3 affected (10x10 px each)
📌 Detected clusters:
   1. Grid(12,9) Pixel(125,95) Pos(47.5,95.3cm) Depth=1630mm Pen=520mm
   2. Grid(13,9) Pixel(135,95) Pos(52.8,95.3cm) Depth=1645mm Pen=505mm
   3. Grid(12,10) Pixel(125,105) Pos(47.5,100.5cm) Depth=1650mm Pen=500mm
══════════════════════════════════════════════════════
```

**Key Information Shown:**
- ✅ Event number (sequential count)
- ✅ Timestamp and unique ID
- ✅ **Real-world position** (X, Y in cm)
- ✅ **Depth comparison** (measured vs wall)
- ✅ **Penetration distance**
- ✅ **All detected clusters** (up to 5 shown)
- ✅ **Per-cluster details**: Grid position, pixel position, real-world position, depth, penetration

---

### **Debug Mode (Press 'D' to Enable)**

**Shows additional diagnostic information:**

```
🔍 DEBUG - Cluster Stats:
   Total clusters: 68
   Motion clusters: 5
   Penetrating clusters: 3
   Interference clusters: 3
   Temporal filter: 2/3 frames
   Max change: 520.5mm at grid (12, 8)
   → Baseline: 2150.0mm
   → Current: 1629.5mm
   → Valid pixels: 87

[Then shows the normal detection event above]
```

**OR if filtered out:**

```
ℹ️  DEBUG - Filtered: temporal=1/3, clusters=1/2
```

---

## 🎮 New Control: Debug Toggle

| Key | Action |
|-----|--------|
| `D` | Toggle debug mode (detailed logs) |

**Usage:**
```bash
# During monitoring, press 'D' to enable debug mode
# Press 'D' again to disable and return to clean logs
```

---

## 📋 Log Output Structure

### **What Each Line Means:**

#### **Position Information:**
```
📍 Position: 47.5 cm (X), 95.3 cm (Y)
```
- **X**: Horizontal position (0 = left edge, max = shelf width)
- **Y**: Depth position (0 = front edge, max = shelf depth)
- Based on bilinear interpolation from corner positions

#### **Depth Information:**
```
📏 Depth: 1630 mm (measured) vs 2150 mm (wall)
```
- **Measured**: Current depth from ToF sensor (median of cluster)
- **Wall**: Expected depth at that position (interpolated from 4 corners)
- **Difference**: Shows how far the object penetrated

#### **Cluster Information:**
```
📊 Clusters: 3 affected (10x10 px each)
```
- Number of 10×10 pixel clusters that detected interference
- Each cluster = median of up to 100 pixels (noise filtered!)

#### **Detailed Cluster List:**
```
1. Grid(12,9) Pixel(125,95) Pos(47.5,95.3cm) Depth=1630mm Pen=520mm
```
- **Grid(12,9)**: Cluster position in the grid (24×18 total)
- **Pixel(125,95)**: Center pixel of the cluster
- **Pos(47.5,95.3cm)**: Real-world X,Y position
- **Depth=1630mm**: Median depth of this cluster
- **Pen=520mm**: How far this cluster penetrated the wall

---

## 🔍 Diagnostic Guide

### **If Lower Shelf NOT Detecting:**

**Enable debug mode (press 'D')** and watch for:

#### **1. Check if clusters are being created:**
```
🔍 DEBUG - Cluster Stats:
   Total clusters: 68        ← Should be 60-80 for full frame
```
- If **< 30 clusters**: Most of frame has no valid data
- Likely: Confidence threshold too high or depth out of range

#### **2. Check if motion is detected:**
```
   Motion clusters: 5        ← Should be > 0 when hand reaches in
```
- If **0 when hand is in frame**: DEPTH_CHANGE_THRESHOLD too high (350mm)
- Or baseline not established properly

#### **3. Check if penetration is detected:**
```
   Penetrating clusters: 3   ← Should be > 0 when hand penetrates wall
```
- If **motion YES but penetrating NO**: Wall depth interpolation issue
- Or hand is actually behind the wall plane

#### **4. Check the max change value:**
```
   Max change: 520.5mm at grid (12, 8)
   → Baseline: 2150.0mm      ← What depth was during calibration
   → Current: 1629.5mm       ← What depth is now
   → Valid pixels: 87        ← How many pixels in cluster were valid
```
- **Valid pixels**: Should be 80-100 for a good cluster
- If **< 50**: Poor data quality (confidence too low, or noisy area)
- **Baseline vs Current**: Should differ by >350mm for motion detection

#### **5. Check temporal filter:**
```
   Temporal filter: 2/3 frames
```
- Need **2 out of 3 frames** to log
- If stuck at **1/3**: Detections are intermittent (noisy)

#### **6. Check cluster count:**
```
   Interference clusters: 3
   [...]
   ℹ️  DEBUG - Filtered: temporal=2/3, clusters=1/2
```
- Need **MIN_CLUSTERS_FOR_DETECTION = 2** clusters
- If **clusters=1/2**: Only single cluster detected (might be noise)

---

## 🛠️ Troubleshooting Lower Shelf

Based on debug output, here are solutions:

| Debug Shows | Problem | Solution |
|-------------|---------|----------|
| `Total clusters: 20` | Not enough valid data | Lower `confidence_threshold` (currently 15) |
| `Motion clusters: 0` | No motion detected | Lower `DEPTH_CHANGE_THRESHOLD` (currently 350mm) |
| `Penetrating: 0` | Wall plane wrong | Check setup corner positions |
| `Valid pixels: 30` | Poor data quality | Lower confidence threshold or adjust camera |
| `Baseline: 800mm, Current: 750mm` | Change too small | Objects too similar to baseline |
| `Temporal: 1/3` | Intermittent detection | Lower motion threshold or check noise |
| `clusters=1/2` | Only 1 cluster | Lower `MIN_CLUSTERS_FOR_DETECTION` to 1 |

---

## 📝 Key Changes Summary

### **Files Modified:**
- `simple_virtual_wall_monitor.cpp`

### **Changes Made:**
1. ✅ Added `debug_mode_` flag (default: OFF)
2. ✅ Wrapped all debug output in `if (debug_mode_)` checks
3. ✅ Restructured detection log output:
   - Event number added
   - Cleaner format
   - Shows all detected clusters (up to 5)
   - Per-cluster details (grid, pixel, position, depth, penetration)
4. ✅ Added 'D' key to toggle debug mode
5. ✅ Updated controls display

### **Logging Behavior:**
- **Default**: Clean logs, only shows actual detections
- **Debug mode**: Shows cluster statistics, filter reasons, diagnostic info
- **All detections**: Show comprehensive cluster information

---

## 🚀 Testing the New Logging

```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make -j4
sudo ./simple_virtual_wall_monitor
```

### **Normal Operation (Clean Logs):**
1. Wait for 5-second baseline
2. Reach hand into shelf
3. **Should see**: ONE clean detection event with all clusters listed
4. **Should NOT see**: Debug statistics every frame

### **Debug Mode (Detailed Logs):**
1. Press `D` during monitoring
2. Reach hand into shelf
3. **Should see**: 
   - Cluster statistics when motion detected
   - Filter reasons when detection rejected
   - Max change details
   - Then the normal detection event

### **Troubleshooting Lower Shelf:**
1. Press `D` to enable debug mode
2. Move hand over lower shelf area
3. Watch debug output to see:
   - Are clusters being created?
   - Is motion being detected?
   - Is penetration being detected?
   - Why is it being filtered?

---

## 🎯 Expected Results

### **Upper Shelf Detection:**
```
🚨 ══════════════════════════════════════════════════════
   INTERFERENCE DETECTED - Event #1
══════════════════════════════════════════════════════
📍 Position: 47.5 cm (X), 95.3 cm (Y)
📏 Depth: 460 mm (measured) vs 650 mm (wall)
⚠️  Penetration: 190 mm
📊 Clusters: 4 affected
📌 Detected clusters:
   1. Grid(8,2) ... Depth=460mm Pen=190mm
   2. Grid(9,2) ... Depth=475mm Pen=175mm
   [...]
══════════════════════════════════════════════════════
```

### **Lower Shelf Detection (if working):**
```
🚨 ══════════════════════════════════════════════════════
   INTERFERENCE DETECTED - Event #2
══════════════════════════════════════════════════════
📍 Position: 52.0 cm (X), 165.0 cm (Y)
📏 Depth: 2100 mm (measured) vs 2450 mm (wall)
⚠️  Penetration: 350 mm
📊 Clusters: 3 affected
📌 Detected clusters:
   1. Grid(10,16) ... Depth=2100mm Pen=350mm
   2. Grid(11,16) ... Depth=2115mm Pen=335mm
   [...]
══════════════════════════════════════════════════════
```

**Note the Y position:** Lower shelf should have Y ~ 140-190 cm (back of shelf)

---

## 💡 Understanding the Virtual Wall

From your setup screenshot:
- **UL (Upper Left)**: 460mm depth
- **UR (Upper Right)**: 648mm depth
- **LL (Lower Left)**: 2410mm depth
- **LR (Lower Right)**: 2500mm depth

**The virtual wall is a VERTICAL PLANE:**
```
Camera 📷 (top view)
    ↓ 460mm
    UL────────UR (Upper edge - close to camera)
    │          │
    │  WALL    │  Vertical plane
    │          │
    LL────────LR (Lower edge - far from camera)
    ↓ 2410mm   ↓ 2500mm
```

**Detection works when:**
- Baseline depth (no hand): ~460-2500mm (wall surface)
- Current depth (with hand): < baseline - 350mm (closer than wall)
- Result: "Hand penetrated wall" → LOG!

---

The logging system is now **clean by default** with **optional detailed debugging** when needed! 🎯

