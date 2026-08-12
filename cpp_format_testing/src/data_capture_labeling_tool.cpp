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
#include <filesystem>
#include <queue>

#ifdef WITH_OPEN3D
#include <open3d/Open3D.h>
#endif

// Enhanced data saver for labeling tool
class LabelingDataSaver {
public:
    struct CaptureSession {
        std::string session_id;
        std::string label;
        std::string start_timestamp;
        std::string end_timestamp;
        std::vector<std::string> frame_files;
        std::vector<std::string> raw_files;
        std::vector<std::string> pointcloud_files;
        std::string metadata_file;
        int frame_count = 0;
    };
    
    LabelingDataSaver(const Config& config) : config_(config), session_counter_(0) {
        base_output_dir_ = "/home/dev/Arducam_tof_camera/cpp_format_testing/labeled_data";
        initializeOutputDirectory();
    }
    
    bool initializeOutputDirectory() {
        if (!std::filesystem::exists(base_output_dir_)) {
            std::filesystem::create_directories(base_output_dir_);
        }
        
        // Find next session number
        for (int i = 1; i <= 9999; i++) {
            std::string session_dir = base_output_dir_ + "/data" + std::to_string(i).insert(0, 3 - std::to_string(i).length(), '0');
            if (!std::filesystem::exists(session_dir)) {
                session_counter_ = i;
                break;
            }
        }
        
        std::cout << "📁 Next session will be: data" << std::setfill('0') << std::setw(3) << session_counter_ << std::endl;
        return true;
    }
    
    std::string startCaptureSession(const std::string& label) {
        // New naming format: data_01_[label]
        std::string session_num = std::to_string(session_counter_);
        if (session_num.length() == 1) session_num = "0" + session_num;
        current_session_.session_id = "data_" + session_num + "_" + label;
        current_session_.label = label;
        current_session_.start_timestamp = Utils::getCurrentTimestamp();
        current_session_.frame_count = 0;
        
        // Initialize session start time for timing measurements (P0 Enhancement)
        auto now = std::chrono::high_resolution_clock::now();
        session_start_time_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
        
        // Clear previous timing data
        frame_timing_data_.clear();
        
        // Create session directory structure
        session_dir_ = base_output_dir_ + "/" + current_session_.session_id;
        std::filesystem::create_directories(session_dir_);
        
        // Create separate folders for each camera and format type
        std::filesystem::create_directories(session_dir_ + "/frames/cam0_depth");
        std::filesystem::create_directories(session_dir_ + "/frames/cam0_confidence");
        std::filesystem::create_directories(session_dir_ + "/frames/cam0_amplitude");
        
        if (config_.enable_dual_camera) {
            std::filesystem::create_directories(session_dir_ + "/frames/cam1_depth");
            std::filesystem::create_directories(session_dir_ + "/frames/cam1_confidence");
            std::filesystem::create_directories(session_dir_ + "/frames/cam1_amplitude");
        }
        
        std::filesystem::create_directories(session_dir_ + "/raw_data");
        std::filesystem::create_directories(session_dir_ + "/pointclouds");
        std::filesystem::create_directories(session_dir_ + "/videos");
        
        // Initialize separate video writers for each format
        initializeVideoWriters();
        
        recording_ = true;
        std::cout << "🔴 Started recording session: " << current_session_.session_id << " (" << label << ")" << std::endl;
        std::cout << "⏱️  Precise frame timing enabled" << std::endl;
        
        return current_session_.session_id;
    }
    
    bool captureFrame(const std::vector<FormatResults>& results_cam0,
                     const std::vector<FormatResults>& results_cam1,
                     ArducamFrameBuffer* frame0,
                     ArducamFrameBuffer* frame1,
                     const cv::Mat& dashboard_image) {
        if (!recording_) return false;
        
        current_session_.frame_count++;
        std::string frame_timestamp = Utils::getCurrentTimestamp();
        std::string frame_id = std::to_string(current_session_.frame_count).insert(0, 6 - std::to_string(current_session_.frame_count).length(), '0');
        
        // Record precise frame timing (P0 Enhancement)
        recordFrameTiming("frame_" + frame_id);
        
        // Save individual frames for each format type - Camera 0
        if (frame0 && !results_cam0.empty()) {
            saveIndividualFrames(results_cam0, frame0, frame_id, 0);
        }
        
        // Save individual frames for each format type - Camera 1
        if (frame1 && config_.enable_dual_camera && !results_cam1.empty()) {
            saveIndividualFrames(results_cam1, frame1, frame_id, 1);
        }
        
        // Save raw data
        saveRawData(frame0, frame1, frame_id);
        
        // Save point cloud data
        #ifdef WITH_OPEN3D
        savePointCloudData(frame0, frame1, frame_id);
        #endif
        
        return true;
    }
    
    std::string endCaptureSession() {
        if (!recording_) return "";
        
        current_session_.end_timestamp = Utils::getCurrentTimestamp();
        recording_ = false;
        
        // Close all video writers
        if (video_writer_cam0_depth_.isOpened()) {
            video_writer_cam0_depth_.release();
        }
        if (video_writer_cam0_confidence_.isOpened()) {
            video_writer_cam0_confidence_.release();
        }
        if (video_writer_cam0_amplitude_.isOpened()) {
            video_writer_cam0_amplitude_.release();
        }
        
        if (config_.enable_dual_camera) {
            if (video_writer_cam1_depth_.isOpened()) {
                video_writer_cam1_depth_.release();
            }
            if (video_writer_cam1_confidence_.isOpened()) {
                video_writer_cam1_confidence_.release();
            }
            if (video_writer_cam1_amplitude_.isOpened()) {
                video_writer_cam1_amplitude_.release();
            }
        }
        
        // Save metadata
        saveSessionMetadata();
        
        std::cout << "⏹️  Ended recording session: " << current_session_.session_id;
        std::cout << " (" << current_session_.frame_count << " frames)" << std::endl;
        
        std::string completed_session = current_session_.session_id;
        session_counter_++;
        
        // Reset current session
        current_session_ = CaptureSession();
        
        return completed_session;
    }
    
    bool isRecording() const { return recording_; }
    int getCurrentFrameCount() const { return current_session_.frame_count; }
    std::string getCurrentSessionId() const { return current_session_.session_id; }
    
private:
    Config config_;
    std::string base_output_dir_;
    std::string session_dir_;
    int session_counter_;
    bool recording_ = false;
    CaptureSession current_session_;
    
    // Frame timing data (P0 Enhancement)
    std::map<std::string, Json::Value> frame_timing_data_;
    int64_t session_start_time_ns_;
    
    // Separate video writers for each camera and format
    cv::VideoWriter video_writer_cam0_depth_;
    cv::VideoWriter video_writer_cam0_confidence_;
    cv::VideoWriter video_writer_cam0_amplitude_;
    cv::VideoWriter video_writer_cam1_depth_;
    cv::VideoWriter video_writer_cam1_confidence_;
    cv::VideoWriter video_writer_cam1_amplitude_;
     
     void recordFrameTiming(const std::string& frame_id) {
         auto now = std::chrono::high_resolution_clock::now();
         int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch()
         ).count();
         
         Json::Value timing;
         timing["frame_number"] = current_session_.frame_count;
         timing["timestamp_ns"] = Json::Value::Int64(timestamp_ns);
         timing["relative_time_ms"] = (timestamp_ns - session_start_time_ns_) / 1000000.0;
         
