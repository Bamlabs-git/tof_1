# Baseline Calibration Tuning

## 🎯 Problem
Still detecting false interferences - baseline period was too short and threshold too sensitive.

## ✅ Solution

### Extended Baseline Period
```cpp
BASELINE_FRAMES_NEEDED = 150  // 5 seconds (was 30 frames / 1 second)
```

**Why?**
- 1 second wasn't enough to capture depth sensor stabilization
- Short baseline may include initial sensor noise/fluctuations
- 5 seconds provides a stable, reliable "normal state" reference
- Averages out any transient depth variations

### Increased Motion Threshold
```cpp
DEPTH_CHANGE_THRESHOLD = 250mm  // 25cm (was 200mm / 20cm)
```

**Why?**
- 20cm was too sensitive to minor depth variations
- ToF sensor has ±15-20mm accuracy at short distances
- Small shelf vibrations or camera movement could trigger false detections
- 25cm ensures only REAL interferences (hand, object) are detected

### Progress Feedback
Added countdown display during calibration:
```
📸 Establishing baseline depth map...
   (Capturing normal state for 5 seconds - please wait)
   ⏳ Keep the area in normal state with shelves/items
   ⏳ Calibrating... 4 seconds remaining
   ⏳ Calibrating... 3 seconds remaining
   ⏳ Calibrating... 2 seconds remaining
   ⏳ Calibrating... 1 seconds remaining
✅ Baseline established! Now monitoring for changes...
```

## 📊 Expected Results

### Before:
- 1 second baseline → captured initial sensor noise
- 20cm threshold → detected small variations
- Result: False positives from depth sensor fluctuations

### After:
- 5 second baseline → stable reference depth map
- 25cm threshold → only real motion detected
- Result: **Only actual hand/object penetrations detected!** ✅

## 🧪 Test Scenarios

✅ **Should DETECT (≥25cm motion):**
- Hand reaching through wall
- Object placed in wall area
- Person walking through boundary
- Large movements into the zone

❌ **Should IGNORE:**
- Depth sensor noise (±15-20mm)
- Small shelf vibrations (<25cm)
- Static objects at startup
- Ambient lighting changes
- Camera micro-movements

## 🔧 Parameters Summary

| Parameter | Old Value | New Value | Purpose |
|-----------|-----------|-----------|---------|
| `BASELINE_FRAMES_NEEDED` | 30 (1s) | 150 (5s) | Stable baseline |
| `DEPTH_CHANGE_THRESHOLD` | 200mm | 250mm | Less sensitive |
| `MIN_EVENT_INTERVAL_MS` | 1000ms | 1000ms | (unchanged) |
| `MIN_PIXELS_FOR_DETECTION` | 3 | 3 | (unchanged) |
| `confidence_threshold` | 25 | 25 | (unchanged) |

## 🚀 Build & Test

```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make simple_virtual_wall_monitor -j4
sudo ./simple_virtual_wall_monitor
```

### What to Expect:

1. **Startup:**
   ```
   📸 Establishing baseline depth map...
   (Capturing normal state for 5 seconds - please wait)
   ```

2. **Wait 5 seconds** - countdown will show progress

3. **After calibration:**
   ```
   ✅ Baseline established! Now monitoring for changes...
   ```

4. **Test interference:**
   - Wave hand through virtual wall
   - Should detect when hand moves >25cm into wall area
   - Should NOT detect small movements or sensor noise

## 🎯 If Still Getting False Positives

If you still see false detections, try:

### Option 1: Even Stricter
```cpp
// Line 60: Increase to 30cm
static constexpr float DEPTH_CHANGE_THRESHOLD = 300.0f;

// Line 64: Need more pixels
static constexpr int MIN_PIXELS_FOR_DETECTION = 5;
```

### Option 2: Longer Baseline
```cpp
// Line 59: 10 seconds instead of 5
static constexpr int BASELINE_FRAMES_NEEDED = 300;
```

### Option 3: Higher Rate Limit
```cpp
// Line 64: 2 seconds between events
static constexpr int MIN_EVENT_INTERVAL_MS = 2000;
```

## 📝 When to Reset Baseline

Press **'B'** to reset baseline if:
- You rearrange items on shelves
- You move the camera
- Lighting conditions change significantly
- You want to recalibrate after making changes

## ✅ Success Criteria

1. ✅ No false positives during normal operation
2. ✅ Detects real hand/object penetrations
3. ✅ 5-second calibration completes successfully
4. ✅ Event counter reflects actual interference count
5. ✅ Clean console output (no spam)

---

**The combination of longer baseline + higher threshold should eliminate false positives!** 🎯

