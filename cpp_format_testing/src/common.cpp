#include "common.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

// CameraManager Implementation
CameraManager::CameraManager(int camera_id) : camera_id_(camera_id) {}

CameraManager::~CameraManager() {
    if (started_) stop();
    if (initialized_) tof_.close();
}

bool CameraManager::initialize() {
    if (initialized_) return true;
    
    // Use device index mapping: Camera 0 = index 0, Camera 1 = index 8
    int device_index = (camera_id_ == 0) ? 0 : 8;
    
    if (tof_.open(Connection::CSI, device_index) != 0) {
        std::cerr << "Failed to open camera " << camera_id_ << " (device index: " << device_index << ")" << std::endl;
        return false;
    }
    
    // Set range
    tof_.setControl(Control::RANGE, 4000);
    
    // CRITICAL: Configure sensor for proper confidence calculation
    // This register write enables proper confidence vs amplitude differentiation
    tof_.writeSensor(0x0402, 0x01300a00);
    
    // Additional sensor configurations for proper ToF operation
    auto info = tof_.getCameraInfo();
    if (info.device_type == DeviceType::DEVICE_HQVGA) {
        // Enable proper mode for HQVGA sensors
        tof_.setControl(Control::MODE, 1); // Far mode for better confidence calculation
    } else if (info.device_type == DeviceType::DEVICE_VGA) {
        // VGA sensors support dual frequency mode for better confidence
        tof_.setControl(Control::MODE, 1); // Dual frequency mode
    }
    
    // Enable calibration data loading for proper confidence calculation
    tof_.setControl(Control::LOAD_CALI_DATA, 1);
    
    initialized_ = true;
    std::cout << "✅ Camera " << camera_id_ << " initialized (device index: " << device_index << ")" << std::endl;
    return true;
}

bool CameraManager::start() {
    if (!initialized_) return false;
    if (started_) return true;
    
    if (tof_.start(FrameType::DEPTH_FRAME) != 0) {
        std::cerr << "Failed to start camera " << camera_id_ << std::endl;
        return false;
    }
    
    started_ = true;
    return true;
}

bool CameraManager::stop() {
    if (!started_) return true;
    
    int result = tof_.stop();
    started_ = false;
    return result == 0;
}

ArducamFrameBuffer* CameraManager::requestFrame(int timeout_ms) {
    if (!started_) return nullptr;
    return tof_.requestFrame(timeout_ms);
}

void CameraManager::releaseFrame(ArducamFrameBuffer* frame) {
    if (frame) tof_.releaseFrame(frame);
}

CameraInfo CameraManager::getCameraInfo() const {
    return tof_.getCameraInfo();
}

// Utility functions implementation
namespace Utils {
    void displayFPS() {
        using std::chrono::high_resolution_clock;
        using namespace std::literals;
        static int count = 0;
        static auto time_beg = high_resolution_clock::now();
        static bool show_fps = false; // Control FPS display
        
        auto time_end = high_resolution_clock::now();
        ++count;
        auto duration_ms = (time_end - time_beg) / 1ms;
        
        if (duration_ms >= 5000) { // Display every 5 seconds instead of every second
            if (show_fps) {
                std::cout << "📊 FPS: " << (count / 5) << std::endl;
            }
            count = 0;
            time_beg = time_end;
        }
    }
    
    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
    
    bool createDirectory(const std::string& path) {
        try {
            // Check if directory already exists
            if (std::filesystem::exists(path)) {
                return std::filesystem::is_directory(path);
            }
            // Create directory if it doesn't exist
            return std::filesystem::create_directories(path);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create directory " << path << ": " << e.what() << std::endl;
            return false;
        }
    }
    
    cv::Mat applyConfidenceFilter(const cv::Mat& image, const cv::Mat& confidence, int threshold) {
        cv::Mat result = image.clone();
        result.setTo(cv::Scalar(0, 0, 0), confidence < threshold);
        return result;
    }
    
    void addTextOverlay(cv::Mat& image, const std::string& text, cv::Point position, cv::Scalar color) {
        cv::putText(image, text, position, cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    }
}
