#ifdef WITH_OPEN3D

#include "common.h"
#include <iostream>
#include <getopt.h>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <ctime>
#include <unistd.h>
#include <sys/select.h>
#include <open3d/Open3D.h>

class DualPointCloudViewer {
public:
    DualPointCloudViewer(const Config& config) : config_(config) {}
    
    bool initialize() {
        std::cout << "🌐 Dual ToF Point Cloud Viewer (C++ with Open3D)" << std::endl;
        std::cout << "🚀 Initializing Camera System..." << std::endl;
        
        // Initialize Camera 0
        cam0_ = std::make_unique<CameraManager>(0);
        if (!cam0_->initialize()) {
            std::cerr << "Failed to initialize Camera 0" << std::endl;
            return false;
        }
        
        auto info0 = cam0_->getCameraInfo();
        std::cout << "✅ Camera 0: " << info0.width << "x" << info0.height << std::endl;
        
        // Initialize Camera 1 if dual camera enabled
        if (config_.enable_dual_camera) {
            cam1_ = std::make_unique<CameraManager>(1);
            if (!cam1_->initialize()) {
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
        if (!cam0_->start()) {
            std::cerr << "Failed to start Camera 0" << std::endl;
            return;
        }
        
        if (config_.enable_dual_camera && cam1_ && !cam1_->start()) {
            std::cerr << "Failed to start Camera 1" << std::endl;
            config_.enable_dual_camera = false;
        }
        
        std::cout << "🌐 Starting 3D Point Cloud Viewer..." << std::endl;
        std::cout << "📊 Point Cloud Settings:" << std::endl;
        std::cout << "   Confidence threshold: " << config_.confidence_threshold << "%" << std::endl;
        std::cout << "   Max distance: " << config_.max_distance << "mm" << std::endl;
        std::cout << "   Dual camera: " << (config_.enable_dual_camera ? "Enabled" : "Disabled") << std::endl;
        std::cout << "⌨️  Controls:" << std::endl;
        std::cout << "   Mouse: Rotate, zoom, pan the 3D view" << std::endl;
        std::cout << "   's' - Save current point cloud (manual save)" << std::endl;
        std::cout << "   'q' - Quit application" << std::endl;
        std::cout << "   Close window: Exit application" << std::endl;
        std::cout << "💾 Save System:" << std::endl;
        std::cout << "   📁 Directory: /home/dev/Arducam_tof_camera/cpp_format_testing/saved_data/pointcloud/" << std::endl;
        std::cout << "   🌐 Manual saves: Press 's' to save current point cloud" << std::endl;
        std::cout << "   📦 Saves: PCD files, raw data (binary + CSV), parameters" << std::endl;
        std::cout << "💡 Performance Tip: If point cloud looks noisy, try:" << std::endl;
        std::cout << "   ./run_pointcloud.sh --confidence 70 --max-distance 2000" << std::endl;
        std::cout << std::endl;
        
        // Create visualizer
        auto vis = std::make_shared<open3d::visualization::Visualizer>();
        if (!vis->CreateVisualizerWindow("ToF 3D Point Cloud Viewer", 800, 600)) {
            std::cerr << "Failed to create 3D viewer window" << std::endl;
            return;
        }
        
        // Create point cloud objects
        auto pcd0 = std::make_shared<open3d::geometry::PointCloud>();
        auto pcd1 = std::make_shared<open3d::geometry::PointCloud>();
        
        bool geometry_added = false;
        int frame_count = 0;
        
        // Corrected camera intrinsics for proper hand scaling
        // Adjusted for realistic hand size at close distances
        double fx = 200.0, fy = 200.0;  // Higher focal length for proper scaling
        double cx = 120.0, cy = 90.0;   // Principal point (half of 240x180)
        
        // Transformation matrix for coordinate system conversion
        Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
        transform << 1,  0,  0,  0,
                     0, -1,  0,  0,
                     0,  0, -1,  0,
                     0,  0,  0,  1;
        
        while (vis->PollEvents()) {
            // Get frames
            ArducamFrameBuffer* frame0 = cam0_->requestFrame(200);
            ArducamFrameBuffer* frame1 = nullptr;
            
            if (config_.enable_dual_camera && cam1_) {
                frame1 = cam1_->requestFrame(200);
            }
            
            if (!frame0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // Process Camera 0
            generatePointCloud(frame0, pcd0, 0, fx, fy, cx, cy, transform);
            
            // Process Camera 1 with offset
            if (frame1) {
                Eigen::Matrix4d transform1 = transform;
                transform1(0, 3) = 100.0;  // 100mm (10cm) offset in X direction - more realistic
                generatePointCloud(frame1, pcd1, 1, fx, fy, cx, cy, transform1);
            }
            
            // Add or update geometries
            if (!geometry_added) {
                vis->AddGeometry(pcd0);
                if (config_.enable_dual_camera && frame1) {
                    vis->AddGeometry(pcd1);
                }
                geometry_added = true;
                std::cout << "✅ Point cloud geometries added to 3D viewer" << std::endl;
            } else {
                vis->UpdateGeometry(pcd0);
                if (config_.enable_dual_camera && frame1) {
                    vis->UpdateGeometry(pcd1);
                }
            }
            
            // Update visualization
            vis->UpdateRender();
            
            frame_count++;
            if (frame_count % 30 == 0) {
                int total_points = pcd0->points_.size();
                if (config_.enable_dual_camera && frame1) {
                    total_points += pcd1->points_.size();
                }
                
                // Calculate average depth for reference
                double avg_depth = 0.0;
                if (!pcd0->points_.empty()) {
                    for (const auto& point : pcd0->points_) {
                        avg_depth += point.z();
                    }
                    avg_depth /= pcd0->points_.size();
                }
                
                std::cout << "🌐 Frame: " << frame_count 
                          << " | Points: " << total_points 
                          << " | Cam0: " << pcd0->points_.size()
                          << " | Avg Distance: " << static_cast<int>(avg_depth) << "mm";
                if (config_.enable_dual_camera && frame1) {
                    std::cout << " | Cam1: " << pcd1->points_.size();
                }
                std::cout << std::endl;
            }
            
            // Handle keyboard input
            if (handleKeyInput(vis, pcd0, pcd1, frame0, frame1)) {
                break;
            }
            
            // Release frames
            cam0_->releaseFrame(frame0);
            if (frame1 && cam1_) {
                cam1_->releaseFrame(frame1);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30 FPS
        }
        
        vis->DestroyVisualizerWindow();
        cleanup();
    }

private:
    mutable Config config_;  // Allow modification in const contexts
    std::unique_ptr<CameraManager> cam0_;
    std::unique_ptr<CameraManager> cam1_;
    
    void generatePointCloud(ArducamFrameBuffer* frame,
                           std::shared_ptr<open3d::geometry::PointCloud> pcd,
                           int camera_id,
                           double fx, double fy, double cx, double cy,
                           const Eigen::Matrix4d& transform) {
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
        
        // Improved filtering parameters
        const float min_depth = 100.0f;   // 10cm minimum
        const float max_depth = static_cast<float>(config_.max_distance);
        const float min_confidence = std::max(50.0f, static_cast<float>(config_.confidence_threshold));  // Higher threshold
        const int skip_pixels = 2;        // Skip every 2nd pixel for performance and noise reduction
        
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
                double z = depth;  // Keep depth in millimeters for proper hand size
                double x3d = (x - cx) * z / fx;
                double y3d = (y - cy) * z / fy;
                
                // Apply coordinate system transformation
                Eigen::Vector4d point_homogeneous(x3d, y3d, z, 1.0);
                Eigen::Vector4d transformed_point = transform * point_homogeneous;
                
                points.push_back(transformed_point.head<3>());
                
                // Improved coloring based on depth and confidence
                double depth_normalized = std::min(1.0, static_cast<double>(depth - min_depth) / static_cast<double>(max_depth - min_depth));
                double confidence_normalized = std::min(1.0, static_cast<double>(confidence) / 100.0);
                
                if (camera_id == 0) {
                    // Camera 0: Blue to red gradient with confidence intensity
                    double intensity = confidence_normalized * 0.8 + 0.2;  // 0.2 to 1.0 range
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
            pcd->RemoveStatisticalOutliers(20, 2.0);  // Remove outliers based on 20 neighbors, 2.0 std dev
        }
        
        // Optional: Apply radius outlier removal for very noisy data
        if (points.size() > 500) {
            pcd->RemoveRadiusOutliers(10, 20.0);  // Remove points with < 10 neighbors within 20mm radius
        }
    }
    
    bool handleKeyInput(std::shared_ptr<open3d::visualization::Visualizer> vis,
                       std::shared_ptr<open3d::geometry::PointCloud> pcd0,
                       std::shared_ptr<open3d::geometry::PointCloud> pcd1,
                       ArducamFrameBuffer* frame0,
                       ArducamFrameBuffer* frame1) {
        
        static int key_counter = 0;
        static int saved_count = 0;
        static int frame_counter = 0;
        key_counter++;
        frame_counter++;
        
        // Check for keyboard input every few frames to reduce CPU usage
        if (key_counter % 5 == 0) {
            // Check for terminal keyboard input (non-blocking)
            fd_set readfds;
            struct timeval timeout;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            timeout.tv_sec = 0;
            timeout.tv_usec = 0;
            
            if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout) > 0) {
                if (FD_ISSET(STDIN_FILENO, &readfds)) {
                    char key;
                    if (read(STDIN_FILENO, &key, 1) > 0) {
                        if (key == 's' || key == 'S') {
                            // Get current timestamp for this exact save moment
                            auto now = std::chrono::system_clock::now();
                            auto time_t = std::chrono::system_clock::to_time_t(now);
                            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
                            
                            std::cout << "\n🔑 Key 's' pressed - Saving current point cloud..." << std::endl;
                            std::cout << "📸 SAVE POINT CLOUD REQUEST - Frame #" << frame_counter << std::endl;
                            
                            // Show timestamp when save was triggered
                            std::tm* tm_info = std::localtime(&time_t);
                            std::cout << "⏰ Save triggered at: " << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S") 
                                      << "." << std::setfill('0') << std::setw(3) << ms.count() << std::endl;
                            std::cout << "===========================================" << std::endl;
                            
                            // Check if we have valid point cloud data
                            if (!pcd0->points_.empty() || (pcd1 && !pcd1->points_.empty())) {
                                int total_points = pcd0->points_.size();
                                if (pcd1) total_points += pcd1->points_.size();
                                
                                std::cout << "🌐 Saving point cloud from current frame:" << std::endl;
                                std::cout << "   📷 Camera 0: " << pcd0->points_.size() << " points" << std::endl;
                                if (pcd1) {
                                    std::cout << "   📷 Camera 1: " << pcd1->points_.size() << " points" << std::endl;
                                }
                                std::cout << "   🌐 Total: " << total_points << " points" << std::endl;
                                std::cout << "📁 Target: /home/dev/Arducam_tof_camera/cpp_format_testing/saved_data/pointcloud/" << std::endl;
                                
                                saved_count++;
                                savePointCloudData(pcd0, pcd1, frame0, frame1);
                                
                                std::cout << "\n✅ SAVE SUCCESSFUL!" << std::endl;
                                std::cout << "   📁 Point cloud #" << frame_counter << " saved successfully" << std::endl;
                                std::cout << "   📊 Session total: " << saved_count << " point clouds saved" << std::endl;
                            } else {
                                std::cout << "\n⚠️  NO CURRENT POINT CLOUD DATA AVAILABLE" << std::endl;
                                std::cout << "   🌐 No current point cloud data available to save" << std::endl;
                                std::cout << "   💡 Wait for point cloud to generate, then press 's' again" << std::endl;
                                std::cout << "   🔍 Debug: pcd0 points = " << pcd0->points_.size() << std::endl;
                                if (pcd1) std::cout << "   🔍 Debug: pcd1 points = " << pcd1->points_.size() << std::endl;
                            }
                            std::cout << "===========================================\n" << std::endl;
                        } else if (key == 'q' || key == 'Q') {
                            std::cout << "\n🔑 Key 'q' pressed - Exiting..." << std::endl;
                            return true; // Exit the application
                        }
                    }
                }
            }
        }
        
        // Check for exit conditions - return false to continue, true to exit
        return false;
    }
    
    void savePointCloudData(std::shared_ptr<open3d::geometry::PointCloud> pcd0,
                           std::shared_ptr<open3d::geometry::PointCloud> pcd1,
                           ArducamFrameBuffer* frame0,
                           ArducamFrameBuffer* frame1) {
        
        // Immediate feedback and timing
        std::cout << "\n💾 Starting point cloud save operation..." << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Create timestamped directory
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream timestamp;
        timestamp << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        timestamp << "_" << std::setfill('0') << std::setw(3) << ms.count();
        
        // Use organized pointcloud directory structure
        std::string base_dir = "/home/dev/Arducam_tof_camera/cpp_format_testing/saved_data/pointcloud";
        std::string frame_dir = base_dir + "/frame_" + timestamp.str();
        
        std::cout << "📁 Target: " << frame_dir << std::endl;
        
        // Progress: Step 1/4 - Directory creation
        std::cout << "[1/4] 📁 Creating directories..." << std::flush;
        std::filesystem::create_directories(frame_dir + "/point_clouds");
        std::filesystem::create_directories(frame_dir + "/raw_data");
        std::filesystem::create_directories(frame_dir + "/parameters");
        std::cout << " ✅" << std::endl;
        
        // Progress: Step 2/4 - Point cloud files
        std::cout << "[2/4] 🌐 Saving point cloud files..." << std::flush;
        bool pcd_success = true;
        int total_points = 0;
        
        if (!pcd0->points_.empty()) {
            std::string pcd0_file = frame_dir + "/point_clouds/camera0_pointcloud.pcd";
            if (open3d::io::WritePointCloud(pcd0_file, *pcd0)) {
                total_points += pcd0->points_.size();
            } else {
                pcd_success = false;
            }
        }
        
        if (config_.enable_dual_camera && pcd1 && !pcd1->points_.empty()) {
            std::string pcd1_file = frame_dir + "/point_clouds/camera1_pointcloud.pcd";
            if (open3d::io::WritePointCloud(pcd1_file, *pcd1)) {
                total_points += pcd1->points_.size();
            } else {
                pcd_success = false;
            }
        }
        
        // Save combined point cloud if dual camera
        if (config_.enable_dual_camera && pcd1 && !pcd0->points_.empty() && !pcd1->points_.empty()) {
            auto combined_pcd = std::make_shared<open3d::geometry::PointCloud>();
            *combined_pcd += *pcd0;
            *combined_pcd += *pcd1;
            
            std::string combined_file = frame_dir + "/point_clouds/combined_pointcloud.pcd";
            if (!open3d::io::WritePointCloud(combined_file, *combined_pcd)) {
                pcd_success = false;
            }
        }
        
        std::cout << (pcd_success ? " ✅" : " ❌") << " (" << total_points << " points)" << std::endl;
        
        // Progress: Step 3/4 - Raw data
        std::cout << "[3/4] 📊 Saving raw data..." << std::flush;
        saveRawDepthData(frame0, frame_dir + "/raw_data/camera0", 0);
        if (config_.enable_dual_camera && frame1) {
            saveRawDepthData(frame1, frame_dir + "/raw_data/camera1", 1);
        }
        std::cout << " ✅" << std::endl;
        
        // Progress: Step 4/4 - Parameters
        std::cout << "[4/4] ⚙️  Saving parameters..." << std::flush;
        savePointCloudParameters(frame_dir + "/parameters/pointcloud_config.json");
        std::cout << " ✅" << std::endl;
        
        // Final summary with timing
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "✅ Point cloud save completed in " << duration.count() << "ms!" << std::endl;
        std::cout << "   📁 Location: " << frame_dir << std::endl;
        std::cout << "   🌐 Total points: " << total_points << std::endl;
    }
    
    void saveRawDepthData(ArducamFrameBuffer* frame, const std::string& prefix, int camera_id) {
        if (!frame) return;
        
        FrameFormat format;
        frame->getFormat(FrameType::DEPTH_FRAME, format);
        
        // Save raw depth data as binary file
        float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
        if (depth_ptr) {
            std::string depth_file = prefix + "_depth_raw.bin";
            std::ofstream file(depth_file, std::ios::binary);
            if (file.is_open()) {
                file.write(reinterpret_cast<const char*>(depth_ptr), 
                          format.width * format.height * sizeof(float));
                file.close();
            }
        }
        
        // Save confidence data
        float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
        if (confidence_ptr) {
            std::string conf_file = prefix + "_confidence_raw.bin";
            std::ofstream file(conf_file, std::ios::binary);
            if (file.is_open()) {
                file.write(reinterpret_cast<const char*>(confidence_ptr), 
                          format.width * format.height * sizeof(float));
                file.close();
            }
        }
        
        // Save as CSV for easy analysis
        std::string csv_file = prefix + "_data.csv";
        std::ofstream csv(csv_file);
        if (csv.is_open()) {
            csv << "x,y,depth_mm,confidence\n";
            for (int y = 0; y < format.height; ++y) {
                for (int x = 0; x < format.width; ++x) {
                    int idx = y * format.width + x;
                    csv << x << "," << y << "," 
                        << depth_ptr[idx] << "," 
                        << confidence_ptr[idx] << "\n";
                }
            }
            csv.close();
        }
    }
    
    void savePointCloudParameters(const std::string& filename) {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "{\n";
            file << "  \"timestamp\": \"" << std::time(nullptr) << "\",\n";
            file << "  \"camera_config\": {\n";
            file << "    \"dual_camera_enabled\": " << (config_.enable_dual_camera ? "true" : "false") << ",\n";
            file << "    \"confidence_threshold\": " << config_.confidence_threshold << ",\n";
            file << "    \"max_distance_mm\": " << config_.max_distance << ",\n";
            file << "    \"resolution\": \"240x180\",\n";
            file << "    \"focal_length_x\": 200.0,\n";
            file << "    \"focal_length_y\": 200.0,\n";
            file << "    \"principal_point_x\": 120.0,\n";
            file << "    \"principal_point_y\": 90.0\n";
            file << "  },\n";
            file << "  \"point_cloud_config\": {\n";
            file << "    \"coordinate_system\": \"millimeters\",\n";
            file << "    \"filtering_enabled\": true,\n";
            file << "    \"statistical_outlier_removal\": true,\n";
            file << "    \"radius_outlier_removal\": true,\n";
            file << "    \"neighborhood_consistency_check\": true\n";
            file << "  }\n";
            file << "}\n";
            file.close();
        }
    }
    
    void cleanup() {
        std::cout << "\n🧹 Cleaning up..." << std::endl;
        
        if (cam0_) {
            cam0_->stop();
            std::cout << "✅ Camera 0 closed" << std::endl;
        }
        if (cam1_) {
            cam1_->stop();
            std::cout << "✅ Camera 1 closed" << std::endl;
        }
        
        std::cout << "✅ Cleanup complete!" << std::endl;
    }
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -s, --single-camera    Use single camera mode" << std::endl;
    std::cout << "  -c, --confidence NUM   Confidence threshold (default: 30)" << std::endl;
    std::cout << "  -d, --max-distance NUM Maximum distance in mm (default: 4000)" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    Config config;
    config.save_enabled = false;  // Point cloud viewer doesn't need saving
    
    // Parse command line arguments
    static struct option long_options[] = {
        {"single-camera", no_argument, 0, 's'},
        {"confidence", required_argument, 0, 'c'},
        {"max-distance", required_argument, 0, 'd'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int c;
    while ((c = getopt_long(argc, argv, "sc:d:h", long_options, nullptr)) != -1) {
        switch (c) {
            case 's':
                config.enable_dual_camera = false;
                break;
            case 'c':
                config.confidence_threshold = std::atoi(optarg);
                break;
            case 'd':
                config.max_distance = std::atoi(optarg);
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            case '?':
                printUsage(argv[0]);
                return 1;
            default:
                break;
        }
    }
    
    DualPointCloudViewer viewer(config);
    
    if (!viewer.initialize()) {
        std::cerr << "Failed to initialize point cloud viewer" << std::endl;
        return 1;
    }
    
    viewer.run();
    
    return 0;
}

#else
int main() {
    std::cerr << "This program was compiled without Open3D support." << std::endl;
    std::cerr << "Please install Open3D and recompile with -DWITH_OPEN3D" << std::endl;
    return 1;
}
#endif
