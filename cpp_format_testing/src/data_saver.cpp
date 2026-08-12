#include "data_saver.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <cstdio>
#include <sys/statvfs.h>
#include <cerrno>
#include <cstring>

DataSaver::DataSaver(const Config& config) : config_(config), session_frame_count_(0) {
    // Use the specified directory structure
    base_output_dir_ = "/home/dev/Arducam_tof_camera/cpp_format_testing/saved_data";
    three_formats_dir_ = base_output_dir_ + "/three_formats";
    pointcloud_dir_ = base_output_dir_ + "/pointcloud";
}

bool DataSaver::initializeOutputDirectory() {
    if (!config_.save_enabled) {
        std::cout << "💾 Data saving is disabled" << std::endl;
        return true;
    }
    
    std::cout << "📁 Initializing organized save directories..." << std::endl;
    std::cout << "   📊 Three formats: " << three_formats_dir_ << std::endl;
    std::cout << "   🌐 Point cloud: " << pointcloud_dir_ << std::endl;
    
    // Create main directory structure
    if (!Utils::createDirectory(base_output_dir_)) {
        std::cerr << "❌ Failed to create base directory: " << base_output_dir_ << std::endl;
        return false;
    }
    
    if (!Utils::createDirectory(three_formats_dir_)) {
        std::cerr << "❌ Failed to create three_formats directory: " << three_formats_dir_ << std::endl;
        return false;
    }
    
    if (!Utils::createDirectory(pointcloud_dir_)) {
        std::cerr << "❌ Failed to create pointcloud directory: " << pointcloud_dir_ << std::endl;
        return false;
    }
    
    std::cout << "✅ Organized directory structure ready" << std::endl;
    
    // Test write permissions
    std::string test_file = three_formats_dir_ + "/write_test.tmp";
    std::ofstream test(test_file);
    if (test.is_open()) {
        test << "test";
        test.close();
        std::remove(test_file.c_str());
        std::cout << "✅ Write permissions verified" << std::endl;
    } else {
        std::cerr << "❌ No write permissions for directory: " << three_formats_dir_ << std::endl;
        return false;
    }
    
    return true;
}

// Helper function to check available disk space
bool DataSaver::checkDiskSpace(const std::string& path, size_t required_bytes) {
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        std::cerr << "⚠️  Cannot check disk space: " << std::strerror(errno) << std::endl;
        return true; // Assume OK if we can't check
    }
    
    size_t available_bytes = stat.f_bavail * stat.f_frsize;
    size_t required_mb = required_bytes / (1024 * 1024);
    size_t available_mb = available_bytes / (1024 * 1024);
    
    std::cout << "💾 Disk space: " << available_mb << " MB available" << std::endl;
    
    if (available_bytes < required_bytes) {
        std::cerr << "❌ Insufficient disk space!" << std::endl;
        std::cerr << "   Required: " << required_mb << " MB, Available: " << available_mb << " MB" << std::endl;
        return false;
    }
    
    if (available_bytes < required_bytes * 2) {
        std::cout << "⚠️  Low disk space warning: " << available_mb << " MB remaining" << std::endl;
    }
    
    return true;
}

