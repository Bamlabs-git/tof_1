#include <opencv2/opencv.hpp>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <cmath>
#include "simple_virtual_wall_utils.h"
#include "common.h"
#include "warning_suppression.h"

/**
 * SIMPLIFIED VIRTUAL WALL SETUP TOOL (V2)
 * 
 * This tool implements a practical calibration workflow with sophisticated UI:
 * 1. User drags 4 corner points to mark the virtual wall boundary
 * 2. User enters REAL dimensions measured with tape measure (width, height)
 * 3. System reads depth at corners and calculates average wall depth
 * 4. Configuration is saved and ready for monitoring
 * 
 * Features:
 * - Combined layout with sidebar showing corner information
 * - Synchronized confidence/amplitude view
 * - Real-time depth validation with visual feedback
 * - Adjustable parameters via trackbars
 * - Robust depth averaging and outlier rejection
 */

class SimpleVirtualWallSetup {
private:
    std::unique_ptr<CameraManager> camera_manager_;
    SimpleVirtualWallConfig config_;
    cv::Mat depth_data_;
    cv::Mat confidence_data_;
    cv::Mat display_frame_;
    cv::Mat confidence_display_frame_;
    
    // Calibration state
    SimpleCalibrationData calib_data_;
    bool is_dragging_;
    cv::Point2f drag_offset_;
    bool is_running_;
    
    // Display parameters
    int confidence_threshold_;
    int max_distance_;
    bool show_center_marker_;
    
    // Warning suppression
    WarningSuppressionManager warning_manager_;
    
    // Static instance for mouse callback
    static SimpleVirtualWallSetup* instance_;
    
public:
    SimpleVirtualWallSetup() : 
        is_dragging_(false),
        is_running_(false),
        confidence_threshold_(15),  // Lower threshold for steep angles (lower shelf)
        max_distance_(3500),
        show_center_marker_(false) {
        instance_ = this;
    }
    
    ~SimpleVirtualWallSetup() {
        if (camera_manager_) {
            camera_manager_->stop();
        }
        instance_ = nullptr;
    }
    
    bool initialize() {
        std::cout << "🚀 Initializing Simplified Virtual Wall Setup..." << std::endl;
        
        // Suppress warnings during camera initialization
        warning_manager_.suppressWarnings();
        
        camera_manager_ = std::make_unique<CameraManager>(0);
        bool init_success = camera_manager_->initialize();
        
        warning_manager_.restoreWarnings();
        
        if (!init_success) {
            std::cerr << "Failed to initialize camera!" << std::endl;
            return false;
        }
        
        if (!camera_manager_->start()) {
            std::cerr << "Failed to start camera!" << std::endl;
            return false;
        }
        
        std::cout << "✅ Camera initialized successfully" << std::endl;
        return true;
    }
    
