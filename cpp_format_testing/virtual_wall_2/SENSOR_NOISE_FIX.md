# Sensor Noise Fix - Adjusted Thresholds for ToF Camera

## 🎯 Problem Identified

From debug output analysis, your ToF camera has **massive depth measurement noise:**
- ±400-600mm fluctuation for static objects
- Baseline: 2229mm → Current: 1795mm (434mm change with NO movement!)
- This caused continuous false interference detections

---

## ✅ Solution Implemented

### **Increased Detection Thresholds to Handle Sensor Noise**

#### **1. Motion Threshold:**
```cpp
// OLD (too sensitive):
static constexpr float DEPTH_CHANGE_THRESHOLD = 250.0f; // 25cm

// NEW (accounts for ±600mm sensor noise):
static constexpr float DEPTH_CHANGE_THRESHOLD = 700.0f; // 70cm
```

**Why 700mm?**
- Your sensor has ±600mm noise
- Need threshold > noise level to avoid false positives
- 700mm ensures only REAL motion (hand reaching in) triggers detection
- Static objects with ±600mm fluctuation won't trigger

---

#### **2. Penetration Threshold:**
```cpp
// OLD:
penetration_threshold_mm = 50.0f;  // 5cm

// NEW:
penetration_threshold_mm = 200.0f; // 20cm
```

**Why 200mm?**
- Provides tolerance for sensor noise near wall boundary
- Object must be 20cm+ beyond wall plane to count as penetration
- More robust against depth reading fluctuations

---

#### **3. Minimum Pixels for Detection:**
```cpp
// OLD:
static constexpr int MIN_PIXELS_FOR_DETECTION = 3;

// NEW:
static constexpr int MIN_PIXELS_FOR_DETECTION = 5;
```

**Why 5 pixels?**
- Random noise triggers 1-2 pixels
- Real interference (hand) covers 5+ pixels
- Better signal-to-noise ratio

---

## 📊 How Detection Works Now

### **Your Virtual Wall Setup (CORRECT):**

```
Camera at top (0mm)
     ↓ looking down
     
═══ Upper edge (500mm) ═══      ← Top of detection zone
     ║                           
     ║   VIRTUAL WALL PLANE      
     ║   (interpolated between   
     ║    500mm top and          
     ║    2500mm bottom)          
     ║                           
═══ Lower edge (2500mm) ═══     ← Bottom of detection zone
     
📦 Items at 2200mm              ← Behind wall (safe)
```

### **Detection Algorithm:**

#### **Step 1: Baseline Establishment (5 seconds)**
```
Captures 150 frames, averages depth for each pixel:
  Pixel (160, 128) baseline = 2229mm (shelf item)
  Pixel (168, 160) baseline = 1800mm (shelf surface)
  ... etc
```

#### **Step 2: Monitor for Significant Motion (>700mm)**
```
For each frame:
  Current depth = 1795mm
  Baseline = 2229mm
  Change = 2229 - 1795 = 434mm
  
  434mm > 700mm? NO → Ignore (just noise)
```

#### **Step 3: When Hand Reaches In**
```
Hand at 1200mm:
  Baseline = 2229mm (item was here)
  Current = 1200mm (hand is here now)
  Change = 2229 - 1200 = 1029mm
  
  1029mm > 700mm? YES! → Motion detected ✓
  
  Wall depth at this pixel = 1796mm
  Is 1200mm < (1796mm - 200mm)? 
  Is 1200mm < 1596mm? YES! → Penetration ✓
  
  → LOG INTERFERENCE EVENT! ✅
```

---

## 🔍 Debug Output Interpretation

### **Normal Operation (After Fix):**
```
(No debug output - sensor noise <700mm, filtered out)
```

### **Real Interference:**
```
🔍 DEBUG - Detection Stats:
   Pixels checked: 132
   Pixels with motion (>700mm change): 12
   Pixels penetrating wall: 12
   Max depth change: 1050.0mm at pixel (160, 128)
   → Baseline depth: 2200.0mm
   → Current depth: 1150.0mm   ← Hand!
   → Wall depth: 1796.3mm
   → Penetration threshold: 200.0mm
   ⚠️  Logging interference event (passed all filters)
```

**Key indicators of real interference:**
- ✅ Depth change > 700mm (much more than sensor noise)
- ✅ Multiple pixels affected (5+)
- ✅ Depth significantly closer than wall plane
- ✅ Consistent across 2/3 frames (temporal filter)

---

## 🚀 Testing Instructions

### **1. Rebuild:**
```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make -j4
```

### **2. Re-run Setup (Optional but Recommended):**
```bash
sudo ./simple_virtual_wall_setup
```

This will apply the new **200mm penetration threshold** to your config.

**Or** keep your existing config - the new thresholds will still work!

---

### **3. Test Monitoring:**
```bash
sudo ./simple_virtual_wall_monitor
```

#### **During 5-second baseline:**
- Keep everything in normal state
- Don't move anything

#### **After baseline:**
- **DON'T MOVE ANYTHING for 30 seconds**
- Watch terminal - you should see **NO debug output!** ✅
- If you still see false detections, press 'B' to reset baseline

#### **Test real interference:**
- Slowly move your hand down through the virtual wall area
- Should detect when hand crosses the wall plane
- Should log ONE event (not 250 like before!)

---

## 📋 Expected Results

### **Before Fix:**
```
❌ 250+ interference events in 4 seconds
❌ Continuous false positives
❌ Items on shelf triggering detection
```

### **After Fix:**
```
✅ Zero false positives during normal operation
✅ Reliable detection when hand reaches in
✅ One event per actual interference (not spam)
✅ Debug output only shows real motion (>700mm)
```

---

## 🎚️ Tuning (If Needed)

If you still see issues, you can adjust these values:

### **More Sensitive (detect smaller movements):**
```cpp
static constexpr float DEPTH_CHANGE_THRESHOLD = 500.0f; // 50cm
```

### **Less Sensitive (require larger movements):**
```cpp
static constexpr float DEPTH_CHANGE_THRESHOLD = 900.0f; // 90cm
```

### **Longer Baseline (more stable):**
```cpp
static constexpr int BASELINE_FRAMES_NEEDED = 300; // 10 seconds
```

---

## 📊 Files Modified

1. **`simple_virtual_wall_monitor.cpp`:**
   - Motion threshold: 250mm → 700mm
   - Min pixels: 3 → 5
   - Added comprehensive debug output

2. **`simple_virtual_wall_utils.h`:**
   - Default penetration threshold: 50mm → 200mm

3. **`simple_virtual_wall_setup.cpp`:**
   - Default penetration threshold: 50mm → 200mm

---

## 🎯 Summary

**Root Cause:** ToF camera has ±600mm depth noise, causing static objects to appear to be moving

**Solution:** Increase thresholds above noise level:
- Motion: 700mm (>600mm noise)
- Penetration: 200mm (tolerance zone)
- Min pixels: 5 (real events only)

**Result:** System now distinguishes between sensor noise and actual interference! 🎉

---

**Test it and let me know if you still see false detections!** The debug output will show us exactly what's happening. 🔍