bool DataSaver::saveCurrentFrame(const std::vector<FormatResults>& results_cam0,
                                const std::vector<FormatResults>& results_cam1,
                                ArducamFrameBuffer* frame0,
                                ArducamFrameBuffer* frame1,
                                int frame_number) {
    if (!config_.save_enabled) {
        std::cout << "⚠️  Data saving is disabled" << std::endl;
        return false;
    }
    
    // Immediate feedback
    std::cout << "💾 Starting save operation for frame " << frame_number << "..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    session_frame_count_++;
    std::string timestamp = Utils::getCurrentTimestamp();
    
    // Create timestamped folder in three_formats directory
    std::string frame_dir = three_formats_dir_ + "/frame_" + timestamp;
    
    std::cout << "📁 Target: " << frame_dir << std::endl;
    
    // Quick disk space check
    size_t estimated_size = 10 * 1024 * 1024; // 10MB estimate
    if (!checkDiskSpace(three_formats_dir_, estimated_size)) {
        std::cerr << "❌ Save aborted due to insufficient disk space" << std::endl;
        return false;
    }
    
    // Progress: Step 1/4 - Directory creation
    std::cout << "[1/4] 📁 Creating directories..." << std::flush;
    
    if (!Utils::createDirectory(frame_dir) ||
        !Utils::createDirectory(frame_dir + "/raw_data") ||
        !Utils::createDirectory(frame_dir + "/visualizations") ||
        !Utils::createDirectory(frame_dir + "/parameters")) {
        std::cerr << " ❌ Failed" << std::endl;
        return false;
    }
    std::cout << " ✅" << std::endl;
    
    // Temporarily change current_session_dir_ for this frame
    std::string original_session_dir = current_session_dir_;
    current_session_dir_ = frame_dir;
    
    bool success = true;
    int total_steps = 2 + (config_.enable_dual_camera && !results_cam1.empty() ? 1 : 0);
    int current_step = 2;
    
    // Progress: Step 2/X - Camera 0 data
    if (!results_cam0.empty()) {
        std::cout << "[" << current_step << "/" << (total_steps + 1) << "] 📷 Saving Camera 0 data..." << std::flush;
        success &= saveSingleCameraData(results_cam0, frame0, 0, frame_number, timestamp);
        std::cout << (success ? " ✅" : " ❌") << std::endl;
        current_step++;
    }
    
    // Progress: Step 3/X - Camera 1 data (if enabled)
    if (!results_cam1.empty() && config_.enable_dual_camera) {
        std::cout << "[" << current_step << "/" << (total_steps + 1) << "] 📷 Saving Camera 1 data..." << std::flush;
        success &= saveSingleCameraData(results_cam1, frame1, 1, frame_number, timestamp);
        std::cout << (success ? " ✅" : " ❌") << std::endl;
        current_step++;
    }
    
    // Progress: Final step - Analysis report
    std::cout << "[" << (total_steps + 1) << "/" << (total_steps + 1) << "] 📊 Saving analysis report..." << std::flush;
    
    Json::Value report = createAnalysisReport(results_cam0, results_cam1, frame_number);
    std::string report_file = frame_dir + "/parameters/frame_" + 
                             std::to_string(frame_number) + "_" + timestamp + "_analysis.json";
    
    std::ofstream file(report_file);
    if (file.is_open()) {
        file << report;
        file.close();
        std::cout << " ✅" << std::endl;
    } else {
        std::cout << " ❌" << std::endl;
        success = false;
    }
    
    // Restore original session directory
    current_session_dir_ = original_session_dir;
    
    // Final summary with timing
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    if (success) {
        std::cout << "✅ Frame " << frame_number << " saved successfully in " << duration.count() << "ms!" << std::endl;
        std::cout << "   📁 Location: " << frame_dir << std::endl;
        std::cout << "   📊 Session total: " << session_frame_count_ << " frames saved" << std::endl;
    } else {
        std::cout << "❌ Save failed after " << duration.count() << "ms" << std::endl;
    }
    
    return success;
}

