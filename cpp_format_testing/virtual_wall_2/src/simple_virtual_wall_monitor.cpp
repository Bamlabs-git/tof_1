#include <opencv2/opencv.hpp>
#include <iostream>
#include <iomanip>
#include <memory>
#include <thread>
#include <chrono>
#include <deque>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <condition_variable>
#include <limits>
#include <algorithm>
#include "simple_virtual_wall_utils.h"
#include "common.h"
#include "warning_suppression.h"

/**
 * SIMPLIFIED VIRTUAL WALL MONITOR
 * 
 * This tool monitors the virtual wall for penetrations using:
 * - Simple depth comparison (measured_depth < wall_depth - threshold)
 * - Bilinear mapping to convert pixel coordinates to real-world X,Y positions
 * - No Z-axis information (only X, Y reported)
 * - Event logging with real-world coordinates
 * 
 * The detection method is straightforward and robust!
 */

class SimpleVirtualWallMonitor {
private:
    enum class RecordingState {
        IDLE,
        RECORDING,
        ENDING
    };

    enum class RecordingFramePhase {
        BEFORE,
        DURING,
        AFTER
    };

    struct ActionRecordingSession {
        std::string action_id;
        std::string label;
        std::string start_timestamp;
        std::string end_timestamp;
        std::filesystem::path session_dir;
        std::filesystem::path frames_depth_png_dir;
        std::filesystem::path frames_depth_raw_dir;
        std::filesystem::path frames_confidence_raw_dir;
        std::filesystem::path video_dir;
        cv::VideoWriter depth_video;
        cv::Point2f entry_position_cm;
        cv::Point2f last_position_cm;
        float max_penetration_mm;
        size_t max_cluster_count;
        int frame_count;
        int pre_roll_frame_count;
        int post_roll_frame_count;
        std::chrono::steady_clock::time_point start_time;

        ActionRecordingSession() :
            label("unknown"),
            entry_position_cm(0, 0),
            last_position_cm(0, 0),
            max_penetration_mm(0),
            max_cluster_count(0),
            frame_count(0),
            pre_roll_frame_count(0),
            post_roll_frame_count(0) {}
    };

    struct BufferedRecordingFrame {
        cv::Mat depth_frame;
        cv::Mat confidence_frame;
    };

    struct ObjectDepthStats {
        bool valid;
        cv::Point2f center_pixel;
        cv::Point2f center_cm;
        float average_depth_mm;
        float min_depth_mm;
        float max_depth_mm;
        size_t cluster_count;

        ObjectDepthStats() :
            valid(false),
            center_pixel(0, 0),
            center_cm(0, 0),
            average_depth_mm(0),
            min_depth_mm(0),
            max_depth_mm(0),
            cluster_count(0) {}
    };

    struct RecordingFrameJob {
        int frame_number;
        RecordingFramePhase phase;
        cv::Mat depth_frame;
        cv::Mat confidence_frame;
        std::filesystem::path frames_depth_png_dir;
        std::filesystem::path frames_depth_raw_dir;
        std::filesystem::path frames_confidence_raw_dir;
        std::string action_id;
        ObjectDepthStats object_stats;
        bool is_shutdown;

        RecordingFrameJob() : frame_number(0), phase(RecordingFramePhase::DURING), is_shutdown(false) {}
    };

    std::unique_ptr<CameraManager> camera_manager_;
    SimpleVirtualWallConfig config_;
    cv::Mat depth_frame_;
    cv::Mat confidence_frame_;
    cv::Mat display_frame_;
    
    std::atomic<bool> running_;
    std::atomic<bool> monitoring_active_;
    std::atomic<int> interference_count_;
    
    // Event logging
    std::string log_file_path_;
    std::mutex log_mutex_;
    std::deque<SimpleInterferenceEvent> event_queue_;
    std::mutex event_queue_mutex_;
    
    // Detection parameters
    int confidence_threshold_;
    int max_distance_;
    float max_valid_depth_mm_;
    float motion_threshold_mm_;
    int min_valid_pixels_per_cluster_;
    int min_interference_pixels_per_cluster_;
    bool far_shelf_mode_;
    float visualization_min_depth_mm_;
    float visualization_max_depth_mm_;
    
    // Temporal filtering
    static const int TEMPORAL_FILTER_FRAMES = 3;
    std::vector<bool> recent_detections_;
    int temporal_filter_index_;
    
    // Baseline depth map for change detection
    cv::Mat baseline_depth_;
    cv::Mat baseline_valid_;
    bool baseline_established_;
    int baseline_frame_count_;
    static constexpr int BASELINE_FRAMES_NEEDED = 150; // 5 seconds at 30fps
    static constexpr float DEPTH_CHANGE_THRESHOLD = 200.0f; // 20cm change = motion (more sensitive for far shelves)
    
    // Event clustering to avoid spam
    std::chrono::steady_clock::time_point last_event_time_;
    static constexpr int MIN_CLUSTERS_FOR_DETECTION = 1; // Need at least 1 cluster (more sensitive for far shelves)

    // Action recording
    RecordingState recording_state_;
    ActionRecordingSession current_action_;
    ObjectDepthStats latest_object_stats_;
    ObjectDepthStats best_object_stats_;
    std::filesystem::path recordings_base_dir_;
    int action_counter_;
    int clear_frame_count_;
    std::deque<BufferedRecordingFrame> pre_roll_buffer_;
    std::deque<RecordingFrameJob> recording_queue_;
    std::mutex recording_queue_mutex_;
    std::condition_variable recording_queue_cv_;
    std::thread recording_writer_thread_;
    std::mutex video_writer_mutex_;
    bool recording_writer_running_;
    static constexpr size_t MAX_RECORDING_QUEUE_SIZE = 300;
    static constexpr int START_CONFIRM_FRAMES = 2;
    static constexpr int PRE_ROLL_FRAMES = 5;
    static constexpr int POST_ROLL_FRAMES = 5;
    static constexpr int CLEAR_CONFIRM_FRAMES = 5;
    static constexpr int MAX_ACTION_FRAMES = 450; // Safety cap: ~15 seconds at 30 FPS
    static constexpr double RECORDING_FPS = 30.0;
    
    // Grid visualization
    bool show_grid_;
    
    // Debug mode
    bool debug_mode_;
    
    // Warning suppression
    WarningSuppressionManager warning_manager_;
    
public:
    SimpleVirtualWallMonitor() :
        running_(false),
        monitoring_active_(false),
        interference_count_(0),
        confidence_threshold_(15),  // Lower threshold for steep angles (lower shelf)
        max_distance_(3500),
        max_valid_depth_mm_(3500.0f),
        motion_threshold_mm_(DEPTH_CHANGE_THRESHOLD),
        min_valid_pixels_per_cluster_(3),
        min_interference_pixels_per_cluster_(2),
        far_shelf_mode_(false),
        visualization_min_depth_mm_(0.0f),
        visualization_max_depth_mm_(3500.0f),
        recent_detections_(TEMPORAL_FILTER_FRAMES, false),
        temporal_filter_index_(0),
        recording_state_(RecordingState::IDLE),
        action_counter_(0),
        clear_frame_count_(0),
        recording_writer_running_(true),
        
