#include "ArducamTOFCamera.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

using namespace Arducam;

// MAX_DISTANCE value modifiable is 2 or 4
#define MAX_DISTANCE 4000

class DualToFCamera {
private:
    ArducamTOFCamera tof0, tof1;
    std::atomic<bool> running{false};
    std::atomic<int> confidence_value{30};
    
    // Frame queues for thread-safe operation
    std::queue<ArducamFrameBuffer*> frame_queue_cam0;
    std::queue<ArducamFrameBuffer*> frame_queue_cam1;
    std::mutex queue_mutex_cam0, queue_mutex_cam1;
    
    // Mouse interaction rectangles
    cv::Rect selectRect_cam0{0, 0, 0, 0};
    cv::Rect followRect_cam0{0, 0, 0, 0};
    cv::Rect selectRect_cam1{0, 0, 0, 0};
    cv::Rect followRect_cam1{0, 0, 0, 0};
    
    int max_width = 240;
    int max_height = 180;
    int max_range_cam0 = 0;
    int max_range_cam1 = 0;
    
    uint8_t* preview_ptr_cam0;
    uint8_t* preview_ptr_cam1;
    
public:
    DualToFCamera() {
        preview_ptr_cam0 = nullptr;
        preview_ptr_cam1 = nullptr;
    }
    
    ~DualToFCamera() {
        cleanup();
    }
    
    static void on_confidence_changed_cam0(int pos, void* userdata) {
        DualToFCamera* camera = static_cast<DualToFCamera*>(userdata);
        camera->confidence_value = pos;
    }
    
    static void on_confidence_changed_cam1(int pos, void* userdata) {
        DualToFCamera* camera = static_cast<DualToFCamera*>(userdata);
        camera->confidence_value = pos;
    }
    
    static void on_mouse_cam0(int event, int x, int y, int flags, void* param) {
        DualToFCamera* camera = static_cast<DualToFCamera*>(param);
        camera->handle_mouse_cam0(event, x, y, flags);
    }
    
    static void on_mouse_cam1(int event, int x, int y, int flags, void* param) {
        DualToFCamera* camera = static_cast<DualToFCamera*>(param);
        camera->handle_mouse_cam1(event, x, y, flags);
    }
    
    void handle_mouse_cam0(int event, int x, int y, int flags) {
        if (x < 4 || x > (max_width - 4) || y < 4 || y > (max_height - 4))
            return;
            
        switch (event) {
        case cv::EVENT_LBUTTONDOWN:
            break;
        case cv::EVENT_LBUTTONUP:
            selectRect_cam0.x = x - 4 > 0 ? x - 4 : 0;
            selectRect_cam0.y = y - 4 > 0 ? y - 4 : 0;
            selectRect_cam0.width = 8;
            selectRect_cam0.height = 8;
            break;
        default:
            followRect_cam0.x = x - 4 > 0 ? x - 4 : 0;
            followRect_cam0.y = y - 4 > 0 ? y - 4 : 0;
            followRect_cam0.width = 8;
            followRect_cam0.height = 8;
            break;
        }
    }
    
    void handle_mouse_cam1(int event, int x, int y, int flags) {
        if (x < 4 || x > (max_width - 4) || y < 4 || y > (max_height - 4))
            return;
            
        switch (event) {
        case cv::EVENT_LBUTTONDOWN:
            break;
        case cv::EVENT_LBUTTONUP:
            selectRect_cam1.x = x - 4 > 0 ? x - 4 : 0;
            selectRect_cam1.y = y - 4 > 0 ? y - 4 : 0;
            selectRect_cam1.width = 8;
            selectRect_cam1.height = 8;
            break;
        default:
            followRect_cam1.x = x - 4 > 0 ? x - 4 : 0;
            followRect_cam1.y = y - 4 > 0 ? y - 4 : 0;
            followRect_cam1.width = 8;
            followRect_cam1.height = 8;
            break;
        }
    }
    
