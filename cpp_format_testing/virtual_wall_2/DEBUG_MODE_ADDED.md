# Debug Mode Added to Interference Detection

## 🎯 Purpose
Added detailed diagnostic output to identify WHY false interference is being detected.

---

## 📊 What the Debug Output Shows

### **Every time motion or penetration is detected, you'll see:**

```
🔍 DEBUG - Detection Stats:
   Pixels checked: 156
   Pixels with motion (>250mm change): 12
   Pixels penetrating wall: 8
   Max depth change: 387.5mm at pixel (120, 144)
   → Baseline depth: 1500.0mm
   → Current depth: 1112.5mm
   → Wall depth: 2560.0mm
   → Penetration threshold: 100mm
   ⚠️  Logging interference event (passed all filters)
```

### **If filtered out (not logged):**

```
🔍 DEBUG - Detection Stats:
   Pixels checked: 156
   Pixels with motion (>250mm change): 2
   Pixels penetrating wall: 2
   Max depth change: 285.0mm at pixel (96, 152)
   → Baseline depth: 1800.0mm
   → Current depth: 1515.0mm
   → Wall depth: 2560.0mm
   → Penetration threshold: 100mm
   ℹ️  Filtered out: detection_count=1/3, interference_points=2 (need 3+)
```

---

## 🔍 How to Interpret the Output

### **Key Metrics:**

1. **Pixels checked:** How many pixels are in the virtual wall boundary
2. **Pixels with motion:** How many have depth change > 250mm
3. **Pixels penetrating:** How many pass BOTH motion AND penetration checks
4. **Max depth change:** The largest depth change detected

### **Critical Values to Check:**

#### **Baseline depth:**
- Should be the depth captured during calibration (e.g., 1500mm for shelf surface)
- If this is wrong, baseline was corrupted

#### **Current depth:**
- Real-time depth measurement at this pixel
- Should be ~same as baseline if nothing changed
- Should be MUCH less (closer) if hand is interfering

#### **Wall depth:**
- The interpolated depth of the virtual wall plane at this pixel
- Should be at or behind the shelf surface (e.g., 2500mm)

#### **Depth change (baseline - current):**
- **Positive = Something moved CLOSER** (potential interference)
- **Negative = Something moved FARTHER** (ignore)
- **~0 = No change** (normal)

---

## 🐛 Diagnosing False Positives

### **Scenario 1: Depth Sensor Noise**
```
🔍 DEBUG - Detection Stats:
   Pixels with motion (>250mm change): 45
   Max depth change: 320.0mm
   → Baseline depth: 1500.0mm
   → Current depth: 1180.0mm  ← Fluctuating wildly!
```

**Problem:** Depth sensor readings are unstable (>250mm variation)

**Solution:** 
- Increase threshold to 400mm or 500mm
- Extend baseline period to 10+ seconds
- Check for reflective surfaces causing noise

---

### **Scenario 2: Baseline Not Established Properly**
```
🔍 DEBUG - Detection Stats:
   Pixels with motion (>250mm change): 120
   Max depth change: 800.0mm
   → Baseline depth: 2300.0mm  ← Wrong!
   → Current depth: 1500.0mm   ← This is correct (shelf)
```

**Problem:** Baseline captured wrong depth (maybe before items were placed?)

**Solution:**
- Press 'B' to reset baseline
- Ensure items are in "normal" position during 5-second calibration
- Don't move anything during baseline capture

---

### **Scenario 3: Wall Plane Defined Incorrectly**
```
🔍 DEBUG - Detection Stats:
   Pixels with motion (>250mm change): 0
   Pixels penetrating wall: 25  ← Many penetrating without motion!
   Max depth change: 15.0mm     ← No significant motion
   → Baseline depth: 1500.0mm
   → Current depth: 1485.0mm    ← Almost same (good)
   → Wall depth: 1400.0mm       ← Wall is IN FRONT of items! Wrong!
```

**Problem:** Virtual wall plane is defined closer than the actual shelf surface

**Solution:**
- Re-run setup tool
- Make sure corner points are placed ON the shelf edges
- Virtual wall should be AT or BEHIND shelf surface, not in front

---

### **Scenario 4: Items on Shelf ARE Interfering**
```
🔍 DEBUG - Detection Stats:
   Pixels with motion (>250mm change): 0
   Pixels penetrating wall: 50
   Max depth change: -10.0mm  ← Negative! Moving away?
   → Baseline depth: 1500.0mm
   → Current depth: 1510.0mm  ← Slightly farther
   → Wall depth: 2500.0mm
```

**Problem:** This should NOT trigger (no motion closer)

**Cause:** Bug in the logic - depth_change is negative, but still checking penetration?

---

## ✅ Expected Output for CORRECT Operation

### **Normal state (no interference):**
```
(No debug output - nothing detected)
```

### **Hand reaches in:**
```
🔍 DEBUG - Detection Stats:
   Pixels checked: 156
   Pixels with motion (>250mm change): 18
   Pixels penetrating wall: 18
   Max depth change: 550.0mm at pixel (128, 136)
   → Baseline depth: 1850.0mm
   → Current depth: 1300.0mm   ← Hand is 55cm closer!
   → Wall depth: 2560.0mm
   → Penetration threshold: 100mm
   ⚠️  Logging interference event (passed all filters)
```

---

## 🚀 Testing Instructions

### **1. Rebuild with Debug Output:**
```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make simple_virtual_wall_monitor -j4
```

### **2. Run and Observe:**
```bash
sudo ./simple_virtual_wall_monitor
```

### **3. During the 5-second baseline:**
- Keep everything in "normal" state
- Don't move anything
- Wait for "✅ Baseline established!"

### **4. After baseline:**
- **Do NOT move anything**
- Watch terminal for debug output
- **If you see debug output WITHOUT moving anything → BUG CONFIRMED**

### **5. Share the debug output:**
Copy the debug messages showing:
- How many pixels have motion
- What the depth values are (baseline, current, wall)
- How much depth change is happening

---

## 📋 What to Report

When you run this, tell me:

1. **During normal operation (no movement):**
   - Do you see debug output? (You shouldn't!)
   - If yes, what are the values?

2. **When you wave your hand:**
   - Does it detect properly?
   - What are the depth change values?

3. **Values to report:**
   - Baseline depth
   - Current depth  
   - Wall depth
   - Max depth change
   - How many pixels with motion

This will tell us EXACTLY what's wrong and how to fix it! 🎯

---

## 🔧 Quick Fixes Based on Debug Output

**If you see continuous false detections, try:**

### **Option 1: Press 'B' to reset baseline**
```
(During monitoring, press 'B' key)
🔄 Resetting baseline... (establishing new normal state)
```

### **Option 2: Increase threshold temporarily**
Edit line 60 and rebuild:
```cpp
static constexpr float DEPTH_CHANGE_THRESHOLD = 500.0f; // 50cm
```

### **Option 3: Remove penetration check (test only)**
Comment out line 376-379 to test if penetration check is the issue:
```cpp
// if (SimpleVirtualWallUtils::isPointPenetrating(pixel, current_depth, config_)) {
    pixels_penetrating++;
    interference_points.push_back(pixel);
// }
```

---

**Run the debug version and share the output - we'll find the exact problem!** 🔍

