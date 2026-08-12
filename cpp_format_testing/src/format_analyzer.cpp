#include "format_analyzer.h"
#include <numeric>
#include <algorithm>

FormatAnalyzer::FormatAnalyzer(const Config& config) : config_(config) {}

void FormatAnalyzer::updateMaxDistance(int max_distance) {
    config_.max_distance = max_distance;
}

void FormatAnalyzer::updateConfidenceThreshold(int confidence_threshold) {
    config_.confidence_threshold = confidence_threshold;
}

void FormatAnalyzer::updateConfig(const Config& config) {
    config_ = config;
}

std::vector<FormatResults> FormatAnalyzer::analyzeFrame(ArducamFrameBuffer* frame, int camera_id) {
    std::vector<FormatResults> results;
    
    try {
        results.push_back(analyzeDepth(frame, camera_id));
        results.push_back(analyzeConfidence(frame, camera_id));
        results.push_back(analyzeAmplitude(frame, camera_id));
        results.push_back(analyzePointCloud(frame, camera_id));
    } catch (const std::exception& e) {
        std::cerr << "Error analyzing frame from camera " << camera_id << ": " << e.what() << std::endl;
    }
    
    return results;
}

std::vector<FormatResults> FormatAnalyzer::analyzeFrameThreeFormats(ArducamFrameBuffer* frame, int camera_id) {
    std::vector<FormatResults> results;
    
    try {
        results.push_back(analyzeDepth(frame, camera_id));
        results.push_back(analyzeConfidence(frame, camera_id));
        results.push_back(analyzeAmplitude(frame, camera_id));
        // Point cloud analysis removed from main display
    } catch (const std::exception& e) {
        std::cerr << "Error analyzing frame from camera " << camera_id << ": " << e.what() << std::endl;
    }
    
    return results;
}

FormatResults FormatAnalyzer::analyzeDepth(ArducamFrameBuffer* frame, int camera_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    FormatResults results;
    results.format_name = "Depth";
    results.timestamp = std::chrono::system_clock::now();
    
    // Get frame format
    FrameFormat format;
    frame->getFormat(FrameType::DEPTH_FRAME, format);
    
    // Get data pointers
    float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
    float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
    
    if (!depth_ptr || !confidence_ptr) {
        throw std::runtime_error("Failed to get depth or confidence data");
    }
    
    // Create OpenCV matrices
    cv::Mat depth_mat(format.height, format.width, CV_32F, depth_ptr);
    cv::Mat confidence_mat(format.height, format.width, CV_32F, confidence_ptr);
    
    results.raw_data = depth_mat.clone();
    
    // Calculate statistics
    calculateStatistics(depth_mat, results);
    
    // Create visualization
    results.visualization = createDepthVisualization(depth_mat, confidence_mat);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    results.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    return results;
}

FormatResults FormatAnalyzer::analyzeConfidence(ArducamFrameBuffer* frame, int camera_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    FormatResults results;
    results.format_name = "Confidence";
    results.timestamp = std::chrono::system_clock::now();
    
    // Get frame format
    FrameFormat format;
    frame->getFormat(FrameType::DEPTH_FRAME, format);
    
    // Get confidence data directly from sensor (official Arducam method)
    float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
    
    if (!confidence_ptr) {
        throw std::runtime_error("Failed to get confidence data");
    }
    
    // Create OpenCV matrix directly from sensor data
    cv::Mat confidence_mat(format.height, format.width, CV_32F, confidence_ptr);
    results.raw_data = confidence_mat.clone();
    
    // Debug: Print confidence data statistics
    if (config_.verbose && camera_id == 0) {
        double min_val, max_val;
        cv::minMaxLoc(confidence_mat, &min_val, &max_val);
        cv::Scalar mean_val = cv::mean(confidence_mat);
        std::cout << "CONFIDENCE - Min: " << min_val << ", Max: " << max_val 
                  << ", Mean: " << mean_val[0] 
                  << " (Direct from ToF sensor - official method)" << std::endl;
    }
    
    // Calculate statistics
    calculateStatistics(confidence_mat, results);
    
    // Create visualization (black and white)
    results.visualization = createConfidenceVisualization(confidence_mat);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    results.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    return results;
}