        baseline_established_(false),
        baseline_frame_count_(0),
        show_grid_(true),  // Grid enabled by default
        debug_mode_(false) {  // Debug mode disabled by default
        
        // Set up log file path
        std::filesystem::path log_dir = "logs";
        std::filesystem::create_directories(log_dir);
        std::filesystem::path log_file = log_dir / ("simple_interference_events_" + 
                                                   SimpleVirtualWallUtils::getCurrentTimestamp() + ".json");
        log_file_path_ = log_file.string();
        recording_writer_thread_ = std::thread(&SimpleVirtualWallMonitor::recordingWriterLoop, this);
    }
    
    ~SimpleVirtualWallMonitor() {
        if (camera_manager_) {
            camera_manager_->stop();
        }
        processEventQueue();
        if (recording_state_ != RecordingState::IDLE) {
            finishActionRecording();
        }
        stopRecordingWriter();
    }
    
    bool initialize() {
        std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║   SIMPLIFIED VIRTUAL WALL MONITOR                  ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
        
        // Load configuration
        std::string config_path = SimpleVirtualWallUtils::getDefaultConfigPath();
        
        if (!std::filesystem::exists(config_path)) {
            std::cerr << "\n❌ No configuration found!" << std::endl;
            std::cerr << "   Expected: " << config_path << std::endl;
            std::cerr << "\n🔧 Please run the setup tool first:" << std::endl;
            std::cerr << "   ./simple_virtual_wall_setup" << std::endl;
            return false;
        }
        
        std::cout << "\n📁 Loading configuration from: " << config_path << std::endl;
        
        if (!SimpleVirtualWallUtils::loadConfig(config_, config_path)) {
            std::cerr << "\n❌ Failed to load configuration!" << std::endl;
            return false;
        }
        
        if (!SimpleVirtualWallUtils::validateConfig(config_)) {
            std::cerr << "\n❌ Invalid configuration!" << std::endl;
            return false;
        }
        
        printConfigSummary();
        
        // Initialize camera
        std::cout << "\n🚀 Initializing camera..." << std::endl;
        warning_manager_.suppressWarnings();
        
        camera_manager_ = std::make_unique<CameraManager>(0);
        bool init_success = camera_manager_->initialize();
        
        warning_manager_.restoreWarnings();
        
        if (!init_success) {
            std::cerr << "❌ Failed to initialize camera!" << std::endl;
            return false;
        }
        
        if (!camera_manager_->start()) {
            std::cerr << "❌ Failed to start camera!" << std::endl;
            return false;
        }
        
        std::cout << "✅ Camera initialized successfully!" << std::endl;
        std::cout << "📝 Events will be logged to: " << log_file_path_ << std::endl;
        recordings_base_dir_ = "recorded_actions";
        std::filesystem::create_directories(recordings_base_dir_);
        std::cout << "🎥 Action recordings will be saved under: " << recordings_base_dir_ << std::endl;
        
        return true;
    }
    
