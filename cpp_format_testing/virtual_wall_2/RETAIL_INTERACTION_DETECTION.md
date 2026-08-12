# Retail Shelf Interaction Detection System 🛒

## 🎯 Use Case: Supermarket Shelf Monitoring

**Goal**: Detect and record customer interactions with products on shelf

**Three main actions to capture:**
1. **Touch** - Customer examines product
2. **Pick** - Customer removes product from shelf
3. **Return** - Customer returns product to shelf

**System requirement**: 
- Detect interaction START
- Capture entire interaction as video/frames
- Detect interaction END
- Classification happens later (ML/AI)

---

## ⚙️ **Optimal Detection Settings**

### **1. Ultra-Fast Initial Detection**
```cpp
// Detect hand entering as quickly as possible
TEMPORAL_FILTER_FRAMES = 2            // ~66ms (2 frames at 30fps)
DEPTH_CHANGE_THRESHOLD = 300.0f       // 30cm (sensitive to hand approach)
MIN_CLUSTERS_FOR_DETECTION = 1        // Even 1 cluster triggers (sensitive!)
```

**Why:**
- ✅ Catches hand entering **immediately** (~66ms)
- ✅ Starts recording from **beginning** of interaction
- ✅ Won't miss fast movements

---

### **2. Interaction Session Tracking**
```cpp
// Track the entire interaction as one "session"
enum InteractionState {
    IDLE,           // No interaction
    ACTIVE,         // Interaction in progress
    COOLDOWN        // Just ended, waiting
};

InteractionState interaction_state_ = IDLE;
std::chrono::steady_clock::time_point interaction_start_time_;
std::chrono::steady_clock::time_point interaction_last_detection_;
std::vector<cv::Mat> captured_frames_;  // Store frames during interaction

static constexpr int INTERACTION_END_TIMEOUT_MS = 500;  // 0.5s no detection = ended
static constexpr int INTERACTION_COOLDOWN_MS = 1000;    // 1s cooldown before next
```

---

### **3. Frame Capture During Interaction**
```cpp
void detectInterference() {
    auto now = std::chrono::steady_clock::now();
    
    // Detect interference
    bool has_interference = detectInterferenceClusters();
    
    if (has_interference) {
        if (interaction_state_ == IDLE) {
            // START NEW INTERACTION
            interaction_state_ = ACTIVE;
            interaction_start_time_ = now;
            interaction_last_detection_ = now;
            captured_frames_.clear();
            
            std::cout << "\n🛒 INTERACTION STARTED - Recording frames..." << std::endl;
        }
        
        if (interaction_state_ == ACTIVE) {
            // CONTINUE INTERACTION - Capture frame
            captured_frames_.push_back(depth_frame_.clone());
            interaction_last_detection_ = now;
            
            // Show progress
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - interaction_start_time_).count();
            std::cout << "   📹 Recording... " << captured_frames_.size() 
                     << " frames (" << duration << "ms)" << std::endl;
        }
    } else {
        // No interference detected
        if (interaction_state_ == ACTIVE) {
            // Check if interaction ended (no detection for TIMEOUT period)
            auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - interaction_last_detection_).count();
            
            if (time_since_last >= INTERACTION_END_TIMEOUT_MS) {
                // INTERACTION ENDED - Save session
                saveInteractionSession();
                interaction_state_ = COOLDOWN;
                
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - interaction_start_time_).count();
                std::cout << "\n✅ INTERACTION ENDED - " << captured_frames_.size() 
                         << " frames captured over " << duration << "ms" << std::endl;
            }
        } else if (interaction_state_ == COOLDOWN) {
            // Check if cooldown finished
            auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - interaction_last_detection_).count();
            
            if (time_since_last >= INTERACTION_COOLDOWN_MS) {
                interaction_state_ = IDLE;
                std::cout << "   ✅ Ready for next interaction" << std::endl;
            }
        }
    }
}
```

