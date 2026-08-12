#include "common.h"
#include "format_analyzer.h"
#include "data_saver.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <getopt.h>
#include <iomanip>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <vector>
#include <algorithm>
#include <limits>
#include <ctime>

// Forward declarations
class DualFormatTester;
void onMouseClick(int event, int x, int y, int flags, void* userdata);

class DualFormatTester {
    // Friend function for mouse callback
    friend void onMouseClick(int event, int x, int y, int flags, void* userdata);

public:
    // Distance measurement structure
    struct DistanceMeasurement {
        float center_distance;
        float min_distance;
        float max_distance;
        float avg_distance;
        std::string display_text;
        bool valid;
    };
    
    // Independent mouse measurements for each camera
    struct MouseMeasurement {
        bool active = false;
        cv::Point click_point;
        DistanceMeasurement last_measurement;
    };
    
    static MouseMeasurement mouse_state_cam0_;
    static MouseMeasurement mouse_state_cam1_;
    static DualFormatTester* instance_;

private:
    // Temporarily redirect stderr to suppress warnings
    void suppressWarnings() {
        // Set environment variables to reduce various warnings
        setenv("QT_LOGGING_RULES", "*.debug=false", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("OPENCV_LOG_LEVEL", "ERROR", 1);
        setenv("GLOG_minloglevel", "3", 1);  // Suppress Google logging
        
        // Redirect stderr to /dev/null temporarily
        original_stderr_ = dup(STDERR_FILENO);
        null_fd_ = open("/dev/null", O_WRONLY);
        if (null_fd_ != -1) {
            dup2(null_fd_, STDERR_FILENO);
        }
    }
    
    void restoreWarnings() {
        // Restore stderr
        if (original_stderr_ != -1) {
            dup2(original_stderr_, STDERR_FILENO);
            close(original_stderr_);
        }
        if (null_fd_ != -1) {
            close(null_fd_);
        }
    }

public:
    DualFormatTester(const Config& config) 
        : config_(config), analyzer_(config), saver_(config), running_(false), frame_counter_(0),
          saved_frames_count_(0), original_stderr_(-1), null_fd_(-1) {}
    
    bool initialize() {
        std::cout << "🔬 ToF Dual Camera Format Testing Tool (C++)" << std::endl;
        std::cout << "🚀 Initializing Camera System..." << std::endl;
        
        // Suppress warnings during camera initialization
        suppressWarnings();
        
        // Initialize Camera 0
        cam0_ = std::make_unique<CameraManager>(0);
        bool cam0_success = cam0_->initialize();
        
        // Restore warnings
        restoreWarnings();
        
        if (!cam0_success) {
            std::cerr << "Failed to initialize Camera 0" << std::endl;
            return false;
        }
        
        auto info0 = cam0_->getCameraInfo();
        std::cout << "✅ Camera 0: " << info0.width << "x" << info0.height;
        std::cout << ", Type: " << (info0.device_type == DeviceType::DEVICE_VGA ? "DEVICE_VGA" : "DEVICE_HQVGA") << std::endl;
        
        // Initialize Camera 1 if dual camera enabled
        if (config_.enable_dual_camera) {
            suppressWarnings();
            
            cam1_ = std::make_unique<CameraManager>(1);
            bool cam1_success = cam1_->initialize();
            
            restoreWarnings();
            
            if (!cam1_success) {
                std::cerr << "Failed to initialize Camera 1, continuing with single camera" << std::endl;
                config_.enable_dual_camera = false;
                cam1_.reset();
            } else {
                auto info1 = cam1_->getCameraInfo();
                std::cout << "✅ Camera 1: " << info1.width << "x" << info1.height;
                std::cout << ", Type: " << (info1.device_type == DeviceType::DEVICE_VGA ? "DEVICE_VGA" : "DEVICE_HQVGA") << std::endl;
            }
        }
        
        // Initialize data saver
        if (!saver_.initializeOutputDirectory()) {
            std::cerr << "Failed to initialize output directory" << std::endl;
            return false;
        }
        
        return true;
    }
    
    void run() {
        // Start cameras with warning suppression
        suppressWarnings();
        
        bool cam0_start_success = cam0_->start();
        bool cam1_start_success = true;
        if (config_.enable_dual_camera && cam1_) {
            cam1_start_success = cam1_->start();
        }
        
        restoreWarnings();
        
        if (!cam0_start_success) {
            std::cerr << "Failed to start Camera 0" << std::endl;
            return;
        }
        
        if (config_.enable_dual_camera && cam1_ && !cam1_start_success) {
            std::cerr << "Failed to start Camera 1" << std::endl;
            config_.enable_dual_camera = false;
        }
        
        std::cout << "🔬 Format Testing Started!" << std::endl;
        std::cout << "📺 Display Layout:" << std::endl;
        if (config_.enable_dual_camera) {
            std::cout << "   Left Column: Camera 0 | Right Column: Camera 1" << std::endl;
        } else {
            std::cout << "   2x2 Grid: Single Camera Analysis" << std::endl;
        }
        std::cout << "   Row 1: Depth | Row 2: Confidence | Row 3: Amplitude" << std::endl;
        std::cout << std::endl;
        std::cout << "⌨️  Controls:" << std::endl;
        std::cout << "   'q' - Quit" << std::endl;
        std::cout << "   's' - Save current frame (optimized with progress indicators)" << std::endl;
        std::cout << "   'c' - Adjust confidence threshold" << std::endl;
        std::cout << "   'r' - Print analysis report" << std::endl;
        std::cout << "   'x' - Clear all distance measurements" << std::endl;
        std::cout << "   'SPACE' - Pause/Resume" << std::endl;
        std::cout << "   🖱️ Click on depth images to measure distance (independent for each camera)" << std::endl;
        std::cout << "💾 Save System:" << std::endl;
        std::cout << "   📁 Directory: /home/dev/Arducam_tof_camera/cpp_format_testing/saved_data/three_formats/" << std::endl;
        std::cout << "   📊 Saves: Depth, Confidence, Amplitude (2D formats)" << std::endl;
        std::cout << "   🌐 For 3D Point Cloud data, use: './run_pointcloud.sh'" << std::endl;
        std::cout << std::endl;
        
        running_ = true;
        bool paused = false;
        
        // Create display window with better settings
        cv::namedWindow("ToF Format Analysis", cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
        cv::resizeWindow("ToF Format Analysis", 1200, 800);
        cv::setMouseCallback("ToF Format Analysis", onMouseClick, this);
        
        // Set instance pointer for mouse callback
        instance_ = this;
        
        // Frame rate control to prevent flashing
        auto last_frame_time = std::chrono::steady_clock::now();
        const auto target_frame_time = std::chrono::milliseconds(33); // ~30 FPS
        
        while (running_) {
            auto current_time = std::chrono::steady_clock::now();
            
            if (!paused && (current_time - last_frame_time) >= target_frame_time) {
                processFrames();
                last_frame_time = current_time;
            }
            
            // Handle keyboard input with longer wait to reduce CPU usage
            int key = cv::waitKey(10) & 0xFF;
            if (key == 'q' || key == 27) {  // 'q' or ESC
                running_ = false;
            } else if (key == 's') {
                saveCurrentFrame();
            } else if (key == 'c') {
                adjustConfidenceThreshold();
            } else if (key == 'r') {
                printAnalysisReport();
                // Point cloud viewer removed - use ./run_pointcloud.sh instead
            } else if (key == 'x' || key == 'X') {
                // Clear all measurements
                mouse_state_cam0_.active = false;
                mouse_state_cam1_.active = false;
                std::cout << "🧹 Cleared all distance measurements" << std::endl;
            } else if (key == ' ') {  // SPACE
                paused = !paused;
                std::cout << (paused ? "⏸️  Paused" : "▶️  Resumed") << std::endl;
            }
        }
        
        cleanup();
    }

private:
    mutable Config config_;  // Allow modification in const contexts
    FormatAnalyzer analyzer_;
    DataSaver saver_;
    
    std::unique_ptr<CameraManager> cam0_;
    std::unique_ptr<CameraManager> cam1_;
    
    std::atomic<bool> running_;
    int frame_counter_;
    int saved_frames_count_;
    
    // Current frame results for saving
    std::vector<FormatResults> current_results_cam0_;
    std::vector<FormatResults> current_results_cam1_;
    ArducamFrameBuffer* current_frame0_ = nullptr;
    ArducamFrameBuffer* current_frame1_ = nullptr;
    
    // Warning suppression
    int original_stderr_;
    int null_fd_;
    
    void processFrames() {
        // Release previous frames
        if (current_frame0_) {
            cam0_->releaseFrame(current_frame0_);
            current_frame0_ = nullptr;
        }
        if (current_frame1_ && cam1_) {
            cam1_->releaseFrame(current_frame1_);
            current_frame1_ = nullptr;
        }
        
        // Get new frames with reduced warnings
        suppressWarnings();
        current_frame0_ = cam0_->requestFrame(200);
        if (config_.enable_dual_camera && cam1_) {
            current_frame1_ = cam1_->requestFrame(200);
        }
        restoreWarnings();
        
        if (!current_frame0_) {
            std::cout << "⚠️  No frame data from camera 0" << std::endl;
            return;
        }
        
        // Analyze frames (only first 3 formats: Depth, Confidence, Amplitude)
        current_results_cam0_ = analyzer_.analyzeFrameThreeFormats(current_frame0_, 0);
        if (current_frame1_) {
            current_results_cam1_ = analyzer_.analyzeFrameThreeFormats(current_frame1_, 1);
        }
        
        // Debug: Show when frames are processed
        if (frame_counter_ % 30 == 0) {  // Every 30 frames (~1 second at 30fps)
            std::cout << "🔄 Processed frame " << frame_counter_ << " - Results: " << current_results_cam0_.size() << " formats" << std::endl;
        }
        
        // Create display layout
        cv::Mat layout = createDisplayLayout();
        
        // Add text overlays
        addTextOverlays(layout);
        
        // Display
        cv::imshow("ToF Format Analysis", layout);
        
        frame_counter_++;
        
        // Display FPS
        Utils::displayFPS();
    }
    
    cv::Mat createDisplayLayout() {
        int display_width = 240;
        int display_height = 180;
        int layout_cols = config_.enable_dual_camera ? 2 : 2;
        int layout_rows = 3;  // Only 3 formats now: Depth, Confidence, Amplitude
        
        cv::Mat layout = cv::Mat::zeros(layout_rows * display_height, layout_cols * display_width, CV_8UC3);
        
        if (current_results_cam0_.size() >= 3) {
            // Camera 0 column (or left side for single camera)
            for (int i = 0; i < 3; i++) {
                cv::Mat resized;
                cv::resize(current_results_cam0_[i].visualization, resized, cv::Size(display_width, display_height));
                
                cv::Rect roi(0, i * display_height, display_width, display_height);
                resized.copyTo(layout(roi));
                
                // Add format label
                std::string label = current_results_cam0_[i].format_name + " - CAM0";
                cv::Mat roi_mat = layout(roi);
                Utils::addTextOverlay(roi_mat, label, cv::Point(5, 15), cv::Scalar(255, 255, 255));
                
                // Add distance measurement crosshair on depth image (first format)
                if (i == 0) {
                    addDistanceCrosshair(roi_mat, 0);
                }
            }
        }
        
        if (config_.enable_dual_camera && current_results_cam1_.size() >= 3) {
            // Camera 1 column
            for (int i = 0; i < 3; i++) {
                cv::Mat resized;
                cv::resize(current_results_cam1_[i].visualization, resized, cv::Size(display_width, display_height));
                
                cv::Rect roi(display_width, i * display_height, display_width, display_height);
                resized.copyTo(layout(roi));
                
                // Add format label
                std::string label = current_results_cam1_[i].format_name + " - CAM1";
                cv::Mat roi_mat = layout(roi);
                Utils::addTextOverlay(roi_mat, label, cv::Point(5, 15), cv::Scalar(255, 255, 255));
                
                // Add distance measurement crosshair on depth image (first format)
                if (i == 0) {
                    addDistanceCrosshair(roi_mat, 1);
                }
            }
        } else if (!config_.enable_dual_camera && current_results_cam0_.size() >= 3) {
            // Single camera layout (3 formats in a row)
            for (int i = 0; i < 3; i++) {
                int col = i % 2;
                int row = i / 2;
                
                cv::Mat resized;
                cv::resize(current_results_cam0_[i].visualization, resized, cv::Size(display_width, display_height * 2));
                
                cv::Rect roi(col * display_width, row * display_height * 2, display_width, display_height * 2);
                resized.copyTo(layout(roi));
                
                // Add format label
                std::string label = current_results_cam0_[i].format_name;
                cv::Mat roi_mat = layout(roi);
                Utils::addTextOverlay(roi_mat, label, cv::Point(5, 15), cv::Scalar(255, 255, 255));
            }
        }
        
        return layout;
    }
    
    DistanceMeasurement calculatePointDistance(ArducamFrameBuffer* frame, int camera_id, cv::Point point) {
        DistanceMeasurement result = {0.0f, 0.0f, 0.0f, 0.0f, "No Data", false};
        
        if (!frame) return result;
        
        // Get frame format and data
        FrameFormat format;
        frame->getFormat(FrameType::DEPTH_FRAME, format);
        float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
        float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
        
        if (!depth_ptr || !confidence_ptr) return result;
        
        // Sample larger region around clicked point for better reliability
        int region_size = 2;  // 5x5 pixel region for better sampling
        std::vector<float> valid_depths;
        float sum_depth = 0.0f;
        float min_dist = std::numeric_limits<float>::max();
        float max_dist = 0.0f;
        
        // Sample region around clicked point
        for (int y = point.y - region_size; y <= point.y + region_size; y++) {
            for (int x = point.x - region_size; x <= point.x + region_size; x++) {
                if (x >= 0 && x < format.width && y >= 0 && y < format.height) {
                    int idx = y * format.width + x;
                    float depth = depth_ptr[idx];
                    float confidence = confidence_ptr[idx];
                    
                    // More lenient filtering for better measurement success
                    if (confidence > (config_.confidence_threshold * 0.7f) && 
                        depth > 50.0f && depth < config_.max_distance) {
                        valid_depths.push_back(depth);
                        sum_depth += depth;
                        min_dist = std::min(min_dist, depth);
                        max_dist = std::max(max_dist, depth);
                    }
                }
            }
        }
        
        if (!valid_depths.empty()) {
            // Calculate statistics
            result.center_distance = valid_depths[valid_depths.size() / 2];  // Median
            result.min_distance = min_dist;
            result.max_distance = max_dist;
            result.avg_distance = sum_depth / valid_depths.size();
            result.valid = true;
            
            // Create display text with just distance measurement
            std::stringstream ss;
            ss << std::fixed << std::setprecision(0) << result.center_distance << "mm";
            result.display_text = ss.str();
        } else {
            result.display_text = "No Data";
        }
        
        return result;
    }
    
    void addDistanceCrosshair(cv::Mat& image, int camera_id) {
        // Check if measurement is active for this specific camera
        MouseMeasurement* current_state = nullptr;
        if (camera_id == 0 && mouse_state_cam0_.active) {
            current_state = &mouse_state_cam0_;
        } else if (camera_id == 1 && mouse_state_cam1_.active) {
            current_state = &mouse_state_cam1_;
        }
        
        if (!current_state) {
            return;  // No active measurement for this camera
        }
        
        // Draw small crosshair at clicked point
        int crosshair_size = 5;  // Smaller crosshair
        cv::Scalar color = (camera_id == 0) ? cv::Scalar(0, 255, 255) : cv::Scalar(255, 0, 255);  // Yellow for CAM0, Magenta for CAM1
        
        cv::Point click_point = current_state->click_point;
        
        // Draw crosshair lines
        cv::line(image, 
                cv::Point(click_point.x - crosshair_size, click_point.y), 
                cv::Point(click_point.x + crosshair_size, click_point.y), 
                color, 2);
        cv::line(image, 
                cv::Point(click_point.x, click_point.y - crosshair_size), 
                cv::Point(click_point.x, click_point.y + crosshair_size), 
                color, 2);
        
        // Draw center point (small circle)
        cv::circle(image, click_point, 2, color, -1);
    }
    
    void addTextOverlays(cv::Mat& layout) {
        // Add frame counter
        std::string frame_info = "Frame: " + std::to_string(frame_counter_);
        Utils::addTextOverlay(layout, frame_info, cv::Point(10, layout.rows - 50), cv::Scalar(0, 255, 0));
        
        // Add confidence threshold
        std::string conf_info = "Confidence: " + std::to_string(config_.confidence_threshold);
        Utils::addTextOverlay(layout, conf_info, cv::Point(10, layout.rows - 30), cv::Scalar(0, 255, 0));
        
        // Add distance measurements for each camera independently
        bool any_measurement_active = false;
        
        // Camera 0 measurement
        if (mouse_state_cam0_.active && current_frame0_) {
            DistanceMeasurement dist0 = calculatePointDistance(current_frame0_, 0, mouse_state_cam0_.click_point);
            mouse_state_cam0_.last_measurement = dist0;
            std::string dist_info = "CAM0: " + dist0.display_text;
            // Use smaller font size (0.4 instead of default 0.5)
            cv::putText(layout, dist_info, cv::Point(10, layout.rows - 10), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 255), 1);
            any_measurement_active = true;
        }
        
        // Camera 1 measurement
        if (mouse_state_cam1_.active && config_.enable_dual_camera && current_frame1_) {
            DistanceMeasurement dist1 = calculatePointDistance(current_frame1_, 1, mouse_state_cam1_.click_point);
            mouse_state_cam1_.last_measurement = dist1;
            std::string dist1_info = "CAM1: " + dist1.display_text;
            
            // Position CAM1 text to fit properly with smaller font
            int text_x = mouse_state_cam0_.active ? 250 : 10;
            cv::putText(layout, dist1_info, cv::Point(text_x, layout.rows - 10), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 0, 255), 1);
            any_measurement_active = true;
        }
        
        // Show instruction when no measurements are active
        if (!any_measurement_active) {
            std::string instruction = "Click on depth images to measure distance";
            cv::putText(layout, instruction, cv::Point(10, layout.rows - 10), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(128, 128, 128), 1);
        }
    }
    