    void run() {
        if (!initialize()) {
            return;
        }
        
        running_ = true;
        monitoring_active_ = true;
        
        // Create window
        cv::namedWindow("Virtual Wall Monitor", cv::WINDOW_NORMAL);
        cv::resizeWindow("Virtual Wall Monitor", 960, 720);
        
        printControls();
        
        while (running_) {
            // Capture frames
            if (!captureFrames()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                continue;
            }
            
            // Process frames
            processFrames();
            
            // Detect interference and update recording session state
            if (monitoring_active_) {
                detectInterference();
            }
            
            // Display
            displayFrames();
            
            // Process event queue
            processEventQueue();
            
            // Handle keyboard
            int key = cv::waitKey(1) & 0xFF;
            handleKeyboard(key);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        
        cv::destroyAllWindows();
        
        std::cout << "\n📊 Final Statistics:" << std::endl;
        std::cout << "   Total interference events: " << interference_count_.load() << std::endl;
        std::cout << "   Log file: " << log_file_path_ << std::endl;
    }

private:
    void printConfigSummary() {
        std::cout << "\n✅ Configuration loaded successfully!" << std::endl;
        std::cout << "══════════════════════════════════════════════════════" << std::endl;
        std::cout << "📏 Virtual Wall Dimensions:" << std::endl;
        std::cout << "   Width:  " << config_.actual_width_cm << " cm" << std::endl;
        std::cout << "   Height: " << config_.actual_height_cm << " cm" << std::endl;
        std::cout << "   Wall depth: " << std::fixed << std::setprecision(1) 
                 << config_.wall_depth_mm << " mm" << std::endl;
        std::cout << "   Penetration threshold: " << config_.penetration_threshold_mm << " mm" << std::endl;
        std::cout << "══════════════════════════════════════════════════════" << std::endl;
    }
    
    void printControls() {
        std::cout << "\n🎮 CONTROLS:" << std::endl;
        std::cout << "══════════════════════════════════════════════════════" << std::endl;
        std::cout << "   SPACE - Toggle monitoring on/off" << std::endl;
        std::cout << "   R     - Reset interference counter" << std::endl;
        std::cout << "   B     - Reset baseline (recalibrate normal state)" << std::endl;
        std::cout << "   G     - Toggle cluster grid overlay" << std::endl;
        std::cout << "   D     - Toggle debug mode (detailed logs)" << std::endl;
        std::cout << "   Q     - Quit monitor" << std::endl;
        std::cout << "══════════════════════════════════════════════════════\n" << std::endl;
    }
    
    bool captureFrames() {
        warning_manager_.suppressWarnings();
        auto frames = camera_manager_->getLatestFrames();
        warning_manager_.restoreWarnings();
        
        if (frames.empty()) {
            return false;
        }
        
        depth_frame_ = frames[0].clone();
        
        if (frames.size() > 1) {
            confidence_frame_ = frames[1].clone();
        }
        
        return !depth_frame_.empty();
    }
    
    void processFrames() {
        if (depth_frame_.empty()) return;
        
        // Establish baseline depth map (capture the "normal" state with shelves)
        if (!baseline_established_) {
            establishBaseline();
        }
        
        // Convert depth to display format
        cv::Mat depth_8bit;
        depth_frame_.convertTo(depth_8bit, CV_8U, 255.0 / max_distance_, 0);
        cv::applyColorMap(depth_8bit, display_frame_, cv::COLORMAP_RAINBOW);
        
        // Apply confidence filter
        if (!confidence_frame_.empty()) {
            display_frame_.setTo(cv::Scalar(0, 0, 0), confidence_frame_ < confidence_threshold_);
        }

        updatePreRollBuffer();
    }

    void updatePreRollBuffer() {
        if (depth_frame_.empty()) return;

        BufferedRecordingFrame frame;
        frame.depth_frame = depth_frame_.clone();
        if (!confidence_frame_.empty()) {
            frame.confidence_frame = confidence_frame_.clone();
        }

        pre_roll_buffer_.push_back(frame);
        while (pre_roll_buffer_.size() > PRE_ROLL_FRAMES) {
            pre_roll_buffer_.pop_front();
        }
    }
    
    void establishBaseline() {
        if (baseline_frame_count_ == 0) {
            baseline_depth_ = cv::Mat::zeros(depth_frame_.size(), CV_32F);
            baseline_valid_ = cv::Mat::zeros(depth_frame_.size(), CV_8U);
            std::cout << "\n📸 Establishing baseline depth map..." << std::endl;
            std::cout << "   (Capturing normal state for 5 seconds - please wait)" << std::endl;
            std::cout << "   ⏳ Keep the area in normal state with shelves/items" << std::endl;
        }
        
        // Show progress every 30 frames (every second)
        if (baseline_frame_count_ % 30 == 0 && baseline_frame_count_ > 0) {
            int seconds_remaining = (BASELINE_FRAMES_NEEDED - baseline_frame_count_) / 30;
            std::cout << "   ⏳ Calibrating... " << seconds_remaining << " seconds remaining" << std::endl;
        }
        
        // Accumulate valid depth measurements
        for (int y = 0; y < depth_frame_.rows; y++) {
            for (int x = 0; x < depth_frame_.cols; x++) {
                float depth = depth_frame_.at<float>(y, x);
                float confidence = confidence_frame_.at<float>(y, x);
                
                if (confidence >= 1 && depth > 100 && depth < 5000) {
                    baseline_depth_.at<float>(y, x) += depth;
                    baseline_valid_.at<uchar>(y, x)++;
                }
            }
        }
        
        baseline_frame_count_++;
        
        // Average after collecting enough frames
        if (baseline_frame_count_ >= BASELINE_FRAMES_NEEDED) {
            for (int y = 0; y < baseline_depth_.rows; y++) {
                for (int x = 0; x < baseline_depth_.cols; x++) {
                    if (baseline_valid_.at<uchar>(y, x) > 0) {
                        baseline_depth_.at<float>(y, x) /= baseline_valid_.at<uchar>(y, x);
                    }
                }
            }
            baseline_established_ = true;
            autoTuneDetectionParameters();
            std::cout << "✅ Baseline established! Now monitoring for changes..." << std::endl;
            last_event_time_ = std::chrono::steady_clock::now();
        }
    }

    float percentile(std::vector<float>& values, float p) const {
        if (values.empty()) return 0.0f;
        std::sort(values.begin(), values.end());
        size_t index = static_cast<size_t>(std::round((p / 100.0f) * (values.size() - 1)));
        index = std::min(index, values.size() - 1);
        return values[index];
    }

    void autoTuneDetectionParameters() {
        std::vector<float> depths;
        std::vector<float> confidences;

        for (int y = 0; y < baseline_depth_.rows; y++) {
            for (int x = 0; x < baseline_depth_.cols; x++) {
                cv::Point2i pixel(x, y);
                if (!SimpleVirtualWallUtils::isPointInsideTrackingBoundary(pixel, config_)) continue;

                float depth = baseline_depth_.at<float>(y, x);
                if (depth > 100 && depth < 5000) {
                    depths.push_back(depth);
                }

                if (!confidence_frame_.empty()) {
                    float confidence = confidence_frame_.at<float>(y, x);
                    if (confidence > 0 && confidence < 5000) {
                        confidences.push_back(confidence);
                    }
                }
            }
        }

        if (depths.empty()) {
            std::cout << "⚠️  Auto-tune skipped: no valid baseline depths inside wall" << std::endl;
            return;
        }

        float depth_p50 = percentile(depths, 50.0f);
        float depth_p95 = percentile(depths, 95.0f);
        float depth_p99 = percentile(depths, 99.0f);
        far_shelf_mode_ = depth_p95 > 3000.0f;
        max_valid_depth_mm_ = std::clamp(depth_p99 + 500.0f, 2000.0f, 5000.0f);
        max_distance_ = static_cast<int>(std::clamp(depth_p95 + 300.0f, 2000.0f, 5000.0f));

        if (!confidences.empty()) {
            float confidence_p20 = percentile(confidences, 20.0f);
            confidence_threshold_ = static_cast<int>(std::clamp(confidence_p20 * 0.6f, 3.0f, 20.0f));
        }

        std::vector<float> deviations;
        deviations.reserve(depths.size());
        for (float depth : depths) {
            deviations.push_back(std::abs(depth - depth_p50));
        }
        float noise_mad = percentile(deviations, 50.0f);
        motion_threshold_mm_ = std::clamp(noise_mad * 6.0f, 60.0f, DEPTH_CHANGE_THRESHOLD);
        if (far_shelf_mode_) {
            confidence_threshold_ = std::max(2, confidence_threshold_ - 2);
            min_valid_pixels_per_cluster_ = 2;
            min_interference_pixels_per_cluster_ = 2;
            motion_threshold_mm_ = std::min(motion_threshold_mm_, 120.0f);
        } else {
            min_valid_pixels_per_cluster_ = 3;
            min_interference_pixels_per_cluster_ = 3;
        }

        visualization_min_depth_mm_ = std::max(0.0f, depth_p50 - (far_shelf_mode_ ? 1200.0f : 900.0f));
        visualization_max_depth_mm_ = std::min(max_valid_depth_mm_, depth_p95 + 300.0f);
        if (visualization_max_depth_mm_ <= visualization_min_depth_mm_ + 200.0f) {
            visualization_min_depth_mm_ = 0.0f;
            visualization_max_depth_mm_ = static_cast<float>(max_distance_);
        }

        std::cout << "\n🔧 AUTO-TUNED DETECTION PARAMETERS:" << std::endl;
        std::cout << "   Far shelf mode: " << (far_shelf_mode_ ? "ON" : "OFF") << std::endl;
        std::cout << "   Baseline depth p50/p95/p99: " << std::fixed << std::setprecision(0)
                  << depth_p50 << "/" << depth_p95 << "/" << depth_p99 << " mm" << std::endl;
        std::cout << "   Display max distance: " << max_distance_ << " mm" << std::endl;
        std::cout << "   Valid depth max: " << max_valid_depth_mm_ << " mm" << std::endl;
        std::cout << "   Confidence threshold: " << confidence_threshold_ << std::endl;
        std::cout << "   Motion threshold: " << motion_threshold_mm_ << " mm" << std::endl;
        std::cout << "   Min valid pixels/cluster: " << min_valid_pixels_per_cluster_ << std::endl;
        std::cout << "   Min interference pixels/cluster: " << min_interference_pixels_per_cluster_ << std::endl;
        std::cout << "   Saved depth contrast range: " << visualization_min_depth_mm_ 
                  << " - " << visualization_max_depth_mm_ << " mm" << std::endl;
    }
    
    void detectInterference() {
        if (depth_frame_.empty() || confidence_frame_.empty() || !baseline_established_) {
            return;
        }

        // ✨ NEW: Create depth clusters (spatial noise filtering!)
        std::vector<DepthCluster> clusters = SimpleVirtualWallUtils::createDepthClusters(
            depth_frame_, confidence_frame_, baseline_depth_, config_,
            confidence_threshold_, motion_threshold_mm_, max_valid_depth_mm_,
            min_valid_pixels_per_cluster_, far_shelf_mode_, min_interference_pixels_per_cluster_);
        
        // Filter clusters that show interference
        std::vector<DepthCluster> interference_clusters;
        for (const auto& cluster : clusters) {
            if (cluster.has_motion && cluster.penetrates_wall) {
                interference_clusters.push_back(cluster);
            }
        }
        
        // Apply temporal filtering
        bool current_frame_has_interference = !interference_clusters.empty();
        recent_detections_[temporal_filter_index_] = current_frame_has_interference;
        temporal_filter_index_ = (temporal_filter_index_ + 1) % TEMPORAL_FILTER_FRAMES;
        
        // Count detections in recent frames
        int detection_count = 0;
        for (bool detected : recent_detections_) {
            if (detected) detection_count++;
        }
        
        // DEBUG MODE: Print detailed cluster statistics
        if (debug_mode_ && !clusters.empty()) {
            int motion_clusters = 0;
            int penetrating_clusters = 0;
            int far_pixel_hits = 0;
            float max_change = 0;
            const DepthCluster* max_cluster = nullptr;
            
            for (const auto& c : clusters) {
                if (c.has_motion) motion_clusters++;
                if (c.penetrates_wall) penetrating_clusters++;
                far_pixel_hits += c.interference_pixel_count;
                
                float change = c.baseline_median - c.median_depth;
                if (change > max_change) {
                    max_change = change;
                    max_cluster = &c;
                }
            }
            
            if (motion_clusters > 0 || penetrating_clusters > 0) {
                std::cout << "\n🔍 DEBUG - Cluster Stats:" << std::endl;
                std::cout << "   Total clusters: " << clusters.size() << std::endl;
                std::cout << "   Motion clusters: " << motion_clusters << std::endl;
                std::cout << "   Penetrating clusters: " << penetrating_clusters << std::endl;
                std::cout << "   Interference clusters: " << interference_clusters.size() << std::endl;
                std::cout << "   Far pixel hits: " << far_pixel_hits << std::endl;
                std::cout << "   Temporal filter: " << detection_count << "/3 frames" << std::endl;
                
                if (max_cluster) {
                    std::cout << "   Max change: " << std::fixed << std::setprecision(1) 
                             << max_change << "mm at grid (" 
                             << max_cluster->grid_pos.x << ", " << max_cluster->grid_pos.y << ")" << std::endl;
                    std::cout << "   → Baseline: " << max_cluster->baseline_median << "mm" << std::endl;
                    std::cout << "   → Current: " << max_cluster->median_depth << "mm" << std::endl;
                    std::cout << "   → Valid pixels: " << max_cluster->valid_pixel_count << std::endl;
                }
            }
        }
        
        bool confirmed_interference = detection_count >= START_CONFIRM_FRAMES &&
            current_frame_has_interference &&
            interference_clusters.size() >= MIN_CLUSTERS_FOR_DETECTION;

        if (confirmed_interference) {
            latest_object_stats_ = computeObjectDepthStats(interference_clusters);
        }

        updateActionRecording(confirmed_interference, interference_clusters);

        if (confirmed_interference && recording_state_ == RecordingState::RECORDING) {
            updateCurrentActionStats(interference_clusters);
        } else if (debug_mode_ && !clusters.empty() && (interference_clusters.size() > 0)) {
            std::cout << "   ℹ️  DEBUG - Filtered: temporal=" << detection_count 
                      << "/3, clusters=" << interference_clusters.size() 
                      << "/" << MIN_CLUSTERS_FOR_DETECTION << std::endl;
        }
    }

    ObjectDepthStats computeObjectDepthStats(const std::vector<DepthCluster>& clusters) const {
        ObjectDepthStats stats;
        if (clusters.empty()) return stats;

        float weighted_x = 0.0f;
        float weighted_y = 0.0f;
        float weighted_depth = 0.0f;
        int total_weight = 0;
        float min_depth = std::numeric_limits<float>::max();
        float max_depth = 0.0f;

        for (const auto& cluster : clusters) {
            int weight = std::max(1, cluster.interference_pixel_count > 0
                ? cluster.interference_pixel_count
                : cluster.valid_pixel_count);
            weighted_x += cluster.pixel_center.x * weight;
            weighted_y += cluster.pixel_center.y * weight;
            weighted_depth += cluster.median_depth * weight;
            total_weight += weight;
            min_depth = std::min(min_depth, cluster.median_depth);
            max_depth = std::max(max_depth, cluster.median_depth);
        }

        if (total_weight <= 0) return stats;

        stats.valid = true;
        stats.center_pixel = cv::Point2f(weighted_x / total_weight, weighted_y / total_weight);
        stats.center_cm = SimpleVirtualWallUtils::pixelToRealWorld(
            cv::Point2i(static_cast<int>(std::round(stats.center_pixel.x)),
                        static_cast<int>(std::round(stats.center_pixel.y))),
            config_);
        stats.average_depth_mm = weighted_depth / total_weight;
        stats.min_depth_mm = min_depth;
        stats.max_depth_mm = max_depth;
        stats.cluster_count = clusters.size();
        return stats;
    }

    void updateActionRecording(bool has_interference, const std::vector<DepthCluster>& clusters) {
        if (has_interference) {
            clear_frame_count_ = 0;

            if (recording_state_ == RecordingState::IDLE) {
                startActionRecording(clusters);
            } else if (recording_state_ == RecordingState::ENDING) {
                recording_state_ = RecordingState::RECORDING;
            }
        } else if (recording_state_ != RecordingState::IDLE) {
            clear_frame_count_++;
            if (clear_frame_count_ > POST_ROLL_FRAMES || current_action_.frame_count >= MAX_ACTION_FRAMES) {
                finishActionRecording();
                return;
            }
            recording_state_ = RecordingState::ENDING;
        }

        if (recording_state_ != RecordingState::IDLE) {
            recordActionFrame();
        }
    }

    void startActionRecording(const std::vector<DepthCluster>& clusters) {
        if (recording_state_ != RecordingState::IDLE) {
            return;
        }

        current_action_ = ActionRecordingSession();
        latest_object_stats_ = computeObjectDepthStats(clusters);
        best_object_stats_ = latest_object_stats_;
        current_action_.start_timestamp = SimpleVirtualWallUtils::getCurrentTimestamp();
        current_action_.start_time = std::chrono::steady_clock::now();

        std::string day = getDayFromTimestamp(current_action_.start_timestamp);
        std::filesystem::path day_dir = recordings_base_dir_ / day;
        std::filesystem::create_directories(day_dir);

        action_counter_ = getNextActionNumber(day_dir);
        action_counter_++;
        interference_count_++;
        recording_state_ = RecordingState::RECORDING;
        clear_frame_count_ = 0;

        current_action_.action_id = buildActionId(action_counter_);

        current_action_.session_dir = day_dir / current_action_.action_id;
        current_action_.frames_depth_png_dir = current_action_.session_dir / "frames" / "depth_png";
        current_action_.frames_depth_raw_dir = current_action_.session_dir / "frames" / "depth_raw";
        current_action_.frames_confidence_raw_dir = current_action_.session_dir / "frames" / "confidence_raw";
        current_action_.video_dir = current_action_.session_dir / "video";

        std::filesystem::create_directories(current_action_.frames_depth_png_dir);
        std::filesystem::create_directories(current_action_.frames_depth_raw_dir);
        std::filesystem::create_directories(current_action_.frames_confidence_raw_dir);
        std::filesystem::create_directories(current_action_.video_dir);

        initializeActionVideos();
        writePreRollFrames();
        updateCurrentActionStats(clusters);
        logInterferenceEventFromClusters(clusters);

        std::cout << "🎥 Recording action " << current_action_.action_id << std::endl;
        std::cout << "   Folder: " << current_action_.session_dir << std::endl;
    }

    std::string getDayFromTimestamp(const std::string& timestamp) const {
        if (timestamp.size() < 8) {
            return "unknown_date";
        }

        return timestamp.substr(0, 4) + "-" +
               timestamp.substr(4, 2) + "-" +
               timestamp.substr(6, 2);
    }

    int getNextActionNumber(const std::filesystem::path& day_dir) const {
        int max_action_number = 0;

        if (!std::filesystem::exists(day_dir)) {
            return 0;
        }

        for (const auto& entry : std::filesystem::directory_iterator(day_dir)) {
            if (!entry.is_directory()) continue;

            std::string folder_name = entry.path().filename().string();
            const std::string prefix = "action_";
            if (folder_name.rfind(prefix, 0) != 0) continue;

            std::string number_text = folder_name.substr(prefix.size());
            try {
                int number = std::stoi(number_text);
                max_action_number = std::max(max_action_number, number);
            } catch (const std::exception&) {
                continue;
            }
        }

        return max_action_number;
    }

    std::string buildActionId(int action_number) const {
        std::ostringstream ss;
        ss << "action_" << action_number;
        return ss.str();
    }

    void initializeActionVideos() {
        cv::Size frame_size(depth_frame_.cols, depth_frame_.rows);
        int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        std::string depth_video_path = (current_action_.video_dir / (current_action_.action_id + "_depth.mp4")).string();

        current_action_.depth_video.open(depth_video_path, fourcc, RECORDING_FPS, frame_size, true);

        if (!current_action_.depth_video.isOpened()) {
            std::cout << "⚠️  Video writer could not open MP4 output; frame files will still be saved" << std::endl;
        }
    }

    void updateCurrentActionStats(const std::vector<DepthCluster>& clusters) {
        if (clusters.empty()) return;

        const DepthCluster* primary_cluster = &clusters[0];
        float max_penetration = 0;

        for (const auto& cluster : clusters) {
            float wall_depth = SimpleVirtualWallUtils::interpolateWallDepth(cluster.pixel_center, config_);
            float penetration = wall_depth - cluster.median_depth;
            if (penetration > max_penetration) {
                max_penetration = penetration;
                primary_cluster = &cluster;
            }
        }

        cv::Point2f position = SimpleVirtualWallUtils::pixelToRealWorld(primary_cluster->pixel_center, config_);
        if (current_action_.frame_count == 0) {
            current_action_.entry_position_cm = position;
        }
        current_action_.last_position_cm = position;
        current_action_.max_penetration_mm = std::max(current_action_.max_penetration_mm, max_penetration);
        current_action_.max_cluster_count = std::max(current_action_.max_cluster_count, clusters.size());
        latest_object_stats_ = computeObjectDepthStats(clusters);
        if (latest_object_stats_.valid &&
            (!best_object_stats_.valid || latest_object_stats_.cluster_count >= best_object_stats_.cluster_count)) {
            best_object_stats_ = latest_object_stats_;
        }
    }

    cv::Mat createDepthVisualization(const cv::Mat& depth_frame, const cv::Mat& confidence_frame) const {
        cv::Mat depth_8bit;
        cv::Mat depth_color;
        float vis_min = visualization_min_depth_mm_;
        float vis_max = visualization_max_depth_mm_;
        if (vis_max <= vis_min + 1.0f) {
            vis_min = 0.0f;
            vis_max = static_cast<float>(max_distance_);
        }
        depth_frame.convertTo(depth_8bit, CV_8U, 255.0 / (vis_max - vis_min), -vis_min * 255.0 / (vis_max - vis_min));
        cv::applyColorMap(depth_8bit, depth_color, cv::COLORMAP_RAINBOW);
        if (!confidence_frame.empty()) {
            depth_color.setTo(cv::Scalar(0, 0, 0), confidence_frame < confidence_threshold_);
        }
        return depth_color;
    }

    cv::Mat createDepthVisualization() const {
        return createDepthVisualization(depth_frame_, confidence_frame_);
    }

    void writePreRollFrames() {
        current_action_.pre_roll_frame_count = static_cast<int>(pre_roll_buffer_.size());
        for (const auto& frame : pre_roll_buffer_) {
            enqueueRecordingFrame(frame.depth_frame, frame.confidence_frame, RecordingFramePhase::BEFORE);
        }
    }

    void recordActionFrame() {
        RecordingFramePhase phase = RecordingFramePhase::DURING;
        if (recording_state_ == RecordingState::ENDING) {
            current_action_.post_roll_frame_count++;
            phase = RecordingFramePhase::AFTER;
        }
        enqueueRecordingFrame(depth_frame_, confidence_frame_, phase);
    }

    void enqueueRecordingFrame(const cv::Mat& depth_frame, const cv::Mat& confidence_frame,
                               RecordingFramePhase phase) {
        if (depth_frame.empty()) return;

        RecordingFrameJob job;
        job.frame_number = current_action_.frame_count + 1;
        job.phase = phase;
        job.depth_frame = depth_frame.clone();
        if (!confidence_frame.empty()) {
            job.confidence_frame = confidence_frame.clone();
        }
        job.frames_depth_png_dir = current_action_.frames_depth_png_dir;
        job.frames_depth_raw_dir = current_action_.frames_depth_raw_dir;
        job.frames_confidence_raw_dir = current_action_.frames_confidence_raw_dir;
        job.action_id = current_action_.action_id;
        job.object_stats = latest_object_stats_;

        {
            std::lock_guard<std::mutex> lock(recording_queue_mutex_);
            if (recording_queue_.size() >= MAX_RECORDING_QUEUE_SIZE) {
                recording_queue_.pop_front();
                std::cout << "⚠️  Recording queue full; dropped oldest pending frame" << std::endl;
            }
            recording_queue_.push_back(std::move(job));
        }
        recording_queue_cv_.notify_one();

        current_action_.frame_count++;
    }

    void recordingWriterLoop() {
        while (true) {
            RecordingFrameJob job;
            {
                std::unique_lock<std::mutex> lock(recording_queue_mutex_);
                recording_queue_cv_.wait(lock, [this]() {
                    return !recording_queue_.empty() || !recording_writer_running_;
                });

                if (recording_queue_.empty() && !recording_writer_running_) {
                    break;
                }

                job = std::move(recording_queue_.front());
                recording_queue_.pop_front();
            }

            if (job.is_shutdown) {
                break;
            }

            writeRecordingFrameJob(job);
        }
    }

    void writeRecordingFrameJob(const RecordingFrameJob& job) {
        std::ostringstream name;
        name << "frame_" << job.frame_number;
        if (job.phase == RecordingFramePhase::BEFORE) {
            name << "_before";
        } else if (job.phase == RecordingFramePhase::AFTER) {
            name << "_after";
        }

        cv::Mat depth_vis = createDepthVisualization(job.depth_frame, job.confidence_frame);
        annotateDepthVisualization(depth_vis, job.object_stats);
        cv::imwrite((job.frames_depth_png_dir / (name.str() + ".png")).string(), depth_vis);

        cv::FileStorage depth_file((job.frames_depth_raw_dir / (name.str() + ".yml")).string(), cv::FileStorage::WRITE);
        depth_file << "depth_mm" << job.depth_frame;
        depth_file.release();

        if (!job.confidence_frame.empty()) {
            cv::FileStorage confidence_file((job.frames_confidence_raw_dir / (name.str() + ".yml")).string(), cv::FileStorage::WRITE);
            confidence_file << "confidence" << job.confidence_frame;
            confidence_file.release();
        }

        std::lock_guard<std::mutex> video_lock(video_writer_mutex_);
        if (current_action_.action_id == job.action_id && current_action_.depth_video.isOpened()) {
            current_action_.depth_video.write(depth_vis);
        }
    }

    void annotateDepthVisualization(cv::Mat& image, const ObjectDepthStats& stats) const {
        if (!stats.valid || image.empty()) return;

        cv::Point center(
            std::clamp(static_cast<int>(std::round(stats.center_pixel.x)), 0, image.cols - 1),
            std::clamp(static_cast<int>(std::round(stats.center_pixel.y)), 0, image.rows - 1));

        cv::circle(image, center, 3, cv::Scalar(255, 0, 0), -1);

        std::ostringstream text;
        text << std::fixed << std::setprecision(0) << stats.average_depth_mm << "mm";
        int baseline = 0;
        double font_scale = 0.35;
        int thickness = 1;
        cv::Size text_size = cv::getTextSize(text.str(), cv::FONT_HERSHEY_SIMPLEX,
                                             font_scale, thickness, &baseline);
        cv::Point origin(std::max(2, image.cols - text_size.width - 4), text_size.height + 4);
        cv::putText(image, text.str(), origin, cv::FONT_HERSHEY_SIMPLEX,
                    font_scale, cv::Scalar(255, 0, 0), thickness, cv::LINE_AA);
    }

    void waitForRecordingQueueToDrain() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(recording_queue_mutex_);
                if (recording_queue_.empty()) return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void stopRecordingWriter() {
        {
            std::lock_guard<std::mutex> lock(recording_queue_mutex_);
            recording_writer_running_ = false;
        }
        recording_queue_cv_.notify_all();
        if (recording_writer_thread_.joinable()) {
            recording_writer_thread_.join();
        }
    }

    void finishActionRecording() {
        if (recording_state_ == RecordingState::IDLE) return;

        current_action_.end_timestamp = SimpleVirtualWallUtils::getCurrentTimestamp();
        waitForRecordingQueueToDrain();
        std::lock_guard<std::mutex> video_lock(video_writer_mutex_);
        if (current_action_.depth_video.isOpened()) current_action_.depth_video.release();
        saveActionMetadata();

        std::cout << "✅ Finished recording " << current_action_.action_id
                  << " (" << current_action_.frame_count << " frames)" << std::endl;

        recording_state_ = RecordingState::IDLE;
        clear_frame_count_ = 0;
    }

    void saveActionMetadata() {
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - current_action_.start_time).count();
        int during_frame_count = std::max(0, current_action_.frame_count -
            current_action_.pre_roll_frame_count - current_action_.post_roll_frame_count);
        int before_end = current_action_.pre_roll_frame_count;
        int during_start = before_end + 1;
        int during_end = before_end + during_frame_count;
        int after_start = during_end + 1;

        Json::Value root;
        root["action_id"] = current_action_.action_id;
        root["label"] = current_action_.label;
        root["classification_status"] = "unlabeled";
        root["start_timestamp"] = current_action_.start_timestamp;
        root["end_timestamp"] = current_action_.end_timestamp;
        root["duration_ms"] = static_cast<Json::Int64>(duration_ms);
        root["frame_count"] = current_action_.frame_count;
        root["pre_roll_frames"] = current_action_.pre_roll_frame_count;
        root["pre_roll_seconds_target"] = PRE_ROLL_FRAMES / RECORDING_FPS;
        root["post_roll_frames"] = current_action_.post_roll_frame_count;
        root["post_roll_frames_target"] = POST_ROLL_FRAMES;
        root["post_roll_seconds_target"] = POST_ROLL_FRAMES / RECORDING_FPS;
        root["frame_phases"]["before_count"] = current_action_.pre_roll_frame_count;
        root["frame_phases"]["during_count"] = during_frame_count;
        root["frame_phases"]["after_count"] = current_action_.post_roll_frame_count;
        if (current_action_.pre_roll_frame_count > 0) {
            root["frame_phases"]["before_range"].append(1);
            root["frame_phases"]["before_range"].append(before_end);
        }
        if (during_frame_count > 0) {
            root["frame_phases"]["during_range"].append(during_start);
            root["frame_phases"]["during_range"].append(during_end);
        }
        if (current_action_.post_roll_frame_count > 0) {
            root["frame_phases"]["after_range"].append(after_start);
            root["frame_phases"]["after_range"].append(current_action_.frame_count);
        }
        root["fps_target"] = RECORDING_FPS;
        root["recorded_content"].append("depth_png");
        root["recorded_content"].append("depth_raw");
        root["recorded_content"].append("confidence_raw");
        root["recorded_content"].append("depth_video");
        root["entry_position_cm"]["x"] = current_action_.entry_position_cm.x;
        root["entry_position_cm"]["y"] = current_action_.entry_position_cm.y;
        root["last_position_cm"]["x"] = current_action_.last_position_cm.x;
        root["last_position_cm"]["y"] = current_action_.last_position_cm.y;
        root["max_penetration_mm"] = current_action_.max_penetration_mm;
        root["max_cluster_count"] = static_cast<Json::UInt64>(current_action_.max_cluster_count);
        root["wall_config_id"] = config_.unique_id;
        root["wall_width_cm"] = config_.actual_width_cm;
        root["wall_height_cm"] = config_.actual_height_cm;
        root["wall_depth_mm"] = config_.wall_depth_mm;
        root["penetration_threshold_mm"] = config_.penetration_threshold_mm;
        root["auto_tuned_parameters"]["display_max_distance_mm"] = max_distance_;
        root["auto_tuned_parameters"]["valid_depth_max_mm"] = max_valid_depth_mm_;
        root["auto_tuned_parameters"]["confidence_threshold"] = confidence_threshold_;
        root["auto_tuned_parameters"]["motion_threshold_mm"] = motion_threshold_mm_;
        root["auto_tuned_parameters"]["far_shelf_mode"] = far_shelf_mode_;
        root["auto_tuned_parameters"]["min_valid_pixels_per_cluster"] = min_valid_pixels_per_cluster_;
        root["auto_tuned_parameters"]["min_interference_pixels_per_cluster"] = min_interference_pixels_per_cluster_;
        root["auto_tuned_parameters"]["visualization_min_depth_mm"] = visualization_min_depth_mm_;
        root["auto_tuned_parameters"]["visualization_max_depth_mm"] = visualization_max_depth_mm_;
        root["object_depth_stats"]["valid"] = best_object_stats_.valid;
        if (best_object_stats_.valid) {
            root["object_depth_stats"]["center_pixel"]["x"] = best_object_stats_.center_pixel.x;
            root["object_depth_stats"]["center_pixel"]["y"] = best_object_stats_.center_pixel.y;
            root["object_depth_stats"]["center_cm"]["x"] = best_object_stats_.center_cm.x;
            root["object_depth_stats"]["center_cm"]["y"] = best_object_stats_.center_cm.y;
            root["object_depth_stats"]["average_depth_mm"] = best_object_stats_.average_depth_mm;
            root["object_depth_stats"]["min_depth_mm"] = best_object_stats_.min_depth_mm;
            root["object_depth_stats"]["max_depth_mm"] = best_object_stats_.max_depth_mm;
            root["object_depth_stats"]["cluster_count"] = static_cast<Json::UInt64>(best_object_stats_.cluster_count);
        }

        std::ofstream file(current_action_.session_dir / "metadata.json");
        if (file.is_open()) {
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "  ";
            std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(root, &file);
        }
    }
    
