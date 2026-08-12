#include "ArducamTOFCamera.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <open3d/Open3D.h>

using namespace Arducam;
using namespace std::chrono_literals;

#define MAX_DISTANCE 4000

struct CameraData {
    int camera_id;
    std::queue<std::shared_ptr<open3d::geometry::PointCloud>> point_clouds;
    std::mutex mtx;
    std::condition_variable cv;
    bool running = true;
    ArducamTOFCamera tof;
};

class DualPointCloudViewer {
private:
    CameraData camera0, camera1;
    std::thread capture_thread0, capture_thread1;
    std::shared_ptr<open3d::visualization::Visualizer> vis;
    std::shared_ptr<open3d::geometry::PointCloud> pcd0, pcd1;
    
public:
    DualPointCloudViewer() {
        camera0.camera_id = 0;
        camera1.camera_id = 1;
        
        // Initialize point clouds
        pcd0 = std::make_shared<open3d::geometry::PointCloud>();
        pcd1 = std::make_shared<open3d::geometry::PointCloud>();
    }
    
    bool initializeCameras() {
        std::cout << "Initializing Camera 0..." << std::endl;
        if (camera0.tof.open(Connection::CSI, 0)) {
            std::cerr << "Failed to open camera 0" << std::endl;
            return false;
        }
        
        if (camera0.tof.start(FrameType::DEPTH_FRAME)) {
            std::cerr << "Failed to start camera 0" << std::endl;
            return false;
        }
        
        camera0.tof.setControl(Control::RANGE, MAX_DISTANCE);
        
        std::cout << "Initializing Camera 1..." << std::endl;
        if (camera1.tof.open(Connection::CSI, 1)) {
            std::cerr << "Failed to open camera 1" << std::endl;
            return false;
        }
        
        if (camera1.tof.start(FrameType::DEPTH_FRAME)) {
            std::cerr << "Failed to start camera 1" << std::endl;
            return false;
        }
        
        camera1.tof.setControl(Control::RANGE, MAX_DISTANCE);
        
        auto info0 = camera0.tof.getCameraInfo();
        auto info1 = camera1.tof.getCameraInfo();
        
        std::cout << "Camera 0: " << info0.width << "x" << info0.height << std::endl;
        std::cout << "Camera 1: " << info1.width << "x" << info1.height << std::endl;
        
        return true;
    }
    
    void captureFrames(CameraData& camera) {
        while (camera.running) {
            ArducamFrameBuffer* frame = camera.tof.requestFrame(200);
            if (frame == nullptr) {
                continue;
            }
            
            Arducam::FrameFormat format;
            frame->getFormat(FrameType::DEPTH_FRAME, format);
            
            float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
            float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
            
            // Create point cloud
            auto point_cloud = std::make_shared<open3d::geometry::PointCloud>();
            
            // Camera intrinsics (adjust these values based on your camera)
            double fx = 200.0, fy = 200.0;
            double cx = format.width / 2.0, cy = format.height / 2.0;
            
            for (int y = 0; y < format.height; y++) {
                for (int x = 0; x < format.width; x++) {
                    int idx = y * format.width + x;
                    float depth = depth_ptr[idx];
                    float confidence = confidence_ptr[idx];
                    
                    // Filter by confidence and depth range
                    if (confidence > 30 && depth > 0 && depth < MAX_DISTANCE) {
                        // Convert to 3D coordinates
                        double z = depth / 1000.0; // Convert mm to meters
                        double x3d = (x - cx) * z / fx;
                        double y3d = (y - cy) * z / fy;
                        
                        // Offset camera 1 points for visualization
                        if (camera.camera_id == 1) {
                            x3d += 1.0; // Offset by 1 meter
                        }
                        
                        point_cloud->points_.push_back(Eigen::Vector3d(x3d, y3d, z));
                        
                        // Color based on depth
                        double color_val = std::min(1.0, depth / (double)MAX_DISTANCE);
                        if (camera.camera_id == 0) {
                            point_cloud->colors_.push_back(Eigen::Vector3d(color_val, 0, 1 - color_val)); // Blue to red
                        } else {
                            point_cloud->colors_.push_back(Eigen::Vector3d(0, color_val, 1 - color_val)); // Green to red
                        }
                    }
                }
            }
            
            // Add to queue
            {
                std::lock_guard<std::mutex> lock(camera.mtx);
                camera.point_clouds.push(point_cloud);
                if (camera.point_clouds.size() > 5) {
                    camera.point_clouds.pop();
                }
            }
            camera.cv.notify_one();
            
            camera.tof.releaseFrame(frame);
        }
    }
    
    void run() {
        if (!initializeCameras()) {
            return;
        }
        
        // Start capture threads
        capture_thread0 = std::thread(&DualPointCloudViewer::captureFrames, this, std::ref(camera0));
        capture_thread1 = std::thread(&DualPointCloudViewer::captureFrames, this, std::ref(camera1));
        
        // Initialize visualizer
        vis = std::make_shared<open3d::visualization::Visualizer>();
        vis->CreateVisualizerWindow("Dual ToF Point Cloud", 1280, 720);
        
        bool geometry_added = false;
        
        while (true) {
            // Get latest point clouds
            std::shared_ptr<open3d::geometry::PointCloud> latest_pcd0, latest_pcd1;
            
            {
                std::lock_guard<std::mutex> lock(camera0.mtx);
                if (!camera0.point_clouds.empty()) {
                    latest_pcd0 = camera0.point_clouds.back();
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(camera1.mtx);
                if (!camera1.point_clouds.empty()) {
                    latest_pcd1 = camera1.point_clouds.back();
                }
            }
            
            if (latest_pcd0 && latest_pcd1) {
                // Combine point clouds
                auto combined_pcd = std::make_shared<open3d::geometry::PointCloud>();
                *combined_pcd += *latest_pcd0;
                *combined_pcd += *latest_pcd1;
                
                if (!geometry_added) {
                    vis->AddGeometry(combined_pcd);
                    geometry_added = true;
                } else {
                    vis->UpdateGeometry(combined_pcd);
                }
                
                vis->PollEvents();
                vis->UpdateRender();
                
                if (!vis->PollEvents()) {
                    break;
                }
            }
            
            std::this_thread::sleep_for(33ms); // ~30 FPS
        }
        
        cleanup();
    }
    
    void cleanup() {
        std::cout << "Cleaning up..." << std::endl;
        
        camera0.running = false;
        camera1.running = false;
        
        if (capture_thread0.joinable()) capture_thread0.join();
        if (capture_thread1.joinable()) capture_thread1.join();
        
        camera0.tof.stop();
        camera0.tof.close();
        camera1.tof.stop();
        camera1.tof.close();
        
        if (vis) {
            vis->DestroyVisualizerWindow();
        }
    }
};

int main() {
    std::cout << "Dual ToF Camera Point Cloud Viewer" << std::endl;
    std::cout << "Press ESC or close window to exit" << std::endl;
    
    DualPointCloudViewer viewer;
    viewer.run();
    
    return 0;
}