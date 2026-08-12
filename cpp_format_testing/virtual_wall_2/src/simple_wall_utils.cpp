#include "simple_wall_utils.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <random>

// ============================================================================
// CONFIGURATION MANAGEMENT
// ============================================================================

bool SimpleWallUtils::saveConfig(const SimpleWallConfig& config, const std::string& filepath) {
    try {
        Json::Value root;
        
        // Corner pixels
        root["corners"]["upper_left"]["x"] = config.upper_left_px.x;
        root["corners"]["upper_left"]["y"] = config.upper_left_px.y;
        root["corners"]["upper_right"]["x"] = config.upper_right_px.x;
        root["corners"]["upper_right"]["y"] = config.upper_right_px.y;
        root["corners"]["lower_left"]["x"] = config.lower_left_px.x;
        root["corners"]["lower_left"]["y"] = config.lower_left_px.y;
        root["corners"]["lower_right"]["x"] = config.lower_right_px.x;
        root["corners"]["lower_right"]["y"] = config.lower_right_px.y;
        
        // REAL dimensions (measured by user)
        root["real_dimensions"]["width_cm"] = config.actual_width_cm;
        root["real_dimensions"]["height_cm"] = config.actual_height_cm;
        
        // Depth information
        root["depth"]["wall_depth_mm"] = config.wall_depth_mm;
        root["depth"]["penetration_threshold_mm"] = config.penetration_threshold_mm;
        
        // Metadata
        root["metadata"]["unique_id"] = config.unique_id;
        root["metadata"]["creation_timestamp"] = config.creation_timestamp;
        root["metadata"]["is_valid"] = config.is_valid;
        root["metadata"]["version"] = "2.0-simplified";
        
        // Write to file
        std::ofstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open config file for writing: " << filepath << std::endl;
            return false;
        }
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(root, &file);
        
        std::cout << "✅ Configuration saved to: " << filepath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error saving config: " << e.what() << std::endl;
        return false;
    }
}

bool SimpleWallUtils::loadConfig(SimpleWallConfig& config, const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open config file: " << filepath << std::endl;
            return false;
        }
        
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        if (!Json::parseFromStream(builder, file, &root, &errors)) {
            std::cerr << "❌ Failed to parse JSON: " << errors << std::endl;
            return false;
        }
        
        // Load corner pixels
        config.upper_left_px.x = root["corners"]["upper_left"]["x"].asInt();
        config.upper_left_px.y = root["corners"]["upper_left"]["y"].asInt();
        config.upper_right_px.x = root["corners"]["upper_right"]["x"].asInt();
        config.upper_right_px.y = root["corners"]["upper_right"]["y"].asInt();
        config.lower_left_px.x = root["corners"]["lower_left"]["x"].asInt();
        config.lower_left_px.y = root["corners"]["lower_left"]["y"].asInt();
        config.lower_right_px.x = root["corners"]["lower_right"]["x"].asInt();
        config.lower_right_px.y = root["corners"]["lower_right"]["y"].asInt();
        
        // Load REAL dimensions
        config.actual_width_cm = root["real_dimensions"]["width_cm"].asFloat();
        config.actual_height_cm = root["real_dimensions"]["height_cm"].asFloat();
        
        // Load depth information
        config.wall_depth_mm = root["depth"]["wall_depth_mm"].asFloat();
        config.penetration_threshold_mm = root["depth"]["penetration_threshold_mm"].asFloat();
        
        // Load metadata
        config.unique_id = root["metadata"]["unique_id"].asString();
        config.creation_timestamp = root["metadata"]["creation_timestamp"].asString();
        config.is_valid = root["metadata"]["is_valid"].asBool();
        
        std::cout << "✅ Configuration loaded successfully!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error loading config: " << e.what() << std::endl;
        return false;
    }
}

std::string SimpleWallUtils::getDefaultConfigPath() {
    return "simple_wall_config.json";
}

// ============================================================================
// PIXEL-TO-REAL-WORLD MAPPING (THE MAGIC!)
// ============================================================================

RealWorldCoord SimpleWallUtils::pixelToRealWorld(const cv::Point2i& pixel, 
                                                 const SimpleWallConfig& config) {
    RealWorldCoord result;
    
    // Get normalized coordinates (u, v) in [0, 1] range
    float u, v;
    getNormalizedCoordinates(pixel, config, u, v);
    
    // Map to real-world coordinates
    // u maps to width (0 to actual_width_cm)
    // v maps to height (0 to actual_height_cm)
    result.x_cm = u * config.actual_width_cm;
    result.y_cm = v * config.actual_height_cm;
    
    return result;
}