    void logInterferenceEventFromClusters(const std::vector<DepthCluster>& clusters) {
        if (clusters.empty()) return;

        // Find cluster with maximum penetration
        float max_penetration = 0;
        const DepthCluster* primary_cluster = &clusters[0];
        
        for (const auto& cluster : clusters) {
            float wall_depth = SimpleVirtualWallUtils::interpolateWallDepth(cluster.pixel_center, config_);
            float penetration = wall_depth - cluster.median_depth;
            if (penetration > max_penetration) {
                max_penetration = penetration;
                primary_cluster = &cluster;
            }
        }
        
        // Create ONE event representing the entire interference region
        SimpleInterferenceEvent event;
        
        // Timestamp and ID
        event.timestamp = SimpleVirtualWallUtils::getCurrentTimestamp();
        event.event_id = SimpleVirtualWallUtils::generateEventId();
        
        // Use the primary cluster's center
        event.pixel_coord = primary_cluster->pixel_center;
        event.measured_depth_mm = primary_cluster->median_depth;
        event.wall_depth_mm = SimpleVirtualWallUtils::interpolateWallDepth(primary_cluster->pixel_center, config_);
        event.penetration_depth_mm = max_penetration;
        
        // Convert to real-world coordinates (X, Y only - no Z!)
        cv::Point2f real_world = SimpleVirtualWallUtils::pixelToRealWorld(primary_cluster->pixel_center, config_);
        event.x_position_cm = real_world.x;
        event.y_position_cm = real_world.y;
        
        // Clean console output - ONE event!
        std::cout << "\n🚨 ══════════════════════════════════════════════════════" << std::endl;
        std::cout << "   INTERFERENCE DETECTED - Event #" << interference_count_.load() << std::endl;
        std::cout << "══════════════════════════════════════════════════════" << std::endl;
        std::cout << "📋 ID: " << event.event_id << std::endl;
        std::cout << "🕐 Time: " << event.timestamp << std::endl;
        std::cout << "📍 Position: " << std::fixed << std::setprecision(1) 
                 << event.x_position_cm << " cm (X), " << event.y_position_cm << " cm (Y)" << std::endl;
        std::cout << "📏 Depth: " << std::setprecision(0) << event.measured_depth_mm << " mm (measured) vs " 
                 << event.wall_depth_mm << " mm (wall)" << std::endl;
        std::cout << "⚠️  Penetration: " << event.penetration_depth_mm << " mm" << std::endl;
        std::cout << "📊 Clusters: " << clusters.size() << " affected (10x10 px each)" << std::endl;
        
        // Show all detected clusters
        std::cout << "📌 Detected clusters:" << std::endl;
        for (size_t i = 0; i < clusters.size() && i < 5; i++) {
            const auto& c = clusters[i];
            float wall_d = SimpleVirtualWallUtils::interpolateWallDepth(c.pixel_center, config_);
            float pen = wall_d - c.median_depth;
            cv::Point2f rw = SimpleVirtualWallUtils::pixelToRealWorld(c.pixel_center, config_);
            
            std::cout << "   " << (i+1) << ". Grid(" << c.grid_pos.x << "," << c.grid_pos.y << ") "
                     << "Pixel(" << c.pixel_center.x << "," << c.pixel_center.y << ") "
                     << "Pos(" << std::fixed << std::setprecision(1) << rw.x << "," << rw.y << "cm) "
                     << "Depth=" << std::setprecision(0) << c.median_depth << "mm "
                     << "Pen=" << pen << "mm" << std::endl;
        }
        if (clusters.size() > 5) {
            std::cout << "   ... and " << (clusters.size() - 5) << " more clusters" << std::endl;
        }
        std::cout << "══════════════════════════════════════════════════════\n" << std::endl;
        
        // Add to event queue for logging
        {
            std::lock_guard<std::mutex> lock(event_queue_mutex_);
            event_queue_.push_back(event);
        }
    }
    