FormatResults FormatAnalyzer::analyzeAmplitude(ArducamFrameBuffer* frame, int camera_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    FormatResults results;
    results.format_name = "Amplitude";
    results.timestamp = std::chrono::system_clock::now();
    
    // Get frame format
    FrameFormat format;
    frame->getFormat(FrameType::DEPTH_FRAME, format);
    
    // Get amplitude data directly from sensor (official Arducam method)
    float* amplitude_ptr = (float*)frame->getData(FrameType::AMPLITUDE_FRAME);
    float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
    
    if (!amplitude_ptr || !confidence_ptr) {
        throw std::runtime_error("Failed to get amplitude or confidence data from sensor");
    }
    
    // Create amplitude matrix from sensor data
    cv::Mat amplitude_mat(format.height, format.width, CV_32F, amplitude_ptr);
    cv::Mat confidence_mat(format.height, format.width, CV_32F, confidence_ptr);
    
    if (config_.verbose && camera_id == 0) {
        double min_val, max_val;
        cv::minMaxLoc(amplitude_mat, &min_val, &max_val);
        cv::Scalar mean_val = cv::mean(amplitude_mat);
        std::cout << "AMPLITUDE - Min: " << min_val << ", Max: " << max_val 
                  << ", Mean: " << mean_val[0] 
                  << " (Direct from ToF sensor - official method)" << std::endl;
        
        // Compare with confidence to check if they're identical
        cv::Mat diff;
        cv::absdiff(amplitude_mat, confidence_mat, diff);
        double max_diff;
        cv::minMaxLoc(diff, nullptr, &max_diff);
        std::cout << "Max difference between AMPLITUDE and CONFIDENCE: " << max_diff << std::endl;
        if (max_diff < 0.001) {
            std::cout << "⚠️ WARNING: AMPLITUDE and CONFIDENCE data are IDENTICAL!" << std::endl;
        }
    }
    
    results.raw_data = amplitude_mat.clone();
    
    // Calculate statistics
    calculateStatistics(amplitude_mat, results);
    
    // Create visualization with different appearance than confidence
    results.visualization = createAmplitudeVisualization(amplitude_mat);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    results.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    return results;
}

FormatResults FormatAnalyzer::analyzePointCloud(ArducamFrameBuffer* frame, int camera_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    FormatResults results;
    results.format_name = "PointCloud";
    results.timestamp = std::chrono::system_clock::now();
    
    // Get frame format
    FrameFormat format;
    frame->getFormat(FrameType::DEPTH_FRAME, format);
    
    // Get data pointers
    float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
    float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
    
    if (!depth_ptr || !confidence_ptr) {
        throw std::runtime_error("Failed to get depth or confidence data for point cloud");
    }
    
    // Create OpenCV matrices
    cv::Mat depth_mat(format.height, format.width, CV_32F, depth_ptr);
    cv::Mat confidence_mat(format.height, format.width, CV_32F, confidence_ptr);
    
    results.raw_data = depth_mat.clone();
    
    // Calculate statistics
    calculateStatistics(depth_mat, results);
    
    // Create 2D visualization showing point density
    results.visualization = createPointCloudVisualization(depth_mat, confidence_mat, camera_id);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    results.processing_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    return results;
}

void FormatAnalyzer::calculateStatistics(const cv::Mat& data, FormatResults& results) {
    cv::Mat mask = data > 0;  // Valid data mask
    
    if (cv::countNonZero(mask) == 0) {
        results.valid_pixels = 0;
        results.coverage_percentage = 0.0;
        results.min_value = results.max_value = results.mean_value = results.std_value = 0.0;
        results.noise_level = 0.0;
        return;
    }
    
    results.valid_pixels = cv::countNonZero(mask);
    results.coverage_percentage = (double)results.valid_pixels / (data.rows * data.cols) * 100.0;
    
    double min_val, max_val;
    cv::minMaxLoc(data, &min_val, &max_val, nullptr, nullptr, mask);
    results.min_value = min_val;
    results.max_value = max_val;
    
    cv::Scalar mean, stddev;
    cv::meanStdDev(data, mean, stddev, mask);
    results.mean_value = mean[0];
    results.std_value = stddev[0];
    results.noise_level = stddev[0];  // Use standard deviation as noise level
}

