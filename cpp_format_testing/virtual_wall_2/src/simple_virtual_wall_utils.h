#pragma once

#include <opencv2/opencv.hpp>
#include <json/json.h>
#include <string>
#include <vector>
#include <chrono>

// ===========================================================================================
// SIMPLIFIED VIRTUAL WALL SYSTEM
// ===========================================================================================
// This is a clean, practical implementation that uses:
// - User-provided ground truth dimensions (measured with tape measure)
// - Simple 2D pixel-to-real-world mapping (bilinear interpolation)
// - Straightforward penetration detection (depth comparison)
// - No complex 3D coordinate calculations needed
// ===========================================================================================

// Frame constants
constexpr int FRAME_WIDTH = 240;
constexpr int FRAME_HEIGHT = 180;

// Clustering constants
constexpr int CLUSTER_SIZE = 10;  // Each cluster is 10x10 pixels (for monitoring)
constexpr int EDGE_CLUSTER_SIZE = 3;  // Small 3x3 cluster for corner edge detection
constexpr int CLUSTERS_X = FRAME_WIDTH / CLUSTER_SIZE;   // 24 clusters horizontally
constexpr int CLUSTERS_Y = FRAME_HEIGHT / CLUSTER_SIZE;  // 18 clusters vertically

// Simple Virtual Wall Configuration
struct SimpleVirtualWallConfig {
    // Corner pixel coordinates (selected by user)
    cv::Point2i upper_left_px;
    cv::Point2i upper_right_px;
    cv::Point2i lower_left_px;
    cv::Point2i lower_right_px;
    
    // Depth values at corners (in mm)
    float upper_left_depth_mm;
    float upper_right_depth_mm;
    float lower_left_depth_mm;
    float lower_right_depth_mm;
    
    // User-provided REAL dimensions (measured with tape measure)
    float actual_width_cm;       // Real width of the stand (e.g., 95cm)
    float actual_height_cm;      // Real height of the monitored area (e.g., 190cm)
    
    // Wall depth (average of corner depths, for penetration detection)
    float wall_depth_mm;         // Average depth at wall surface
    
    // Detection threshold
    float penetration_threshold_mm;  // How close counts as penetration (default: 200mm)
    
    // Metadata
    std::string unique_id;
    std::string creation_timestamp;
    bool is_valid;
    
    SimpleVirtualWallConfig() : 
        upper_left_px(0, 0),
        upper_right_px(0, 0),
        lower_left_px(0, 0),
        lower_right_px(0, 0),
        upper_left_depth_mm(0),
        upper_right_depth_mm(0),
        lower_left_depth_mm(0),
        lower_right_depth_mm(0),
        actual_width_cm(95.0f),     // Default to typical shelf
        actual_height_cm(190.0f),   // Default to typical height
        wall_depth_mm(0),
        penetration_threshold_mm(200.0f),  // 20cm tolerance for noisy sensor
        is_valid(false) {}
};

// Interference Event (simplified - X,Y only, no Z)
struct SimpleInterferenceEvent {
    std::string timestamp;
    std::string event_id;
    
    // Pixel coordinates where interference occurred
    cv::Point2i pixel_coord;
    
    // Real-world coordinates (X, Y in cm)
    float x_position_cm;    // Horizontal position (0 to actual_width_cm)
    float y_position_cm;    // Vertical position (0 to actual_height_cm)
    
    // Depth information
    float measured_depth_mm;      // Actual depth at interference point
    float wall_depth_mm;          // Expected wall depth
    float penetration_depth_mm;   // How much penetration occurred
    
    SimpleInterferenceEvent() : 
        pixel_coord(0, 0),
        x_position_cm(0),
        y_position_cm(0),
        measured_depth_mm(0),
        wall_depth_mm(0),
        penetration_depth_mm(0) {}
};

