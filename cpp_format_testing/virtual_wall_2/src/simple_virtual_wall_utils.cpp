#include "simple_virtual_wall_utils.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <random>
#include <filesystem>
#include <map>

// ===========================================================================================
// DEPTH VALIDATION & UNIT CONVERSION
// ===========================================================================================

// Helper function to validate and potentially convert depth units
float SimpleVirtualWallUtils::validateAndConvertDepth(float raw_depth, const std::string& source) {
    static std::map<std::string, bool> source_warnings; // Track warnings per source
    
    // Check for invalid values
    if (std::isnan(raw_depth) || std::isinf(raw_depth) || raw_depth <= 0) {
        return -1.0f; // Invalid depth
    }
    
    float converted_depth = raw_depth;
    bool conversion_applied = false;
    
    // Detect and handle potential unit conversion issues
    if (raw_depth > 10000.0f) {
        // Likely in micrometers (μm), convert to mm
        converted_depth = raw_depth / 1000.0f;
        conversion_applied = true;
        if (source_warnings.find(source + "_um") == source_warnings.end()) {
            std::cout << "⚠️  UNIT CONVERSION: " << source << " depth values appear to be in μm, converting to mm" << std::endl;
            std::cout << "   Example: " << raw_depth << "μm -> " << converted_depth << "mm" << std::endl;
            source_warnings[source + "_um"] = true;
        }
    } else if (raw_depth < 10.0f && raw_depth > 0.1f) {
        // More conservative threshold: only convert if clearly in meters (< 10.0)
        // This avoids converting values like 263mm which are already correct
        converted_depth = raw_depth * 1000.0f;
        conversion_applied = true;
        if (source_warnings.find(source + "_m") == source_warnings.end()) {
            std::cout << "⚠️  UNIT CONVERSION: " << source << " depth values appear to be in meters, converting to mm" << std::endl;
            std::cout << "   Example: " << raw_depth << "m -> " << converted_depth << "mm" << std::endl;
            source_warnings[source + "_m"] = true;
        }
    }
    
    // Final validation - should be within reasonable ToF camera range
    if (converted_depth < 200.0f || converted_depth > 5000.0f) {
        // If conversion was applied and result is still invalid, try without conversion
        if (conversion_applied && raw_depth >= 200.0f && raw_depth <= 5000.0f) {
            if (source_warnings.find(source + "_revert") == source_warnings.end()) {
                std::cout << "⚠️  UNIT REVERT: " << source << " conversion resulted in invalid range, using original value" << std::endl;
                std::cout << "   Reverted: " << converted_depth << "mm -> " << raw_depth << "mm" << std::endl;
                source_warnings[source + "_revert"] = true;
            }
            return raw_depth; // Use original value
        }
        return -1.0f; // Outside reasonable range
    }
    
    return converted_depth;
}

// ===========================================================================================
// CONFIGURATION MANAGEMENT
// ===========================================================================================

