# Camera Orientation Fix - Corner Label Swap

## 🎯 Problem
The camera is mounted **upside down** (pointing downward from above), so the corner labels were inverted:
- What appears at the **top** of the camera image is actually the **bottom** physically (lower shelf edge)
- What appears at the **bottom** of the camera image is actually the **top** physically (upper shelf edge)

## ✅ Solution - Swapped Corner Labels

### Before (Incorrect):
```
Camera Image:        Physical Reality:
┌─────────────┐      ┌─────────────┐
│ UL      UR  │      │ LL      LR  │  ← Lower shelf edge
│             │      │             │
│             │      │             │
│ LL      LR  │      │ UL      UR  │  ← Upper shelf edge
└─────────────┘      └─────────────┘
```

### After (Correct):
```
Camera Image:        Physical Reality:
┌─────────────┐      ┌─────────────┐
│ LL      LR  │  →   │ LL      LR  │  ← Lower shelf edge
│             │      │             │
│             │      │             │
│ UL      UR  │  →   │ UL      UR  │  ← Upper shelf edge
└─────────────┘      └─────────────┘
```

## 📝 Changes Made

### 1. Updated `simple_virtual_wall_utils.h`

**Line 100 - Corner ID Comment:**
```cpp
// OLD:
int corner_id;  // 0=UL, 1=UR, 2=LL, 3=LR

// NEW:
int corner_id;  // 0=LL, 1=LR, 2=UL, 3=UR (camera mounted upside down)
```

**Lines 146-149 - Initialization Comments:**
```cpp
// OLD:
corners[0] = DraggableCorner(..., 0); // Upper-left
corners[1] = DraggableCorner(..., 1); // Upper-right
corners[2] = DraggableCorner(..., 2); // Lower-left
corners[3] = DraggableCorner(..., 3); // Lower-right

// NEW:
corners[0] = DraggableCorner(..., 0); // Lower-left (top-left in image)
corners[1] = DraggableCorner(..., 1); // Lower-right (top-right in image)
corners[2] = DraggableCorner(..., 2); // Upper-left (bottom-left in image)
corners[3] = DraggableCorner(..., 3); // Upper-right (bottom-right in image)
```

### 2. Updated `simple_virtual_wall_setup.cpp`

**Line 357 - Short Labels:**
```cpp
// OLD:
std::vector<std::string> labels = {"UL", "UR", "LL", "LR"};

// NEW:
std::vector<std::string> labels = {"LL", "LR", "UL", "UR"};
```

**Line 358 - Full Names:**
```cpp
// OLD:
std::vector<std::string> full_names = {"Upper Left", "Upper Right", "Lower Left", "Lower Right"};

// NEW:
std::vector<std::string> full_names = {"Lower Left", "Lower Right", "Upper Left", "Upper Right"};
```

**Lines 584-599 - Config Mapping (CRITICAL!):**
```cpp
// OLD:
switch (i) {
    case 0: // Upper Left
        config_.upper_left_px = corner.position;
        config_.upper_left_depth_mm = depth_mm;
        break;
    case 1: // Upper Right
        config_.upper_right_px = corner.position;
        config_.upper_right_depth_mm = depth_mm;
        break;
    case 2: // Lower Left
        config_.lower_left_px = corner.position;
        config_.lower_left_depth_mm = depth_mm;
        break;
    case 3: // Lower Right
        config_.lower_right_px = corner.position;
        config_.lower_right_depth_mm = depth_mm;
        break;
}

// NEW:
switch (i) {
    case 0: // Lower Left (top-left in camera image)
        config_.lower_left_px = corner.position;
        config_.lower_left_depth_mm = depth_mm;
        break;
    case 1: // Lower Right (top-right in camera image)
        config_.lower_right_px = corner.position;
        config_.lower_right_depth_mm = depth_mm;
        break;
    case 2: // Upper Left (bottom-left in camera image)
        config_.upper_left_px = corner.position;
        config_.upper_left_depth_mm = depth_mm;
        break;
    case 3: // Upper Right (bottom-right in camera image)
        config_.upper_right_px = corner.position;
        config_.upper_right_depth_mm = depth_mm;
        break;
}
```

## 🚀 Rebuild Instructions

### On Raspberry Pi:
```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make simple_virtual_wall_setup -j4
sudo ./simple_virtual_wall_setup
```

## 📊 Expected Behavior After Fix

### Setup Phase:
1. Run setup tool
2. **Top of camera image** shows:
   - **LL** (Lower Left) - left side of lower shelf edge
   - **LR** (Lower Right) - right side of lower shelf edge
3. **Bottom of camera image** shows:
   - **UL** (Upper Left) - left side of upper shelf edge
   - **UR** (Upper Right) - right side of upper shelf edge

### Sidebar Display:
```
CORNER INFORMATION
━━━━━━━━━━━━━━━━━━━━━━━━━━━
● LL Lower Left
  (52, 108)
  2500mm | VALID

● LR Lower Right
  (153, 109)
  2500mm | VALID

● UL Upper Left
  (2, 164)
  500mm | VALID

● UR Upper Right
  (235, 166)
  705mm | VALID
```

## ✅ Verification Checklist

After rebuilding and running setup:

- [ ] **LL** and **LR** labels appear at **top** of camera image
- [ ] **UL** and **UR** labels appear at **bottom** of camera image
- [ ] Sidebar shows correct corner names
- [ ] Pressing 'S' to save uses correct corner mapping
- [ ] Green wall rectangle aligns with shelf edges
- [ ] Depth measurements are correct for each corner

## 🎯 Why This Matters

**Critical for correct interference detection:**
- If labels are wrong, the virtual wall plane will be **inverted**
- Depth interpolation will use **incorrect corner depths**
- Interference detection will give **false readings**
- X,Y coordinate mapping will be **upside down**

With this fix, the physical reality now matches the labels! ✅

---

**Status:** ✅ Code updated, ready to rebuild on Pi
**Next Step:** Run the rebuild commands on the Raspberry Pi and test the setup tool