bool DataSaver::saveSingleCameraData(const std::vector<FormatResults>& results,
                                    ArducamFrameBuffer* frame,
                                    int camera_id,
                                    int frame_number,
                                    const std::string& timestamp) {
    std::string frame_id = "cam" + std::to_string(camera_id) + "_frame_" + 
                          std::to_string(frame_number) + "_" + timestamp;
    
    bool success = true;
    size_t total_bytes_saved = 0;
    
    std::cout << "   📷 Processing Camera " << camera_id << " data..." << std::endl;
    
    // Save raw frame data
    if (frame) {
        size_t raw_bytes = 0;
        success &= saveRawData(frame, camera_id, frame_number, timestamp, raw_bytes);
        total_bytes_saved += raw_bytes;
        if (success && raw_bytes > 0) {
            std::cout << "      ✅ Raw data: " << (raw_bytes / 1024.0) << " KB" << std::endl;
        } else {
            std::cout << "      ❌ Failed to save raw data" << std::endl;
        }
    }
    
    // Save visualizations
    size_t vis_bytes = 0;
    bool vis_success = saveVisualizations(results, camera_id, frame_number, timestamp, vis_bytes);
    success &= vis_success;
    total_bytes_saved += vis_bytes;
    if (vis_success && vis_bytes > 0) {
        std::cout << "      ✅ Visualizations: " << (vis_bytes / 1024.0) << " KB (" << results.size() << " images)" << std::endl;
    } else {
        std::cout << "      ❌ Failed to save visualizations" << std::endl;
    }
    
    // Save parameters
    size_t param_bytes = 0;
    bool param_success = saveParameters(results, camera_id, frame_number, timestamp, param_bytes);
    success &= param_success;
    total_bytes_saved += param_bytes;
    if (param_success && param_bytes > 0) {
        std::cout << "      ✅ Parameters: " << (param_bytes / 1024.0) << " KB" << std::endl;
    } else {
        std::cout << "      ❌ Failed to save parameters" << std::endl;
    }
    
    if (success) {
        std::cout << "   📊 CAM" << camera_id << " total: " << (total_bytes_saved / 1024.0) << " KB saved" << std::endl;
    }
    
    return success;
}

bool DataSaver::saveRawData(ArducamFrameBuffer* frame, int camera_id, int frame_number, 
                           const std::string& timestamp, size_t& bytes_saved) {
    bytes_saved = 0;
    if (!frame) return false;
    
    std::string frame_id = "cam" + std::to_string(camera_id) + "_frame_" + 
                          std::to_string(frame_number) + "_" + timestamp;
    
    // Get frame format
    FrameFormat format;
    frame->getFormat(FrameType::DEPTH_FRAME, format);
    
    size_t data_size = format.width * format.height * sizeof(float);
    bool success = true;
    
    // Save depth data
    float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
    if (depth_ptr) {
        std::string depth_file = current_session_dir_ + "/raw_data/" + 
                                frame_id + "_depth.raw";
        std::ofstream file(depth_file, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<char*>(depth_ptr), data_size);
            file.close();
            
            // Verify file was written
            std::ifstream verify(depth_file, std::ios::binary | std::ios::ate);
            if (verify.is_open()) {
                size_t file_size = verify.tellg();
                if (file_size == data_size) {
                    bytes_saved += file_size;
                } else {
                    success = false;
                }
                verify.close();
            } else {
                success = false;
            }
        } else {
            success = false;
        }
    }
    
    // Save confidence data
    float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
    if (confidence_ptr) {
        std::string confidence_file = current_session_dir_ + "/raw_data/" + 
                                     frame_id + "_confidence.raw";
        std::ofstream file(confidence_file, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<char*>(confidence_ptr), data_size);
            file.close();
            
            // Verify file was written
            std::ifstream verify(confidence_file, std::ios::binary | std::ios::ate);
            if (verify.is_open()) {
                size_t file_size = verify.tellg();
                if (file_size == data_size) {
                    bytes_saved += file_size;
                } else {
                    success = false;
                }
                verify.close();
            } else {
                success = false;
            }
        } else {
            success = false;
        }
    }
    
    return success;
}

bool DataSaver::saveVisualizations(const std::vector<FormatResults>& results,
                                  int camera_id, int frame_number,
                                  const std::string& timestamp, size_t& bytes_saved) {
    bytes_saved = 0;
    std::string frame_id = "cam" + std::to_string(camera_id) + "_frame_" + 
                          std::to_string(frame_number) + "_" + timestamp;
    
    bool success = true;
    
    for (const auto& result : results) {
        std::string vis_file = current_session_dir_ + "/visualizations/" + 
                              frame_id + "_" + result.format_name + ".png";
        if (!result.visualization.empty()) {
            // Create a copy without color profile to avoid PNG warnings
            cv::Mat clean_image;
            if (result.visualization.channels() == 3) {
                cv::cvtColor(result.visualization, clean_image, cv::COLOR_BGR2RGB);
                cv::cvtColor(clean_image, clean_image, cv::COLOR_RGB2BGR);
            } else {
                result.visualization.copyTo(clean_image);
            }
            
            // Save with minimal PNG parameters
            std::vector<int> png_params = {cv::IMWRITE_PNG_COMPRESSION, 0};
            bool write_success = cv::imwrite(vis_file, clean_image, png_params);
            
            if (write_success) {
                // Verify file was written and get size
                std::ifstream verify(vis_file, std::ios::binary | std::ios::ate);
                if (verify.is_open()) {
                    size_t file_size = verify.tellg();
                    if (file_size > 0) {
                        bytes_saved += file_size;
                    } else {
                        success = false;
                    }
                    verify.close();
                } else {
                    success = false;
                }
            } else {
                success = false;
            }
        }
    }
    
    return success;
}

