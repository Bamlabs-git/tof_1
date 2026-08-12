# Motion-Based Interference Detection Fix

## 🚨 Problem Identified

The previous algorithm was detecting **ALL objects closer than the wall** as interferences:
- Shelves at 874mm vs wall at 2352mm → **FALSE POSITIVE**
- Items on shelves → **FALSE POSITIVE**  
- Static objects → **FALSE POSITIVE**
- Result: **250+ false detections in 4 seconds!** 😱

### Old Logic (BROKEN):
```cpp
if (measured_depth < wall_depth - threshold) {
    INTERFERENCE! // Triggers on EVERYTHING closer than the wall
}
```

---

## ✅ Solution: Motion-Based Detection

The new system uses **baseline comparison** to detect only **ACTIVE penetrations**:

### How It Works:

1. **📸 Baseline Establishment (First ~1 second)**
   - Captures the "normal state" with shelves, items, etc.
   - Averages 30 frames to create a reliable depth map
   - All static objects are recorded as baseline

2. **🔍 Motion Detection**
   - Compares current depth to baseline depth
   - **Only triggers if**:
     - Something moved **CLOSER** by >15cm (DEPTH_CHANGE_THRESHOLD)
     - AND it penetrates the virtual wall plane
     - AND confidence is high enough
     - AND inside the wall boundary

3. **⏱️ Event Rate Limiting**
   - Minimum 500ms between events
   - Prevents spam from single penetration
   - Clusters nearby detections

4. **🎯 Temporal Filtering**
   - Must detect in 2 out of 3 consecutive frames
   - Reduces false positives from noise

---

## 🆕 New Features

### Key Parameters:
- `BASELINE_FRAMES_NEEDED = 30` (1 second of calibration)
- `DEPTH_CHANGE_THRESHOLD = 150.0f` (15cm motion threshold)
- `MIN_EVENT_INTERVAL_MS = 500` (0.5 second between events)

### New Controls:
- **SPACE** - Toggle monitoring on/off
- **R** - Reset interference counter
- **B** - Reset baseline (recalibrate if you move items) ⭐ NEW!
- **Q** - Quit monitor

---

## 🔧 Detection Logic:

```cpp
// Get baseline (what was there originally)
float baseline = baseline_depth_.at<float>(y, x);

// Get current depth
float current_depth = depth_frame_.at<float>(y, x);

// Calculate change
float depth_change = baseline - current_depth;

// Only detect if:
// 1. Something moved CLOSER by >15cm
if (depth_change > 150.0f) {
    // 2. AND it penetrates the wall plane
    if (isPointPenetrating(pixel, current_depth, config_)) {
        REAL_INTERFERENCE_DETECTED!
    }
}
```

---

## 📊 What You'll See:

### At Startup:
```
📸 Establishing baseline depth map...
   (Capturing normal state with shelves/items)
✅ Baseline established! Now monitoring for changes...
```

### During Monitoring:
- **Static objects** (shelves, items) → Ignored ✅
- **Hand reaching past wall** → Detected! 🚨
- **Object placed through wall** → Detected! 🚨
- **Camera vibration/noise** → Filtered out ✅

### When Interference Detected:
```
🚨 ═══════════════════════════════════════════════
   INTERFERENCE DETECTED
═══════════════════════════════════════════════
📋 Event ID: evt_20251104_155012_981210
🕐 Time: 20251104_155012_354
📍 Position (X,Y): 53.5 cm, 165.9 cm
🖼️  Pixel: (112, 160)
📏 Measured Depth: 874 mm
🧱 Wall Depth: 2352 mm
⚠️  Penetration: 1478 mm
═══════════════════════════════════════════════
```

---

## 🎯 Usage Instructions:

### 1. Start Monitoring:
```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
sudo ./simple_virtual_wall_monitor
```

### 2. Wait for Baseline (1 second):
- Keep the area clear or in normal state
- Let it capture the baseline with shelves/items
- You'll see: "✅ Baseline established!"

### 3. Start Testing:
- Reach your hand past the virtual wall
- Move objects through the boundary
- See detections in terminal with minimal false positives!

### 4. If You Move Items:
- Press **'B'** to reset baseline
- System will recalibrate for new "normal state"
- Takes ~1 second to establish new baseline

---

## 🔍 Troubleshooting:

### Too Many Detections:
- Increase `DEPTH_CHANGE_THRESHOLD` (line 59) to 200 or 250
- Increase `MIN_EVENT_INTERVAL_MS` (line 63) to 1000

### Missing Detections:
- Decrease `DEPTH_CHANGE_THRESHOLD` to 100
- Decrease `MIN_EVENT_INTERVAL_MS` to 200
- Press 'B' to reset baseline if environment changed

### Baseline Issues:
- Make sure shelves/items are in place during startup
- Press 'B' to recalibrate after moving things
- Increase `BASELINE_FRAMES_NEEDED` for more stable baseline

---

## 🧪 Testing Scenarios:

✅ **Should DETECT:**
- Hand reaching through wall area
- Object moving into wall zone
- Person walking through boundary

❌ **Should IGNORE:**
- Shelves at startup
- Items placed on shelves before baseline
- Camera vibration/noise
- Ambient lighting changes

---

## 📝 Files Modified:

- `simple_virtual_wall_monitor.cpp` - Complete rewrite of detection logic
  - Added baseline depth map
  - Added motion detection
  - Added event rate limiting
  - Added 'B' key for baseline reset
  - Updated sidebar controls

---

## 🚀 Build & Test:

```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make simple_virtual_wall_monitor -j4
sudo ./simple_virtual_wall_monitor
```

**Expected Result**: Only detects ACTIVE penetrations, not static objects! 🎯

