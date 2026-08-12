# Interference Detection Fix V2 - Event Clustering

## 🚨 Problem Identified

The previous version still had continuous detections because:
1. **Every pixel** was logged as a separate event
2. **27 events** = 27 pixels detected, but should be 1-2 actual interferences!
3. Each pixel triggered its own console output and counter increment

### Example of Old Behavior:
```
Event 1: Pixel (112, 160) - Penetration: 1478mm
Event 2: Pixel (120, 160) - Penetration: 1450mm
Event 3: Pixel (128, 160) - Penetration: 1420mm
... (10 more pixels from SAME hand/object!)
```

**Total**: 1 real interference = 10-20 logged events! 😱

---

## ✅ Solution: Smart Event Clustering

### Key Changes:

#### 1. **Event Clustering** ⭐
- All detected pixels are now **clustered into ONE event**
- Shows "Affected Area: X pixels" instead of logging each pixel
- Counter increments ONCE per real interference

#### 2. **Stricter Thresholds**
```cpp
DEPTH_CHANGE_THRESHOLD = 200.0f;      // 20cm motion (was 15cm)
MIN_EVENT_INTERVAL_MS = 1000;         // 1 second between events (was 500ms)
MIN_PIXELS_FOR_DETECTION = 3;         // Need at least 3 pixels (noise filtering)
```

#### 3. **Better Preview Settings**
```cpp
confidence_threshold_ = 25;           // Better depth visualization (was 50)
max_distance_ = 3300;                 // Already optimal
```

#### 4. **Monitoring Only Virtual Wall Area**
✅ Already implemented: `isPointInsideBoundary()` ensures we only check inside the green boundary

---

## 🔧 How Event Clustering Works:

### Old Logic (BROKEN):
```cpp
for (each detected pixel) {
    interference_count_++;  // Counter increments 10+ times!
    logEvent(pixel);        // 10+ console outputs!
}
```

### New Logic (FIXED):
```cpp
if (detected_pixels.size() >= 3) {  // Need at least 3 pixels
    interference_count_++;            // Counter increments ONCE
    
    // Find pixel with maximum penetration
    // Calculate center position
    // Create ONE event representing entire region
    
    logSingleEvent(all_pixels);       // ONE console output!
}
```

---

## 📊 What You'll See Now:

### Console Output (NEW FORMAT):
```
🚨 ═══════════════════════════════════════════════
   INTERFERENCE DETECTED
═══════════════════════════════════════════════
📋 Event ID: evt_20251104_155012_981210
🕐 Time: 20251104_155012_354
📊 Affected Area: 15 pixels          ⭐ NEW!
📍 Center Position (X,Y): 53.5 cm, 165.9 cm
🖼️  Center Pixel: (112, 160)
📏 Measured Depth: 874 mm
🧱 Wall Depth: 2352 mm
⚠️  Max Penetration: 1478 mm          ⭐ Shows worst penetration
═══════════════════════════════════════════════
```

**ONE event** instead of 15 separate events! 🎯

---

## 🎯 Detection Flow:

```
1. Establish Baseline (1 second)
   └─> Captures normal state with shelves/items
   
2. For each frame:
   ├─> Check if 1+ seconds passed since last event (rate limiting)
   ├─> Scan inside virtual wall boundary only
   ├─> For each pixel:
   │   ├─> Is confidence high enough? (≥25)
   │   ├─> Is depth valid? (200-3000mm)
   │   ├─> Did it move closer by ≥20cm? (motion detection)
   │   └─> Does it penetrate wall plane?
   │
   ├─> Collect all detected pixels
   ├─> Need at least 3 pixels to be real (not noise)
   └─> Cluster into ONE event
       ├─> Find pixel with max penetration
       ├─> Calculate affected area size
       └─> Log SINGLE event
```

---

## 🔍 Parameters Explained:

### `DEPTH_CHANGE_THRESHOLD = 200.0f` (20cm)
- **Purpose**: How much closer something must move to be detected
- **Too Low**: Detects depth sensor noise, small variations
- **Too High**: Misses real interferences
- **Current**: 20cm is good for hand/object detection

