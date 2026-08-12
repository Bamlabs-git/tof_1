# Penetration Detection - Timing Explained ⏱️

## 🕐 Current Time-Based Mechanisms

The system **ALREADY uses time** in **3 different ways**:

---

## 1️⃣ **Baseline Establishment (5 Seconds)**

```cpp
static constexpr int BASELINE_FRAMES_NEEDED = 150; // 5 seconds at 30fps
```

**What it does:**
- Captures **150 frames over 5 seconds**
- Averages depth for each cluster
- Creates "normal state" reference

**Timing:**
- **Duration**: 5 seconds
- **Frame rate**: ~30 FPS
- **Purpose**: Establish what "normal" looks like

**Timeline:**
```
0s ──────────────────────────────────────── 5s
│                                            │
└─ Start monitoring                          └─ Baseline ready
   (Keep area still!)                           (Detection active!)
```

---

## 2️⃣ **Temporal Filtering (~100ms)**

```cpp
static const int TEMPORAL_FILTER_FRAMES = 3;

// Apply temporal filtering
recent_detections_[temporal_filter_index_] = current_frame_has_interference;

// Only log if detected in at least 2 out of 3 frames
if (detection_count >= 2 && current_frame_has_interference) {
    LOG_EVENT();
}
```

**What it does:**
- Tracks last **3 frames** (~100ms at 30fps)
- Requires detection in **2 out of 3 frames**
- Filters single-frame glitches

**Timing:**
- **Window**: 3 frames = ~100ms
- **Threshold**: 2/3 frames must detect
- **Purpose**: Filter transient noise

**Example:**
```
Frame:  1    2    3    4    5
State:  ❌   ✅   ✅   ✅   ❌
Count:  0/3  1/3  2/3  2/3  2/3
Action: Skip Skip LOG! LOG! Skip
        ↑    ↑    ↑    ↑    ↑
        Need 2/3 to trigger!
```

---

## 3️⃣ **Event Rate Limiting (1 Second)**

```cpp
static constexpr int MIN_EVENT_INTERVAL_MS = 1000; // 1 second

// Check event rate limiting
auto now = std::chrono::steady_clock::now();
auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - last_event_time_).count();

if (time_since_last < MIN_EVENT_INTERVAL_MS) {
    return; // Too soon after last event
}
```

**What it does:**
- Prevents logging multiple events too quickly
- Minimum **1 second** between logged events
- Reduces event spam

**Timing:**
- **Cooldown**: 1000ms (1 second)
- **Purpose**: Avoid duplicate events

**Timeline:**
```
Event 1              Event 2 (blocked)    Event 3 (allowed)
   ↓                      ↓                     ↓
0ms ─────────────────► 500ms ─────────────► 1200ms
   └─ Log event          └─ Too soon!          └─ Log event
                            (ignored)              (1.2s elapsed)
```

---

## 🎯 **Complete Detection Flow with Timing**

```
┌─────────────────────────────────────────────────────────────────┐
│ FRAME N (at time T)                                             │
└─────────────────────────────────────────────────────────────────┘
                          ↓
    ┌──────────────────────────────────────────────┐
    │ 1. Create depth clusters                     │
    │    - Compare with BASELINE (captured 5s ago) │
    │    - Check motion (>350mm change)            │
    │    - Check penetration (< wall depth)        │
    └──────────────────────────────────────────────┘
                          ↓
    ┌──────────────────────────────────────────────┐
    │ 2. Temporal filtering (last 3 frames)        │
    │    - Frame N-2: ✅ (detected)                │
    │    - Frame N-1: ✅ (detected)                │
    │    - Frame N:   ✅ (detected)                │
    │    → Count = 3/3 → PASS! ✅                  │
    └──────────────────────────────────────────────┘
                          ↓
    ┌──────────────────────────────────────────────┐
    │ 3. Rate limiting check                       │
    │    - Last event: T - 1200ms                  │
    │    - Current time: T                         │
    │    - Elapsed: 1200ms > 1000ms → PASS! ✅     │
    └──────────────────────────────────────────────┘
                          ↓
    ┌──────────────────────────────────────────────┐
    │ 4. Log interference event! 🚨                │
    │    - One event logged                        │
    │    - Update last_event_time = T              │
    └──────────────────────────────────────────────┘
```

---

## ⚠️ **What's MISSING: Minimum Duration**

**Current behavior:**
- If interference passes all checks **immediately**, it logs
- No requirement for interference to **persist** for a certain time

**Example scenario:**
```
0ms:    Hand enters wall      → Motion detected
33ms:   Still there (frame 2)  → 1/3 frames
66ms:   Still there (frame 3)  → 2/3 frames ✅ → LOG!
100ms:  Hand exits wall        → Detection stops

Result: Event logged after only 66ms!
```

**Is this good or bad?**
- ✅ **Good**: Fast response time
- ❌ **Bad**: May log very brief interferences (shadows, fast movements)

---

## 🚀 **Adding Minimum Duration Requirement**

Let me create an enhanced version with **minimum interference duration**:

### **New Feature: Minimum Duration Tracking**

