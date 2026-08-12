#pragma once

#include "common.h"
#include "format_analyzer.h"
#include <fstream>
#include <json/json.h>
#include <chrono>

class DataSaver {
public:
    DataSaver(const Config& config);
    
    // Save data when 's' key is pressed
    bool saveCurrentFrame(const std::vector<FormatResults>& results_cam0,
                         const std::vector<FormatResults>& results_cam1,
                         ArducamFrameBuffer* frame0,
                         ArducamFrameBuffer* frame1,
                         int frame_number);
    
    // Initialize output directory
    bool initializeOutputDirectory();

private:
    Config config_;
    std::string base_output_dir_;
    std::string three_formats_dir_;
    std::string pointcloud_dir_;
    std::string current_session_dir_;
    int session_frame_count_;
    
    // Helper functions
    bool saveSingleCameraData(const std::vector<FormatResults>& results,
                             ArducamFrameBuffer* frame,
                             int camera_id,
                             int frame_number,
                             const std::string& timestamp);
    
    bool saveRawData(ArducamFrameBuffer* frame, int camera_id, int frame_number, 
                    const std::string& timestamp, size_t& bytes_saved);
    
    bool saveVisualizations(const std::vector<FormatResults>& results,
                           int camera_id, int frame_number,
                           const std::string& timestamp, size_t& bytes_saved);
    
    bool saveParameters(const std::vector<FormatResults>& results,
                       int camera_id, int frame_number,
                       const std::string& timestamp, size_t& bytes_saved);
    
    Json::Value createAnalysisReport(const std::vector<FormatResults>& results_cam0,
                                   const std::vector<FormatResults>& results_cam1,
                                   int frame_number);
    
    // Helper function to check disk space
    bool checkDiskSpace(const std::string& path, size_t required_bytes);
};