    void logInterferenceEvent(const std::vector<cv::Point2i>& points) {
        if (points.empty()) return;
        
        interference_count_++;  // Increment ONCE per detection, not per pixel!
        
        // Cluster all points into a SINGLE event
        // Calculate average position and find most significant penetration
        float avg_x = 0, avg_y = 0;
        float max_penetration = 0;
        cv::Point2i center_pixel = points[0];
        float center_depth = 0;
        float center_wall_depth = 0;
        
        for (const auto& pixel : points) {
            avg_x += pixel.x;
            avg_y += pixel.y;
            
            float depth = depth_frame_.at<float>(pixel.y, pixel.x);
            float wall_depth = SimpleVirtualWallUtils::interpolateWallDepth(pixel, config_);
            float penetration = SimpleVirtualWallUtils::calculatePenetrationDistance(pixel, depth, config_);
            
            if (penetration > max_penetration) {
                max_penetration = penetration;
                center_pixel = pixel;
                center_depth = depth;
                center_wall_depth = wall_depth;
            }
        }
        
        avg_x /= points.size();
        avg_y /= points.size();
        
        // Create ONE event representing the entire interference region
        SimpleInterferenceEvent event;
        
        // Timestamp and ID
        event.timestamp = SimpleVirtualWallUtils::getCurrentTimestamp();
        event.event_id = SimpleVirtualWallUtils::generateEventId();
        
        // Use the pixel with maximum penetration as representative
        event.pixel_coord = center_pixel;
        event.measured_depth_mm = center_depth;
        event.wall_depth_mm = center_wall_depth;
        event.penetration_depth_mm = max_penetration;
        
        // Convert to real-world coordinates (X, Y only - no Z!)
        cv::Point2f real_world = SimpleVirtualWallUtils::pixelToRealWorld(center_pixel, config_);
        event.x_position_cm = real_world.x;
        event.y_position_cm = real_world.y;
        
        // Enhanced console output - ONE event!
        std::cout << "\n🚨 ═══════════════════════════════════════════════" << std::endl;
        std::cout << "   INTERFERENCE DETECTED" << std::endl;
        std::cout << "═══════════════════════════════════════════════" << std::endl;
        std::cout << "📋 Event ID: " << event.event_id << std::endl;
        std::cout << "🕐 Time: " << event.timestamp << std::endl;
        std::cout << "📊 Affected Area: " << points.size() << " pixels" << std::endl;
        std::cout << "📍 Center Position (X,Y): " << std::fixed << std::setprecision(1) 
                 << event.x_position_cm << " cm, " << event.y_position_cm << " cm" << std::endl;
        std::cout << "🖼️  Center Pixel: (" << center_pixel.x << ", " << center_pixel.y << ")" << std::endl;
        std::cout << "📏 Measured Depth: " << std::setprecision(0) << event.measured_depth_mm << " mm" << std::endl;
        std::cout << "🧱 Wall Depth: " << event.wall_depth_mm << " mm" << std::endl;
        std::cout << "⚠️  Max Penetration: " << event.penetration_depth_mm << " mm" << std::endl;
        std::cout << "═══════════════════════════════════════════════\n" << std::endl;
        
        // Add to event queue for logging
        {
            std::lock_guard<std::mutex> lock(event_queue_mutex_);
            event_queue_.push_back(event);
        }
    }
    