cv::Mat FormatAnalyzer::createDepthVisualization(const cv::Mat& depth, const cv::Mat& confidence) {
    cv::Mat result;
    
    // Convert depth to 8-bit with dynamic scaling (better visualization)
    cv::Mat depth_8u;
    depth.convertTo(depth_8u, CV_8U, 255.0 / config_.max_distance, 0);
    cv::applyColorMap(depth_8u, result, cv::COLORMAP_RAINBOW);
    
    // Apply confidence filter
    result = Utils::applyConfidenceFilter(result, confidence, config_.confidence_threshold);
    
    return result;
}

cv::Mat FormatAnalyzer::createConfidenceVisualization(const cv::Mat& confidence) {
    cv::Mat result;
    
    // Use advanced test method for confidence processing
    // First normalize to 0-1 range, then scale to display range
    cv::Mat confidence_normalized;
    
    // Method 1: Direct scaling like preview_depth.cpp
    confidence.convertTo(confidence_normalized, CV_8U, 255.0 / 1024.0, 0);
    
    // Method 2: Advanced processing like test.cpp (commented for now)
    // cv::Mat confidence_temp;
    // confidence.convertTo(confidence_temp, CV_32F, 1.0 / 255.0, 0);
    // confidence_temp.convertTo(confidence_normalized, CV_8U, 255.0, 0);
    
    // Convert to 3-channel grayscale
    cv::cvtColor(confidence_normalized, result, cv::COLOR_GRAY2BGR);
    
    return result;
}

cv::Mat FormatAnalyzer::createAmplitudeVisualization(const cv::Mat& amplitude) {
    cv::Mat result;
    
    // Use advanced amplitude processing for better visualization
    cv::Mat amplitude_normalized;
    
    // Method 1: Basic scaling (official website method)
    amplitude.convertTo(amplitude_normalized, CV_8U, 255.0/1024.0, 0);
    
    // Method 2: Dynamic range adjustment (like advanced test.cpp)
    // This provides better contrast and visibility
    double min_val, max_val;
    cv::minMaxLoc(amplitude, &min_val, &max_val);
    if (max_val > min_val) {
        // Use dynamic scaling for better contrast
        double scale = 255.0 / (max_val - min_val);
        double offset = -min_val * scale;
        amplitude.convertTo(amplitude_normalized, CV_8U, scale, offset);
    }
    
    // Convert to 3-channel grayscale
    cv::cvtColor(amplitude_normalized, result, cv::COLOR_GRAY2BGR);
    
    return result;
}

cv::Mat FormatAnalyzer::createPointCloudVisualization(const cv::Mat& depth, const cv::Mat& confidence, int camera_id) {
    cv::Mat result = cv::Mat::zeros(depth.size(), CV_8UC3);
    
    // Count valid points
    cv::Mat valid_mask = (confidence > config_.confidence_threshold) & 
                         (depth > 0) & (depth < config_.max_distance);
    int valid_points = cv::countNonZero(valid_mask);
    
    // Create density visualization
    cv::Mat density_map = cv::Mat::zeros(depth.size(), CV_8U);
    density_map.setTo(255, valid_mask);
    
    // Apply some visualization based on depth
    for (int y = 0; y < depth.rows; ++y) {
        for (int x = 0; x < depth.cols; ++x) {
            if (valid_mask.at<uchar>(y, x)) {
                float d = depth.at<float>(y, x);
                uchar intensity = (uchar)(255.0 * (1.0 - d / config_.max_distance));
                
                // Different colors for different cameras
                if (camera_id == 0) {
                    result.at<cv::Vec3b>(y, x) = cv::Vec3b(intensity, 0, 255 - intensity);  // Blue to red
                } else {
                    result.at<cv::Vec3b>(y, x) = cv::Vec3b(0, intensity, 255 - intensity);  // Green to red
                }
            }
        }
    }
    
    // Add text overlay
    std::string info = "Points: " + std::to_string(valid_points) + " | Press 'p' for 3D view";
    Utils::addTextOverlay(result, info, cv::Point(10, result.rows - 20));
    
    return result;
}