bool SimpleVirtualWallUtils::saveConfig(const SimpleVirtualWallConfig& config, const std::string& filepath) {
    try {
        Json::Value root;
        
        // Corner pixel coordinates
        root["corners"]["upper_left"]["x"] = config.upper_left_px.x;
        root["corners"]["upper_left"]["y"] = config.upper_left_px.y;
        root["corners"]["upper_left"]["depth_mm"] = config.upper_left_depth_mm;
        
        root["corners"]["upper_right"]["x"] = config.upper_right_px.x;
        root["corners"]["upper_right"]["y"] = config.upper_right_px.y;
        root["corners"]["upper_right"]["depth_mm"] = config.upper_right_depth_mm;
        
        root["corners"]["lower_left"]["x"] = config.lower_left_px.x;
        root["corners"]["lower_left"]["y"] = config.lower_left_px.y;
        root["corners"]["lower_left"]["depth_mm"] = config.lower_left_depth_mm;
        
        root["corners"]["lower_right"]["x"] = config.lower_right_px.x;
        root["corners"]["lower_right"]["y"] = config.lower_right_px.y;
        root["corners"]["lower_right"]["depth_mm"] = config.lower_right_depth_mm;
        
        // User-provided real dimensions
        root["dimensions"]["actual_width_cm"] = config.actual_width_cm;
        root["dimensions"]["actual_height_cm"] = config.actual_height_cm;
        root["dimensions"]["wall_depth_mm"] = config.wall_depth_mm;
        root["dimensions"]["penetration_threshold_mm"] = config.penetration_threshold_mm;
        
        // Metadata
        root["metadata"]["unique_id"] = config.unique_id;
        root["metadata"]["creation_timestamp"] = config.creation_timestamp;
        root["metadata"]["is_valid"] = config.is_valid;
        
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

bool SimpleVirtualWallUtils::loadConfig(SimpleVirtualWallConfig& config, const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open config file: " << filepath << std::endl;
            return false;
        }
        
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        
        if (!Json::parseFromStream(builder, file, &root, &errs)) {
            std::cerr << "❌ Failed to parse config file: " << errs << std::endl;
            return false;
        }
        
        // Load corner pixel coordinates
        config.upper_left_px.x = root["corners"]["upper_left"]["x"].asInt();
        config.upper_left_px.y = root["corners"]["upper_left"]["y"].asInt();
        config.upper_left_depth_mm = root["corners"]["upper_left"]["depth_mm"].asFloat();
        
        config.upper_right_px.x = root["corners"]["upper_right"]["x"].asInt();
        config.upper_right_px.y = root["corners"]["upper_right"]["y"].asInt();
        config.upper_right_depth_mm = root["corners"]["upper_right"]["depth_mm"].asFloat();
        
        config.lower_left_px.x = root["corners"]["lower_left"]["x"].asInt();
        config.lower_left_px.y = root["corners"]["lower_left"]["y"].asInt();
        config.lower_left_depth_mm = root["corners"]["lower_left"]["depth_mm"].asFloat();
        
        config.lower_right_px.x = root["corners"]["lower_right"]["x"].asInt();
        config.lower_right_px.y = root["corners"]["lower_right"]["y"].asInt();
        config.lower_right_depth_mm = root["corners"]["lower_right"]["depth_mm"].asFloat();
        
        // Load user-provided real dimensions
        config.actual_width_cm = root["dimensions"]["actual_width_cm"].asFloat();
        config.actual_height_cm = root["dimensions"]["actual_height_cm"].asFloat();
        config.wall_depth_mm = root["dimensions"]["wall_depth_mm"].asFloat();
        config.penetration_threshold_mm = root["dimensions"]["penetration_threshold_mm"].asFloat();
        
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

std::string SimpleVirtualWallUtils::getDefaultConfigPath() {
    return "simple_virtual_wall_config.json";
}

bool SimpleVirtualWallUtils::validateConfig(const SimpleVirtualWallConfig& config) {
    // Check that all corners are within frame bounds
    if (config.upper_left_px.x < 0 || config.upper_left_px.x >= FRAME_WIDTH ||
        config.upper_left_px.y < 0 || config.upper_left_px.y >= FRAME_HEIGHT) {
        std::cerr << "❌ Upper-left corner out of bounds" << std::endl;
        return false;
    }
    
    if (config.upper_right_px.x < 0 || config.upper_right_px.x >= FRAME_WIDTH ||
        config.upper_right_px.y < 0 || config.upper_right_px.y >= FRAME_HEIGHT) {
        std::cerr << "❌ Upper-right corner out of bounds" << std::endl;
        return false;
    }
    
    if (config.lower_left_px.x < 0 || config.lower_left_px.x >= FRAME_WIDTH ||
        config.lower_left_px.y < 0 || config.lower_left_px.y >= FRAME_HEIGHT) {
        std::cerr << "❌ Lower-left corner out of bounds" << std::endl;
        return false;
    }
    
    if (config.lower_right_px.x < 0 || config.lower_right_px.x >= FRAME_WIDTH ||
        config.lower_right_px.y < 0 || config.lower_right_px.y >= FRAME_HEIGHT) {
        std::cerr << "❌ Lower-right corner out of bounds" << std::endl;
        return false;
    }
    
    // Check that depths are valid
    if (config.upper_left_depth_mm <= 0 || config.upper_right_depth_mm <= 0 ||
        config.lower_left_depth_mm <= 0 || config.lower_right_depth_mm <= 0) {
        std::cerr << "❌ Invalid depth values (must be > 0)" << std::endl;
        return false;
    }
    
    // Check that dimensions are valid
    if (config.actual_width_cm <= 0 || config.actual_height_cm <= 0) {
        std::cerr << "❌ Invalid dimensions (must be > 0)" << std::endl;
        return false;
    }
    
    if (config.wall_depth_mm <= 0) {
        std::cerr << "❌ Invalid wall depth (must be > 0)" << std::endl;
        return false;
    }
    
    return true;
}

// ===========================================================================================
// COORDINATE MAPPING - THE HEART OF THE SYSTEM
// ===========================================================================================

cv::Point2f SimpleVirtualWallUtils::pixelToRealWorld(const cv::Point2i& pixel, 
                                                     const SimpleVirtualWallConfig& config) {
    /*
     * BILINEAR INTERPOLATION FOR PIXEL → REAL-WORLD MAPPING
     * 
     * This is the key function that converts pixel coordinates to real-world coordinates.
     * 
     * Method:
     * 1. Calculate normalized coordinates (u, v) in [0,1] range within the quadrilateral
     * 2. Map (u, v) to real-world dimensions using user-provided measurements
     * 
     * Example:
     * - Pixel at upper-left corner → (0, 0) in real-world
     * - Pixel at upper-right corner → (actual_width_cm, 0) in real-world
     * - Pixel at center → (actual_width_cm/2, actual_height_cm/2) in real-world
     */
    
    // Get corner pixel coordinates
    const cv::Point2f ul = config.upper_left_px;
    const cv::Point2f ur = config.upper_right_px;
    const cv::Point2f ll = config.lower_left_px;
    const cv::Point2f lr = config.lower_right_px;
    
    // Convert pixel to float for calculations
    cv::Point2f p(pixel.x, pixel.y);
    
    // Calculate normalized coordinates (u, v) using bilinear interpolation
    // This accounts for perspective distortion
    
    // Simple approach: calculate u from horizontal position, v from vertical position
    float u = (p.x - ul.x) / (ur.x - ul.x);  // Horizontal normalized coordinate [0,1]
    float v = (p.y - ul.y) / (ll.y - ul.y);  // Vertical normalized coordinate [0,1]
    
    // Clamp to [0, 1] range
    u = clamp(u, 0.0f, 1.0f);
    v = clamp(v, 0.0f, 1.0f);
    
    // Map to real-world coordinates
    float x_cm = u * config.actual_width_cm;
    float y_cm = v * config.actual_height_cm;
    
    return cv::Point2f(x_cm, y_cm);
}

cv::Point2i SimpleVirtualWallUtils::realWorldToPixel(const cv::Point2f& real_world, 
                                                     const SimpleVirtualWallConfig& config) {
    /*
     * REVERSE MAPPING: REAL-WORLD → PIXEL
     * 
     * Used for visualization purposes (drawing grid overlays, etc.)
     */
    
    // Calculate normalized coordinates
    float u = real_world.x / config.actual_width_cm;
    float v = real_world.y / config.actual_height_cm;
    
    // Clamp to [0, 1]
    u = clamp(u, 0.0f, 1.0f);
    v = clamp(v, 0.0f, 1.0f);
    
    // Map to pixel coordinates
    float px = config.upper_left_px.x + u * (config.upper_right_px.x - config.upper_left_px.x);
    float py = config.upper_left_px.y + v * (config.lower_left_px.y - config.upper_left_px.y);
    
    return cv::Point2i(static_cast<int>(px), static_cast<int>(py));
}

float SimpleVirtualWallUtils::interpolateWallDepth(const cv::Point2i& pixel, 
                                                   const SimpleVirtualWallConfig& config) {
    /*
     * BILINEAR DEPTH INTERPOLATION
     * 
     * Calculate expected wall depth at any pixel position by interpolating
     * between the four corner depths.
     * 
     * This handles cases where the wall surface isn't perfectly flat.
     */
    
    // Get corner coordinates and depths
    const cv::Point2f ul = config.upper_left_px;
    const cv::Point2f ur = config.upper_right_px;
    const cv::Point2f ll = config.lower_left_px;
    const cv::Point2f lr = config.lower_right_px;
    
    float d_ul = config.upper_left_depth_mm;
    float d_ur = config.upper_right_depth_mm;
    float d_ll = config.lower_left_depth_mm;
    float d_lr = config.lower_right_depth_mm;
    
    // Calculate normalized coordinates
    cv::Point2f p(pixel.x, pixel.y);
    float u = (p.x - ul.x) / (ur.x - ul.x);
    float v = (p.y - ul.y) / (ll.y - ul.y);
    
    // Clamp to [0, 1]
    u = clamp(u, 0.0f, 1.0f);
    v = clamp(v, 0.0f, 1.0f);
    
    // Bilinear interpolation of depth
    float depth_top = d_ul * (1 - u) + d_ur * u;       // Interpolate along top edge
    float depth_bottom = d_ll * (1 - u) + d_lr * u;    // Interpolate along bottom edge
    float depth = depth_top * (1 - v) + depth_bottom * v;  // Interpolate vertically
    
    return depth;
}

// ===========================================================================================
// PENETRATION DETECTION
// ===========================================================================================

bool SimpleVirtualWallUtils::isPointInsideBoundary(const cv::Point2i& pixel, 
                                                   const SimpleVirtualWallConfig& config) {
    /*
     * CHECK IF POINT IS INSIDE THE QUADRILATERAL BOUNDARY
     * 
     * Uses point-in-polygon algorithm with the 4 corner points.
     */
    
    std::vector<cv::Point2i> boundary = {
        config.upper_left_px,
        config.upper_right_px,
        config.lower_right_px,
        config.lower_left_px
    };
    
    double result = cv::pointPolygonTest(boundary, cv::Point2f(pixel), false);
    return result >= 0;  // Inside or on boundary
}

bool SimpleVirtualWallUtils::isPointPenetrating(const cv::Point2i& pixel, float depth_mm, 
                                               const SimpleVirtualWallConfig& config) {
    /*
     * CORE PENETRATION DETECTION LOGIC
     * 
     * A point is penetrating if:
     * 1. It's inside the virtual wall boundary
     * 2. Its depth is LESS than the expected wall depth (closer to camera)
     * 
     * This is the simple, robust detection method:
     * - No background subtraction needed
     * - No complex 3D calculations
     * - Just compare: measured_depth < wall_depth - threshold
     */
    
    // First check if inside boundary
    if (!isPointInsideBoundary(pixel, config)) {
        return false;
    }
    
    // Get expected wall depth at this position
    float expected_wall_depth = interpolateWallDepth(pixel, config);
    
    // Check if measured depth is closer than wall (penetrating)
    // Add threshold to avoid false positives from noise
    return (depth_mm < expected_wall_depth - config.penetration_threshold_mm);
}

float SimpleVirtualWallUtils::calculatePenetrationDistance(const cv::Point2i& pixel, float depth_mm,
                                                          const SimpleVirtualWallConfig& config) {
    /*
     * CALCULATE HOW MUCH PENETRATION OCCURRED
     * 
     * Returns:
     * - Positive value: Object penetrated into wall by X mm
     * - Zero: Object at wall surface
     * - Negative value: Object in front of wall (no penetration)
     */
    
    float expected_wall_depth = interpolateWallDepth(pixel, config);
    float penetration = expected_wall_depth - depth_mm;  // How much closer than wall
    
    return penetration;
}

// ===========================================================================================
// EVENT LOGGING
// ===========================================================================================

bool SimpleVirtualWallUtils::logInterferenceEvent(const SimpleInterferenceEvent& event, 
                                                  const std::string& log_file) {
    try {
        Json::Value json_event;
        
        // Timestamp and ID
        json_event["timestamp"] = event.timestamp;
        json_event["event_id"] = event.event_id;
        
        // Pixel coordinates
        json_event["pixel"]["x"] = event.pixel_coord.x;
        json_event["pixel"]["y"] = event.pixel_coord.y;
        
        // Real-world coordinates (X, Y only - no Z)
        json_event["position"]["x_cm"] = event.x_position_cm;
        json_event["position"]["y_cm"] = event.y_position_cm;
        
        // Depth information
        json_event["depth"]["measured_mm"] = event.measured_depth_mm;
        json_event["depth"]["wall_mm"] = event.wall_depth_mm;
        json_event["depth"]["penetration_mm"] = event.penetration_depth_mm;
        
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

std::string SimpleVirtualWallUtils::generateEventId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << "evt_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << "_" << dis(gen);
    return ss.str();
}

std::string SimpleVirtualWallUtils::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// ===========================================================================================
// VISUALIZATION
// ===========================================================================================

void SimpleVirtualWallUtils::drawVirtualWall(cv::Mat& image, const SimpleVirtualWallConfig& config) {
    if (!config.is_valid) return;
    
    // Fill the wall area with semi-transparent green (like a real wall)
    std::vector<cv::Point> wall_points = {
        config.upper_left_px, 
        config.upper_right_px, 
        config.lower_right_px, 
        config.lower_left_px
    };
    cv::Mat overlay = image.clone();
    cv::fillPoly(overlay, wall_points, cv::Scalar(0, 255, 0)); // Green fill
    cv::addWeighted(image, 0.7, overlay, 0.3, 0, image); // 30% green opacity
    
    // Draw green border around the wall
    cv::Scalar wall_color(0, 255, 0);  // Green
    cv::line(image, config.upper_left_px, config.upper_right_px, wall_color, 2);
    cv::line(image, config.upper_right_px, config.lower_right_px, wall_color, 2);
    cv::line(image, config.lower_right_px, config.lower_left_px, wall_color, 2);
    cv::line(image, config.lower_left_px, config.upper_left_px, wall_color, 2);
    
    // Draw corner markers
    int radius = 4;
    cv::circle(image, config.upper_left_px, radius, cv::Scalar(0, 255, 0), -1);
    cv::circle(image, config.upper_right_px, radius, cv::Scalar(0, 255, 0), -1);
    cv::circle(image, config.lower_left_px, radius, cv::Scalar(0, 255, 0), -1);
    cv::circle(image, config.lower_right_px, radius, cv::Scalar(0, 255, 0), -1);
    
    // NO text overlay - all info moved to sidebar in monitor tool
}

void SimpleVirtualWallUtils::drawInteractiveOverlay(cv::Mat& image, 
                                                    const SimpleCalibrationData& calib_data) {
    if (calib_data.corners.size() != 4) return;
    
    // Draw lines connecting corners
    cv::Scalar line_color = calib_data.corners_positioned ? 
                           cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255);
    
    cv::line(image, calib_data.corners[0].position, calib_data.corners[1].position, line_color, 1);
    cv::line(image, calib_data.corners[1].position, calib_data.corners[3].position, line_color, 1);
    cv::line(image, calib_data.corners[3].position, calib_data.corners[2].position, line_color, 1);
    cv::line(image, calib_data.corners[2].position, calib_data.corners[0].position, line_color, 1);
    
    // Draw corner markers
    drawDraggableCorners(image, calib_data.corners);
}

void SimpleVirtualWallUtils::drawDraggableCorners(cv::Mat& image, 
                                                  const std::vector<DraggableCorner>& corners) {
    for (const auto& corner : corners) {
        cv::Scalar color;
        
        // Color based on state
        if (corner.is_dragging) {
            color = cv::Scalar(0, 255, 0);  // Green when dragging
        } else if (corner.is_hovered) {
            color = cv::Scalar(0, 255, 255);  // Yellow when hovered
        } else if (corner.depth > 0) {
            color = cv::Scalar(0, 255, 0);  // Green when valid depth
        } else {
            color = cv::Scalar(255, 0, 255);  // Magenta when no depth
        }
        
        // Draw reticle-style marker
        int radius = 6;
        cv::circle(image, corner.position, radius, color, 1);
        cv::line(image, cv::Point(corner.position.x, corner.position.y - radius),
                cv::Point(corner.position.x, corner.position.y - 2), color, 1);
        cv::line(image, cv::Point(corner.position.x, corner.position.y + 2),
                cv::Point(corner.position.x, corner.position.y + radius), color, 1);
        cv::line(image, cv::Point(corner.position.x - radius, corner.position.y),
                cv::Point(corner.position.x - 2, corner.position.y), color, 1);
        cv::line(image, cv::Point(corner.position.x + 2, corner.position.y),
                cv::Point(corner.position.x + radius, corner.position.y), color, 1);
        cv::circle(image, corner.position, 1, color, -1);
    }
}

int SimpleVirtualWallUtils::findNearestCorner(const cv::Point2i& mouse_pos, 
                                             const std::vector<DraggableCorner>& corners,
                                             int threshold) {
    int nearest = -1;
    float min_distance = threshold;
    
    for (size_t i = 0; i < corners.size(); ++i) {
        float dist = calculateDistance(cv::Point2f(mouse_pos), cv::Point2f(corners[i].position));
        if (dist < min_distance) {
            min_distance = dist;
            nearest = static_cast<int>(i);
        }
    }
    
    return nearest;
}

void SimpleVirtualWallUtils::drawClusterGrid(cv::Mat& image, int cluster_size, 
                                            const cv::Scalar& color) {
    // Draw vertical lines
    for (int x = 0; x <= FRAME_WIDTH; x += cluster_size) {
        cv::line(image, cv::Point(x, 0), cv::Point(x, FRAME_HEIGHT), color, 1);
    }
    
    // Draw horizontal lines
    for (int y = 0; y <= FRAME_HEIGHT; y += cluster_size) {
        cv::line(image, cv::Point(0, y), cv::Point(FRAME_WIDTH, y), color, 1);
    }
}

void SimpleVirtualWallUtils::drawClusterGridWithWall(cv::Mat& image, int cluster_size,
                                                    const SimpleVirtualWallConfig& config) {
    if (!config.is_valid) {
        // Just draw the basic grid if no wall configured
        drawClusterGrid(image, cluster_size, cv::Scalar(80, 80, 80));
        return;
    }
    
    // Draw grid with different colors for inside/outside wall
    for (int grid_y = 0; grid_y < CLUSTERS_Y; ++grid_y) {
        for (int grid_x = 0; grid_x < CLUSTERS_X; ++grid_x) {
            int px = grid_x * cluster_size;
            int py = grid_y * cluster_size;
            cv::Point2i center(px + cluster_size/2, py + cluster_size/2);
            
            // Check if cluster center is inside wall
            bool inside = isPointInsideBoundary(center, config);
            cv::Scalar grid_color = inside ? cv::Scalar(100, 150, 100) : cv::Scalar(60, 60, 60);
            
            // Draw cell borders
            cv::rectangle(image, 
                         cv::Rect(px, py, cluster_size, cluster_size),
                         grid_color, 1);
        }
    }
}

void SimpleVirtualWallUtils::drawCornerWithSquareEdge(cv::Mat& image, const DraggableCorner& corner, 
                                                     int edge_size) {
    cv::Scalar color;
    
    // Color based on state
    if (corner.is_dragging) {
        color = cv::Scalar(0, 255, 0);  // Green when dragging
    } else if (corner.is_hovered) {
        color = cv::Scalar(0, 255, 255);  // Yellow when hovered
    } else if (corner.depth > 0) {
        color = cv::Scalar(0, 255, 0);  // Green when valid depth
    } else {
        color = cv::Scalar(255, 0, 255);  // Magenta when no depth
    }
    
    cv::Point2i pos = corner.position;
    
    // Draw L-shaped edge marker at the corner position (no rectangle!)
    // Marker length proportional to edge measurement size
    int marker_len = 12;  // Fixed length for clean edge visualization
    
    if (corner.corner_id == 0) {
        // Lower-Left: L pointing RIGHT and DOWN
        cv::line(image, pos, cv::Point(pos.x + marker_len, pos.y), color, 3);
        cv::line(image, pos, cv::Point(pos.x, pos.y + marker_len), color, 3);
    } else if (corner.corner_id == 1) {
        // Lower-Right: L pointing LEFT and DOWN
        cv::line(image, pos, cv::Point(pos.x - marker_len, pos.y), color, 3);
        cv::line(image, pos, cv::Point(pos.x, pos.y + marker_len), color, 3);
    } else if (corner.corner_id == 2) {
        // Upper-Left: L pointing RIGHT and UP
        cv::line(image, pos, cv::Point(pos.x + marker_len, pos.y), color, 3);
        cv::line(image, pos, cv::Point(pos.x, pos.y - marker_len), color, 3);
    } else if (corner.corner_id == 3) {
        // Upper-Right: L pointing LEFT and UP
        cv::line(image, pos, cv::Point(pos.x - marker_len, pos.y), color, 3);
        cv::line(image, pos, cv::Point(pos.x, pos.y - marker_len), color, 3);
    }
    
    // Draw prominent dot at corner position
    cv::circle(image, pos, 4, color, -1);
    cv::circle(image, pos, 5, color, 1);  // Outer ring for emphasis
}

// ===========================================================================================
// SPATIAL CLUSTERING (Noise Filtering)
// ===========================================================================================

float SimpleVirtualWallUtils::computeClusterMedianDepth(const cv::Mat& depth_frame, 
                                                       const cv::Mat& confidence_frame,
                                                       const cv::Rect& cluster_bounds,
                                                       int confidence_threshold,
                                                       int& valid_pixel_count,
                                                       float max_valid_depth_mm) {
    std::vector<float> valid_depths;
    valid_pixel_count = 0;
    
    // Collect all valid depth values in the cluster
    for (int y = cluster_bounds.y; y < cluster_bounds.y + cluster_bounds.height; ++y) {
        for (int x = cluster_bounds.x; x < cluster_bounds.x + cluster_bounds.width; ++x) {
            if (y >= depth_frame.rows || x >= depth_frame.cols) continue;
            
            float confidence = confidence_frame.at<float>(y, x);
            if (confidence < confidence_threshold) continue;
            
            float depth = depth_frame.at<float>(y, x);
            if (depth > 100 && depth < max_valid_depth_mm) {  // Valid ToF range (dynamic for lower shelves)
                valid_depths.push_back(depth);
                valid_pixel_count++;
            }
        }
    }
    
    // Return median (filters outliers!)
    if (valid_depths.empty()) return 0.0f;
    
    std::sort(valid_depths.begin(), valid_depths.end());
    size_t mid = valid_depths.size() / 2;
    
    if (valid_depths.size() % 2 == 0) {
        return (valid_depths[mid - 1] + valid_depths[mid]) / 2.0f;
    } else {
        return valid_depths[mid];
    }
}

float SimpleVirtualWallUtils::getClusterMedianAtPosition(const cv::Mat& depth_frame,
                                                        const cv::Mat& confidence_frame,
                                                        const cv::Point2i& corner_pos,
                                                        int cluster_size,
                                                        int confidence_threshold,
                                                        int corner_id) {
    cv::Rect cluster_bounds;
    
    // Position cluster based on corner type (edge at corner, not center!)
    // corner_id: 0=LL, 1=LR, 2=UL, 3=UR
    if (corner_id == 0) {
        // Lower-Left: cluster extends RIGHT and DOWN from corner
        cluster_bounds = cv::Rect(corner_pos.x, corner_pos.y, cluster_size, cluster_size);
    } else if (corner_id == 1) {
        // Lower-Right: cluster extends LEFT and DOWN from corner
        cluster_bounds = cv::Rect(corner_pos.x - cluster_size, corner_pos.y, cluster_size, cluster_size);
    } else if (corner_id == 2) {
        // Upper-Left: cluster extends RIGHT and UP from corner
        cluster_bounds = cv::Rect(corner_pos.x, corner_pos.y - cluster_size, cluster_size, cluster_size);
    } else if (corner_id == 3) {
        // Upper-Right: cluster extends LEFT and UP from corner
        cluster_bounds = cv::Rect(corner_pos.x - cluster_size, corner_pos.y - cluster_size, cluster_size, cluster_size);
    } else {
        // Default: center cluster on position (for non-corner usage)
        int half_size = cluster_size / 2;
        cluster_bounds = cv::Rect(corner_pos.x - half_size, corner_pos.y - half_size, cluster_size, cluster_size);
    }
    
    // Clamp to frame bounds
    cluster_bounds.x = std::max(0, cluster_bounds.x);
    cluster_bounds.y = std::max(0, cluster_bounds.y);
    cluster_bounds.width = std::min(cluster_bounds.width, depth_frame.cols - cluster_bounds.x);
    cluster_bounds.height = std::min(cluster_bounds.height, depth_frame.rows - cluster_bounds.y);
    
    int valid_count = 0;
    return computeClusterMedianDepth(depth_frame, confidence_frame, cluster_bounds, 
                                    confidence_threshold, valid_count);
}

std::vector<DepthCluster> SimpleVirtualWallUtils::createDepthClusters(
    const cv::Mat& depth_frame,
    const cv::Mat& confidence_frame,
    const cv::Mat& baseline_depth,
    const SimpleVirtualWallConfig& config,
    int confidence_threshold,
    float motion_threshold_mm,
    float max_valid_depth_mm) {
    
    std::vector<DepthCluster> clusters;
    
    // Create grid of clusters
    for (int grid_y = 0; grid_y < CLUSTERS_Y; ++grid_y) {
        for (int grid_x = 0; grid_x < CLUSTERS_X; ++grid_x) {
            DepthCluster cluster;
            cluster.grid_pos = cv::Point2i(grid_x, grid_y);
            
            // Calculate pixel bounds for this cluster
            int px = grid_x * CLUSTER_SIZE;
            int py = grid_y * CLUSTER_SIZE;
            cluster.bounds = cv::Rect(px, py, CLUSTER_SIZE, CLUSTER_SIZE);
            cluster.pixel_center = cv::Point2i(px + CLUSTER_SIZE/2, py + CLUSTER_SIZE/2);
            
            // Check if cluster is inside virtual wall boundary
            if (!isPointInsideBoundary(cluster.pixel_center, config)) {
                continue; // Skip clusters outside the wall
            }
            
            // Compute median depth for current frame
            cluster.median_depth = computeClusterMedianDepth(
                depth_frame, confidence_frame, cluster.bounds, 
                confidence_threshold, cluster.valid_pixel_count, max_valid_depth_mm);
            
            if (cluster.median_depth == 0 || cluster.valid_pixel_count < 3) {
                continue; // Skip clusters with insufficient data
            }
            
            // Compute baseline median if available
            if (!baseline_depth.empty()) {
                int baseline_valid_count = 0;
                cluster.baseline_median = computeClusterMedianDepth(
                    baseline_depth, confidence_frame, cluster.bounds,
                    confidence_threshold, baseline_valid_count, max_valid_depth_mm);
                
                // Check for motion (baseline - current > threshold)
                float depth_change = cluster.baseline_median - cluster.median_depth;
                cluster.has_motion = (depth_change > motion_threshold_mm);
                
                // Check for wall penetration
                if (cluster.has_motion) {
                    cluster.penetrates_wall = isPointPenetrating(
                        cluster.pixel_center, cluster.median_depth, config);
                }
            }
            
            clusters.push_back(cluster);
        }
    }
    
    return clusters;
}

// ===========================================================================================
// UTILITY HELPERS
// ===========================================================================================

float SimpleVirtualWallUtils::calculateDistance(const cv::Point2f& p1, const cv::Point2f& p2) {
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    return std::sqrt(dx * dx + dy * dy);
}

float SimpleVirtualWallUtils::clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}