    void processEventQueue() {
        std::lock_guard<std::mutex> lock(event_queue_mutex_);
        
        while (!event_queue_.empty()) {
            SimpleInterferenceEvent event = event_queue_.front();
            event_queue_.pop_front();
            
            SimpleVirtualWallUtils::logInterferenceEvent(event, log_file_path_);
        }
    }
    
    void displayFrames() {
        if (display_frame_.empty()) return;
        
        cv::Mat display = display_frame_.clone();
        
        // Draw cluster grid (shows spatial filtering regions)
        if (show_grid_) {
            SimpleVirtualWallUtils::drawClusterGridWithWall(display, CLUSTER_SIZE, config_);
        }
        
        // Draw virtual wall boundary (no text)
        SimpleVirtualWallUtils::drawVirtualWall(display, config_);
        
        // Create combined layout with small sidebar
        cv::Mat combined = createCombinedLayout(display);
        
        cv::imshow("Virtual Wall Monitor", combined);
    }
    
    cv::Mat createCombinedLayout(const cv::Mat& camera_frame) {
        // Small sidebar (150 pixels wide)
        int sidebar_width = 150;
        int camera_height = camera_frame.rows;
        int camera_width = camera_frame.cols;
        
        // Create combined image
        cv::Mat combined = cv::Mat::zeros(camera_height, camera_width + sidebar_width, CV_8UC3);
        
        // Copy camera frame to left
        cv::Rect camera_roi(0, 0, camera_width, camera_height);
        camera_frame.copyTo(combined(camera_roi));
        
        // Create sidebar on right
        cv::Rect sidebar_roi(camera_width, 0, sidebar_width, camera_height);
        cv::Mat sidebar = combined(sidebar_roi);
        sidebar.setTo(cv::Scalar(30, 30, 30)); // Dark background
        
        // Draw sidebar content
        drawSidebarInfo(sidebar);
        
        return combined;
    }
    