         // Calculate FPS for this frame (if not first frame)
         if (frame_timing_data_.size() > 0) {
             // Get last frame timing
             auto last_it = frame_timing_data_.rbegin();
             if (last_it != frame_timing_data_.rend()) {
                 int64_t prev_timestamp = static_cast<int64_t>(last_it->second["timestamp_ns"].asInt64());
                 double frame_interval_ms = (timestamp_ns - prev_timestamp) / 1000000.0;
                 double instantaneous_fps = 1000.0 / frame_interval_ms;
                 timing["frame_interval_ms"] = frame_interval_ms;
                 timing["instantaneous_fps"] = instantaneous_fps;
             }
         }
         
         frame_timing_data_[frame_id] = timing;
     }
     
     void initializeVideoWriters() {
         // Initialize video writers for Camera 0
         std::string cam0_depth_video = session_dir_ + "/videos/cam0_depth.mp4";
         std::string cam0_conf_video = session_dir_ + "/videos/cam0_confidence.mp4";
         std::string cam0_amp_video = session_dir_ + "/videos/cam0_amplitude.mp4";
         
         video_writer_cam0_depth_.open(cam0_depth_video, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0, cv::Size(240, 180));
         video_writer_cam0_confidence_.open(cam0_conf_video, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0, cv::Size(240, 180));
         video_writer_cam0_amplitude_.open(cam0_amp_video, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0, cv::Size(240, 180));
         
         // Initialize video writers for Camera 1 if dual camera enabled
         if (config_.enable_dual_camera) {
             std::string cam1_depth_video = session_dir_ + "/videos/cam1_depth.mp4";
             std::string cam1_conf_video = session_dir_ + "/videos/cam1_confidence.mp4";
             std::string cam1_amp_video = session_dir_ + "/videos/cam1_amplitude.mp4";
             
             video_writer_cam1_depth_.open(cam1_depth_video, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0, cv::Size(240, 180));
             video_writer_cam1_confidence_.open(cam1_conf_video, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0, cv::Size(240, 180));
             video_writer_cam1_amplitude_.open(cam1_amp_video, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0, cv::Size(240, 180));
         }
     }
     
     void saveIndividualFrames(const std::vector<FormatResults>& results, ArducamFrameBuffer* frame, const std::string& frame_id, int camera_id) {
         std::string cam_prefix = "cam" + std::to_string(camera_id);
         
         for (const auto& result : results) {
             cv::Mat frame_image;
             std::string format_name;
             cv::VideoWriter* video_writer = nullptr;
             
             // Determine format type and corresponding video writer
             if (result.format_name == "Depth") {
                 frame_image = result.visualization.clone();
                 format_name = "depth";
                 video_writer = (camera_id == 0) ? &video_writer_cam0_depth_ : &video_writer_cam1_depth_;
             } else if (result.format_name == "Confidence") {
                 frame_image = result.visualization.clone();
                 format_name = "confidence";
                 video_writer = (camera_id == 0) ? &video_writer_cam0_confidence_ : &video_writer_cam1_confidence_;
             } else if (result.format_name == "Amplitude") {
                 frame_image = result.visualization.clone();
                 format_name = "amplitude";
                 video_writer = (camera_id == 0) ? &video_writer_cam0_amplitude_ : &video_writer_cam1_amplitude_;
             } else {
                 continue; // Skip unknown formats
             }
             
             // Save individual frame as PNG
             std::string frame_path = session_dir_ + "/frames/" + cam_prefix + "_" + format_name + "/frame_" + frame_id + ".png";
             cv::imwrite(frame_path, frame_image);
             current_session_.frame_files.push_back(frame_path);
             
             // Write to corresponding video
             if (video_writer && video_writer->isOpened()) {
                 video_writer->write(frame_image);
             }
         }
     }
     
     void saveRawData(ArducamFrameBuffer* frame0, ArducamFrameBuffer* frame1, const std::string& frame_id) {
        if (frame0) {
            // Save depth data
            float* depth_ptr = (float*)frame0->getData(FrameType::DEPTH_FRAME);
            if (depth_ptr) {
                std::string depth_file = session_dir_ + "/raw_data/depth_cam0_" + frame_id + ".raw";
                std::ofstream file(depth_file, std::ios::binary);
                file.write(reinterpret_cast<const char*>(depth_ptr), 240 * 180 * sizeof(float));
                file.close();
                current_session_.raw_files.push_back(depth_file);
            }
            
            // Save confidence data
            float* conf_ptr = (float*)frame0->getData(FrameType::CONFIDENCE_FRAME);
            if (conf_ptr) {
                std::string conf_file = session_dir_ + "/raw_data/confidence_cam0_" + frame_id + ".raw";
                std::ofstream file(conf_file, std::ios::binary);
                file.write(reinterpret_cast<const char*>(conf_ptr), 240 * 180 * sizeof(float));
                file.close();
                current_session_.raw_files.push_back(conf_file);
            }
            
            // Save amplitude data
            float* amp_ptr = (float*)frame0->getData(FrameType::AMPLITUDE_FRAME);
            if (amp_ptr) {
                std::string amp_file = session_dir_ + "/raw_data/amplitude_cam0_" + frame_id + ".raw";
                std::ofstream file(amp_file, std::ios::binary);
                file.write(reinterpret_cast<const char*>(amp_ptr), 240 * 180 * sizeof(float));
                file.close();
                current_session_.raw_files.push_back(amp_file);
            }
        }
        
        // Save Camera 1 data if available
        if (frame1 && config_.enable_dual_camera) {
            float* depth_ptr = (float*)frame1->getData(FrameType::DEPTH_FRAME);
            if (depth_ptr) {
                std::string depth_file = session_dir_ + "/raw_data/depth_cam1_" + frame_id + ".raw";
                std::ofstream file(depth_file, std::ios::binary);
                file.write(reinterpret_cast<const char*>(depth_ptr), 240 * 180 * sizeof(float));
                file.close();
                current_session_.raw_files.push_back(depth_file);
            }
            
            float* conf_ptr = (float*)frame1->getData(FrameType::CONFIDENCE_FRAME);
            if (conf_ptr) {
                std::string conf_file = session_dir_ + "/raw_data/confidence_cam1_" + frame_id + ".raw";
                std::ofstream file(conf_file, std::ios::binary);
                file.write(reinterpret_cast<const char*>(conf_ptr), 240 * 180 * sizeof(float));
                file.close();
                current_session_.raw_files.push_back(conf_file);
            }
            
            float* amp_ptr = (float*)frame1->getData(FrameType::AMPLITUDE_FRAME);
            if (amp_ptr) {
                std::string amp_file = session_dir_ + "/raw_data/amplitude_cam1_" + frame_id + ".raw";
                std::ofstream file(amp_file, std::ios::binary);
                file.write(reinterpret_cast<const char*>(amp_ptr), 240 * 180 * sizeof(float));
                file.close();
                current_session_.raw_files.push_back(amp_file);
            }
        }
    }
    
    #ifdef WITH_OPEN3D
    void savePointCloudData(ArducamFrameBuffer* frame0, ArducamFrameBuffer* frame1, const std::string& frame_id) {
        // Generate and save point cloud for Camera 0
        if (frame0) {
            auto pcd = generatePointCloud(frame0, 0);
            if (pcd && !pcd->points_.empty()) {
                std::string pcd_file = session_dir_ + "/pointclouds/pointcloud_cam0_" + frame_id + ".pcd";
                open3d::io::WritePointCloud(pcd_file, *pcd);
                current_session_.pointcloud_files.push_back(pcd_file);
            }
        }
        
        // Generate and save point cloud for Camera 1
        if (frame1 && config_.enable_dual_camera) {
            auto pcd = generatePointCloud(frame1, 1);
            if (pcd && !pcd->points_.empty()) {
                std::string pcd_file = session_dir_ + "/pointclouds/pointcloud_cam1_" + frame_id + ".pcd";
                open3d::io::WritePointCloud(pcd_file, *pcd);
                current_session_.pointcloud_files.push_back(pcd_file);
            }
        }
    }
    
    std::shared_ptr<open3d::geometry::PointCloud> generatePointCloud(ArducamFrameBuffer* frame, int camera_id) {
        auto pcd = std::make_shared<open3d::geometry::PointCloud>();
        
        float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
        float* conf_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
        
        if (!depth_ptr || !conf_ptr) return pcd;
        
        // Use improved camera intrinsics for better scaling (matching dual_pointcloud_viewer values)
        double fx = 200.0, fy = 200.0, cx = 120.0, cy = 90.0;
        
        for (int y = 0; y < 180; y++) {
            for (int x = 0; x < 240; x++) {
                int idx = y * 240 + x;
                float depth = depth_ptr[idx];
                float confidence = conf_ptr[idx];
                
                if (depth > 200 && depth < config_.max_distance && confidence > config_.confidence_threshold) {
                    // Convert to 3D coordinates
                    double z = depth / 1000.0; // Convert mm to meters
                    double px = (x - cx) * z / fx;
                    double py = (y - cy) * z / fy;
                    
                    pcd->points_.push_back(Eigen::Vector3d(px, -py, -z));
                    
                    // Color based on depth
                    double normalized_depth = (depth - 200) / (config_.max_distance - 200);
                    pcd->colors_.push_back(Eigen::Vector3d(normalized_depth, 1.0 - normalized_depth, 0.5));
                }
            }
        }
        
        return pcd;
    }
    #endif
    
    void saveSessionMetadata() {
        Json::Value metadata;
        metadata["session_id"] = current_session_.session_id;
        metadata["label"] = current_session_.label;
        metadata["start_timestamp"] = current_session_.start_timestamp;
        metadata["end_timestamp"] = current_session_.end_timestamp;
        metadata["frame_count"] = current_session_.frame_count;
        
        // Save frame timing information (P0 Enhancement)
        Json::Value frame_timing(Json::objectValue);
        for (const auto& [frame_id, timing] : frame_timing_data_) {
            frame_timing[frame_id] = timing;
        }
        metadata["frame_timing"] = frame_timing;
        
        // Calculate and save FPS statistics (P0 Enhancement)
        if (frame_timing_data_.size() > 1) {
            auto first_frame_it = frame_timing_data_.begin();
            auto last_frame_it = std::prev(frame_timing_data_.end());
            
            int64_t first_time = static_cast<int64_t>(first_frame_it->second["timestamp_ns"].asInt64());
            int64_t last_time = static_cast<int64_t>(last_frame_it->second["timestamp_ns"].asInt64());
            
            double duration_sec = (last_time - first_time) / 1e9;
            double avg_fps = current_session_.frame_count / duration_sec;
            
            // Collect FPS values for statistics
            std::vector<double> fps_values;
            for (const auto& [frame_id, timing] : frame_timing_data_) {
                if (timing.isMember("instantaneous_fps")) {
                    fps_values.push_back(timing["instantaneous_fps"].asDouble());
                }
            }
            
            if (!fps_values.empty()) {
                std::sort(fps_values.begin(), fps_values.end());
                double min_fps = fps_values.front();
                double max_fps = fps_values.back();
                double median_fps = fps_values[fps_values.size() / 2];
                
                metadata["statistics"]["duration_seconds"] = duration_sec;
                metadata["statistics"]["average_fps"] = avg_fps;
                metadata["statistics"]["min_fps"] = min_fps;
                metadata["statistics"]["max_fps"] = max_fps;
                metadata["statistics"]["median_fps"] = median_fps;
            }
        }
        
        // Camera and resolution info
        metadata["camera_info"]["resolution_width"] = 240;
        metadata["camera_info"]["resolution_height"] = 180;
        metadata["camera_info"]["depth_range_min_mm"] = 200;
        metadata["camera_info"]["depth_range_max_mm"] = config_.max_distance;
        
        // Video files for each format and camera
        Json::Value video_files;
        video_files["cam0_depth"] = session_dir_ + "/videos/cam0_depth.mp4";
        video_files["cam0_confidence"] = session_dir_ + "/videos/cam0_confidence.mp4";
        video_files["cam0_amplitude"] = session_dir_ + "/videos/cam0_amplitude.mp4";
        
        if (config_.enable_dual_camera) {
            video_files["cam1_depth"] = session_dir_ + "/videos/cam1_depth.mp4";
            video_files["cam1_confidence"] = session_dir_ + "/videos/cam1_confidence.mp4";
            video_files["cam1_amplitude"] = session_dir_ + "/videos/cam1_amplitude.mp4";
        }
        metadata["video_files"] = video_files;
        
        // Frame directories for each format and camera
        Json::Value frame_directories;
        frame_directories["cam0_depth"] = session_dir_ + "/frames/cam0_depth/";
        frame_directories["cam0_confidence"] = session_dir_ + "/frames/cam0_confidence/";
        frame_directories["cam0_amplitude"] = session_dir_ + "/frames/cam0_amplitude/";
        
        if (config_.enable_dual_camera) {
            frame_directories["cam1_depth"] = session_dir_ + "/frames/cam1_depth/";
            frame_directories["cam1_confidence"] = session_dir_ + "/frames/cam1_confidence/";
            frame_directories["cam1_amplitude"] = session_dir_ + "/frames/cam1_amplitude/";
        }
        metadata["frame_directories"] = frame_directories;
        
        Json::Value frame_files(Json::arrayValue);
        for (const auto& file : current_session_.frame_files) {
            frame_files.append(file);
        }
        metadata["frame_files"] = frame_files;
        
        Json::Value raw_files(Json::arrayValue);
        for (const auto& file : current_session_.raw_files) {
            raw_files.append(file);
        }
        metadata["raw_files"] = raw_files;
        
        Json::Value pointcloud_files(Json::arrayValue);
        for (const auto& file : current_session_.pointcloud_files) {
            pointcloud_files.append(file);
        }
        metadata["pointcloud_files"] = pointcloud_files;
        
        // Camera configuration
        metadata["camera_config"]["confidence_threshold"] = config_.confidence_threshold;
        metadata["camera_config"]["max_distance"] = config_.max_distance;
        metadata["camera_config"]["dual_camera_enabled"] = config_.enable_dual_camera;
        
        std::string metadata_file = session_dir_ + "/metadata.json";
        std::ofstream file(metadata_file);
        file << metadata;
        file.close();
        
        current_session_.metadata_file = metadata_file;
    }
};

