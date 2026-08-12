# Sidebar Layout Fix - Better Font Sizing

## 🎯 Problem
Sidebar text was being cropped (e.g., "95x206cm" cut off) due to:
- Font sizes too large for 150px wide sidebar
- Spacing too generous
- Text positioned too far right

## ✅ Solution - Reduced Font Sizes & Tighter Spacing

### Font Size Changes (in `simple_virtual_wall_monitor.cpp`)

| Element | Old Size | New Size | Change |
|---------|----------|----------|--------|
| **STATUS** title | 0.5 | 0.4 | -20% |
| Monitor label | 0.35 | 0.3 | -14% |
| ACTIVE/PAUSED | 0.5 | 0.4 | -20% |
| Events label | 0.35 | 0.3 | -14% |
| Event count | 0.6 | 0.5 | -17% |
| Wall label | 0.35 | 0.3 | -14% |
| Dimensions text | 0.35 | 0.3 | -14% |
| Depth text | 0.35 | 0.3 | -14% |
| CONTROLS title | 0.4 | 0.35 | -13% |
| Control items | 0.28 | 0.26 | -7% |

### Spacing Improvements

**Vertical Spacing:**
```cpp
// OLD spacing:
y_pos += 30;  // After title
y_pos += 20;  // After labels
y_pos += 30;  // After values

// NEW spacing (tighter):
y_pos += 25;  // After title (-17%)
y_pos += 16;  // After labels (-20%)
y_pos += 25;  // After values (-17%)
```

**Horizontal Positioning:**
```cpp
// OLD: cv::Point(10, y_pos)
// NEW: cv::Point(8, y_pos)  // 2px closer to left edge
```

**Separator Lines:**
```cpp
// OLD: y_pos - 10
// NEW: y_pos - 8  // Closer to text
```

### Control Text Adjustments

Changed "Space: Pause" → "Space:Pause" (removed space) to fit better.

## 📊 Expected Result

### Before:
```
┌──────────────┐
│ STATUS       │  ← Too big
│──────────────│
│ Monitor:     │
│   ACTIVE     │  ← Too big, too spaced
│──────────────│
│ Events:      │
│   0          │  ← Very big
│──────────────│
│ Wall:        │
│   95x206cm   │  ← CROPPED!
│   2560mm     │
│──────────────│
│ CONTROLS     │
│ Space: Pause │
│ R: Reset     │
│ B: Baseline  │
│ Q: Quit      │
└──────────────┘
```

### After:
```
┌──────────────┐
│ STATUS       │  ✅ Smaller
│──────────────│
│ Monitor:     │
│  ACTIVE      │  ✅ Compact
│──────────────│
│ Events:      │
│  0           │  ✅ Fits
│──────────────│
│ Wall:        │
│  95x206cm    │  ✅ NOT CROPPED!
│  2560mm      │  ✅ Visible
│──────────────│
│ CONTROLS     │
│ Space:Pause  │  ✅ Compact
│ R: Reset     │
│ B: Baseline  │
│ Q: Quit      │
└──────────────┘
```

## 🚀 Rebuild Instructions

```bash
cd ~/Arducam_tof_camera/cpp_format_testing/virtual_wall_2/build
make simple_virtual_wall_monitor -j4
sudo ./simple_virtual_wall_monitor
```

## ✅ Verification Checklist

After rebuilding:
- [ ] All text visible (not cropped)
- [ ] Dimensions "95x206cm" fully shown
- [ ] Depth "2560mm" (or your value) visible
- [ ] Clean, compact layout
- [ ] All controls readable
- [ ] STATUS section clear
- [ ] Event counter visible
- [ ] Wall info complete

## 📐 Technical Details

**Sidebar dimensions:** 150px width (unchanged)
**Font scale:** Reduced by 7-20% across all elements
**Line spacing:** Reduced by 13-20%
**Horizontal margin:** 10px → 8px (20% reduction)

**Why 150px width?**
- Small enough to not obscure camera view
- Wide enough for essential info
- Standard sidebar width for this type of UI

**Font choices:**
- `FONT_HERSHEY_SIMPLEX` - clean, readable at small sizes
- Scale range: 0.26 to 0.5 (was 0.28 to 0.6)
- Thickness: 1px for most, 2px for highlights

## 🎨 Color Scheme (unchanged)

- **Background:** Dark gray (30, 30, 30)
- **Title text:** White (255, 255, 255)
- **Labels:** Light gray (200, 200, 200)
- **Values:** Lighter gray (180, 180, 180)
- **ACTIVE status:** Green (0, 255, 0)
- **PAUSED status:** Orange (0, 165, 255)
- **Event count:** Yellow (255, 255, 0)
- **Separators:** Dark gray (60-100, 60-100, 60-100)

---

**Status:** ✅ Code updated with optimized font sizes and spacing
**Impact:** No functionality changes, only improved UI readability
**Next:** Rebuild on Pi and verify text is no longer cropped