---

## 📊 **Detection Timeline Example**

### **Scenario: Customer Picks Up Item**

```
Time    Event                           State      Frames  Action
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
0ms     Customer approaches             IDLE       0       -
100ms   Hand enters virtual wall        ACTIVE     3       START CAPTURE! 🎬
200ms   Hand moves toward product       ACTIVE     9       Recording...
500ms   Hand touches product            ACTIVE     18      Recording...
800ms   Hand grasps product             ACTIVE     27      Recording...
1200ms  Hand removes product            ACTIVE     39      Recording...
1500ms  Hand exits wall                 ACTIVE     48      Still recording...
2000ms  No detection for 500ms          COOLDOWN   48      SAVE SESSION! 💾
3000ms  Cooldown complete               IDLE       0       Ready for next
```

**Result:**
- ✅ Captured **48 frames** (1.5 seconds of interaction)
- ✅ Captured from **start to end**
- ✅ One session = one interaction
- ✅ Ready for classification

---

## 🎬 **Saved Interaction Data**

Each interaction session saves:

```json
{
  "session_id": "interaction_20251110_143052_789",
  "timestamp": "2025-11-10T14:30:52.789Z",
  "duration_ms": 1500,
  "frame_count": 48,
  "frames_directory": "sessions/interaction_20251110_143052_789/",
  "start_cluster": {
    "grid_pos": [12, 8],
    "depth_mm": 1245,
    "position_cm": [47.5, 95.3]
  },
  "peak_penetration_mm": 420,
  "affected_area_clusters": 8,
  "metadata": {
    "shelf_id": "shelf_A3",
    "camera_id": "cam_01"
  }
}
```

**Frames saved as:**
```
sessions/interaction_20251110_143052_789/
├── depth_000.png       (frame 0)
├── depth_001.png       (frame 1)
├── depth_002.png       (frame 2)
...
├── depth_048.png       (frame 48)
├── metadata.json       (session info)
└── video.mp4           (optional: compiled video)
```

---

## ⚙️ **Configurable Parameters**

| Parameter | Recommended | Purpose |
|-----------|-------------|---------|
| `TEMPORAL_FILTER_FRAMES` | **2** | Fast detection (~66ms) |
| `DEPTH_CHANGE_THRESHOLD` | **300mm** | Sensitive to hand approach |
| `MIN_CLUSTERS_FOR_DETECTION` | **1** | Trigger on any interference |
| `INTERACTION_END_TIMEOUT_MS` | **500ms** | Time to wait before ending |
| `INTERACTION_COOLDOWN_MS` | **1000ms** | Prevent double-triggers |
| `BASELINE_FRAMES_NEEDED` | **150** (5s) | Establish normal state |

---

## 🔧 **Three Detection Sensitivity Modes**

### **Mode 1: Ultra-Sensitive (Catch Everything)**
```cpp
TEMPORAL_FILTER_FRAMES = 2              // 2 frames = ~66ms
DEPTH_CHANGE_THRESHOLD = 250.0f         // 25cm (very sensitive)
MIN_CLUSTERS_FOR_DETECTION = 1          // Single cluster triggers
INTERACTION_END_TIMEOUT_MS = 500        // 0.5s timeout
```
**Use for**: High-traffic stores, expensive items, security

---

### **Mode 2: Balanced (Recommended)**
```cpp
TEMPORAL_FILTER_FRAMES = 2              // 2 frames = ~66ms
DEPTH_CHANGE_THRESHOLD = 350.0f         // 35cm (balanced)
MIN_CLUSTERS_FOR_DETECTION = 2          // 2 clusters = more confident
INTERACTION_END_TIMEOUT_MS = 500        // 0.5s timeout
```
**Use for**: Normal retail, general products

---