class DataCaptureLabelingTool {
public:
    DataCaptureLabelingTool(const Config& config) 
        : config_(config), analyzer_(config), labeling_saver_(config), running_(false), 
          frame_counter_(0), current_label_("pickup"), original_stderr_(-1), null_fd_(-1),
          measured_distance_cam0_(-1.0f), measured_distance_cam1_(-1.0f),
          click_x_cam0_(-1), click_y_cam0_(-1), click_x_cam1_(-1), click_y_cam1_(-1) {
        // Reserve space for depth data (240x180 pixels)
        depth_data_cam0_.reserve(240 * 180);
        depth_data_cam1_.reserve(240 * 180);
    }
    
    bool initialize() {
        std::cout << "🏷️  ToF Data Capture & Labeling Tool (C++)" << std::endl;
        std::cout << "🚀 Initializing Camera System..." << std::endl;
        
        // Suppress warnings during camera initialization
        suppressWarnings();
        
        // Initialize Camera 0
        cam0_ = std::make_unique<CameraManager>(0);
        bool cam0_success = cam0_->initialize();
        
        restoreWarnings();
        
        if (!cam0_success) {
            std::cerr << "Failed to initialize Camera 0" << std::endl;
            return false;
        }
        
        auto info0 = cam0_->getCameraInfo();
        std::cout << "✅ Camera 0: " << info0.width << "x" << info0.height << std::endl;
        
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
                std::cout << "✅ Camera 1: " << info1.width << "x" << info1.height << std::endl;
            }
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
        