```cpp
// Track when interference first started
std::chrono::steady_clock::time_point interference_start_time_;
bool interference_active_;
static constexpr int MIN_INTERFERENCE_DURATION_MS = 500; // Must persist 500ms

void detectInterference() {
    // ... existing detection logic ...
    
    bool has_interference = (detection_count >= 2 && 
                            current_frame_has_interference && 
                            interference_clusters.size() >= MIN_CLUSTERS_FOR_DETECTION);
    
    auto now = std::chrono::steady_clock::now();
    
    if (has_interference) {
        if (!interference_active_) {
            // NEW interference detected - start timer
            interference_active_ = true;
            interference_start_time_ = now;
            std::cout << "   🕐 Interference detected, waiting for duration..." << std::endl;
        } else {
            // Interference still active - check duration
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - interference_start_time_).count();
            
            if (duration >= MIN_INTERFERENCE_DURATION_MS) {
                // Check rate limiting
                auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_event_time_).count();
                
                if (time_since_last >= MIN_EVENT_INTERVAL_MS) {
                    std::cout << "   ⚠️  Interference persisted for " << duration 
                             << "ms - Logging event!" << std::endl;
                    logInterferenceEventFromClusters(interference_clusters);
                    last_event_time_ = now;
                    interference_active_ = false; // Reset
                }
            } else {
                std::cout << "   ⏳ Duration: " << duration << "ms / " 
                         << MIN_INTERFERENCE_DURATION_MS << "ms (waiting...)" << std::endl;
            }
        }
    } else {
        if (interference_active_) {
            std::cout << "   ℹ️  Interference ended before minimum duration" << std::endl;
            interference_active_ = false; // Reset
        }
    }
}
```

---

## 📊 **Timing Comparison**

### **Current System (No Duration Requirement):**
```
Time:    0ms   33ms  66ms  100ms  133ms
Frame:   1     2     3     4      5
State:   ✅    ✅    ✅    ❌     ❌
Action:  Wait  Wait  LOG!  -      -
         ↑           ↑
         Detected    Logged after 66ms
```

### **With 500ms Duration Requirement:**
```
Time:    0ms   33ms  66ms  ... 500ms  533ms
Frame:   1     2     3     ... 16     17
State:   ✅    ✅    ✅    ... ✅     ✅
Action:  Wait  Wait  Wait  ... LOG!   -
         ↑                      ↑
         Detected               Logged after 500ms+ persistence
```

---

## ⚙️ **Configurable Time Parameters**

Here are all the timing parameters you can adjust:

| Parameter | Current Value | What It Controls |
|-----------|---------------|------------------|
| `BASELINE_FRAMES_NEEDED` | 150 (5s) | How long to capture normal state |
| `TEMPORAL_FILTER_FRAMES` | 3 (~100ms) | How many frames to check for consistency |
| `MIN_EVENT_INTERVAL_MS` | 1000ms | Minimum time between logged events |
| `MIN_INTERFERENCE_DURATION_MS` | **NEW!** 500ms | How long interference must persist |

---

## 🎯 **Recommended Settings**

### **Fast Response (Current):**
```cpp
TEMPORAL_FILTER_FRAMES = 3        // ~100ms
MIN_EVENT_INTERVAL_MS = 1000      // 1 second
MIN_INTERFERENCE_DURATION_MS = 0  // Immediate (no duration requirement)
```
- ✅ Fast detection (~100ms)
- ❌ May log brief events

### **Balanced (Recommended):**
```cpp
TEMPORAL_FILTER_FRAMES = 3          // ~100ms
MIN_EVENT_INTERVAL_MS = 1000        // 1 second
MIN_INTERFERENCE_DURATION_MS = 500  // 0.5 seconds
```
- ✅ Still responsive
- ✅ Filters brief shadows/movements
- ✅ Only logs persistent interference

### **Conservative (Less Sensitive):**
```cpp
TEMPORAL_FILTER_FRAMES = 5          // ~166ms
MIN_EVENT_INTERVAL_MS = 2000        // 2 seconds
MIN_INTERFERENCE_DURATION_MS = 1000 // 1 second
```
- ✅ Very stable
- ✅ Ignores brief disturbances
- ❌ Slower response

---

## 🔍 **Real-World Examples**

### **Scenario 1: Hand Reaches In**
```
0ms:     Hand enters wall           ✅ Motion detected
100ms:   Hand reaches for item      ✅ 2/3 frames ✅
500ms:   Hand still inside          ✅ Duration met → LOG! 🚨
1000ms:  Hand grabs item            ✅ (already logged)
1500ms:  Hand exits                 ❌ Interference ends
```
**Result**: One event logged at 500ms mark

### **Scenario 2: Shadow Passes By**
```
0ms:     Shadow crosses wall        ✅ Motion detected
100ms:   Shadow still visible       ✅ 2/3 frames ✅
200ms:   Shadow gone                ❌ Interference ends
```
**Result**: NO event logged (didn't persist 500ms)

### **Scenario 3: Object Placed on Shelf**
```
0ms:     Object placed              ✅ Motion detected
100ms:   Object still there         ✅ 2/3 frames ✅
500ms:   Object still there         ✅ Duration met → LOG! 🚨
1500ms:  Rate limit (1s) passes     ✅ Can detect again
2000ms:  Object still there         ✅ Would log again (but same object)
```
**Result**: Multiple events for persistent object

---

## 🛠️ **Implementation Guide**

Want me to implement the **minimum duration** feature? Here's what I'll add:

1. ✅ `MIN_INTERFERENCE_DURATION_MS` parameter (configurable)
2. ✅ `interference_start_time_` tracking
3. ✅ `interference_active_` state flag
4. ✅ Duration check before logging
5. ✅ Debug output showing duration progress
6. ✅ Automatic reset when interference ends

**Would you like me to implement this?** It will add a more intelligent, duration-aware detection system! 🎯

---

## 📝 **Summary**

### **Current Timing:**
1. ✅ Baseline: 5 seconds
2. ✅ Temporal filter: 3 frames (~100ms)
3. ✅ Rate limiting: 1 second between events
4. ❌ Duration requirement: **NONE** (logs immediately)

### **Proposed Addition:**
5. ✅ **Minimum duration: 500ms** (configurable)
   - Interference must **persist** for this long
   - Filters shadows, brief movements
   - Still responsive for real interference

**Let me know if you want me to implement the duration tracking!** 🚀