### **Mode 3: Conservative (Reduce False Positives)**
```cpp
TEMPORAL_FILTER_FRAMES = 3              // 3 frames = ~100ms
DEPTH_CHANGE_THRESHOLD = 400.0f         // 40cm (less sensitive)
MIN_CLUSTERS_FOR_DETECTION = 3          // 3 clusters = very confident
INTERACTION_END_TIMEOUT_MS = 800        // 0.8s timeout (longer actions)
```
**Use for**: Low-traffic stores, reduce system load

---

## 📈 **Performance Metrics**

### **Frame Capture Rate:**
- **Camera**: 30 FPS
- **Processing**: ~25-28 FPS (real-time capable)
- **Storage**: ~1-2 MB per second (depth frames)

### **Typical Interaction Durations:**
- **Touch/Examine**: 0.5 - 2 seconds (15-60 frames)
- **Pick**: 1 - 3 seconds (30-90 frames)
- **Return**: 0.8 - 2 seconds (24-60 frames)

### **Storage Requirements:**
- **Per interaction**: 1-5 MB (30-150 frames)
- **100 interactions/day**: 100-500 MB/day
- **Weekly cleanup recommended**

---

## 🎯 **Implementation Strategy**

### **Phase 1: Basic Interaction Detection**
```cpp
class InteractionRecorder {
    InteractionState state_;
    std::vector<cv::Mat> frames_;
    
    void onInterferenceDetected() {
        if (state_ == IDLE) startInteraction();
        if (state_ == ACTIVE) captureFrame();
    }
    
    void onNoInterference() {
        if (state_ == ACTIVE) checkTimeout();
    }
};
```

### **Phase 2: Frame Management**
- Circular buffer (keep last N frames before trigger)
- Pre-trigger frames (capture a bit before detection)
- Post-trigger frames (capture a bit after)

### **Phase 3: Compression & Storage**
- Save frames as compressed PNG
- Optional: Compile to MP4 video
- Automatic cleanup of old sessions

### **Phase 4: ML Integration**
- Export frames for training
- Real-time classification (optional)
- Action labeling UI

---

## 🚦 **State Machine Diagram**

```
                    ┌─────────────┐
                    │    IDLE     │ ← No interference, ready
                    └──────┬──────┘
                           │
                  Interference detected
                           │
                           ↓
                    ┌─────────────┐
         ┌─────────▶│   ACTIVE    │ ← Recording frames
         │          └──────┬──────┘
         │                 │
    Still detecting   No detection for
    interference      500ms (timeout)
         │                 │
         └─────────────────┘
                           ↓
                    ┌─────────────┐
                    │  COOLDOWN   │ ← Saving session, preventing re-trigger
                    └──────┬──────┘
                           │
                    1s cooldown passes
                           │
                           ↓
                    ┌─────────────┐
                    │    IDLE     │ ← Ready for next interaction
                    └─────────────┘
```

---

## 💡 **Key Differences from Current System**

| Feature | Current | Optimal for Retail |
|---------|---------|-------------------|
| **Detection mode** | Single event | Session recording |
| **Rate limiting** | 1s between events | Per session (allow continuous) |
| **Frame capture** | ❌ None | ✅ Entire interaction |
| **Sensitivity** | Conservative | High (catch all interactions) |
| **Output** | JSON event log | Video sequences + metadata |
| **Purpose** | Count events | Capture interactions for ML |

---

## 🛠️ **Recommended Implementation**

Shall I implement this retail-optimized system with:

1. ✅ **Interaction session tracking** (IDLE → ACTIVE → COOLDOWN)
2. ✅ **Continuous frame capture** during interaction
3. ✅ **Fast detection** (2 frames = 66ms)
4. ✅ **Smart timeout** (500ms no-detection = ended)
5. ✅ **Session saving** (frames + metadata to disk)
6. ✅ **Buffer frames** (optional: capture before/after)
7. ✅ **Configurable sensitivity** (3 modes: ultra/balanced/conservative)

This will give you **complete interaction recordings** ready for classification! 🎯