    void display_fps() {
        using std::chrono::high_resolution_clock;
        using namespace std::literals;
        static int count = 0;
        static auto time_beg = high_resolution_clock::now();
        auto time_end = high_resolution_clock::now();
        ++count;
        auto duration_ms = (time_end - time_beg) / 1ms;
        if (duration_ms >= 1000) {
            std::cout << "fps: " << count << std::endl;
            count = 0;
            time_beg = time_end;
        }
    }
    
    void save_image(float* image, int width, int height, int camera_id) {
        using namespace std::literals;
        auto now = std::chrono::system_clock::now().time_since_epoch() / 1ms;
        std::string filename = "depth_cam" + std::to_string(camera_id) + "_" + 
                              std::to_string(width) + "_" + std::to_string(height) + 
                              "_f32_" + std::to_string(now) + ".raw";
        std::ofstream file(filename, std::ios::binary);
        file.write(reinterpret_cast<char*>(image), width * height * sizeof(float));
        file.close();
        std::cout << "Saved: " << filename << std::endl;
    }
    
    cv::Mat matRotateClockWise180(cv::Mat src) {
        if (src.empty()) {
            std::cerr << "RotateMat src is empty!" << std::endl;
        }
        flip(src, src, 0);
        flip(src, src, 1);
        return src;
    }
    
    void getPreviewRGB(cv::Mat preview_ptr, cv::Mat amplitude_image_ptr) {
        preview_ptr.setTo(cv::Scalar(0, 0, 0), amplitude_image_ptr < confidence_value.load());
    }
    
    bool initialize_cameras() {
        std::cout << "Initializing dual ToF cameras..." << std::endl;
        
        // Open Camera 0 (CSI port 0)
        std::cout << "Opening Camera 0..." << std::endl;
        if (tof0.open(Connection::CSI, 0)) {
            std::cerr << "Failed to open camera 0" << std::endl;
            return false;
        }
        
        // Open Camera 1 (CSI port 1)
        std::cout << "Opening Camera 1..." << std::endl;
        if (tof1.open(Connection::CSI, 1)) {
            std::cerr << "Failed to open camera 1" << std::endl;
            tof0.close();
            return false;
        }
        
        // Set ranges before starting (important for ToF cameras)
        std::cout << "Setting camera ranges..." << std::endl;
        tof0.setControl(Control::RANGE, MAX_DISTANCE);
        tof1.setControl(Control::RANGE, MAX_DISTANCE);
        tof0.getControl(Control::RANGE, &max_range_cam0);
        tof1.getControl(Control::RANGE, &max_range_cam1);
        
        // Start Camera 0
        std::cout << "Starting Camera 0..." << std::endl;
        if (tof0.start(FrameType::DEPTH_FRAME)) {
            std::cerr << "Failed to start camera 0" << std::endl;
            tof1.close();
            tof0.close();
            return false;
        }
        
        // Add small delay between camera starts
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Start Camera 1
        std::cout << "Starting Camera 1..." << std::endl;
        if (tof1.start(FrameType::DEPTH_FRAME)) {
            std::cerr << "Failed to start camera 1" << std::endl;
            tof0.stop();
            tof0.close();
            tof1.close();
            return false;
        }
        
        // Get camera info
        auto info0 = tof0.getCameraInfo();
        auto info1 = tof1.getCameraInfo();
        
        std::cout << "Camera 0: (" << info0.width << "x" << info0.height << ")" << std::endl;
        std::cout << "Camera 1: (" << info1.width << "x" << info1.height << ")" << std::endl;
        std::cout << "Both cameras initialized successfully!" << std::endl;
        
        max_width = std::max(info0.width, info1.width);
        max_height = std::max(info0.height, info1.height);
        
        // Allocate preview buffers
        preview_ptr_cam0 = new uint8_t[info0.width * info0.height * 3];
        preview_ptr_cam1 = new uint8_t[info1.width * info1.height * 3];
        
        return true;
    }
    
