#pragma once

#include "ArducamTOFCamera.hpp"
#include <opencv2/opencv.hpp>
#include <chrono>
#include <string>
#include <memory>

using namespace Arducam;

// Configuration structure
struct Config {
    int confidence_threshold = 60;
    int max_distance = 2000;
    bool save_enabled = true;
    std::string output_dir = "format_test_results";
    bool verbose = false;
    bool enable_dual_camera = true;
};

// Camera wrapper class
class CameraManager {
public:
    CameraManager(int camera_id);
    ~CameraManager();
    
    bool initialize();
    bool start();
    bool stop();
    ArducamFrameBuffer* requestFrame(int timeout_ms = 200);
    void releaseFrame(ArducamFrameBuffer* frame);
    CameraInfo getCameraInfo() const;
    bool isInitialized() const { return initialized_; }
    int getCameraId() const { return camera_id_; }

private:
    ArducamTOFCamera tof_;
    int camera_id_;
    bool initialized_ = false;
    bool started_ = false;
};

// Format analysis results
struct FormatResults {
    std::string format_name;
    cv::Mat visualization;
    cv::Mat raw_data;
    double processing_time_ms;
    int valid_pixels;
    double coverage_percentage;
    double noise_level;
    std::chrono::system_clock::time_point timestamp;
    
    // Statistics
    double min_value, max_value, mean_value, std_value;
};

// Utility functions
namespace Utils {
    void displayFPS();
    std::string getCurrentTimestamp();
    bool createDirectory(const std::string& path);
    cv::Mat applyConfidenceFilter(const cv::Mat& image, const cv::Mat& confidence, int threshold);
    void addTextOverlay(cv::Mat& image, const std::string& text, cv::Point position, 
                       cv::Scalar color = cv::Scalar(255, 255, 255));
}