    void run() {
        if (!initialize()) {
            return;
        }
        
        is_running_ = true;
        
        // Create single window for combined view with resizable option
        cv::namedWindow("Simplified Virtual Wall Setup", cv::WINDOW_NORMAL);
        cv::resizeWindow("Simplified Virtual Wall Setup", 1400, 1000);
        
        // Set mouse callback
        cv::setMouseCallback("Simplified Virtual Wall Setup", onMouse, this);
        
        std::cout << "\n🎯 Interactive Virtual Wall Calibration:" << std::endl;
        std::cout << "1. Drag the corner dots to match the shelf boundaries" << std::endl;
        std::cout << "   (For downward-facing camera: corners mark the shelf area)" << std::endl;
        std::cout << "2. Use the live depth feed to position corners precisely" << std::endl;
        std::cout << "3. Press 'S' to proceed and enter real dimensions" << std::endl;
        std::cout << "\nControls:" << std::endl;
        std::cout << "- Drag: Move corner dots" << std::endl;
        std::cout << "- 's': Proceed to enter dimensions (width: 95cm, depth: 190cm)" << std::endl;
        std::cout << "- 'r': Reset corner positions" << std::endl;
        std::cout << "- 'q': Quit" << std::endl;
        
        while (is_running_) {
            // Capture frame
            if (!captureFrame()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                continue;
            }
            
            // Process and display frame
            processFrame();
            displayFrame();
            
            // Handle keyboard input
            int key = cv::waitKey(1) & 0xFF;
            handleKeyboard(key);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        
        // Cleanup
        cv::destroyWindow("Simplified Virtual Wall Setup");
    }

private:
    void resetCalibration() {
        // Reset interactive calibration data
        calib_data_ = SimpleCalibrationData();
        
        // Reset config
        config_.is_valid = false;
        is_dragging_ = false;
        
        std::cout << "\n🔄 Calibration reset. Drag corners to match shelf edges." << std::endl;
    }
    
    bool captureFrame() {
        auto frames = camera_manager_->getLatestFrames();
        if (frames.empty()) {
            return false;
        }
        
        depth_data_ = frames[0].clone();
        
        // Get confidence data if available
        if (frames.size() > 1) {
            confidence_data_ = frames[1].clone();
        }
        
        return !depth_data_.empty();
    }
    
    void processFrame() {
        if (!depth_data_.empty()) {
            // Convert depth to 8-bit with adjustable max distance
            cv::Mat depth_8bit;
            depth_data_.convertTo(depth_8bit, CV_8U, 255.0 / max_distance_, 0);
            
            // Apply rainbow colormap
            cv::applyColorMap(depth_8bit, display_frame_, cv::COLORMAP_RAINBOW);
            
            // Apply confidence filter with adjustable threshold
            if (!confidence_data_.empty()) {
                display_frame_.setTo(cv::Scalar(0, 0, 0), confidence_data_ < confidence_threshold_);
            }
        }
        
        // Process confidence/amplitude data for grayscale display
        if (!confidence_data_.empty()) {
            // Convert confidence to 8-bit for visualization
            cv::Mat confidence_8bit;
            confidence_data_.convertTo(confidence_8bit, CV_8U, 255.0 / 1024.0, 0);
            
            // Convert to 3-channel grayscale
            cv::cvtColor(confidence_8bit, confidence_display_frame_, cv::COLOR_GRAY2BGR);
        }
    }
    
    void displayFrame() {
        if (display_frame_.empty()) {
            return;
        }
        
        cv::Mat display = display_frame_.clone();
        
        // ✨ Draw cluster grid (shows spatial filtering regions for monitoring)
        if (config_.is_valid) {
            SimpleVirtualWallUtils::drawClusterGridWithWall(display, CLUSTER_SIZE, config_);
        } else {
            SimpleVirtualWallUtils::drawClusterGrid(display, CLUSTER_SIZE, cv::Scalar(80, 80, 80));
        }
        
        // Draw interactive overlay with draggable corners using L-shaped edge markers
        for (const auto& corner : calib_data_.corners) {
            SimpleVirtualWallUtils::drawCornerWithSquareEdge(display, corner, EDGE_CLUSTER_SIZE);
        }
        
        // Draw connecting lines between corners
        if (calib_data_.corners.size() == 4) {
            cv::Scalar line_color = calib_data_.corners_positioned ? 
                                   cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255);
            
            cv::line(display, calib_data_.corners[0].position, calib_data_.corners[1].position, line_color, 1);
            cv::line(display, calib_data_.corners[1].position, calib_data_.corners[3].position, line_color, 1);
            cv::line(display, calib_data_.corners[3].position, calib_data_.corners[2].position, line_color, 1);
            cv::line(display, calib_data_.corners[2].position, calib_data_.corners[0].position, line_color, 1);
        }
        
        // Draw virtual wall if calibration is complete
        if (calib_data_.calibration_complete && config_.is_valid) {
            SimpleVirtualWallUtils::drawVirtualWall(display, config_);
        }
        
        // Draw center marker if requested
        if (show_center_marker_) {
            drawCenterMarker(display);
        }
        
        // Create combined layout with sidebar
        cv::Mat combined_display = createCombinedLayout(display);
        
        // Show combined frame
        cv::imshow("Simplified Virtual Wall Setup", combined_display);
    }
    