cv::Point2f SimpleWallUtils::realWorldToPixel(const RealWorldCoord& coord,
                                             const SimpleWallConfig& config) {
    // Normalize real-world coordinates
    float u = coord.x_cm / config.actual_width_cm;
    float v = coord.y_cm / config.actual_height_cm;
    
    // Clamp to valid range
    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));
    
    // Bilinear interpolation to get pixel coordinates
    const cv::Point2f& ul = config.upper_left_px;
    const cv::Point2f& ur = config.upper_right_px;
    const cv::Point2f& ll = config.lower_left_px;
    const cv::Point2f& lr = config.lower_right_px;
    
    // Interpolate top edge
    cv::Point2f top = ul * (1.0f - u) + ur * u;
    
    // Interpolate bottom edge
    cv::Point2f bottom = ll * (1.0f - u) + lr * u;
    
    // Interpolate between top and bottom
    cv::Point2f result = top * (1.0f - v) + bottom * v;
    
    return result;
}

void SimpleWallUtils::getNormalizedCoordinates(const cv::Point2i& pixel,
                                              const SimpleWallConfig& config,
                                              float& u, float& v) {
    // Convert to homogeneous coordinates for perspective transformation
    // Using bilinear interpolation within the quadrilateral
    
    const cv::Point2f& ul = config.upper_left_px;
    const cv::Point2f& ur = config.upper_right_px;
    const cv::Point2f& ll = config.lower_left_px;
    const cv::Point2f& lr = config.lower_right_px;
    
    cv::Point2f p(pixel.x, pixel.y);
    
    // Simple approach: project onto axes defined by corners
    // Horizontal: interpolate between left and right edges
    cv::Point2f left_edge = ll - ul;
    cv::Point2f right_edge = lr - ur;
    
    // Vertical projection (approximate)
    cv::Point2f top_edge = ur - ul;
    cv::Point2f bottom_edge = lr - ll;
    
    // Calculate u (horizontal position)
    cv::Point2f p_from_ul = p - ul;
    float top_width = cv::norm(ur - ul);
    if (top_width > 0) {
        u = p_from_ul.dot(top_edge) / (top_width * top_width);
    } else {
        u = 0.5f;
    }
    
    // Calculate v (vertical position)
    float left_height = cv::norm(ll - ul);
    if (left_height > 0) {
        v = p_from_ul.dot(left_edge) / (left_height * left_height);
    } else {
        v = 0.5f;
    }
    
    // Clamp to [0, 1]
    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));
}

// ============================================================================
// BOUNDARY CHECKING
// ============================================================================

bool SimpleWallUtils::isInsideBoundary(const cv::Point2i& pixel, 
                                      const SimpleWallConfig& config) {
    // Check if point is inside the quadrilateral formed by the 4 corners
    cv::Point2f p(pixel.x, pixel.y);
    
    return isPointInQuad(p,
                        config.upper_left_px,
                        config.upper_right_px,
                        config.lower_right_px,
                        config.lower_left_px);
}

bool SimpleWallUtils::isPointInQuad(const cv::Point2f& point,
                                   const cv::Point2f& p1, const cv::Point2f& p2,
                                   const cv::Point2f& p3, const cv::Point2f& p4) {
    // Use cross product to determine if point is on the same side of all edges
    auto sign = [](const cv::Point2f& p1, const cv::Point2f& p2, const cv::Point2f& p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    
    float d1 = sign(point, p1, p2);
    float d2 = sign(point, p2, p3);
    float d3 = sign(point, p3, p4);
    float d4 = sign(point, p4, p1);
    
    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0) || (d4 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0) || (d4 > 0);
    
    return !(has_neg && has_pos);
}

// ============================================================================
// PENETRATION DETECTION
// ============================================================================

bool SimpleWallUtils::isPenetrating(const cv::Point2i& pixel, 
                                   float measured_depth_mm,
                                   const SimpleWallConfig& config) {
    // First check if inside boundary
    if (!isInsideBoundary(pixel, config)) {
        return false;
    }
    
    // Check if object is closer than wall (penetrating)
    float distance_to_wall = config.wall_depth_mm - measured_depth_mm;
    return (distance_to_wall > config.penetration_threshold_mm);
}

float SimpleWallUtils::calculatePenetrationDistance(float measured_depth_mm,
                                                    const SimpleWallConfig& config) {
    return config.wall_depth_mm - measured_depth_mm;
}

// ============================================================================
// EVENT LOGGING
// ============================================================================