### `MIN_EVENT_INTERVAL_MS = 1000` (1 second)
- **Purpose**: Minimum time between logged events
- **Effect**: If hand stays in wall for 5 seconds, only 5 events (not 150!)
- **Prevents spam** from continuous penetration

### `MIN_PIXELS_FOR_DETECTION = 3`
- **Purpose**: Filter out single-pixel noise
- **Effect**: Need at least 3 pixels to count as real interference
- **Prevents**: Random depth sensor errors from triggering

### `confidence_threshold_ = 25`
- **Purpose**: Lower threshold shows more depth data in preview
- **Effect**: Better visualization, more complete depth map
- **Trade-off**: Slightly more noise, but filtered by other checks

---

## 🧪 Expected Results:

### Before Fix:
- **27 events in 4 seconds** from 1-2 real interferences
- Console spam with nearly identical detections
- Counter increments 10+ times per hand movement

### After Fix:
- **1-2 events in 4 seconds** from 1-2 real interferences ✅
- Clean console output with clustered information
- Counter accurately reflects real interference count

---

## 🎮 Controls:

- **SPACE** - Toggle monitoring on/off
- **R** - Reset interference counter
- **B** - Reset baseline (if you rearrange shelves/items)
- **Q** - Quit monitor

---

## 🚀 Build & Test:

```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make clean
make -j4
sudo ./simple_virtual_wall_monitor
```

### Test Scenarios:

✅ **Should Log 1 Event:**
- Wave hand through wall once
- Place object in wall area
- Walk through boundary once

❌ **Should NOT Spam:**
- Shelves at startup
- Static items
- Hand staying in wall (max 1 event/second)
- Small depth variations/noise

---

## 🔧 Tuning (if needed):

### Still Too Many Events?
```cpp
// Line 59: Increase motion threshold
static constexpr float DEPTH_CHANGE_THRESHOLD = 300.0f; // 30cm

// Line 63: Increase rate limit
static constexpr int MIN_EVENT_INTERVAL_MS = 2000; // 2 seconds

// Line 64: Need more pixels to detect
static constexpr int MIN_PIXELS_FOR_DETECTION = 5; // Need 5+ pixels
```

### Missing Real Interferences?
```cpp
// Line 59: Decrease motion threshold
static constexpr float DEPTH_CHANGE_THRESHOLD = 150.0f; // 15cm

// Line 63: Decrease rate limit
static constexpr int MIN_EVENT_INTERVAL_MS = 500; // 0.5 seconds

// Line 64: Need fewer pixels
static constexpr int MIN_PIXELS_FOR_DETECTION = 2; // Need 2+ pixels
```

---

## 📝 Files Modified:

### `simple_virtual_wall_monitor.cpp`:
- Added `#include <iomanip>` for formatting
- Changed `confidence_threshold_` from 50 to 25
- Changed `DEPTH_CHANGE_THRESHOLD` from 150mm to 200mm
- Changed `MIN_EVENT_INTERVAL_MS` from 500ms to 1000ms
- Added `MIN_PIXELS_FOR_DETECTION = 3`
- Renamed `logInterferenceEvents()` → `logInterferenceEvent()` (singular!)
- Implemented event clustering logic:
  - Find pixel with maximum penetration
  - Calculate affected area size
  - Create ONE event for all pixels
  - Show "Affected Area" in output

---

## 🎯 Summary:

**Before**: 27 events = continuous spam ❌  
**After**: 1-2 events = accurate detection ✅

**Key Insight**: One hand/object = multiple pixels, but should log as **ONE interference event**!

---

## ✅ Success Criteria:

1. ✅ Events counter matches actual interference count
2. ✅ No continuous spam when hand stays in wall
3. ✅ Clean console output (one event per interference)
4. ✅ Better preview with confidence=25
5. ✅ Monitoring only inside virtual wall boundary
6. ✅ Proper motion-based detection (ignores static objects)

---

**Ready to test!** 🚀

