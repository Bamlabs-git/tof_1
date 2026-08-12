#pragma once

#include <opencv2/opencv.hpp>
#include <json/json.h>
#include <string>
#include <chrono>

// ============================================================================
// SIMPLIFIED VIRTUAL WALL SYSTEM v2.0
// 
// Philosophy: Simple, practical, user-measures-dimensions approach
// No complex 3D reconstruction, just pixel-to-real-world 2D mapping
// ============================================================================

// Simple 2D Real-World Coordinate
struct RealWorldCoord {
    float x_cm;  // Horizontal position (0 to width_cm)
    float y_cm;  // Vertical position (0 to height_cm)
    
    RealWorldCoord() : x_cm(0), y_cm(0) {}
    RealWorldCoord(float x, float y) : x_cm(x), y_cm(y) {}
};

// Simple Wall Configuration
struct SimpleWallConfig {
    // Corner pixel coordinates (user-selected)
    cv::Point2i upper_left_px;
    cv::Point2i upper_right_px;
    cv::Point2i lower_left_px;
    cv::Point2i lower_right_px;
    
    // REAL dimensions (user-measured with tape measure!)
    float actual_width_cm;   // User tells us: "My stand is X cm wide"
    float actual_height_cm;  // User tells us: "My stand is Y cm tall"
    
    // Wall depth for penetration detection
    float wall_depth_mm;     // Average depth at the 4 corners
    float penetration_threshold_mm;  // Objects closer than this = penetration
    
    // Metadata
    std::string unique_id;
    std::string creation_timestamp;
    bool is_valid;
    
    SimpleWallConfig() : 
        actual_width_cm(95.0f),
        actual_height_cm(190.0f),
        wall_depth_mm(0),
        penetration_threshold_mm(50.0f),
        is_valid(false) {}
};

// Interference Event (simplified - only X,Y position)
struct InterferenceEvent {
    std::string timestamp;
    std::string event_id;
    
    // Pixel coordinates
    int pixel_x, pixel_y;
    
    // Real-world position (what user cares about!)
    float x_cm;  // Position across width (0 to actual_width_cm)
    float y_cm;  // Position down height (0 to actual_height_cm)
    
    // Depth information (for internal use)
    float measured_depth_mm;
    float wall_depth_mm;
    float penetration_distance_mm;
    
    InterferenceEvent() : 
        pixel_x(0), pixel_y(0),
        x_cm(0), y_cm(0),
        measured_depth_mm(0),
        wall_depth_mm(0),
        penetration_distance_mm(0) {}
};

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

class SimpleWallUtils {
public:
    // ========== Configuration Management ==========
    static bool saveConfig(const SimpleWallConfig& config, const std::string& filepath);
    static bool loadConfig(SimpleWallConfig& config, const std::string& filepath);
    static std::string getDefaultConfigPath();
    
    // ========== Pixel-to-Real-World Mapping (THE CORE!) ==========
    // This is the magic: convert pixel (x,y) to real-world (X_cm, Y_cm)
    // Uses bilinear interpolation within the quadrilateral defined by corners
    static RealWorldCoord pixelToRealWorld(const cv::Point2i& pixel, 
                                           const SimpleWallConfig& config);
    
    // Inverse: real-world to pixel (for visualization)
    static cv::Point2f realWorldToPixel(const RealWorldCoord& coord,
                                        const SimpleWallConfig& config);
    
    // ========== Boundary Checking ==========
    // Is this pixel inside the virtual wall boundary?
    static bool isInsideBoundary(const cv::Point2i& pixel, 
                                const SimpleWallConfig& config);
    
    // ========== Penetration Detection ==========
    // Is this object penetrating the virtual wall?
    static bool isPenetrating(const cv::Point2i& pixel, 
                            float measured_depth_mm,
                            const SimpleWallConfig& config);
    
    // Calculate how far an object penetrated
    static float calculatePenetrationDistance(float measured_depth_mm,
                                             const SimpleWallConfig& config);
    
    // ========== Event Logging ==========
    static bool logEvent(const InterferenceEvent& event, const std::string& log_file);
    static std::string generateEventId();
    static std::string getCurrentTimestamp();
    
    // ========== Visualization ==========
    static void drawVirtualWall(cv::Mat& image, const SimpleWallConfig& config,
                              const cv::Scalar& color = cv::Scalar(0, 255, 255));
    static void drawCorners(cv::Mat& image, const SimpleWallConfig& config);
    static void drawInterferenceMarker(cv::Mat& image, const InterferenceEvent& event);
    
    // ========== Validation ==========
    static bool validateConfig(const SimpleWallConfig& config);
    static std::string validateConfigDetailed(const SimpleWallConfig& config);

private:
    // Helper: Check if point is inside quadrilateral
    static bool isPointInQuad(const cv::Point2f& point,
                            const cv::Point2f& p1, const cv::Point2f& p2,
                            const cv::Point2f& p3, const cv::Point2f& p4);
    
    // Helper: Bilinear interpolation normalized coordinates
    static void getNormalizedCoordinates(const cv::Point2i& pixel,
                                       const SimpleWallConfig& config,
                                       float& u, float& v);
};

// ============================================================================
// MOUSE CALLBACK HELPER
// ============================================================================

struct MouseCallbackData {
    SimpleWallConfig* config;
    cv::Mat* depth_frame;
    int corners_selected;
    bool setup_complete;
    
    MouseCallbackData() : 
        config(nullptr), 
        depth_frame(nullptr),
        corners_selected(0),
        setup_complete(false) {}
};