    void drawSidebarInfo(cv::Mat& sidebar) {
        int y_pos = 25;
        
        // Title - smaller
        cv::putText(sidebar, "STATUS", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 255, 255), 1);
        y_pos += 25;
        
        cv::line(sidebar, cv::Point(5, y_pos - 8), cv::Point(145, y_pos - 8), 
                cv::Scalar(100, 100, 100), 1);
        
        // Monitoring status
        std::string status = monitoring_active_ ? "ACTIVE" : "PAUSED";
        cv::Scalar status_color = monitoring_active_ ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 165, 255);
        
        cv::putText(sidebar, "Monitor:", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(200, 200, 200), 1);
        y_pos += 16;
        cv::putText(sidebar, status, cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.4, status_color, 1);
        y_pos += 25;
        
        // Separator
        cv::line(sidebar, cv::Point(5, y_pos - 8), cv::Point(145, y_pos - 8), 
                cv::Scalar(60, 60, 60), 1);
        
        // Interference count
        cv::putText(sidebar, "Events:", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(200, 200, 200), 1);
        y_pos += 16;
        
        std::string count = std::to_string(interference_count_.load());
        cv::putText(sidebar, count, cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 2);
        y_pos += 25;
        
        // Separator
        cv::line(sidebar, cv::Point(5, y_pos - 8), cv::Point(145, y_pos - 8), 
                cv::Scalar(60, 60, 60), 1);
        
        // Wall dimensions
        cv::putText(sidebar, "Wall:", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(200, 200, 200), 1);
        y_pos += 16;
        
        std::stringstream ss;
        ss << (int)config_.actual_width_cm << "x" << (int)config_.actual_height_cm << "cm";
        cv::putText(sidebar, ss.str(), cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(180, 180, 180), 1);
        y_pos += 16;
        
        ss.str("");
        ss << (int)config_.wall_depth_mm << "mm";
        cv::putText(sidebar, ss.str(), cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(180, 180, 180), 1);
        y_pos += 30;
        
        // Controls at bottom
        cv::line(sidebar, cv::Point(5, y_pos - 8), cv::Point(145, y_pos - 8), 
                cv::Scalar(60, 60, 60), 1);
        
        cv::putText(sidebar, "CONTROLS", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(255, 255, 255), 1);
        y_pos += 18;
        
        cv::putText(sidebar, "Space:Pause", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.26, cv::Scalar(200, 200, 200), 1);
        y_pos += 13;
        
        cv::putText(sidebar, "R: Reset", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.26, cv::Scalar(200, 200, 200), 1);
        y_pos += 13;
        
        cv::putText(sidebar, "B: Baseline", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.26, cv::Scalar(200, 200, 200), 1);
        y_pos += 13;
        
        cv::putText(sidebar, "G: Grid", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.26, cv::Scalar(200, 200, 200), 1);
        y_pos += 13;
        
        cv::putText(sidebar, "Q: Quit", cv::Point(8, y_pos), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.26, cv::Scalar(200, 200, 200), 1);
    }
    
    void handleKeyboard(int key) {
        switch (key) {
            case ' ':  // Space
                monitoring_active_ = !monitoring_active_;
                std::cout << (monitoring_active_ ? "🟢 Monitoring ACTIVE" : "🔴 Monitoring PAUSED") << std::endl;
                break;
                
            case 'r':
            case 'R':
                interference_count_ = 0;
                std::cout << "🔄 Interference counter reset to 0" << std::endl;
                break;
                
            case 'b':
            case 'B':
                baseline_established_ = false;
                baseline_frame_count_ = 0;
                std::cout << "🔄 Resetting baseline... (establishing new normal state)" << std::endl;
                break;
                
            case 'g':
            case 'G':
                show_grid_ = !show_grid_;
                std::cout << (show_grid_ ? "🟢 Grid overlay ENABLED" : "🔴 Grid overlay DISABLED") << std::endl;
                break;
                
            case 'd':
            case 'D':
                debug_mode_ = !debug_mode_;
                std::cout << (debug_mode_ ? "🟢 Debug mode ENABLED (detailed logs)" : "🔴 Debug mode DISABLED (clean logs)") << std::endl;
                break;
                
            case 'q':
            case 'Q':
            case 27:  // ESC
                running_ = false;
                std::cout << "\n👋 Shutting down..." << std::endl;
                break;
        }
    }
};

int main() {
    try {
        SimpleVirtualWallMonitor monitor;
        monitor.run();
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return -1;
    }
}