    cv::Mat createCombinedLayout(const cv::Mat& camera_frame) {
        // Define dimensions
        int sidebar_width = 300;
        int camera_height = camera_frame.rows;
        int camera_width = camera_frame.cols;
        
        // Calculate confidence view dimensions maintaining aspect ratio
        int confidence_height = camera_height;
        if (!confidence_display_frame_.empty()) {
            double aspect_ratio = (double)confidence_display_frame_.rows / (double)confidence_display_frame_.cols;
            confidence_height = (int)(camera_width * aspect_ratio);
        }
        
        // Total layout dimensions
        int combined_width = camera_width + sidebar_width;
        int combined_height = camera_height + confidence_height;
        
        // Create combined image
        cv::Mat combined = cv::Mat::zeros(combined_height, combined_width, CV_8UC3);
        
        // Copy camera frame to top-left
        cv::Rect camera_roi(0, 0, camera_width, camera_height);
        camera_frame.copyTo(combined(camera_roi));
        
        // Add confidence/amplitude view below camera frame
        if (!confidence_display_frame_.empty()) {
            cv::Mat confidence_resized;
            cv::resize(confidence_display_frame_, confidence_resized, cv::Size(camera_width, confidence_height));
            
            // Draw synchronized wall overlay on confidence view
            drawSynchronizedWallOverlay(confidence_resized);
            
            cv::Rect confidence_roi(0, camera_height, camera_width, confidence_height);
            confidence_resized.copyTo(combined(confidence_roi));
        }
        
        // Create sidebar on the right side
        cv::Rect sidebar_roi(camera_width, 0, sidebar_width, combined_height);
        cv::Mat sidebar = combined(sidebar_roi);
        sidebar.setTo(cv::Scalar(40, 40, 40)); // Dark gray background
        
        // Draw sidebar content
        drawSidebarContent(sidebar);
        
        // Add trackbars for parameter adjustment
        addParameterControls(combined, camera_width);
        
        return combined;
    }
    
    void drawSynchronizedWallOverlay(cv::Mat& image) {
        // Draw only corner dots and lines without any text overlays
        if (calib_data_.corners.size() == 4) {
            cv::Scalar rect_color = calib_data_.calibration_complete ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255);
            
            // Draw rectangle edges
            cv::line(image, calib_data_.corners[0].position, calib_data_.corners[1].position, rect_color, 1); // Top
            cv::line(image, calib_data_.corners[1].position, calib_data_.corners[3].position, rect_color, 1); // Right
            cv::line(image, calib_data_.corners[3].position, calib_data_.corners[2].position, rect_color, 1); // Bottom
            cv::line(image, calib_data_.corners[2].position, calib_data_.corners[0].position, rect_color, 1); // Left
            
            // Draw sniper-style reticle markers
            for (const auto& corner : calib_data_.corners) {
                cv::Scalar color = cv::Scalar(255, 0, 255); // Magenta
                int reticle_radius = 4;
                
                // Draw outer circle (outline only)
                cv::circle(image, corner.position, reticle_radius, color, 1);
                
                // Draw cross lines
                cv::line(image, cv::Point2i(corner.position.x, corner.position.y - reticle_radius), 
                         cv::Point2i(corner.position.x, corner.position.y - 2), color, 1);
                cv::line(image, cv::Point2i(corner.position.x, corner.position.y + 2), 
                         cv::Point2i(corner.position.x, corner.position.y + reticle_radius), color, 1);
                cv::line(image, cv::Point2i(corner.position.x - reticle_radius, corner.position.y), 
                         cv::Point2i(corner.position.x - 2, corner.position.y), color, 1);
                cv::line(image, cv::Point2i(corner.position.x + 2, corner.position.y), 
                         cv::Point2i(corner.position.x + reticle_radius, corner.position.y), color, 1);
                
                // Draw center dot
                cv::circle(image, corner.position, 1, color, -1);
            }
        }
        