// Spatial Cluster for Noise Filtering
struct DepthCluster {
    cv::Point2i grid_pos;        // Grid position (cluster coordinates)
    cv::Point2i pixel_center;    // Center pixel position in image
    cv::Rect bounds;             // Pixel bounds of this cluster
    float median_depth;          // Median depth (filters outliers!)
    float baseline_median;       // Baseline median depth
    int valid_pixel_count;       // Number of valid pixels in cluster
    int interference_pixel_count; // Number of individual pixels that penetrate the wall
    float interference_pixel_ratio;
    bool has_motion;             // Whether cluster shows significant motion
    bool penetrates_wall;        // Whether cluster penetrates virtual wall
    
    DepthCluster() : 
        grid_pos(0, 0),
        pixel_center(0, 0),
        bounds(0, 0, 0, 0),
        median_depth(0),
        baseline_median(0),
        valid_pixel_count(0),
        interference_pixel_count(0),
        interference_pixel_ratio(0),
        has_motion(false),
        penetrates_wall(false) {}
};

// Draggable Corner for Setup UI
struct DraggableCorner {
    cv::Point2i position;
    bool is_dragging;
    bool is_hovered;
    bool is_set;
    int corner_id;  // 0=LL, 1=LR, 2=UL, 3=UR (camera mounted upside down)
    float depth;
    bool has_error;     // Whether corner has validation errors
    std::string error_message; // Detailed error message for UI feedback
    
    DraggableCorner() : 
        position(0, 0), 
        is_dragging(false), 
        is_hovered(false), 
        is_set(false), 
        corner_id(-1), 
        depth(0),
        has_error(false),
        error_message("") {}
        
    DraggableCorner(int x, int y, int id) : 
        position(x, y), 
        is_dragging(false), 
        is_hovered(false), 
        is_set(false), 
        corner_id(id), 
        depth(0),
        has_error(false),
        error_message("") {}
};

// Interactive Calibration Data
struct SimpleCalibrationData {
    std::vector<DraggableCorner> corners;
    bool calibration_complete;
    bool corners_positioned;
    int active_corner_id;
    cv::Point2f last_mouse_pos;
    
    SimpleCalibrationData() : 
        calibration_complete(false),
        corners_positioned(false), 
        active_corner_id(-1), 
        last_mouse_pos(0, 0) {
        
        // Initialize 4 corners in a rectangle at center
        int center_x = FRAME_WIDTH / 2;
        int center_y = FRAME_HEIGHT / 2;
        int rect_size = 60;
        
        corners.resize(4);
        corners[0] = DraggableCorner(center_x - rect_size/2, center_y - rect_size/2, 0); // Lower-left (top-left in image)
        corners[1] = DraggableCorner(center_x + rect_size/2, center_y - rect_size/2, 1); // Lower-right (top-right in image)
        corners[2] = DraggableCorner(center_x - rect_size/2, center_y + rect_size/2, 2); // Upper-left (bottom-left in image)
        corners[3] = DraggableCorner(center_x + rect_size/2, center_y + rect_size/2, 3); // Upper-right (bottom-right in image)
    }
};

// ===========================================================================================
// UTILITY CLASS - Simple Virtual Wall Utils
// ===========================================================================================

class SimpleVirtualWallUtils {
public:
    // ===========================================================================================
    // CONFIGURATION MANAGEMENT
    // ===========================================================================================
    
    static bool saveConfig(const SimpleVirtualWallConfig& config, const std::string& filepath);
    static bool loadConfig(SimpleVirtualWallConfig& config, const std::string& filepath);
    static std::string getDefaultConfigPath();
    static bool validateConfig(const SimpleVirtualWallConfig& config);
    
    // ===========================================================================================
    // COORDINATE MAPPING (The Heart of the System)
    // ===========================================================================================
    
    // Convert pixel coordinates to real-world coordinates using bilinear interpolation
    static cv::Point2f pixelToRealWorld(const cv::Point2i& pixel, 
                                       const SimpleVirtualWallConfig& config);
    
    // Convert real-world coordinates back to pixel coordinates (for visualization)
    static cv::Point2i realWorldToPixel(const cv::Point2f& real_world, 
                                       const SimpleVirtualWallConfig& config);
    
    // Interpolate wall depth at any pixel position using bilinear interpolation
    static float interpolateWallDepth(const cv::Point2i& pixel, 
                                     const SimpleVirtualWallConfig& config);
    
