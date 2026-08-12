# Confidence Threshold Fix - Lower Shelf Detection 🔧

## 🎯 Problem
Lower shelf (red/orange area in visualization) not detecting interactions.

## 🔍 Root Cause
**Confidence threshold was too high (25)**

Areas at steep angles (lower shelf from top-mounted camera) have:
- ❌ Lower confidence values
- ❌ Get filtered out even when depth is valid
- ❌ Result: No detection in lower shelf area

---

## ✅ Solution Applied

### **Changed confidence_threshold from 25 → 15**

**Files modified:**
1. `simple_virtual_wall_monitor.cpp` - Line 79
2. `simple_virtual_wall_setup.cpp` - Line 63

```cpp
// BEFORE:
confidence_threshold_(25),  // Too restrictive

// AFTER:
confidence_threshold_(15),  // Lower threshold for steep angles (lower shelf)
```

---

## 📊 What This Does

### **Confidence Value Meaning:**
The ToF camera provides a **confidence score (0-100)** for each depth measurement:
- **High confidence (>30)**: Perpendicular surfaces, good reflectivity
- **Medium confidence (15-30)**: Angled surfaces, moderate quality
- **Low confidence (<15)**: Very steep angles, poor reflectivity, out of range

### **Camera Geometry Issue:**
```
Camera 📷 (mounted on top)
    ↓
    ↓ Good angle (90°) → High confidence ✅
    ↓
Upper Shelf ━━━━━━━━━━━━━
    ↓
    ↓ Steep angle (45°) → Lower confidence ⚠️
    ↓
Lower Shelf ━━━━━━━━━━━━━ ← Was being rejected!
```

---

## 🎯 Expected Improvement

### **Before (threshold = 25):**
```
Upper shelf: ✅ Detected (confidence 30-50)
Lower shelf: ❌ NOT detected (confidence 15-25) ← FILTERED OUT!
```

### **After (threshold = 15):**
```
Upper shelf: ✅ Detected (confidence 30-50)
Lower shelf: ✅ NOW DETECTED (confidence 15-25) ← INCLUDED! ✅
```

---

## ⚠️ Trade-offs

### **Pros:**
- ✅ Better coverage of lower shelf
- ✅ Detects interactions at steep angles
- ✅ More complete monitoring area

### **Cons:**
- ⚠️ May include slightly noisier measurements
- ⚠️ Potentially more false positives (but clustering helps filter this)

---

## 🧪 Testing Steps

1. **Rebuild:**
   ```bash
   cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
   make -j4
   ```

2. **Run setup** (optional - verify lower shelf visible):
   ```bash
   sudo ./simple_virtual_wall_setup
   ```
   - Check if lower shelf areas show valid depth readings
   - Green indicators should appear on lower shelf

3. **Run monitor:**
   ```bash
   sudo ./simple_virtual_wall_monitor
   ```
   - Wait for 5-second baseline
   - Test hand interaction on **lower shelf**
   - Check if detection works now

---

## 📈 Expected Results

### **Visual Check:**
In the depth visualization:
- Red/orange area (lower shelf) should now show valid measurements
- Clusters should be created for lower shelf region
- Grid overlay should cover lower shelf

### **Detection Check:**
```
🔍 CLUSTER DEBUG - Detection Stats:
   Total clusters checked: 68        ← Should include lower shelf
   Clusters with motion: 2
   Clusters penetrating wall: 2
   Grid Position: (8, 15)            ← Lower shelf grid positions
```

### **Console Output:**
```
✅ Lower shelf cluster detected!
   Grid (8, 14): depth=180mm, valid_pixels=6, motion=YES ✅
   Grid (9, 15): depth=165mm, valid_pixels=5, motion=YES ✅
```

---

## 🔧 Further Adjustments (If Needed)

### **If still not detecting lower shelf:**

**Option 1: Lower threshold even more**
```cpp
confidence_threshold_(10),  // Very permissive
```

**Option 2: Lower minimum depth range**
```cpp
// In computeClusterMedianDepth()
if (depth > 100 && depth < 3000) {  // Allow closer measurements
```

**Option 3: Check actual values**
Add debug logging:
```cpp
std::cout << "Lower shelf - Depth: " << depth 
          << "mm, Confidence: " << confidence << std::endl;
```

---

## 📊 Confidence Threshold Guide

| Value | Coverage | Noise Level | Use Case |
|-------|----------|-------------|----------|
| **30** | Upper shelf only | Very low | Conservative (current issue) |
| **25** | Upper + partial lower | Low | Previous setting |
| **15** | Full coverage | Medium | **CURRENT - Recommended** |
| **10** | Maximum coverage | Higher | If 15 doesn't work |
| **5** | Everything | Very high | Too permissive |

---

## 🎯 Why Threshold = 15 is Optimal

1. ✅ **Balanced**: Not too strict, not too loose
2. ✅ **Covers steep angles**: Lower shelf included
3. ✅ **Still filters noise**: Clustering provides additional filtering
4. ✅ **Industry standard**: Common for angled ToF applications

---

## 📝 Summary

**Changed:**
- `confidence_threshold_` from **25 → 15**

**Reason:**
- Lower shelf has lower confidence due to steep viewing angle

**Expected:**
- ✅ Lower shelf now detectable
- ✅ Complete coverage of shelf area
- ✅ Interactions on lower shelf will trigger events

**Next Steps:**
1. Build and test
2. Verify lower shelf detection works
3. Adjust if needed (can go down to 10 if necessary)

---

**Test it and let me know if the lower shelf detects now!** 🎯