        std::cout << "🏷️  Data Capture & Labeling Tool Started!" << std::endl;
        std::cout << "📺 Dashboard Layout:" << std::endl;
        std::cout << "   Top Row: Depth (Rainbow) | Confidence (Grayscale)" << std::endl;
        std::cout << "   Bottom Row: Amplitude (Grayscale) | Status Panel" << std::endl;
        std::cout << std::endl;
        std::cout << "⌨️  Controls:" << std::endl;
        std::cout << "   'q' - Quit" << std::endl;
        std::cout << "   '1' - Set label to 'pickup'" << std::endl;
        std::cout << "   '2' - Set label to 'touch'" << std::endl;
        std::cout << "   '3' - Set label to 'return'" << std::endl;
        std::cout << "   '4' - Set label to 'no_gesture' (NEW - negative class)" << std::endl;
        std::cout << "   '5' - Set label to 'hand_present' (NEW - negative class)" << std::endl;
        std::cout << "   'SPACE' - Start/Stop recording" << std::endl;
        std::cout << "   'p' - Pause/Resume preview" << std::endl;
        std::cout << "   '+/-' - Adjust confidence threshold" << std::endl;
        std::cout << "   '<>' - Adjust max distance" << std::endl;
        std::cout << "   'Left Click' - Measure distance on depth view (LIVE!) (NEW!)" << std::endl;
        std::cout << "   'r' - Reset distance measurements" << std::endl;
        std::cout << "💾 Save System:" << std::endl;
        std::cout << "   📁 Directory: /home/dev/Arducam_tof_camera/cpp_format_testing/labeled_data/" << std::endl;
        std::cout << "   📊 Saves: Frames (PNG), Videos (MP4), Raw data, Point clouds, Metadata (JSON)" << std::endl;
        std::cout << "   ⏱️  Frame timing data (P0 Enhancement - nanosecond precision)" << std::endl;
        std::cout << std::endl;
        
        running_ = true;
        bool paused = false;
        