    // ===========================================================================================
    // PENETRATION DETECTION
    // ===========================================================================================
    
    // Check if a point is inside the virtual wall boundary
    static bool isPointInsideBoundary(const cv::Point2i& pixel, 
                                     const SimpleVirtualWallConfig& config);

    // Extended tracking region below the calibrated wall band for downward-facing shelf setups.
    static bool isPointInsideTrackingBoundary(const cv::Point2i& pixel,
                                             const SimpleVirtualWallConfig& config);
    
    // Check if a point is penetrating the virtual wall (closer than wall depth)
    static bool isPointPenetrating(const cv::Point2i& pixel, float depth_mm, 
                                  const SimpleVirtualWallConfig& config);
    
    // Calculate how much penetration occurred (negative = behind wall, positive = in front)
    static float calculatePenetrationDistance(const cv::Point2i& pixel, float depth_mm,
                                             const SimpleVirtualWallConfig& config);
    
    // ===========================================================================================
    // EVENT LOGGING
    // ===========================================================================================
    
    static bool logInterferenceEvent(const SimpleInterferenceEvent& event, 
                                    const std::string& log_file);
    static std::string generateEventId();
    static std::string getCurrentTimestamp();
    
    // ===========================================================================================
    // SPATIAL CLUSTERING (Noise Filtering)
    // ===========================================================================================
    
    // Compute median depth for a cluster (filters outliers!)
    static float computeClusterMedianDepth(const cv::Mat& depth_frame, 
                                          const cv::Mat& confidence_frame,
                                          const cv::Rect& cluster_bounds,
                                          int confidence_threshold,
                                          int& valid_pixel_count,
                                          float max_valid_depth_mm = 3500.0f);
    
    // Get cluster median depth at a specific position (for corner selection)
    static float getClusterMedianAtPosition(const cv::Mat& depth_frame,
                                           const cv::Mat& confidence_frame,
                                           const cv::Point2i& corner_pos,
                                           int cluster_size,
                                           int confidence_threshold,
                                           int corner_id = -1);
    
    // Create depth clusters from frame (returns grid of clusters)
    static std::vector<DepthCluster> createDepthClusters(const cv::Mat& depth_frame,
                                                         const cv::Mat& confidence_frame,
                                                         const cv::Mat& baseline_depth,
                                                         const SimpleVirtualWallConfig& config,
                                                         int confidence_threshold,
                                                         float motion_threshold_mm,
                                                         float max_valid_depth_mm = 3500.0f,
                                                         int min_valid_pixels = 3,
                                                         bool far_object_mode = false,
                                                         int min_interference_pixels = 2);
    
    // ===========================================================================================
    // VISUALIZATION
    // ===========================================================================================
    
    static void drawVirtualWall(cv::Mat& image, const SimpleVirtualWallConfig& config);
    static void drawInteractiveOverlay(cv::Mat& image, const SimpleCalibrationData& calib_data);
    static void drawDraggableCorners(cv::Mat& image, const std::vector<DraggableCorner>& corners);
    static int findNearestCorner(const cv::Point2i& mouse_pos, 
                                const std::vector<DraggableCorner>& corners, 
                                int threshold = 15);
    
    // Grid visualization
    static void drawClusterGrid(cv::Mat& image, int cluster_size, 
                               const cv::Scalar& color = cv::Scalar(100, 100, 100));
    static void drawClusterGridWithWall(cv::Mat& image, int cluster_size,
                                       const SimpleVirtualWallConfig& config);
    
    // Corner visualization with square edges
    static void drawCornerWithSquareEdge(cv::Mat& image, const DraggableCorner& corner, 
                                        int edge_size = 20);
    
    // ===========================================================================================
    // UTILITY HELPERS
    // ===========================================================================================
    
    static float calculateDistance(const cv::Point2f& p1, const cv::Point2f& p2);
    static float clamp(float value, float min, float max);
    
    // ===========================================================================================
    // DEPTH VALIDATION & UNIT CONVERSION
    // ===========================================================================================
    
    // Validate and convert depth units (handles μm, m, mm automatically)
    static float validateAndConvertDepth(float raw_depth, const std::string& source = "");
};

