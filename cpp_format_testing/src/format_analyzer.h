#pragma once

#include "common.h"
#include <vector>

class FormatAnalyzer {
public:
    FormatAnalyzer(const Config& config);
    
    // Analyze all four formats for a single frame
    std::vector<FormatResults> analyzeFrame(ArducamFrameBuffer* frame, int camera_id);
    
    // Analyze only first three formats (Depth, Confidence, Amplitude) - for main display
    std::vector<FormatResults> analyzeFrameThreeFormats(ArducamFrameBuffer* frame, int camera_id);
    
    // Individual format analysis
    FormatResults analyzeDepth(ArducamFrameBuffer* frame, int camera_id);
    FormatResults analyzeConfidence(ArducamFrameBuffer* frame, int camera_id);
    FormatResults analyzeAmplitude(ArducamFrameBuffer* frame, int camera_id);
    FormatResults analyzePointCloud(ArducamFrameBuffer* frame, int camera_id);
    
    // Configuration update methods
    void updateMaxDistance(int max_distance);
    void updateConfidenceThreshold(int confidence_threshold);
    void updateConfig(const Config& config);

private:
    Config config_;
    
    // Helper functions
    void calculateStatistics(const cv::Mat& data, FormatResults& results);
    cv::Mat createVisualization(const cv::Mat& data, const std::string& format_name);
    cv::Mat createDepthVisualization(const cv::Mat& depth, const cv::Mat& confidence);
    cv::Mat createConfidenceVisualization(const cv::Mat& confidence);
    cv::Mat createAmplitudeVisualization(const cv::Mat& amplitude);
    cv::Mat createPointCloudVisualization(const cv::Mat& depth, const cv::Mat& confidence, int camera_id);
};