        // Create display window
        cv::namedWindow("ToF Data Capture & Labeling Tool", cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
        
        // Set window size based on camera mode
        if (config_.enable_dual_camera) {
            cv::resizeWindow("ToF Data Capture & Labeling Tool", 1400, 900);  // Dual camera layout (smaller)
        } else {
            cv::resizeWindow("ToF Data Capture & Labeling Tool", 1000, 600);   // Single camera layout (smaller)
        }
        
        // Position main window on the left side of screen
        cv::moveWindow("ToF Data Capture & Labeling Tool", 50, 50);
        
        // Register mouse callback for distance measurement
        cv::setMouseCallback("ToF Data Capture & Labeling Tool", mouseCallback, this);
        
        #ifdef WITH_OPEN3D
        // Start point cloud viewer in separate thread
        std::thread pointcloud_thread(&DataCaptureLabelingTool::runPointCloudViewer, this);
        #endif
        
        // Frame rate control
        auto last_frame_time = std::chrono::steady_clock::now();
        const auto target_frame_time = std::chrono::milliseconds(33); // ~30 FPS
        
        while (running_) {
            auto current_time = std::chrono::steady_clock::now();
            
            if (!paused && (current_time - last_frame_time) >= target_frame_time) {
                processFrames();
                last_frame_time = current_time;
            }
            
            // Handle keyboard input
            int key = cv::waitKey(1) & 0xFF;
            if (key != 255) {
                if (handleKeyInput(key, paused)) {
                    break;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        #ifdef WITH_OPEN3D
        if (pointcloud_thread.joinable()) {
            pointcloud_thread.join();
        }
        #endif
        
        cleanup();
    }
    
private:
    Config config_;
    FormatAnalyzer analyzer_;
    LabelingDataSaver labeling_saver_;
    std::unique_ptr<CameraManager> cam0_;
    std::unique_ptr<CameraManager> cam1_;
    std::atomic<bool> running_;
    int frame_counter_;
    std::string current_label_;
    int original_stderr_;
    int null_fd_;
    
    // Distance measurement
    float measured_distance_cam0_;
    float measured_distance_cam1_;
    int click_x_cam0_;
    int click_y_cam0_;
    int click_x_cam1_;
    int click_y_cam1_;
    std::vector<float> depth_data_cam0_;
    std::vector<float> depth_data_cam1_;
    
    #ifdef WITH_OPEN3D
    std::shared_ptr<open3d::visualization::Visualizer> pointcloud_vis_;
    std::shared_ptr<open3d::geometry::PointCloud> pcd0_;
    std::shared_ptr<open3d::geometry::PointCloud> pcd1_;
    std::atomic<bool> pointcloud_running_{true};
    #endif
    
    // Static mouse callback for OpenCV
    static void mouseCallback(int event, int x, int y, int flags, void* userdata) {
        if (event == cv::EVENT_LBUTTONDOWN) {
            DataCaptureLabelingTool* tool = static_cast<DataCaptureLabelingTool*>(userdata);
            tool->handleMouseClick(x, y);
        }
    }
    
    void handleMouseClick(int x, int y) {
        if (config_.enable_dual_camera) {
            // Dual camera layout: each depth panel is 350x300
            int panel_width = 350;
            int panel_height = 300;
            
            // Check if clicked on Camera 0 depth (top-left)
            if (x >= 0 && x < panel_width && y >= 0 && y < panel_height) {
                // Map to original 240x180 coordinates
                click_x_cam0_ = (x * 240) / panel_width;
                click_y_cam0_ = (y * 180) / panel_height;
                std::cout << "📏 Cam0 measurement point set at (" << click_x_cam0_ << ", " << click_y_cam0_ << ") - Live tracking enabled" << std::endl;
            }
            // Check if clicked on Camera 1 depth (top-right)
            else if (x >= panel_width && x < panel_width * 2 && y >= 0 && y < panel_height) {
                // Map to original 240x180 coordinates
                click_x_cam1_ = ((x - panel_width) * 240) / panel_width;
                click_y_cam1_ = (y * 180) / panel_height;
                std::cout << "📏 Cam1 measurement point set at (" << click_x_cam1_ << ", " << click_y_cam1_ << ") - Live tracking enabled" << std::endl;
            }
        } else {
            // Single camera layout: depth panel is 400x300 at top-left
            int panel_width = 400;
            int panel_height = 300;
            
            // Check if clicked on depth panel
            if (x >= 0 && x < panel_width && y >= 0 && y < panel_height) {
                // Map to original 240x180 coordinates
                click_x_cam0_ = (x * 240) / panel_width;
                click_y_cam0_ = (y * 180) / panel_height;
                std::cout << "📏 Measurement point set at (" << click_x_cam0_ << ", " << click_y_cam0_ << ") - Live tracking enabled" << std::endl;
            }
        }
    }
    
    void measureDistance(int camera_id) {
        const std::vector<float>& depth_data = (camera_id == 0) ? depth_data_cam0_ : depth_data_cam1_;
        
        if (depth_data.empty()) {
            return;
        }
        
        int click_x = (camera_id == 0) ? click_x_cam0_ : click_x_cam1_;
        int click_y = (camera_id == 0) ? click_y_cam0_ : click_y_cam1_;
        
        // Bounds check
        if (click_x < 0 || click_x >= 240 || click_y < 0 || click_y >= 180) {
            return;
        }
        
        int idx = click_y * 240 + click_x;
        if (idx >= depth_data.size()) {
            return;
        }
        
        float distance = depth_data[idx];
        
        // Update measurement silently (live update every frame)
        if (camera_id == 0) {
            measured_distance_cam0_ = distance;
        } else {
            measured_distance_cam1_ = distance;
        }
    }
    
    void suppressWarnings() {
        setenv("QT_LOGGING_RULES", "*.debug=false", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        setenv("OPENCV_LOG_LEVEL", "ERROR", 1);
        setenv("GLOG_minloglevel", "3", 1);
        
        original_stderr_ = dup(STDERR_FILENO);
        null_fd_ = open("/dev/null", O_WRONLY);
        if (null_fd_ != -1) {
            dup2(null_fd_, STDERR_FILENO);
        }
    }
    
    void restoreWarnings() {
        if (original_stderr_ != -1) {
            dup2(original_stderr_, STDERR_FILENO);
            close(original_stderr_);
        }
        if (null_fd_ != -1) {
            close(null_fd_);
        }
    }
    
    void processFrames() {
        // Get frames
        ArducamFrameBuffer* frame0 = cam0_->requestFrame(200);
        ArducamFrameBuffer* frame1 = nullptr;
        
        if (config_.enable_dual_camera && cam1_) {
            frame1 = cam1_->requestFrame(200);
        }
        
        if (!frame0) {
            return;
        }
        
        // Copy depth data for distance measurement (before frames are released)
        float* depth_ptr0 = (float*)frame0->getData(FrameType::DEPTH_FRAME);
        if (depth_ptr0) {
            depth_data_cam0_.assign(depth_ptr0, depth_ptr0 + (240 * 180));
        }
        
        if (frame1) {
            float* depth_ptr1 = (float*)frame1->getData(FrameType::DEPTH_FRAME);
            if (depth_ptr1) {
                depth_data_cam1_.assign(depth_ptr1, depth_ptr1 + (240 * 180));
            }
        }
        
        // Update live distance measurements if measurement points are set
        if (click_x_cam0_ >= 0 && click_y_cam0_ >= 0) {
            measureDistance(0);
        }
        if (config_.enable_dual_camera && click_x_cam1_ >= 0 && click_y_cam1_ >= 0) {
            measureDistance(1);
        }
        
        frame_counter_++;
        
        // Analyze frames
        std::vector<FormatResults> results_cam0 = analyzer_.analyzeFrameThreeFormats(frame0, 0);
        std::vector<FormatResults> results_cam1;
        
        if (frame1) {
            results_cam1 = analyzer_.analyzeFrameThreeFormats(frame1, 1);
        }
        
        // Create dashboard display
        cv::Mat dashboard = createDashboard(results_cam0, results_cam1);
        
        // Capture frame if recording
        if (labeling_saver_.isRecording()) {
            labeling_saver_.captureFrame(results_cam0, results_cam1, frame0, frame1, dashboard);
        }
        
        // Display dashboard
        cv::imshow("ToF Data Capture & Labeling Tool", dashboard);
        
        // Release frames
        cam0_->releaseFrame(frame0);
        if (frame1 && cam1_) {
            cam1_->releaseFrame(frame1);
        }
    }
    
    cv::Mat createDashboard(const std::vector<FormatResults>& results_cam0,
                           const std::vector<FormatResults>& results_cam1) {
        if (config_.enable_dual_camera && !results_cam1.empty()) {
            // Dual camera layout (3x2 grid) - smaller to fit new window size
            cv::Mat dashboard(900, 1400, CV_8UC3, cv::Scalar(30, 30, 30));
            
            int panel_width = 350;
            int panel_height = 300;
            int status_width = 350;  // Smaller status panel
            
            // Top row: Camera 0 Depth (left) and Camera 1 Depth (right)
            if (results_cam0.size() >= 1) {
                cv::Mat depth0_resized;
                cv::resize(results_cam0[0].visualization, depth0_resized, cv::Size(panel_width, panel_height));
                
                // Draw crosshair if a measurement was taken
                if (click_x_cam0_ >= 0 && click_y_cam0_ >= 0) {
                    int display_x = (click_x_cam0_ * panel_width) / 240;
                    int display_y = (click_y_cam0_ * panel_height) / 180;
                    cv::drawMarker(depth0_resized, cv::Point(display_x, display_y), cv::Scalar(0, 255, 255), 
                                   cv::MARKER_CROSS, 20, 2);
                }
                
                depth0_resized.copyTo(dashboard(cv::Rect(0, 0, panel_width, panel_height)));
            }
            if (results_cam1.size() >= 1) {
                cv::Mat depth1_resized;
                cv::resize(results_cam1[0].visualization, depth1_resized, cv::Size(panel_width, panel_height));
                
                // Draw crosshair if a measurement was taken
                if (click_x_cam1_ >= 0 && click_y_cam1_ >= 0) {
                    int display_x = (click_x_cam1_ * panel_width) / 240;
                    int display_y = (click_y_cam1_ * panel_height) / 180;
                    cv::drawMarker(depth1_resized, cv::Point(display_x, display_y), cv::Scalar(0, 255, 255), 
                                   cv::MARKER_CROSS, 20, 2);
                }
                
                depth1_resized.copyTo(dashboard(cv::Rect(panel_width, 0, panel_width, panel_height)));
            }
            
            // Middle row: Camera 0 Confidence (left) and Camera 1 Confidence (right)
            if (results_cam0.size() >= 2) {
                cv::Mat conf0_resized;
                cv::resize(results_cam0[1].visualization, conf0_resized, cv::Size(panel_width, panel_height));
                conf0_resized.copyTo(dashboard(cv::Rect(0, panel_height, panel_width, panel_height)));
            }
            if (results_cam1.size() >= 2) {
                cv::Mat conf1_resized;
                cv::resize(results_cam1[1].visualization, conf1_resized, cv::Size(panel_width, panel_height));
                conf1_resized.copyTo(dashboard(cv::Rect(panel_width, panel_height, panel_width, panel_height)));
            }
            
            // Bottom row: Camera 0 Amplitude (left) and Camera 1 Amplitude (right)
            if (results_cam0.size() >= 3) {
                cv::Mat amp0_resized;
                cv::resize(results_cam0[2].visualization, amp0_resized, cv::Size(panel_width, panel_height));
                amp0_resized.copyTo(dashboard(cv::Rect(0, panel_height * 2, panel_width, panel_height)));
            }
            if (results_cam1.size() >= 3) {
                cv::Mat amp1_resized;
                cv::resize(results_cam1[2].visualization, amp1_resized, cv::Size(panel_width, panel_height));
                amp1_resized.copyTo(dashboard(cv::Rect(panel_width, panel_height * 2, panel_width, panel_height)));
            }
            
            // Status panel on the right side (third column) - smaller
            cv::Mat status_panel(panel_height * 3, status_width, CV_8UC3, cv::Scalar(50, 50, 50));
            addStatusInfo(status_panel);
            status_panel.copyTo(dashboard(cv::Rect(panel_width * 2, 0, status_width, panel_height * 3)));
            
            // Add dual camera labels
            cv::putText(dashboard, "Cam0 Depth", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(dashboard, "Cam1 Depth", cv::Point(panel_width + 10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(dashboard, "Cam0 Confidence", cv::Point(10, panel_height + 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(dashboard, "Cam1 Confidence", cv::Point(panel_width + 10, panel_height + 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(dashboard, "Cam0 Amplitude", cv::Point(10, panel_height * 2 + 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(dashboard, "Cam1 Amplitude", cv::Point(panel_width + 10, panel_height * 2 + 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            // Status panel label removed to prevent overlap with tabs
            
            return dashboard;
        } else {
            // Single camera layout (2x2 grid) - optimized to remove extra space
            cv::Mat dashboard(600, 800, CV_8UC3, cv::Scalar(30, 30, 30));
            
            int panel_width = 400;
            int panel_height = 300;
            int status_width = 400;  // Full width status panel
            
            // Top row: Depth and Confidence
            if (results_cam0.size() >= 2) {
                // Depth view (top-left)
                cv::Mat depth_resized;
                cv::resize(results_cam0[0].visualization, depth_resized, cv::Size(panel_width, panel_height));
                
                // Draw crosshair if a measurement was taken
                if (click_x_cam0_ >= 0 && click_y_cam0_ >= 0) {
                    int display_x = (click_x_cam0_ * panel_width) / 240;
                    int display_y = (click_y_cam0_ * panel_height) / 180;
                    cv::drawMarker(depth_resized, cv::Point(display_x, display_y), cv::Scalar(0, 255, 255), 
                                   cv::MARKER_CROSS, 20, 2);
                }
                
                depth_resized.copyTo(dashboard(cv::Rect(0, 0, panel_width, panel_height)));
                
                // Confidence view (top-right)
                cv::Mat conf_resized;
                cv::resize(results_cam0[1].visualization, conf_resized, cv::Size(panel_width, panel_height));
                conf_resized.copyTo(dashboard(cv::Rect(panel_width, 0, panel_width, panel_height)));
            }
            
            // Bottom row: Amplitude and Status
            if (results_cam0.size() >= 3) {
                // Amplitude view (bottom-left)
                cv::Mat amp_resized;
                cv::resize(results_cam0[2].visualization, amp_resized, cv::Size(panel_width, panel_height));
                amp_resized.copyTo(dashboard(cv::Rect(0, panel_height, panel_width, panel_height)));
            }
            
            // Status panel (bottom-right) - full width
            cv::Mat status_panel(panel_height, status_width, CV_8UC3, cv::Scalar(50, 50, 50));
            addStatusInfo(status_panel);
            status_panel.copyTo(dashboard(cv::Rect(0, panel_height, status_width, panel_height)));
            
            // Add single camera labels
            cv::putText(dashboard, "Depth", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(dashboard, "Confidence", cv::Point(panel_width + 10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            cv::putText(dashboard, "Amplitude", cv::Point(10, panel_height + 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
            // Status panel label removed to prevent overlap with tabs
            
            return dashboard;
        }
    }
    
    void addStatusInfo(cv::Mat& status_panel) {
        int y_offset = 25;
        int line_height = 22;
        cv::Scalar text_color(255, 255, 255);
        cv::Scalar highlight_color(0, 255, 0);
        cv::Scalar recording_color(0, 0, 255);
        
        // Current label
        std::string label_text = "Label: " + current_label_;
        cv::putText(status_panel, label_text, cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.55, highlight_color, 1);
        y_offset += line_height;
        
        // Recording status with visual indicators
        if (labeling_saver_.isRecording()) {
            // Red filled circle for recording
            cv::circle(status_panel, cv::Point(20, y_offset - 5), 6, recording_color, -1);
            std::string recording_text = " REC";
            cv::putText(status_panel, recording_text, cv::Point(35, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.55, recording_color, 2);
            y_offset += line_height;
            
            std::string session_text = labeling_saver_.getCurrentSessionId();
            if (session_text.length() > 15) session_text = session_text.substr(0, 15) + "...";
            cv::putText(status_panel, session_text, cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.4, text_color, 1);
            y_offset += line_height;
            
            std::string frame_text = "Frames: " + std::to_string(labeling_saver_.getCurrentFrameCount());
            cv::putText(status_panel, frame_text, cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.4, text_color, 1);
            y_offset += line_height;
        } else {
            // Green empty circle for ready
            cv::circle(status_panel, cv::Point(20, y_offset - 5), 6, highlight_color, 2);
            std::string ready_text = " READY";
            cv::putText(status_panel, ready_text, cv::Point(35, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.55, highlight_color, 1);
            y_offset += line_height;
        }
        
        // Frame counter
        std::string fps_text = "Frame: " + std::to_string(frame_counter_);
        cv::putText(status_panel, fps_text, cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.4, text_color, 1);
        y_offset += line_height;
        
        // Camera status
        std::string cam_text = "Cameras: " + std::to_string(config_.enable_dual_camera ? 2 : 1);
        cv::putText(status_panel, cam_text, cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.4, text_color, 1);
        y_offset += line_height + 8;
        
        // Distance measurements
        cv::putText(status_panel, "Distance Measurements:", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(100, 200, 255), 1);
        y_offset += line_height;
        
        if (measured_distance_cam0_ >= 0) {
            std::stringstream ss;
            ss << "Cam0: " << std::fixed << std::setprecision(1) << measured_distance_cam0_ << " mm";
            if (click_x_cam0_ >= 0 && click_y_cam0_ >= 0) {
                ss << " @(" << click_x_cam0_ << "," << click_y_cam0_ << ")";
            }
            cv::putText(status_panel, ss.str(), cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(100, 200, 255), 1);
            y_offset += line_height;
        } else {
            cv::putText(status_panel, "Cam0: Click depth to measure", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.35, text_color, 1);
            y_offset += line_height;
        }
        
        if (config_.enable_dual_camera) {
            if (measured_distance_cam1_ >= 0) {
                std::stringstream ss;
                ss << "Cam1: " << std::fixed << std::setprecision(1) << measured_distance_cam1_ << " mm";
                if (click_x_cam1_ >= 0 && click_y_cam1_ >= 0) {
                    ss << " @(" << click_x_cam1_ << "," << click_y_cam1_ << ")";
                }
                cv::putText(status_panel, ss.str(), cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(100, 200, 255), 1);
                y_offset += line_height;
            } else {
                cv::putText(status_panel, "Cam1: Click depth to measure", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.35, text_color, 1);
                y_offset += line_height;
            }
        }
        y_offset += 8;
        
        // Confidence and Max Distance controls
        std::string conf_text = "Confidence: " + std::to_string(config_.confidence_threshold);
        cv::putText(status_panel, conf_text, cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.45, highlight_color, 1);
        y_offset += line_height;
        
        std::string max_dist_text = "Max Dist: " + std::to_string(config_.max_distance) + "mm";
        cv::putText(status_panel, max_dist_text, cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.45, highlight_color, 1);
        y_offset += line_height + 8;
        
        // Controls reminder - more compact
        cv::putText(status_panel, "Controls:", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.45, highlight_color, 1);
        y_offset += 18;
        cv::putText(status_panel, "1/2/3/4/5 - Set label", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.38, text_color, 1);
        y_offset += 16;
        cv::putText(status_panel, "SPACE - Record, P - Pause, Q - Quit", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.38, text_color, 1);
        y_offset += 16;
        cv::putText(status_panel, "+/- Confidence, </> Distance", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.38, text_color, 1);
        y_offset += 16;
        cv::putText(status_panel, "Click depth - Measure, R - Reset", cv::Point(10, y_offset), cv::FONT_HERSHEY_SIMPLEX, 0.38, text_color, 1);
    }
    
    bool handleKeyInput(int key, bool& paused) {
        switch (key) {
            case 'q':
            case 'Q':
                std::cout << "🛑 Shutting down..." << std::endl;
                if (labeling_saver_.isRecording()) {
                    labeling_saver_.endCaptureSession();
                }
                running_ = false;
                #ifdef WITH_OPEN3D
                pointcloud_running_ = false;
                #endif
                return true;
                
            case '1':
                current_label_ = "pickup";
                std::cout << "🏷️  Label set to: pickup" << std::endl;
                break;
                
            case '2':
                current_label_ = "touch";
                std::cout << "🏷️  Label set to: touch" << std::endl;
                break;
                
            case '3':
                current_label_ = "return";
                std::cout << "🏷️  Label set to: return" << std::endl;
                break;
                
            case '4':
                current_label_ = "no_gesture";
                std::cout << "🏷️  Label set to: no_gesture (background)" << std::endl;
                break;
                
            case '5':
                current_label_ = "hand_present";
                std::cout << "🏷️  Label set to: hand_present (no action)" << std::endl;
                break;
                
            case ' ':
                if (labeling_saver_.isRecording()) {
                    std::string session_id = labeling_saver_.endCaptureSession();
                    std::cout << "⏹️  Recording stopped: " << session_id << std::endl;
                } else {
                    std::string session_id = labeling_saver_.startCaptureSession(current_label_);
                    std::cout << "🔴 Recording started: " << session_id << " (" << current_label_ << ")" << std::endl;
                }
                break;
                
            case 'p':
            case 'P':
                paused = !paused;
                std::cout << (paused ? "⏸️  Paused" : "▶️  Resumed") << std::endl;
                break;
                
            case 'c':
            case 'C':
                adjustConfidenceThreshold();
                break;
                
            case '+':
            case '=':
                config_.confidence_threshold = std::min(100, config_.confidence_threshold + 5);
                analyzer_.updateConfidenceThreshold(config_.confidence_threshold);
                std::cout << "🔧 Confidence threshold: " << config_.confidence_threshold << std::endl;
                break;
                
            case '-':
            case '_':
                config_.confidence_threshold = std::max(0, config_.confidence_threshold - 5);
                analyzer_.updateConfidenceThreshold(config_.confidence_threshold);
                std::cout << "🔧 Confidence threshold: " << config_.confidence_threshold << std::endl;
                break;
                
            case '.':
            case '>':
                config_.max_distance = std::min(10000, config_.max_distance + 100);
                analyzer_.updateMaxDistance(config_.max_distance);
                std::cout << "🔧 Max distance: " << config_.max_distance << std::endl;
                break;
                
            case ',':
            case '<':
                config_.max_distance = std::max(100, config_.max_distance - 100);
                analyzer_.updateMaxDistance(config_.max_distance);
                std::cout << "🔧 Max distance: " << config_.max_distance << std::endl;
                break;
                
            case 'r':
            case 'R':
                // Reset distance measurements
                measured_distance_cam0_ = -1.0f;
                measured_distance_cam1_ = -1.0f;
                click_x_cam0_ = -1;
                click_y_cam0_ = -1;
                click_x_cam1_ = -1;
                click_y_cam1_ = -1;
                std::cout << "🔄 Distance measurements reset" << std::endl;
                break;
        }
        
        return false;
    }
    
    void adjustConfidenceThreshold() {
        std::cout << "Current confidence threshold: " << config_.confidence_threshold << std::endl;
        std::cout << "Enter new threshold (0-100): ";
        
        int new_threshold;
        std::cin >> new_threshold;
        
        if (new_threshold >= 0 && new_threshold <= 100) {
            config_.confidence_threshold = new_threshold;
            analyzer_.updateConfidenceThreshold(new_threshold);
            std::cout << "✅ Confidence threshold set to: " << new_threshold << std::endl;
        } else {
            std::cout << "❌ Invalid threshold. Must be between 0-100." << std::endl;
        }
    }
    
    #ifdef WITH_OPEN3D
    void runPointCloudViewer() {
        // Create visualizer positioned next to main window
        std::string window_title = config_.enable_dual_camera ? 
            "ToF Tool - 3D Point Clouds (Cam0: Left | Cam1: Right)" : 
            "ToF Tool - 3D Point Cloud";
        
        pointcloud_vis_ = std::make_shared<open3d::visualization::Visualizer>();
        if (!pointcloud_vis_->CreateVisualizerWindow(window_title, 1000, 600, 1500, 50)) {
            std::cerr << "Failed to create 3D viewer window" << std::endl;
            return;
        }
        
        // Create point cloud objects
        pcd0_ = std::make_shared<open3d::geometry::PointCloud>();
        pcd1_ = std::make_shared<open3d::geometry::PointCloud>();
        
        // Add small coordinate frame in corner for reference
        auto coordinate_frame = open3d::geometry::TriangleMesh::CreateCoordinateFrame(50.0); // 5cm axes
        
        // Position in bottom-left corner
        Eigen::Matrix4d corner_transform = Eigen::Matrix4d::Identity();
        corner_transform(0, 3) = -200.0;  // Left
        corner_transform(1, 3) = -150.0;  // Bottom
        corner_transform(2, 3) = -800.0;  // Near camera view
        coordinate_frame->Transform(corner_transform);
        
        pointcloud_vis_->AddGeometry(coordinate_frame);
        
        if (config_.enable_dual_camera) {
            std::cout << "🌐 Point Cloud Viewer: Dual camera mode" << std::endl;
            std::cout << "   📍 Cam0 (Blue tones): Left side" << std::endl;
            std::cout << "   📍 Cam1 (Green tones): Right side (offset +60cm)" << std::endl;
        } else {
            std::cout << "🌐 Point Cloud Viewer: Single camera mode" << std::endl;
        }
        
        bool geometry_added = false;
        
        while (pointcloud_running_ && pointcloud_vis_->PollEvents()) {
            // Get frames for point cloud generation
            ArducamFrameBuffer* frame0 = cam0_->requestFrame(100);
            ArducamFrameBuffer* frame1 = nullptr;
            
            if (config_.enable_dual_camera && cam1_) {
                frame1 = cam1_->requestFrame(100);
            }
            
            if (frame0) {
                // Generate point clouds
                generatePointCloud(frame0, pcd0_, 0);
                if (frame1) {
                    generatePointCloud(frame1, pcd1_, 1);
                }
                
                // Add or update geometries
                if (!geometry_added) {
                    pointcloud_vis_->AddGeometry(pcd0_);
                    if (config_.enable_dual_camera && frame1) {
                        pointcloud_vis_->AddGeometry(pcd1_);
                    }
                    geometry_added = true;
                } else {
                    pointcloud_vis_->UpdateGeometry(pcd0_);
                    if (config_.enable_dual_camera && frame1) {
                        pointcloud_vis_->UpdateGeometry(pcd1_);
                    }
                }
                
                pointcloud_vis_->UpdateRender();
                
                // Release frames
                cam0_->releaseFrame(frame0);
                if (frame1 && cam1_) {
                    cam1_->releaseFrame(frame1);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        
        pointcloud_vis_->DestroyVisualizerWindow();
    }
    
    void generatePointCloud(ArducamFrameBuffer* frame,
                           std::shared_ptr<open3d::geometry::PointCloud> pcd,
                           int camera_id) {
        // Get frame format
        FrameFormat format;
        frame->getFormat(FrameType::DEPTH_FRAME, format);
        
        // Get data pointers
        float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
        float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
        
        if (!depth_ptr || !confidence_ptr) return;
        
        // Clear previous points
        pcd->Clear();
        
        std::vector<Eigen::Vector3d> points;
        std::vector<Eigen::Vector3d> colors;
        
        // Use correct camera intrinsics (matching dual_pointcloud_viewer)
        // These values provide realistic distance scaling
        double fx = 200.0, fy = 200.0, cx = 120.0, cy = 90.0;
        
        // Create transformation matrix for coordinate system conversion
        Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
        transform << 1,  0,  0,  0,
                     0, -1,  0,  0,
                     0,  0, -1,  0,
                     0,  0,  0,  1;
        
        // Separate cameras side-by-side for clear visualization
        if (camera_id == 1) {
            transform(0, 3) = 600.0; // 60cm offset to the right - shows cameras side-by-side
        }
        
        // Improved filtering parameters (matching dual_pointcloud_viewer)
        const float min_depth = 100.0f;   // 10cm minimum
        const float max_depth = static_cast<float>(config_.max_distance);
        const float min_confidence = std::max(50.0f, static_cast<float>(config_.confidence_threshold));
        const int skip_pixels = 2;        // Skip every 2nd pixel for performance
        
        for (int y = skip_pixels; y < format.height - skip_pixels; y += skip_pixels) {
            for (int x = skip_pixels; x < format.width - skip_pixels; x += skip_pixels) {
                int idx = y * format.width + x;
                float depth = depth_ptr[idx];
                float confidence = confidence_ptr[idx];
                
                // Strict filtering for clean point cloud
                if (confidence < min_confidence || depth <= 0 || 
                    depth < min_depth || depth > max_depth) {
                    continue;
                }
                
                // Additional noise filtering - check neighboring pixels
                bool valid_neighbors = true;
                const float depth_tolerance = depth * 0.1f;  // 10% tolerance
                
                // Check 3x3 neighborhood for consistency
                for (int dy = -1; dy <= 1 && valid_neighbors; dy++) {
                    for (int dx = -1; dx <= 1 && valid_neighbors; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        
                        int ny = y + dy;
                        int nx = x + dx;
                        if (ny >= 0 && ny < format.height && nx >= 0 && nx < format.width) {
                            int neighbor_idx = ny * format.width + nx;
                            float neighbor_depth = depth_ptr[neighbor_idx];
                            float neighbor_confidence = confidence_ptr[neighbor_idx];
                            
                            if (neighbor_confidence < min_confidence * 0.7f || 
                                std::abs(neighbor_depth - depth) > depth_tolerance) {
                                valid_neighbors = false;
                            }
                        }
                    }
                }
                
                if (!valid_neighbors) continue;
                
                // Convert to 3D coordinates - keep depth in mm for realistic scaling
                double z = depth;  // Keep depth in millimeters
                double x3d = (x - cx) * z / fx;
                double y3d = (y - cy) * z / fy;
                
                // Apply coordinate system transformation
                Eigen::Vector4d point_homogeneous(x3d, y3d, z, 1.0);
                Eigen::Vector4d transformed_point = transform * point_homogeneous;
                
                points.push_back(transformed_point.head<3>());
                
                // Improved coloring based on depth and confidence (matching dual_pointcloud_viewer)
                double depth_normalized = std::min(1.0, static_cast<double>(depth - min_depth) / static_cast<double>(max_depth - min_depth));
                double confidence_normalized = std::min(1.0, static_cast<double>(confidence) / 100.0);
                
                if (camera_id == 0) {
                    // Camera 0: Blue to red gradient with confidence intensity
                    double intensity = confidence_normalized * 0.8 + 0.2;
                    colors.push_back(Eigen::Vector3d(
                        depth_normalized * intensity,           // Red component
                        0.1 * intensity,                        // Green component  
                        (1.0 - depth_normalized) * intensity    // Blue component
                    ));
                } else {
                    // Camera 1: Green to red gradient with confidence intensity
                    double intensity = confidence_normalized * 0.8 + 0.2;
                    colors.push_back(Eigen::Vector3d(
                        depth_normalized * intensity,           // Red component
                        (1.0 - depth_normalized) * intensity,   // Green component
                        0.1 * intensity                         // Blue component
                    ));
                }
            }
        }
        
        // Update point cloud
        pcd->points_ = points;
        pcd->colors_ = colors;
        
        // Apply statistical outlier removal if we have enough points
        if (points.size() > 100) {
            pcd->RemoveStatisticalOutliers(20, 2.0);
        }
        
        // Optional: Apply radius outlier removal for very noisy data
        if (points.size() > 500) {
            pcd->RemoveRadiusOutliers(10, 20.0);
        }
    }
    #endif
    
    void cleanup() {
        if (cam0_) {
            cam0_->stop();
        }
        if (cam1_) {
            cam1_->stop();
        }
        
        cv::destroyAllWindows();
        std::cout << "✅ Cleanup completed" << std::endl;
    }
};

// Main function
int main(int argc, char* argv[]) {
    Config config;
    
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "c:d:sv")) != -1) {
        switch (opt) {
            case 'c':
                config.confidence_threshold = std::atoi(optarg);
                break;
            case 'd':
                config.max_distance = std::atoi(optarg);
                break;
            case 's':
                config.enable_dual_camera = false;
                break;
            case 'v':
                config.verbose = true;
                break;
            default:
                std::cerr << "Usage: " << argv[0] << " [-c confidence] [-d max_distance] [-s single_camera] [-v verbose]" << std::endl;
                return 1;
        }
    }
    
    std::cout << "🏷️  ToF Data Capture & Labeling Tool" << std::endl;
    std::cout << "📊 Configuration:" << std::endl;
    std::cout << "   Confidence threshold: " << config.confidence_threshold << "%" << std::endl;
    std::cout << "   Max distance: " << config.max_distance << "mm" << std::endl;
    std::cout << "   Dual camera: " << (config.enable_dual_camera ? "Enabled" : "Disabled") << std::endl;
    std::cout << "   Verbose: " << (config.verbose ? "Enabled" : "Disabled") << std::endl;
    std::cout << std::endl;
    
    DataCaptureLabelingTool tool(config);
    
    if (!tool.initialize()) {
        std::cerr << "❌ Failed to initialize tool" << std::endl;
        return 1;
    }
    
    tool.run();
    
    return 0;
}