        // Draw virtual wall if corners are positioned (show green fill after pressing 'S')
        if (calib_data_.corners_positioned || calib_data_.calibration_complete) {
            // Check if we have corner positions to draw
            bool has_valid_corners = true;
            for (const auto& corner : calib_data_.corners) {
                if (!corner.is_set) {
                    has_valid_corners = false;
                    break;
                }
            }
            
            if (has_valid_corners) {
                cv::Point upper_left = calib_data_.corners[0].position;   // UL
                cv::Point upper_right = calib_data_.corners[1].position;  // UR
                cv::Point lower_left = calib_data_.corners[2].position;   // LL
                cv::Point lower_right = calib_data_.corners[3].position;  // LR
                
                // Fill the wall area with semi-transparent green (like a real wall)
                std::vector<cv::Point> wall_points = {upper_left, upper_right, lower_right, lower_left};
                cv::Mat overlay = image.clone();
                cv::fillPoly(overlay, wall_points, cv::Scalar(0, 255, 0)); // Green fill
                cv::addWeighted(image, 0.7, overlay, 0.3, 0, image); // 30% green opacity
                
                // Draw green border around the wall
                cv::line(image, upper_left, upper_right, cv::Scalar(0, 255, 0), 2);
                cv::line(image, upper_right, lower_right, cv::Scalar(0, 255, 0), 2);
                cv::line(image, lower_right, lower_left, cv::Scalar(0, 255, 0), 2);
                cv::line(image, lower_left, upper_left, cv::Scalar(0, 255, 0), 2);
            }
        }
    }
    
    void drawSidebarContent(cv::Mat& sidebar) {
        // Clear background with dark gray
        sidebar.setTo(cv::Scalar(45, 45, 45));
        
        // Main title
        cv::putText(sidebar, "CORNER INFORMATION", cv::Point(20, 30), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        
        // Main separator line
        cv::line(sidebar, cv::Point(20, 40), cv::Point(280, 40), cv::Scalar(120, 120, 120), 2);
        
        // Corner labels and colors (camera mounted upside down)
        std::vector<std::string> labels = {"LL", "LR", "UL", "UR"};
        std::vector<std::string> full_names = {"Lower Left", "Lower Right", "Upper Left", "Upper Right"};
        std::vector<cv::Scalar> colors = {
            cv::Scalar(255, 0, 255), cv::Scalar(255, 0, 255),
            cv::Scalar(255, 0, 255), cv::Scalar(255, 0, 255)
        };
        
        // Display corner information
        int corner_section_start = 60;
        for (size_t i = 0; i < calib_data_.corners.size() && i < 4; ++i) {
            const auto& corner = calib_data_.corners[i];
            int y_base = corner_section_start + i * 75;
            
            // Determine color and status
            cv::Scalar color = colors[i];
            std::string status = "Normal";
            cv::Scalar status_color = cv::Scalar(100, 255, 100);
            
            if (corner.has_error) {
                color = cv::Scalar(0, 0, 255);
                status = "ERROR";
                status_color = cv::Scalar(0, 0, 255);
            } else if (corner.is_dragging) {
                color = cv::Scalar(0, 255, 0);
                status = "DRAGGING";
                status_color = cv::Scalar(0, 255, 0);
            } else if (corner.is_hovered) {
                color = cv::Scalar(0, 255, 255);
                status = "HOVERED";
                status_color = cv::Scalar(0, 255, 255);
            } else if (corner.depth > 0) {
                color = cv::Scalar(0, 255, 0);
                status = "VALID";
                status_color = cv::Scalar(0, 255, 0);
            } else {
                color = cv::Scalar(255, 0, 255);
                status = "NO DATA";
                status_color = cv::Scalar(255, 0, 255);
            }
            
            // Corner indicator
            cv::circle(sidebar, cv::Point(35, y_base), 6, color, -1);
            
            // Corner label
            cv::putText(sidebar, labels[i], cv::Point(55, y_base + 5), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
            cv::putText(sidebar, full_names[i], cv::Point(85, y_base + 5), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(180, 180, 180), 1);
            
            // Position
            std::stringstream pos_str;
            pos_str << "(" << corner.position.x << ", " << corner.position.y << ")";
            cv::putText(sidebar, pos_str.str(), cv::Point(55, y_base + 22), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(220, 220, 220), 1);
            
            // Depth and status
            std::string depth_status_str;
            if (corner.depth > 0) {
                std::stringstream depth_str;
                depth_str << std::fixed << std::setprecision(0) << corner.depth << "mm";
                depth_status_str = depth_str.str() + " | " + status;
            } else {
                depth_status_str = "No Data | " + status;
            }
            cv::putText(sidebar, depth_status_str, cv::Point(55, y_base + 38), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, status_color, 1);
            
            // Error message
            if (corner.has_error && !corner.error_message.empty()) {
                std::string error_msg = corner.error_message;
                if (error_msg.length() > 30) {
                    error_msg = error_msg.substr(0, 27) + "...";
                }
                cv::putText(sidebar, error_msg, cv::Point(55, y_base + 54), 
                           cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 255), 1);
            }
            
            // Separator
            if (i < 3) {
                int separator_y = corner.has_error && !corner.error_message.empty() ? y_base + 65 : y_base + 50;
                cv::line(sidebar, cv::Point(25, separator_y), cv::Point(275, separator_y), 
                        cv::Scalar(80, 80, 80), 1);
            }
        }
        
        // Parameters section
        int param_y_start = 380;
        cv::line(sidebar, cv::Point(20, param_y_start), cv::Point(280, param_y_start), 
                cv::Scalar(120, 120, 120), 2);
        
        cv::putText(sidebar, "SYSTEM PARAMETERS", cv::Point(20, param_y_start + 25), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 2);
        
        cv::putText(sidebar, "Confidence:", cv::Point(30, param_y_start + 50), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
        std::stringstream conf_str;
        conf_str << confidence_threshold_ << "%";
        cv::putText(sidebar, conf_str.str(), cv::Point(120, param_y_start + 50), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(100, 255, 100), 2);
        
        cv::putText(sidebar, "Max Distance:", cv::Point(30, param_y_start + 70), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
        std::stringstream dist_str;
        dist_str << max_distance_ << "mm";
        cv::putText(sidebar, dist_str.str(), cv::Point(140, param_y_start + 70), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(100, 255, 100), 2);
        
        // Controls
        cv::line(sidebar, cv::Point(20, param_y_start + 165), cv::Point(280, param_y_start + 165), 
                cv::Scalar(80, 80, 80), 1);
        cv::putText(sidebar, "CONTROLS", cv::Point(20, param_y_start + 185), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 2);
        cv::putText(sidebar, "Drag corners to adjust", cv::Point(30, param_y_start + 205), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
        cv::putText(sidebar, "Press 'S' to save config", cv::Point(30, param_y_start + 220), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
        cv::putText(sidebar, "Press 'R' to reset", cv::Point(30, param_y_start + 235), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(200, 200, 200), 1);
    }
    
    void addParameterControls(cv::Mat& combined, int camera_width) {
        static bool trackbars_created = false;
        if (!trackbars_created) {
            cv::createTrackbar("Confidence", "Simplified Virtual Wall Setup", &confidence_threshold_, 100, onConfidenceChange, this);
            cv::createTrackbar("Max Distance", "Simplified Virtual Wall Setup", &max_distance_, 5000, onMaxDistanceChange, this);
            trackbars_created = true;
        }
    }
    
    static void onConfidenceChange(int value, void* userdata) {
        SimpleVirtualWallSetup* setup = static_cast<SimpleVirtualWallSetup*>(userdata);
        setup->confidence_threshold_ = value;
        setup->processFrame();
    }
    
    static void onMaxDistanceChange(int value, void* userdata) {
        SimpleVirtualWallSetup* setup = static_cast<SimpleVirtualWallSetup*>(userdata);
        setup->max_distance_ = std::max(500, value);
        setup->processFrame();
    }
    
    void drawCenterMarker(cv::Mat& image) {
        cv::Point center(image.cols / 2, image.rows / 2);
        cv::Scalar marker_color(255, 255, 0); // Cyan
        
        int cross_size = 4;
        int thickness = 1;
        
        cv::line(image, cv::Point(center.x - cross_size, center.y), 
                cv::Point(center.x + cross_size, center.y), marker_color, thickness);
        cv::line(image, cv::Point(center.x, center.y - cross_size), 
                cv::Point(center.x, center.y + cross_size), marker_color, thickness);
        cv::circle(image, center, 1, marker_color, -1);
        cv::circle(image, center, 2, cv::Scalar(255, 255, 255), 1);
    }
    
    void handleKeyboard(int key) {
        switch (key) {
            case 'q':
            case 'Q':
            case 27: // ESC
                is_running_ = false;
                std::cout << "\n👋 Exiting setup..." << std::endl;
                break;
                
            case 'r':
            case 'R':
                resetCalibration();
                break;
                
            case '+':
            case '=':
                confidence_threshold_ = std::min(100, confidence_threshold_ + 5);
                std::cout << "🎯 Confidence threshold: " << confidence_threshold_ << " (+)" << std::endl;
                break;
                
            case '-':
            case '_':
                confidence_threshold_ = std::max(0, confidence_threshold_ - 5);
                std::cout << "🎯 Confidence threshold: " << confidence_threshold_ << " (-)" << std::endl;
                break;
                
            case '>':
            case '.':
                max_distance_ = std::min(5000, max_distance_ + 250);
                std::cout << "📏 Max distance: " << max_distance_ << "mm (>)" << std::endl;
                break;
                
            case '<':
            case ',':
                max_distance_ = std::max(500, max_distance_ - 250);
                std::cout << "📏 Max distance: " << max_distance_ << "mm (<)" << std::endl;
                break;
                
            case 's':
            case 'S':
                if (calib_data_.corners_positioned) {
                    show_center_marker_ = true;
                    captureAndGenerateWall();
                } else {
                    std::cout << "\n⚠️ Please position all four corners first." << std::endl;
                }
                break;
        }
    }
    
    void captureAndGenerateWall() {
        std::cout << "\n🎯 Capturing depth and generating virtual wall..." << std::endl;
        
        // Capture precise depth from positioned corners
        for (int i = 0; i < 4; i++) {
            const auto& corner = calib_data_.corners[i];
            
            std::string corner_name;
            switch (i) {
                case 0: corner_name = "Upper-Left"; break;
                case 1: corner_name = "Upper-Right"; break;
                case 2: corner_name = "Lower-Left"; break;
                case 3: corner_name = "Lower-Right"; break;
            }
            
            std::cout << "\n📍 Processing " << corner_name << " corner at pixel (" 
                     << corner.position.x << ", " << corner.position.y << ")" << std::endl;
            
            float depth_mm = getDepthAtPosition(corner.position, corner.corner_id, corner_name);
            
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
        }
        
        // Mark calibration as complete
        calib_data_.calibration_complete = true;
        
        std::cout << "\n📋 Corner depths captured:" << std::endl;
        std::cout << "   UL: " << config_.upper_left_depth_mm << " mm" << std::endl;
        std::cout << "   UR: " << config_.upper_right_depth_mm << " mm" << std::endl;
        std::cout << "   LL: " << config_.lower_left_depth_mm << " mm" << std::endl;
        std::cout << "   LR: " << config_.lower_right_depth_mm << " mm" << std::endl;
        
        // Now prompt for dimensions
        promptDimensionsAndSave();
    }
    
    void promptDimensionsAndSave() {
        std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
        std::cout << "║   ENTER REAL DIMENSIONS                            ║" << std::endl;
        std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
        std::cout << "\nNOTE: For downward-facing camera setup:" << std::endl;
        std::cout << "  - WIDTH = Shelf width (left to right)" << std::endl;
        std::cout << "  - HEIGHT = Shelf depth (back to front / monitoring distance)" << std::endl;
        std::cout << "══════════════════════════════════════════════════════\n" << std::endl;
        
        // Get width
        float width_cm;
        while (true) {
            std::cout << "Enter shelf WIDTH in cm (e.g., 95): ";
            std::string input;
            std::getline(std::cin, input);
            
            try {
                width_cm = std::stof(input);
                if (width_cm > 0 && width_cm < 500) {
                    break;
                }
                std::cout << "❌ Invalid width. Please enter a value between 1 and 500 cm." << std::endl;
            } catch (...) {
                std::cout << "❌ Invalid input. Please enter a number." << std::endl;
            }
        }
        
        // Get height (actually depth for downward-facing setup)
        float height_cm;
        while (true) {
            std::cout << "Enter shelf DEPTH in cm (e.g., 190): ";
            std::string input;
            std::getline(std::cin, input);
            
            try {
                height_cm = std::stof(input);
                if (height_cm > 0 && height_cm < 500) {
                    break;
                }
                std::cout << "❌ Invalid depth. Please enter a value between 1 and 500 cm." << std::endl;
            } catch (...) {
                std::cout << "❌ Invalid input. Please enter a number." << std::endl;
            }
        }
        
        std::cout << "\n✅ Dimensions entered:" << std::endl;
        std::cout << "   Shelf Width: " << width_cm << " cm" << std::endl;
        std::cout << "   Shelf Depth: " << height_cm << " cm" << std::endl;
        
        // Build configuration
        buildConfiguration(width_cm, height_cm);
        
        // Save configuration
        saveConfiguration();
    }
    
    void buildConfiguration(float width_cm, float height_cm) {
        std::cout << "\n🔧 Building simplified configuration..." << std::endl;
        
        // User-provided dimensions
        config_.actual_width_cm = width_cm;
        config_.actual_height_cm = height_cm;
        
        // Calculate average wall depth from 4 corners
        config_.wall_depth_mm = (config_.upper_left_depth_mm + 
                                config_.upper_right_depth_mm +
                                config_.lower_left_depth_mm + 
                                config_.lower_right_depth_mm) / 4.0f;
        
        std::cout << "📏 Average wall depth (Z-plane): " << config_.wall_depth_mm << " mm" << std::endl;
        
        // Set default penetration threshold
        config_.penetration_threshold_mm = 200.0f;  // 20cm default (tolerance for noisy sensor)
        
        // Set metadata
        config_.unique_id = SimpleVirtualWallUtils::generateEventId();
        config_.creation_timestamp = SimpleVirtualWallUtils::getCurrentTimestamp();
        config_.is_valid = true;
        
        std::cout << "✅ Configuration built successfully!" << std::endl;
        std::cout << "\n📊 Final Configuration:" << std::endl;
        std::cout << "   Shelf Width:  " << config_.actual_width_cm << " cm" << std::endl;
        std::cout << "   Shelf Depth:  " << config_.actual_height_cm << " cm" << std::endl;
        std::cout << "   Wall Z-plane: " << std::fixed << std::setprecision(1) << config_.wall_depth_mm << " mm" << std::endl;
        std::cout << "   Penetration threshold: " << config_.penetration_threshold_mm << " mm" << std::endl;
    }
    
    // Helper functions for depth validation and analysis
    std::vector<float> getSurroundingDepths(const cv::Point2i& position, int radius) {
        std::vector<float> depths;
        
        if (depth_data_.empty() || radius <= 0) {
            return depths;
        }
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                int x = position.x + dx;
                int y = position.y + dy;
                
                if (x >= 0 && x < depth_data_.cols && y >= 0 && y < depth_data_.rows) {
                    float depth = depth_data_.at<float>(y, x);
                    
                    if (depth > 0.0f && !std::isnan(depth) && !std::isinf(depth) && 
                        depth >= 100.0f && depth <= 5000.0f) {
                        depths.push_back(depth);
                    }
                }
            }
        }
        
        return depths;
    }
    
    float getRobustAveragedDepth(const cv::Point2i& position, int radius) {
        std::vector<float> depths = getSurroundingDepths(position, radius);
        
        if (depths.empty()) {
            return 0.0f;
        }
        
        if (depths.size() == 1) {
            return depths[0];
        }
        
        // Sort depths for robust statistics
        std::sort(depths.begin(), depths.end());
        
        // Remove outliers using IQR method
        size_t n = depths.size();
        if (n >= 4) {
            size_t q1_idx = n / 4;
            size_t q3_idx = 3 * n / 4;
            float q1 = depths[q1_idx];
            float q3 = depths[q3_idx];
            float iqr = q3 - q1;
            float lower_bound = q1 - 1.5f * iqr;
            float upper_bound = q3 + 1.5f * iqr;
            
            std::vector<float> filtered_depths;
            for (float depth : depths) {
                if (depth >= lower_bound && depth <= upper_bound) {
                    filtered_depths.push_back(depth);
                }
            }
            
            if (!filtered_depths.empty()) {
                depths = filtered_depths;
            }
        }
        
        // Calculate mean
        float sum = std::accumulate(depths.begin(), depths.end(), 0.0f);
        return sum / depths.size();
    }
    
    float getDepthAtPosition(const cv::Point2i& position, int corner_id, const std::string& corner_name = "") {
        if (depth_data_.empty() || position.x < 0 || position.y < 0 || 
            position.x >= depth_data_.cols || position.y >= depth_data_.rows) {
            std::cout << "❌ [" << corner_name << "] Invalid position or empty depth data" << std::endl;
            return 0.0f;
        }
        
        // ✨ Use small 3x3 edge cluster for precise edge measurement!
        std::cout << "🔍 [" << corner_name << "] Computing edge median (3x3) at (" << position.x << "," << position.y << ")" << std::endl;
        
        float median_depth = SimpleVirtualWallUtils::getClusterMedianAtPosition(
            depth_data_, confidence_data_, position, EDGE_CLUSTER_SIZE, confidence_threshold_, corner_id);
        
        if (median_depth <= 0 || median_depth < 200.0f || median_depth > 4000.0f) {
            std::cout << "❌ [" << corner_name << "] Invalid edge median: " << median_depth << "mm" << std::endl;
            return 0.0f;
        }
        
        std::cout << "✅ [" << corner_name << "] Edge median depth (3x3): " << median_depth << "mm" << std::endl;
        return median_depth;
    }
    
    void saveConfiguration() {
        std::string config_path = SimpleVirtualWallUtils::getDefaultConfigPath();
        
        std::cout << "\n💾 Saving configuration..." << std::endl;
        
        if (SimpleVirtualWallUtils::saveConfig(config_, config_path)) {
            std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║   SETUP COMPLETE!                                  ║" << std::endl;
            std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
            std::cout << "\n✅ Configuration saved to: " << config_path << std::endl;
            std::cout << "\n🎯 You can now run the monitor tool:" << std::endl;
            std::cout << "   cd cpp_format_testing/virtual_wall_2/build" << std::endl;
            std::cout << "   ./simple_virtual_wall_monitor" << std::endl;
            std::cout << "══════════════════════════════════════════════════════\n" << std::endl;
            
            is_running_ = false;
        } else {
            std::cerr << "\n❌ Failed to save configuration!" << std::endl;
        }
    }
    
    // Mouse handling
    static void onMouse(int event, int x, int y, int flags, void* userdata) {
        SimpleVirtualWallSetup* setup = static_cast<SimpleVirtualWallSetup*>(userdata);
        setup->handleMouseEvent(event, x, y, flags);
    }
    
    void handleMouseEvent(int event, int x, int y, int flags) {
        cv::Point2f mouse_pos(x, y);
        calib_data_.last_mouse_pos = mouse_pos;
        
        switch (event) {
            case cv::EVENT_LBUTTONDOWN:
                handleMouseDown(mouse_pos);
                break;
                
            case cv::EVENT_MOUSEMOVE:
                handleMouseMove(mouse_pos);
                break;
                
            case cv::EVENT_LBUTTONUP:
                handleMouseUp(mouse_pos);
                break;
        }
    }
    
    void handleMouseDown(const cv::Point2f& pos) {
        // Find nearest corner to mouse position
        int nearest_corner = SimpleVirtualWallUtils::findNearestCorner(cv::Point2i(pos.x, pos.y), calib_data_.corners);
        
        if (nearest_corner >= 0) {
            is_dragging_ = true;
            calib_data_.active_corner_id = nearest_corner;
            calib_data_.corners[nearest_corner].is_dragging = true;
            
            // Calculate drag offset
            drag_offset_ = pos - cv::Point2f(calib_data_.corners[nearest_corner].position);
            
            std::cout << "🎯 Dragging corner " << (nearest_corner + 1) << std::endl;
        }
    }
    
    void handleMouseMove(const cv::Point2f& pos) {
        if (is_dragging_ && calib_data_.active_corner_id >= 0) {
            // Update corner position with drag offset
            cv::Point2f new_pos = pos - drag_offset_;
            
            // Update corner position
            cv::Point2i corner_pos(new_pos.x, new_pos.y);
            calib_data_.corners[calib_data_.active_corner_id].position = corner_pos;
            calib_data_.corners[calib_data_.active_corner_id].is_set = true;
            
            // Get edge median depth at new position (3x3 edge measurement!)
            if (depth_data_.empty() || corner_pos.x < 0 || corner_pos.y < 0 || 
                corner_pos.x >= depth_data_.cols || corner_pos.y >= depth_data_.rows) {
                calib_data_.corners[calib_data_.active_corner_id].depth = 0.0f;
                calib_data_.corners[calib_data_.active_corner_id].has_error = true;
                calib_data_.corners[calib_data_.active_corner_id].error_message = "Out of bounds";
            } else {
                // Use small 3x3 edge cluster with corner-specific positioning
                int corner_id = calib_data_.corners[calib_data_.active_corner_id].corner_id;
                float depth = SimpleVirtualWallUtils::getClusterMedianAtPosition(
                    depth_data_, confidence_data_, corner_pos, EDGE_CLUSTER_SIZE, confidence_threshold_, corner_id);
                
                if (depth > 100.0f && depth < 4000.0f) {  // Lower min for close shelves
                    calib_data_.corners[calib_data_.active_corner_id].depth = depth;
                    calib_data_.corners[calib_data_.active_corner_id].has_error = false;
                    calib_data_.corners[calib_data_.active_corner_id].error_message = "";
                } else {
                    calib_data_.corners[calib_data_.active_corner_id].depth = 0.0f;
                    calib_data_.corners[calib_data_.active_corner_id].has_error = true;
                    calib_data_.corners[calib_data_.active_corner_id].error_message = "Invalid depth";
                }
            }
        } else {
            // Update hover state
            int nearest_corner = SimpleVirtualWallUtils::findNearestCorner(cv::Point2i(pos.x, pos.y), calib_data_.corners);
            for (size_t i = 0; i < calib_data_.corners.size(); ++i) {
                calib_data_.corners[i].is_hovered = (i == static_cast<size_t>(nearest_corner));
            }
        }
    }
    
    void handleMouseUp(const cv::Point2f& pos) {
        if (is_dragging_) {
            is_dragging_ = false;
            if (calib_data_.active_corner_id >= 0) {
                calib_data_.corners[calib_data_.active_corner_id].is_dragging = false;
            }
            calib_data_.active_corner_id = -1;
            
            // Check if all corners are positioned
            bool all_set = true;
            for (const auto& corner : calib_data_.corners) {
                if (!corner.is_set || corner.depth <= 0 || corner.has_error) {
                    all_set = false;
                    break;
                }
            }
            
            calib_data_.corners_positioned = all_set;
            
            if (all_set) {
                std::cout << "✅ All corners positioned! Press 'S' to continue." << std::endl;
            }
        }
    }
};

// Static instance pointer
SimpleVirtualWallSetup* SimpleVirtualWallSetup::instance_ = nullptr;

int main() {
    try {
        SimpleVirtualWallSetup setup;
        setup.run();
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return -1;
    }
}

