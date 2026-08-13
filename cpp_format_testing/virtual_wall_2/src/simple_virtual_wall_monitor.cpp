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

    struct ActionRecordingSession {
        std::string action_id;
        std::string label;
        std::string start_timestamp;
        std::string end_timestamp;
        std::filesystem::path session_dir;
        std::filesystem::path frames_display_dir;
        std::filesystem::path frames_depth_png_dir;
        std::filesystem::path frames_depth_raw_dir;
        std::filesystem::path frames_confidence_raw_dir;
        std::filesystem::path video_dir;
        cv::VideoWriter display_video;
        cv::VideoWriter depth_video;
        cv::Point2f entry_position_cm;
        cv::Point2f last_position_cm;
        float max_penetration_mm;
        size_t max_cluster_count;
        int frame_count;
        std::chrono::steady_clock::time_point start_time;

        ActionRecordingSession() :
            label("unknown"),
            entry_position_cm(0, 0),
            last_position_cm(0, 0),
            max_penetration_mm(0),
            max_cluster_count(0),
            frame_count(0) {}
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
    std::filesystem::path recordings_base_dir_;
    int action_counter_;
    int clear_frame_count_;
    static constexpr int START_CONFIRM_FRAMES = 2;
    static constexpr int CLEAR_CONFIRM_FRAMES = 8;
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
        recent_detections_(TEMPORAL_FILTER_FRAMES, false),
        temporal_filter_index_(0),
        recording_state_(RecordingState::IDLE),
        action_counter_(0),
        clear_frame_count_(0),
        
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
    }
    
    ~SimpleVirtualWallMonitor() {
        if (camera_manager_) {
            camera_manager_->stop();
        }
        processEventQueue();
        if (recording_state_ != RecordingState::IDLE) {
            finishActionRecording();
        }
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
                
                if (confidence >= confidence_threshold_ && depth > 100 && depth < 3500) {  // Lower min for close shelves
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
            std::cout << "✅ Baseline established! Now monitoring for changes..." << std::endl;
            last_event_time_ = std::chrono::steady_clock::now();
        }
    }
    
    void detectInterference() {
        if (depth_frame_.empty() || confidence_frame_.empty() || !baseline_established_) {
            return;
        }

        // ✨ NEW: Create depth clusters (spatial noise filtering!)
        std::vector<DepthCluster> clusters = SimpleVirtualWallUtils::createDepthClusters(
            depth_frame_, confidence_frame_, baseline_depth_, config_,
            confidence_threshold_, DEPTH_CHANGE_THRESHOLD);
        
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
            float max_change = 0;
            const DepthCluster* max_cluster = nullptr;
            
            for (const auto& c : clusters) {
                if (c.has_motion) motion_clusters++;
                if (c.penetrates_wall) penetrating_clusters++;
                
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

        updateActionRecording(confirmed_interference, interference_clusters);

        if (confirmed_interference && recording_state_ == RecordingState::RECORDING) {
            updateCurrentActionStats(interference_clusters);
        } else if (debug_mode_ && !clusters.empty() && (interference_clusters.size() > 0)) {
            std::cout << "   ℹ️  DEBUG - Filtered: temporal=" << detection_count 
                      << "/3, clusters=" << interference_clusters.size() 
                      << "/" << MIN_CLUSTERS_FOR_DETECTION << std::endl;
        }
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
            if (clear_frame_count_ >= CLEAR_CONFIRM_FRAMES || current_action_.frame_count >= MAX_ACTION_FRAMES) {
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
        current_action_.frames_display_dir = current_action_.session_dir / "frames" / "display_png";
        current_action_.frames_depth_png_dir = current_action_.session_dir / "frames" / "depth_png";
        current_action_.frames_depth_raw_dir = current_action_.session_dir / "frames" / "depth_raw";
        current_action_.frames_confidence_raw_dir = current_action_.session_dir / "frames" / "confidence_raw";
        current_action_.video_dir = current_action_.session_dir / "video";

        std::filesystem::create_directories(current_action_.frames_display_dir);
        std::filesystem::create_directories(current_action_.frames_depth_png_dir);
        std::filesystem::create_directories(current_action_.frames_depth_raw_dir);
        std::filesystem::create_directories(current_action_.frames_confidence_raw_dir);
        std::filesystem::create_directories(current_action_.video_dir);

        initializeActionVideos();
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
        std::string display_video_path = (current_action_.video_dir / (current_action_.action_id + "_display.mp4")).string();
        std::string depth_video_path = (current_action_.video_dir / (current_action_.action_id + "_depth.mp4")).string();

        current_action_.display_video.open(display_video_path, fourcc, RECORDING_FPS, frame_size, true);
        current_action_.depth_video.open(depth_video_path, fourcc, RECORDING_FPS, frame_size, true);

        if (!current_action_.display_video.isOpened() || !current_action_.depth_video.isOpened()) {
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
    }

    cv::Mat createDepthVisualization() const {
        cv::Mat depth_8bit;
        cv::Mat depth_color;
        depth_frame_.convertTo(depth_8bit, CV_8U, 255.0 / max_distance_, 0);
        cv::applyColorMap(depth_8bit, depth_color, cv::COLORMAP_RAINBOW);
        if (!confidence_frame_.empty()) {
            depth_color.setTo(cv::Scalar(0, 0, 0), confidence_frame_ < confidence_threshold_);
        }
        return depth_color;
    }

    void recordActionFrame() {
        if (depth_frame_.empty() || display_frame_.empty()) return;

        int frame_number = current_action_.frame_count + 1;
        std::ostringstream name;
        name << "frame_" << std::setfill('0') << std::setw(6) << frame_number;

        cv::Mat depth_vis = createDepthVisualization();
        cv::imwrite((current_action_.frames_display_dir / (name.str() + ".png")).string(), display_frame_);
        cv::imwrite((current_action_.frames_depth_png_dir / (name.str() + ".png")).string(), depth_vis);

        cv::FileStorage depth_file((current_action_.frames_depth_raw_dir / (name.str() + ".yml")).string(), cv::FileStorage::WRITE);
        depth_file << "depth_mm" << depth_frame_;
        depth_file.release();

        if (!confidence_frame_.empty()) {
            cv::FileStorage confidence_file((current_action_.frames_confidence_raw_dir / (name.str() + ".yml")).string(), cv::FileStorage::WRITE);
            confidence_file << "confidence" << confidence_frame_;
            confidence_file.release();
        }

        if (current_action_.display_video.isOpened()) {
            current_action_.display_video.write(display_frame_);
        }
        if (current_action_.depth_video.isOpened()) {
            current_action_.depth_video.write(depth_vis);
        }

        current_action_.frame_count++;
    }

    void finishActionRecording() {
        if (recording_state_ == RecordingState::IDLE) return;

        current_action_.end_timestamp = SimpleVirtualWallUtils::getCurrentTimestamp();
        if (current_action_.display_video.isOpened()) current_action_.display_video.release();
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

        Json::Value root;
        root["action_id"] = current_action_.action_id;
        root["label"] = current_action_.label;
        root["classification_status"] = "unlabeled";
        root["start_timestamp"] = current_action_.start_timestamp;
        root["end_timestamp"] = current_action_.end_timestamp;
        root["duration_ms"] = static_cast<Json::Int64>(duration_ms);
        root["frame_count"] = current_action_.frame_count;
        root["fps_target"] = RECORDING_FPS;
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