    void capture_frames_cam0() {
        while (running.load()) {
            ArducamFrameBuffer* frame = tof0.requestFrame(200);  // Increased timeout
            if (frame != nullptr) {
                std::lock_guard<std::mutex> lock(queue_mutex_cam0);
                
                // Keep queue size manageable
                if (frame_queue_cam0.size() >= 2) {
                    ArducamFrameBuffer* old_frame = frame_queue_cam0.front();
                    frame_queue_cam0.pop();
                    tof0.releaseFrame(old_frame);
                }
                
                frame_queue_cam0.push(frame);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Longer delay when no frame
            }
        }
    }
    
    void capture_frames_cam1() {
        while (running.load()) {
            ArducamFrameBuffer* frame = tof1.requestFrame(200);  // Increased timeout
            if (frame != nullptr) {
                std::lock_guard<std::mutex> lock(queue_mutex_cam1);
                
                // Keep queue size manageable
                if (frame_queue_cam1.size() >= 2) {
                    ArducamFrameBuffer* old_frame = frame_queue_cam1.front();
                    frame_queue_cam1.pop();
                    tof1.releaseFrame(old_frame);
                }
                
                frame_queue_cam1.push(frame);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Longer delay when no frame
            }
        }
    }
    
    void process_frame(ArducamFrameBuffer* frame, int camera_id, 
                      cv::Rect& selectRect, cv::Rect& followRect,
                      uint8_t* preview_ptr, int max_range) {
        Arducam::FrameFormat format;
        frame->getFormat(FrameType::DEPTH_FRAME, format);
        
        float* depth_ptr = (float*)frame->getData(FrameType::DEPTH_FRAME);
        float* confidence_ptr = (float*)frame->getData(FrameType::CONFIDENCE_FRAME);
        
        cv::Mat result_frame(format.height, format.width, CV_8UC3, preview_ptr);
        cv::Mat depth_frame(format.height, format.width, CV_32F, depth_ptr);
        cv::Mat confidence_frame(format.height, format.width, CV_32F, confidence_ptr);
        
        // Convert depth to 8-bit and apply colormap
        cv::Mat depth_8u;
        depth_frame.convertTo(depth_8u, CV_8U, 255.0 / max_range, 0);
        cv::applyColorMap(depth_8u, result_frame, cv::COLORMAP_RAINBOW);
        
        // Apply confidence filtering
        getPreviewRGB(result_frame, confidence_frame);
        
        // Draw rectangles and distance measurement
        cv::rectangle(result_frame, selectRect, cv::Scalar(0, 0, 0), 2);
        cv::rectangle(result_frame, followRect, cv::Scalar(255, 255, 255), 1);
        
        if (selectRect.width > 0 && selectRect.height > 0) {
            cv::Scalar mean_distance = cv::mean(depth_frame(selectRect));
            std::string distance_text = "Dist: " + std::to_string((int)mean_distance.val[0]) + "mm";
            cv::putText(result_frame, distance_text, 
                       cv::Point(selectRect.x, selectRect.y - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        }
        
        // Add camera label
        std::string camera_label = "Camera " + std::to_string(camera_id);
        cv::putText(result_frame, camera_label, cv::Point(10, 30),
                   cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2);
        
        // Display frames
        std::string depth_window = "Camera " + std::to_string(camera_id) + " - Depth";
        std::string conf_window = "Camera " + std::to_string(camera_id) + " - Confidence";
        
        cv::imshow(depth_window, result_frame);
        
        // Show confidence
        cv::Mat confidence_display;
        confidence_frame.convertTo(confidence_display, CV_8U, 255.0 / 1024, 0);
        cv::imshow(conf_window, confidence_display);
    }
    
    void run() {
        if (!initialize_cameras()) {
            return;
        }
        
        // Setup windows
        cv::namedWindow("Camera 0 - Depth", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("Camera 1 - Depth", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("Camera 0 - Confidence", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("Camera 1 - Confidence", cv::WINDOW_AUTOSIZE);
        
        // Set mouse callbacks
        cv::setMouseCallback("Camera 0 - Depth", on_mouse_cam0, this);
        cv::setMouseCallback("Camera 1 - Depth", on_mouse_cam1, this);
        
        // Create trackbars
        cv::createTrackbar("Confidence", "Camera 0 - Depth", &confidence_value, 255, on_confidence_changed_cam0, this);
        cv::createTrackbar("Confidence", "Camera 1 - Depth", &confidence_value, 255, on_confidence_changed_cam1, this);
        
        // Start capture threads
        running = true;
        std::thread thread0(&DualToFCamera::capture_frames_cam0, this);
        std::thread thread1(&DualToFCamera::capture_frames_cam1, this);
        
        std::cout << "Dual camera system started." << std::endl;
        std::cout << "Controls:" << std::endl;
        std::cout << "  - Click on depth image to measure distance" << std::endl;
        std::cout << "  - Press 's' to save current frames" << std::endl;
        std::cout << "  - Press 'q' or ESC to quit" << std::endl;
        
        try {
            while (true) {
                // Process Camera 0
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_cam0);
                    if (!frame_queue_cam0.empty()) {
                        ArducamFrameBuffer* frame = frame_queue_cam0.front();
                        frame_queue_cam0.pop();
                        
                        process_frame(frame, 0, selectRect_cam0, followRect_cam0, 
                                    preview_ptr_cam0, max_range_cam0);
                        
                        tof0.releaseFrame(frame);
                    }
                }
                
                // Process Camera 1
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_cam1);
                    if (!frame_queue_cam1.empty()) {
                        ArducamFrameBuffer* frame = frame_queue_cam1.front();
                        frame_queue_cam1.pop();
                        
                        process_frame(frame, 1, selectRect_cam1, followRect_cam1, 
                                    preview_ptr_cam1, max_range_cam1);
                        
                        tof1.releaseFrame(frame);
                    }
                }
                
                auto key = cv::waitKey(1);
                if (key == 27 || key == 'q') { // ESC or 'q'
                    break;
                } else if (key == 's') {
                    // Save current frames
                    std::cout << "Saving frames..." << std::endl;
                    // Note: This would require access to current depth data
                    // Implementation would need to be added based on requirements
                }
                
                display_fps();
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in main loop: " << e.what() << std::endl;
        }
        
        // Stop threads
        running = false;
        if (thread0.joinable()) thread0.join();
        if (thread1.joinable()) thread1.join();
        
        cleanup();
    }
    
    void cleanup() {
        std::cout << "Cleaning up..." << std::endl;
        
        running = false;
        
        // Clean up frame queues
        {
            std::lock_guard<std::mutex> lock(queue_mutex_cam0);
            while (!frame_queue_cam0.empty()) {
                ArducamFrameBuffer* frame = frame_queue_cam0.front();
                frame_queue_cam0.pop();
                tof0.releaseFrame(frame);
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_cam1);
            while (!frame_queue_cam1.empty()) {
                ArducamFrameBuffer* frame = frame_queue_cam1.front();
                frame_queue_cam1.pop();
                tof1.releaseFrame(frame);
            }
        }
        
        // Stop and close cameras
        if (tof0.stop()) {
            std::cerr << "Error stopping camera 0" << std::endl;
        }
        if (tof0.close()) {
            std::cerr << "Error closing camera 0" << std::endl;
        }
        
        if (tof1.stop()) {
            std::cerr << "Error stopping camera 1" << std::endl;
        }
        if (tof1.close()) {
            std::cerr << "Error closing camera 1" << std::endl;
        }
        
        // Clean up preview buffers
        if (preview_ptr_cam0) {
            delete[] preview_ptr_cam0;
            preview_ptr_cam0 = nullptr;
        }
        if (preview_ptr_cam1) {
            delete[] preview_ptr_cam1;
            preview_ptr_cam1 = nullptr;
        }
        
        cv::destroyAllWindows();
        std::cout << "Cleanup completed." << std::endl;
    }
};

int main() {
    std::cout << "Arducam Dual ToF Camera Demo (C++)" << std::endl;
    
    DualToFCamera dual_camera;
    dual_camera.run();
    
    return 0;
}