bool SimpleWallUtils::logEvent(const InterferenceEvent& event, const std::string& log_file) {
    try {
        Json::Value json_event;
        
        json_event["timestamp"] = event.timestamp;
        json_event["event_id"] = event.event_id;
        
        // Pixel coordinates
        json_event["pixel"]["x"] = event.pixel_x;
        json_event["pixel"]["y"] = event.pixel_y;
        
        // Real-world position (what matters!)
        json_event["position"]["x_cm"] = event.x_cm;
        json_event["position"]["y_cm"] = event.y_cm;
        
        // Depth information
        json_event["depth"]["measured_mm"] = event.measured_depth_mm;
        json_event["depth"]["wall_mm"] = event.wall_depth_mm;
        json_event["depth"]["penetration_mm"] = event.penetration_distance_mm;
        
        // Append to log file
        std::ofstream file(log_file, std::ios::app);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open log file: " << log_file << std::endl;
            return false;
        }
        
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(json_event, &file);
        file << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error logging event: " << e.what() << std::endl;
        return false;
    }
}

std::string SimpleWallUtils::generateEventId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::stringstream ss;
    ss << "evt_" << ms << "_" << dis(gen);
    return ss.str();
}

std::string SimpleWallUtils::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// ============================================================================
// VISUALIZATION
// ============================================================================

void SimpleWallUtils::drawVirtualWall(cv::Mat& image, const SimpleWallConfig& config,
                                     const cv::Scalar& color) {
    // Draw the virtual wall boundary
    std::vector<cv::Point> points = {
        config.upper_left_px,
        config.upper_right_px,
        config.lower_right_px,
        config.lower_left_px
    };
    
    // Draw edges
    for (size_t i = 0; i < points.size(); ++i) {
        size_t next = (i + 1) % points.size();
        cv::line(image, points[i], points[next], color, 2);
    }
    
    // Draw semi-transparent fill
    cv::Mat overlay = image.clone();
    cv::fillPoly(overlay, points, cv::Scalar(color[0], color[1], color[2], 50));
    cv::addWeighted(image, 0.7, overlay, 0.3, 0, image);
}

void SimpleWallUtils::drawCorners(cv::Mat& image, const SimpleWallConfig& config) {
    std::vector<cv::Point> corners = {
        config.upper_left_px,
        config.upper_right_px,
        config.lower_left_px,
        config.lower_right_px
    };
    
    std::vector<std::string> labels = {"UL", "UR", "LL", "LR"};
    cv::Scalar color(0, 255, 0); // Green
    
    for (size_t i = 0; i < corners.size(); ++i) {
        // Draw circle
        cv::circle(image, corners[i], 8, color, 2);
        cv::circle(image, corners[i], 2, color, -1);
        
        // Draw label
        cv::putText(image, labels[i], cv::Point(corners[i].x + 10, corners[i].y - 10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }
}

void SimpleWallUtils::drawInterferenceMarker(cv::Mat& image, const InterferenceEvent& event) {
    cv::Point pixel(event.pixel_x, event.pixel_y);
    cv::Scalar color(0, 0, 255); // Red
    
    // Draw cross marker
    int size = 15;
    cv::line(image, cv::Point(pixel.x - size, pixel.y), 
             cv::Point(pixel.x + size, pixel.y), color, 2);
    cv::line(image, cv::Point(pixel.x, pixel.y - size), 
             cv::Point(pixel.x, pixel.y + size), color, 2);
    
    // Draw circle
    cv::circle(image, pixel, size + 2, color, 2);
    
    // Draw coordinate label
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << "(" << event.x_cm << "," << event.y_cm << ")";
    cv::putText(image, ss.str(), cv::Point(pixel.x + 20, pixel.y - 10),
               cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
}

// ============================================================================
// VALIDATION
// ============================================================================

bool SimpleWallUtils::validateConfig(const SimpleWallConfig& config) {
    return validateConfigDetailed(config).empty();
}

std::string SimpleWallUtils::validateConfigDetailed(const SimpleWallConfig& config) {
    std::stringstream errors;
    
    // Check if corners are set
    if (config.upper_left_px.x == 0 && config.upper_left_px.y == 0 &&
        config.upper_right_px.x == 0 && config.upper_right_px.y == 0) {
        errors << "\n❌ Corners not properly set";
    }
    
    // Check dimensions
    if (config.actual_width_cm <= 0 || config.actual_width_cm > 300) {
        errors << "\n❌ Invalid width: " << config.actual_width_cm << " cm (expected: 10-300 cm)";
    }
    
    if (config.actual_height_cm <= 0 || config.actual_height_cm > 300) {
        errors << "\n❌ Invalid height: " << config.actual_height_cm << " cm (expected: 10-300 cm)";
    }
    
    // Check wall depth
    if (config.wall_depth_mm <= 0 || config.wall_depth_mm > 5000) {
        errors << "\n❌ Invalid wall depth: " << config.wall_depth_mm << " mm";
    }
    
    return errors.str();
}