    void saveCurrentFrame() {
        // Get current timestamp for this exact save moment
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::cout << "\n🔑 Key 's' pressed - Saving current frame..." << std::endl;
        std::cout << "📸 SAVE FRAME REQUEST - Frame #" << frame_counter_ << std::endl;
        
        // Show timestamp when save was triggered
        std::tm* tm_info = std::localtime(&time_t);
        std::cout << "⏰ Save triggered at: " << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S") 
                  << "." << std::setfill('0') << std::setw(3) << ms.count() << std::endl;
        std::cout << "===========================================" << std::endl;
        
        // Check if we have current frame data
        if (!current_results_cam0_.empty() && current_frame0_) {
            // Show what formats are being saved from the CURRENT frame
            std::cout << "📊 Saving formats from current frame:" << std::endl;
            std::cout << "   📷 Camera 0: ";
            for (size_t i = 0; i < current_results_cam0_.size(); ++i) {
                std::cout << current_results_cam0_[i].format_name;
                if (i < current_results_cam0_.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
            
            if (!current_results_cam1_.empty() && current_frame1_) {
                std::cout << "   📷 Camera 1: ";
                for (size_t i = 0; i < current_results_cam1_.size(); ++i) {
                    std::cout << current_results_cam1_[i].format_name;
                    if (i < current_results_cam1_.size() - 1) std::cout << ", ";
                }
                std::cout << std::endl;
            }
            
            std::cout << "📁 Target: /home/dev/Arducam_tof_camera/cpp_format_testing/saved_data/three_formats/" << std::endl;
            std::cout << "\n💾 Starting save process..." << std::endl;
            
            saved_frames_count_++;
            
            // Save the CURRENT frame data only
            bool success = saver_.saveCurrentFrame(current_results_cam0_, current_results_cam1_,
                                                  current_frame0_, current_frame1_, frame_counter_);
            if (success) {
                std::cout << "\n✅ SAVE SUCCESSFUL!" << std::endl;
                std::cout << "   📁 Frame #" << frame_counter_ << " saved successfully" << std::endl;
                std::cout << "   📊 Session total: " << saved_frames_count_ << " frames saved" << std::endl;
            } else {
                std::cout << "\n❌ SAVE FAILED!" << std::endl;
                std::cout << "   ⚠️  Frame #" << frame_counter_ << " could not be saved" << std::endl;
                saved_frames_count_--; // Revert counter on failure
            }
        } else {
            std::cout << "\n⚠️  NO CURRENT FRAME DATA AVAILABLE" << std::endl;
            std::cout << "   📷 No current frame data available to save" << std::endl;
            std::cout << "   💡 Wait for frames to process, then press 's' again" << std::endl;
            std::cout << "   🔍 Debug: current_results_cam0_.size() = " << current_results_cam0_.size() << std::endl;
            std::cout << "   🔍 Debug: current_frame0_ = " << (current_frame0_ ? "valid" : "null") << std::endl;
        }
        std::cout << "===========================================\n" << std::endl;
    }
    
    void adjustConfidenceThreshold() {
        config_.confidence_threshold = (config_.confidence_threshold + 10) % 100;
        std::cout << "🎯 Confidence threshold: " << config_.confidence_threshold << std::endl;
    }
    
    void printAnalysisReport() {
        if (current_results_cam0_.empty()) {
            std::cout << "No analysis data available" << std::endl;
            return;
        }
        
        std::cout << "\n📊 Analysis Report - Frame " << frame_counter_ << std::endl;
        std::cout << "================================" << std::endl;
        
        // Camera 0 report
        std::cout << "Camera 0:" << std::endl;
        for (const auto& result : current_results_cam0_) {
            std::cout << "  " << result.format_name << ":" << std::endl;
            std::cout << "    Processing time: " << std::fixed << std::setprecision(2) 
                     << result.processing_time_ms << " ms" << std::endl;
            std::cout << "    Valid pixels: " << result.valid_pixels << std::endl;
            std::cout << "    Coverage: " << std::fixed << std::setprecision(1) 
                     << result.coverage_percentage << "%" << std::endl;
            std::cout << "    Range: [" << std::fixed << std::setprecision(1) 
                     << result.min_value << ", " << result.max_value << "]" << std::endl;
        }
        
        // Camera 1 report
        if (!current_results_cam1_.empty()) {
            std::cout << "Camera 1:" << std::endl;
            for (const auto& result : current_results_cam1_) {
                std::cout << "  " << result.format_name << ":" << std::endl;
                std::cout << "    Processing time: " << std::fixed << std::setprecision(2) 
                         << result.processing_time_ms << " ms" << std::endl;
                std::cout << "    Valid pixels: " << result.valid_pixels << std::endl;
                std::cout << "    Coverage: " << std::fixed << std::setprecision(1) 
                         << result.coverage_percentage << "%" << std::endl;
                std::cout << "    Range: [" << std::fixed << std::setprecision(1) 
                         << result.min_value << ", " << result.max_value << "]" << std::endl;
            }
        }
        
        std::cout << "================================\n" << std::endl;
    }
    
    
    void cleanup() {
        std::cout << "\n🧹 Cleaning up..." << std::endl;
        
        // Properly destroy OpenCV windows first
        cv::destroyAllWindows();
        cv::waitKey(1); // Give time for window destruction
        
        // Release current frames
        if (current_frame0_) {
            cam0_->releaseFrame(current_frame0_);
            current_frame0_ = nullptr;
        }
        if (current_frame1_ && cam1_) {
            cam1_->releaseFrame(current_frame1_);
            current_frame1_ = nullptr;
        }
        
        // Stop cameras
        if (cam0_) {
            cam0_->stop();
            std::cout << "✅ Camera 0 closed" << std::endl;
        }
        if (cam1_) {
            cam1_->stop();
            std::cout << "✅ Camera 1 closed" << std::endl;
        }
        
        cv::destroyAllWindows();
        
        if (config_.save_enabled) {
            std::cout << "📁 Three formats data: /home/dev/Arducam_tof_camera/cpp_format_testing/saved_data/three_formats/" << std::endl;
            std::cout << "📁 Point cloud data: /home/dev/Arducam_tof_camera/cpp_format_testing/saved_data/pointcloud/" << std::endl;
        }
        
        std::cout << "✅ Cleanup complete!" << std::endl;
    }
};

// Static member definitions
DualFormatTester::MouseMeasurement DualFormatTester::mouse_state_cam0_;
DualFormatTester::MouseMeasurement DualFormatTester::mouse_state_cam1_;
DualFormatTester* DualFormatTester::instance_ = nullptr;

// Mouse callback function
void onMouseClick(int event, int x, int y, int flags, void* userdata) {
    if (event == cv::EVENT_LBUTTONDOWN) {
        DualFormatTester* tester = static_cast<DualFormatTester*>(userdata);
        
        // Determine which camera region was clicked
        int display_width = 240;
        int display_height = 180;
        
        // Calculate which format and camera was clicked
        int format_idx = y / display_height;  // 0=Depth, 1=Confidence, 2=Amplitude
        int camera_idx = x / display_width;   // 0=Camera0, 1=Camera1
        
        // Only measure distance on depth images (format_idx == 0)
        if (format_idx == 0) {
            // Convert screen coordinates to image coordinates
            cv::Point image_point;
            image_point.x = x % display_width;
            image_point.y = y % display_height;
            
            // Scale to original image size (240x180 display -> 240x180 original)
            // No scaling needed since display size matches image size
            
            // Set measurement for the specific camera
            if (camera_idx == 0) {
                DualFormatTester::mouse_state_cam0_.active = true;
                DualFormatTester::mouse_state_cam0_.click_point = image_point;
                std::cout << "🎯 CAM0: Measuring distance at (" << image_point.x << ", " << image_point.y << ")" << std::endl;
            } else if (camera_idx == 1) {
                DualFormatTester::mouse_state_cam1_.active = true;
                DualFormatTester::mouse_state_cam1_.click_point = image_point;
                std::cout << "🎯 CAM1: Measuring distance at (" << image_point.x << ", " << image_point.y << ")" << std::endl;
            }
        }
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -s, --single-camera    Use single camera mode" << std::endl;
    std::cout << "  -n, --no-save          Disable data saving" << std::endl;
    std::cout << "  -v, --verbose          Enable verbose output" << std::endl;
    std::cout << "  -c, --confidence NUM   Initial confidence threshold (default: 30)" << std::endl;
    std::cout << "  -d, --max-distance NUM Maximum distance in mm (default: 4000)" << std::endl;
    std::cout << "  -o, --output DIR       Output directory (default: format_test_results)" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    Config config;
    
    // Parse command line arguments
    static struct option long_options[] = {
        {"single-camera", no_argument, 0, 's'},
        {"no-save", no_argument, 0, 'n'},
        {"verbose", no_argument, 0, 'v'},
        {"confidence", required_argument, 0, 'c'},
        {"max-distance", required_argument, 0, 'd'},
        {"output", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "snvc:d:o:h", long_options, nullptr)) != -1) {
        switch (c) {
            case 's':
                config.enable_dual_camera = false;
                break;
            case 'n':
                config.save_enabled = false;
                break;
            case 'v':
                config.verbose = true;
                break;
            case 'c':
                config.confidence_threshold = std::atoi(optarg);
                break;
            case 'd':
                config.max_distance = std::atoi(optarg);
                break;
            case 'o':
                config.output_dir = optarg;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            case '?':
                printUsage(argv[0]);
                return 1;
            default:
                break;
        }
    }
    
    // Print configuration
    std::cout << "=" << std::string(50, '=') << std::endl;
    std::cout << "🔬 ToF Camera Format Testing Tool (C++)" << std::endl;
    std::cout << "  📷 Depth Format" << std::endl;
    std::cout << "  🎯 Confidence Format" << std::endl;
    std::cout << "  📶 Amplitude Format" << std::endl;
    std::cout << "=" << std::string(50, '=') << std::endl;
    
    DualFormatTester tester(config);
    
    if (!tester.initialize()) {
        std::cerr << "Failed to initialize format tester" << std::endl;
        return 1;
    }
    
    tester.run();
    
    return 0;
}
