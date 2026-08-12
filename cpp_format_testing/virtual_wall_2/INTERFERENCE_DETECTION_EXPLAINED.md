# Interference Detection Algorithm - Detailed Explanation

## 🎯 How It SHOULD Work

### Overview
The system uses **BASELINE COMPARISON + MOTION DETECTION** to identify when something NEW enters the virtual wall area.

---

## 📋 Step-by-Step Detection Process

### **PHASE 1: Baseline Establishment (5 seconds)**

```cpp
void establishBaseline() {
    // Captures 150 frames (5 seconds at 30fps)
    // For each pixel in the virtual wall area:
    //   - Accumulate depth values from valid measurements
    //   - Average them to create a "normal state" depth map
    
    baseline_depth_[y][x] = average of 150 depth readings at that pixel
}
```

**Purpose:** Learn what the "normal" scene looks like with shelves/items in place.

**Example:**
- Pixel (100, 120) has a shelf edge at 1500mm
- Baseline captures: 1500mm average over 150 frames
- This becomes the reference: "1500mm is NORMAL at this position"

---

### **PHASE 2: Monitoring for Interference**

For EVERY frame after baseline, for EVERY pixel in the wall area:

#### **Step 1: Get Current Depth**
```cpp
float current_depth = depth_frame_.at<float>(y, x);  // e.g., 1500mm (shelf) or 1200mm (hand)
```

#### **Step 2: Get Baseline Depth** 
```cpp
float baseline = baseline_depth_.at<float>(y, x);  // e.g., 1500mm (shelf in normal state)
```

#### **Step 3: Calculate Motion (Depth Change)**
```cpp
float depth_change = baseline - current_depth;
// Example 1: No interference
//   baseline = 1500mm, current = 1500mm
//   depth_change = 0mm → NO MOTION

// Example 2: Hand reaches in
//   baseline = 1500mm, current = 1200mm  
//   depth_change = 300mm → MOTION DETECTED! (closer)
```

#### **Step 4: Motion Threshold Check**
```cpp
if (depth_change > DEPTH_CHANGE_THRESHOLD) {  // 250mm
    // Something moved CLOSER by more than 25cm!
    // Proceed to penetration check...
}
```

**Why 250mm?**
- Filters out sensor noise (±15-20mm)
- Ignores small vibrations
- Only detects REAL movements (hand, object, person)

#### **Step 5: Penetration Check**
```cpp
if (SimpleVirtualWallUtils::isPointPenetrating(pixel, current_depth, config_)) {
    // Check if the NEW depth is inside the virtual wall plane
    
    float expected_wall_depth = interpolateWallDepth(pixel, config_);
    // e.g., 2500mm (back wall of shelf area)
    
    return (current_depth < expected_wall_depth - penetration_threshold_mm);
    // e.g., 1200mm < 2500mm - 100mm = YES, PENETRATING!
}
```

#### **Step 6: Temporal Filtering**
```cpp
// Store detection in buffer: [true, false, true] over last 3 frames
recent_detections_[temporal_filter_index_] = current_frame_has_interference;

// Count: How many of last 3 frames had interference?
int detection_count = 0;
for (bool detected : recent_detections_) {
    if (detected) detection_count++;
}

// Require detection in AT LEAST 2 out of 3 frames
if (detection_count >= 2 && interference_points.size() >= MIN_PIXELS_FOR_DETECTION) {
    logInterferenceEvent(interference_points);  // LOG IT!
}
```

#### **Step 7: Rate Limiting**
```cpp
// Don't log events faster than once per second
auto time_since_last = now - last_event_time_;
if (time_since_last < 1000ms) {
    return;  // Skip logging
}
```

---

## ❌ **WHY IT'S FAILING - The Problem**

### **Issue: Conflicting Checks**

The algorithm does TWO checks:
1. ✅ **Motion Check:** Did depth change by >250mm from baseline?
2. ⚠️ **Penetration Check:** Is current depth < wall plane depth?

### **The Conflict Scenario:**

Imagine your shelf setup:
```
Camera (upside down at top)
    ↓ (looking down)
    
[500mm] - Empty air above shelf
[1500mm] - Shelf surface with items
[2500mm] - Back wall behind shelf
```

When you define the virtual wall corners:
- **You're clicking on the SHELF EDGES** (at ~1500mm depth)
- The config saves these as "wall depth" = 1500mm

**Baseline establishes:**
```
baseline_depth_[100][120] = 1500mm  (shelf surface with items)
```

**During monitoring (NO INTERFERENCE):**
```cpp
current_depth = 1500mm  (same shelf, no change)
baseline = 1500mm
depth_change = 1500 - 1500 = 0mm
0mm > 250mm? NO → Skip this pixel ✓ CORRECT
```

**But wait... there's a BUG!**

### **🐛 THE ACTUAL BUG:**

The penetration check happens INSIDE the depth change check:
```cpp
if (depth_change > DEPTH_CHANGE_THRESHOLD) {  // 250mm
    if (SimpleVirtualWallUtils::isPointPenetrating(pixel, current_depth, config_)) {
        // ...
    }
}
```

So if items are sitting on the shelf at 1500mm and the wall is at 1500mm:
- They should NOT penetrate (1500mm is NOT < 1500mm - 100mm)
- This part is actually correct!

### **🔍 Real Issue: Depth Sensor Fluctuation**

**Most Likely Problem:**
The ToF camera depth readings are **fluctuating** more than 250mm even for static objects!

**Why?**
1. **Low confidence regions:** Areas with poor depth data (edges, dark surfaces)
2. **Sensor warm-up drift:** Depth shifts during the first few minutes
3. **Baseline too short:** 5 seconds might not be enough for sensor to stabilize
4. **Interference from reflections:** Shiny surfaces causing noisy readings

**Evidence:**
- You said it detects "continuously" (not just once)
- This suggests MANY pixels are triggering
- Even with rate limiting (1 second), it keeps firing

---

## ✅ **SOLUTION - Add Debug Output First**

Before we fix it, let's **see what's happening**:

### **Debug Version of Detection:**

I'll add logging to show:
- How many pixels have motion > 250mm
- What are the actual depth changes
- Which pixels are triggering
- Baseline vs current depth values

This will tell us:
1. Is baseline correct?
2. Are depths fluctuating wildly?
3. Is the wall plane defined correctly?
4. Which check is causing false positives?

---

## 🔧 **Proposed Fixes (After Debugging)**

### **Option 1: Stricter Motion Threshold**
```cpp
static constexpr float DEPTH_CHANGE_THRESHOLD = 400.0f; // 40cm
```

### **Option 2: Remove Penetration Check**
```cpp
// Just use motion detection alone
if (depth_change > DEPTH_CHANGE_THRESHOLD) {
    interference_points.push_back(pixel);  // Don't check wall plane
}
```

### **Option 3: Longer Baseline + Better Filtering**
```cpp
static constexpr int BASELINE_FRAMES_NEEDED = 300; // 10 seconds
static constexpr float DEPTH_CHANGE_THRESHOLD = 350.0f; // 35cm
static constexpr int MIN_PIXELS_FOR_DETECTION = 10; // More pixels required
```

### **Option 4: Adaptive Threshold Based on Noise**
```cpp
// During baseline, also calculate depth VARIANCE
// Set threshold = 3x standard deviation
```

---

## 🎯 Next Step

**Let me add debug output to the detection function** so we can see:
- Actual depth values
- How much they're changing
- How many pixels are triggering

Then we can identify the exact problem and apply the right fix!

**Should I add the debug output?**