bool DataSaver::saveParameters(const std::vector<FormatResults>& results,
                              int camera_id, int frame_number,
                              const std::string& timestamp, size_t& bytes_saved) {
    bytes_saved = 0;
    std::string frame_id = "cam" + std::to_string(camera_id) + "_frame_" + 
                          std::to_string(frame_number) + "_" + timestamp;
    
    Json::Value params;
    
    for (const auto& result : results) {
        Json::Value format_params;
        format_params["processing_time_ms"] = result.processing_time_ms;
        format_params["valid_pixels"] = result.valid_pixels;
        format_params["coverage_percentage"] = result.coverage_percentage;
        format_params["noise_level"] = result.noise_level;
        format_params["min_value"] = result.min_value;
        format_params["max_value"] = result.max_value;
        format_params["mean_value"] = result.mean_value;
        format_params["std_value"] = result.std_value;
        
        params[result.format_name] = format_params;
    }
    
    std::string params_file = current_session_dir_ + "/parameters/" + 
                             frame_id + "_parameters.json";
    
    std::ofstream file(params_file);
    if (file.is_open()) {
        file << params;
        file.close();
        
        // Verify file was written and get size
        std::ifstream verify(params_file, std::ios::binary | std::ios::ate);
        if (verify.is_open()) {
            size_t file_size = verify.tellg();
            if (file_size > 0) {
                bytes_saved = file_size;
                verify.close();
                return true;
            }
            verify.close();
        }
    }
    
    return false;
}

Json::Value DataSaver::createAnalysisReport(const std::vector<FormatResults>& results_cam0,
                                           const std::vector<FormatResults>& results_cam1,
                                           int frame_number) {
    Json::Value report;
    
    // Frame info
    report["frame_info"]["frame_number"] = frame_number;
    report["frame_info"]["confidence_threshold"] = config_.confidence_threshold;
    report["frame_info"]["max_distance"] = config_.max_distance;
    report["frame_info"]["timestamp"] = Utils::getCurrentTimestamp();
    
    // Camera 0 results
    if (!results_cam0.empty()) {
        Json::Value cam0_data;
        for (const auto& result : results_cam0) {
            Json::Value format_data;
            format_data["processing_time_ms"] = result.processing_time_ms;
            format_data["valid_pixels"] = result.valid_pixels;
            format_data["coverage_percentage"] = result.coverage_percentage;
            format_data["noise_level"] = result.noise_level;
            format_data["min_value"] = result.min_value;
            format_data["max_value"] = result.max_value;
            format_data["mean_value"] = result.mean_value;
            format_data["std_value"] = result.std_value;
            
            cam0_data[result.format_name] = format_data;
        }
        report["camera_0"] = cam0_data;
    }
    
    // Camera 1 results
    if (!results_cam1.empty() && config_.enable_dual_camera) {
        Json::Value cam1_data;
        for (const auto& result : results_cam1) {
            Json::Value format_data;
            format_data["processing_time_ms"] = result.processing_time_ms;
            format_data["valid_pixels"] = result.valid_pixels;
            format_data["coverage_percentage"] = result.coverage_percentage;
            format_data["noise_level"] = result.noise_level;
            format_data["min_value"] = result.min_value;
            format_data["max_value"] = result.max_value;
            format_data["mean_value"] = result.mean_value;
            format_data["std_value"] = result.std_value;
            
            cam1_data[result.format_name] = format_data;
        }
        report["camera_1"] = cam1_data;
    }
    
    return report;
}
