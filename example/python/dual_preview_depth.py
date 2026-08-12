import cv2
import numpy as np
import ArducamDepthCamera as ac
import threading
import time
from queue import Queue

# MAX_DISTANCE value modifiable is 2000 or 4000
MAX_DISTANCE = 4000

class UserRect:
    def __init__(self) -> None:
        self.start_x = 0
        self.start_y = 0
        self.end_x = 0
        self.end_y = 0

    @property
    def rect(self):
        return (
            self.start_x,
            self.start_y,
            self.end_x - self.start_x,
            self.end_y - self.start_y,
        )

    @property
    def slice(self):
        return (slice(self.start_y, self.end_y), slice(self.start_x, self.end_x))

    @property
    def empty(self):
        return self.start_x == self.end_x and self.start_y == self.end_y

class DualToFCamera:
    def __init__(self):
        self.cam0 = None
        self.cam1 = None
        self.confidence_value = 30
        self.selectRect_cam0, self.followRect_cam0 = UserRect(), UserRect()
        self.selectRect_cam1, self.followRect_cam1 = UserRect(), UserRect()
        self.frame_queue_cam0 = Queue(maxsize=2)
        self.frame_queue_cam1 = Queue(maxsize=2)
        self.running = False
        self.current_camera = 0  # 0 for cam0, 1 for cam1
        
    def getPreviewRGB(self, preview: np.ndarray, confidence: np.ndarray) -> np.ndarray:
        preview = np.nan_to_num(preview)
        preview[confidence < self.confidence_value] = (0, 0, 0)
        return preview

    def on_mouse_cam0(self, event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN:
            pass
        elif event == cv2.EVENT_LBUTTONUP:
            self.selectRect_cam0.start_x = x - 4
            self.selectRect_cam0.start_y = y - 4
            self.selectRect_cam0.end_x = x + 4
            self.selectRect_cam0.end_y = y + 4
        else:
            self.followRect_cam0.start_x = x - 4
            self.followRect_cam0.start_y = y - 4
            self.followRect_cam0.end_x = x + 4
            self.followRect_cam0.end_y = y + 4

    def on_mouse_cam1(self, event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN:
            pass
        elif event == cv2.EVENT_LBUTTONUP:
            self.selectRect_cam1.start_x = x - 4
            self.selectRect_cam1.start_y = y - 4
            self.selectRect_cam1.end_x = x + 4
            self.selectRect_cam1.end_y = y + 4
        else:
            self.followRect_cam1.start_x = x - 4
            self.followRect_cam1.start_y = y - 4
            self.followRect_cam1.end_x = x + 4
            self.followRect_cam1.end_y = y + 4

    def on_confidence_changed(self, value):
        self.confidence_value = value

    def initialize_cameras(self):
        """Initialize both ToF cameras with proper sequencing and resource management"""
        print("Initializing dual ToF cameras...")
        print("SDK version:", ac.__version__)
        
        # Try different initialization strategies
        strategies = [
            self._init_strategy_sequential,
            self._init_strategy_reverse,
            self._init_strategy_delayed
        ]
        
        for i, strategy in enumerate(strategies):
            print(f"\nTrying initialization strategy {i+1}...")
            if strategy():
                print("Strategy succeeded!")
                return True
            else:
                print(f"Strategy {i+1} failed, trying next...")
                # Clean up any partial initialization
                self.cleanup()
                time.sleep(0.5)
        
        print("All initialization strategies failed.")
        return False
    
    def _init_strategy_sequential(self):
        """Strategy 1: Initialize cameras sequentially with delays"""
        try:
            # Initialize Camera 0 completely first
            print("  Opening Camera 0...")
            self.cam0 = ac.ArducamCamera()
            ret0 = self.cam0.open(ac.Connection.CSI, 0)
            if ret0 != 0:
                print(f"  Failed to open camera 0. Error code: {ret0}")
                return False
            
            print("  Setting Camera 0 range...")
            self.cam0.setControl(ac.Control.RANGE, MAX_DISTANCE)
            
            print("  Starting Camera 0...")
            ret0 = self.cam0.start(ac.FrameType.DEPTH)
            if ret0 != 0:
                print(f"  Failed to start camera 0. Error code: {ret0}")
                return False
            
            # Wait longer before initializing second camera
            print("  Waiting before initializing Camera 1...")
            time.sleep(1.0)
            
            # Now initialize Camera 1
            print("  Opening Camera 1...")
            self.cam1 = ac.ArducamCamera()
            ret1 = self.cam1.open(ac.Connection.CSI, 1)
            if ret1 != 0:
                print(f"  Failed to open camera 1. Error code: {ret1}")
                return False
            
            print("  Setting Camera 1 range...")
            self.cam1.setControl(ac.Control.RANGE, MAX_DISTANCE)
            
            print("  Starting Camera 1...")
            ret1 = self.cam1.start(ac.FrameType.DEPTH)
            if ret1 != 0:
                print(f"  Failed to start camera 1. Error code: {ret1}")
                return False
            
            # Get camera info
            info0 = self.cam0.getCameraInfo()
            info1 = self.cam1.getCameraInfo()
            print(f"  Camera 0 resolution: {info0.width}x{info0.height}")
            print(f"  Camera 1 resolution: {info1.width}x{info1.height}")
            
            return True
            
        except Exception as e:
            print(f"  Exception in sequential strategy: {e}")
            return False
    
    def _init_strategy_reverse(self):
        """Strategy 2: Initialize Camera 1 first, then Camera 0"""
        try:
            # Initialize Camera 1 first
            print("  Opening Camera 1...")
            self.cam1 = ac.ArducamCamera()
            ret1 = self.cam1.open(ac.Connection.CSI, 1)
            if ret1 != 0:
                print(f"  Failed to open camera 1. Error code: {ret1}")
                return False
            
            print("  Setting Camera 1 range...")
            self.cam1.setControl(ac.Control.RANGE, MAX_DISTANCE)
            
            print("  Starting Camera 1...")
            ret1 = self.cam1.start(ac.FrameType.DEPTH)
            if ret1 != 0:
                print(f"  Failed to start camera 1. Error code: {ret1}")
                return False
            
            # Wait before initializing Camera 0
            print("  Waiting before initializing Camera 0...")
            time.sleep(1.0)
            
            # Now initialize Camera 0
            print("  Opening Camera 0...")
            self.cam0 = ac.ArducamCamera()
            ret0 = self.cam0.open(ac.Connection.CSI, 0)
            if ret0 != 0:
                print(f"  Failed to open camera 0. Error code: {ret0}")
                return False
            
            print("  Setting Camera 0 range...")
            self.cam0.setControl(ac.Control.RANGE, MAX_DISTANCE)
            
            print("  Starting Camera 0...")
            ret0 = self.cam0.start(ac.FrameType.DEPTH)
            if ret0 != 0:
                print(f"  Failed to start camera 0. Error code: {ret0}")
                return False
            
            # Get camera info
            info0 = self.cam0.getCameraInfo()
            info1 = self.cam1.getCameraInfo()
            print(f"  Camera 0 resolution: {info0.width}x{info0.height}")
            print(f"  Camera 1 resolution: {info1.width}x{info1.height}")
            
            return True
            
        except Exception as e:
            print(f"  Exception in reverse strategy: {e}")
            return False
    
    def _init_strategy_delayed(self):
        """Strategy 3: Open both, wait, then start both with longer delays"""
        try:
            # Open both cameras first
            print("  Opening both cameras...")
            self.cam0 = ac.ArducamCamera()
            self.cam1 = ac.ArducamCamera()
            
            ret0 = self.cam0.open(ac.Connection.CSI, 0)
            if ret0 != 0:
                print(f"  Failed to open camera 0. Error code: {ret0}")
                return False
            
            time.sleep(0.5)
            
            ret1 = self.cam1.open(ac.Connection.CSI, 1)
            if ret1 != 0:
                print(f"  Failed to open camera 1. Error code: {ret1}")
                return False
            
            # Set ranges
            print("  Setting ranges...")
            self.cam0.setControl(ac.Control.RANGE, MAX_DISTANCE)
            time.sleep(0.2)
            self.cam1.setControl(ac.Control.RANGE, MAX_DISTANCE)
            
            # Wait longer before starting
            print("  Waiting before starting cameras...")
            time.sleep(2.0)
            
            # Start cameras with delays
            print("  Starting Camera 0...")
            ret0 = self.cam0.start(ac.FrameType.DEPTH)
            if ret0 != 0:
                print(f"  Failed to start camera 0. Error code: {ret0}")
                return False
            
            time.sleep(1.5)
            
            print("  Starting Camera 1...")
            ret1 = self.cam1.start(ac.FrameType.DEPTH)
            if ret1 != 0:
                print(f"  Failed to start camera 1. Error code: {ret1}")
                return False
            
            # Get camera info
            info0 = self.cam0.getCameraInfo()
            info1 = self.cam1.getCameraInfo()
            print(f"  Camera 0 resolution: {info0.width}x{info0.height}")
            print(f"  Camera 1 resolution: {info1.width}x{info1.height}")
            
            return True
            
        except Exception as e:
            print(f"  Exception in delayed strategy: {e}")
            return False

    def capture_frames_cam0(self):
        """Capture frames from camera 0 in separate thread"""
        while self.running:
            try:
                frame = self.cam0.requestFrame(200)  # Longer timeout for stability
                if frame is not None and isinstance(frame, ac.DepthData):
                    if not self.frame_queue_cam0.full():
                        self.frame_queue_cam0.put(frame)
                    else:
                        # Release old frame if queue is full
                        try:
                            old_frame = self.frame_queue_cam0.get_nowait()
                            self.cam0.releaseFrame(old_frame)
                        except:
                            pass
                        self.frame_queue_cam0.put(frame)
                else:
                    time.sleep(0.01)  # Longer delay when no frame available
            except Exception as e:
                print(f"Error in camera 0 capture: {e}")
                time.sleep(0.05)  # Longer delay on error

    def capture_frames_cam1(self):
        """Capture frames from camera 1 in separate thread"""
        while self.running:
            try:
                frame = self.cam1.requestFrame(200)  # Longer timeout for stability
                if frame is not None and isinstance(frame, ac.DepthData):
                    if not self.frame_queue_cam1.full():
                        self.frame_queue_cam1.put(frame)
                    else:
                        # Release old frame if queue is full
                        try:
                            old_frame = self.frame_queue_cam1.get_nowait()
                            self.cam1.releaseFrame(old_frame)
                        except:
                            pass
                        self.frame_queue_cam1.put(frame)
                else:
                    time.sleep(0.01)  # Longer delay when no frame available
            except Exception as e:
                print(f"Error in camera 1 capture: {e}")
                time.sleep(0.05)  # Longer delay on error

    def process_frame(self, frame, camera_id, selectRect, followRect):
        """Process a single frame from either camera"""
        depth_buf = frame.depth_data
        confidence_buf = frame.confidence_data
        
        # Get range for normalization
        if camera_id == 0:
            r = self.cam0.getControl(ac.Control.RANGE)
        else:
            r = self.cam1.getControl(ac.Control.RANGE)
            
        result_image = (depth_buf * (255.0 / r)).astype(np.uint8)
        result_image = cv2.applyColorMap(result_image, cv2.COLORMAP_RAINBOW)
        result_image = self.getPreviewRGB(result_image, confidence_buf)
        
        # Draw rectangles
        white_color = (255, 255, 255)
        black_color = (0, 0, 0)
        
        cv2.rectangle(result_image, followRect.rect, white_color, 1)
        if not selectRect.empty:
            cv2.rectangle(result_image, selectRect.rect, black_color, 2)
            distance = np.mean(depth_buf[selectRect.slice])
            # Add text overlay with distance
            cv2.putText(result_image, f"Dist: {distance:.1f}mm", 
                       (selectRect.start_x, selectRect.start_y - 10),
                       cv2.FONT_HERSHEY_SIMPLEX, 0.5, white_color, 1)
        
        # Add camera label
        cv2.putText(result_image, f"Camera {camera_id}", (10, 30),
                   cv2.FONT_HERSHEY_SIMPLEX, 1, white_color, 2)
        
        return result_image, confidence_buf

    def run(self):
        """Main execution loop"""
        if not self.initialize_cameras():
            return
            
        # Setup windows
        cv2.namedWindow("Camera 0 - Depth", cv2.WINDOW_AUTOSIZE)
        cv2.namedWindow("Camera 1 - Depth", cv2.WINDOW_AUTOSIZE)
        cv2.namedWindow("Camera 0 - Confidence", cv2.WINDOW_AUTOSIZE)
        cv2.namedWindow("Camera 1 - Confidence", cv2.WINDOW_AUTOSIZE)
        
        # Set mouse callbacks
        cv2.setMouseCallback("Camera 0 - Depth", self.on_mouse_cam0)
        cv2.setMouseCallback("Camera 1 - Depth", self.on_mouse_cam1)
        
        # Create confidence trackbars
        cv2.createTrackbar("Confidence", "Camera 0 - Depth", self.confidence_value, 255, self.on_confidence_changed)
        cv2.createTrackbar("Confidence", "Camera 1 - Depth", self.confidence_value, 255, self.on_confidence_changed)
        
        # Start capture threads
        self.running = True
        thread0 = threading.Thread(target=self.capture_frames_cam0, daemon=True)
        thread1 = threading.Thread(target=self.capture_frames_cam1, daemon=True)
        thread0.start()
        thread1.start()
        
        print("Dual camera system started. Press 'q' to quit, 's' to switch focus between cameras.")
        
        try:
            while True:
                # Process Camera 0
                try:
                    frame0 = self.frame_queue_cam0.get_nowait()
                    result_image0, confidence0 = self.process_frame(
                        frame0, 0, self.selectRect_cam0, self.followRect_cam0)
                    
                    cv2.normalize(confidence0, confidence0, 1, 0, cv2.NORM_MINMAX)
                    cv2.imshow("Camera 0 - Depth", result_image0)
                    cv2.imshow("Camera 0 - Confidence", confidence0)
                    
                    self.cam0.releaseFrame(frame0)
                except:
                    pass
                
                # Process Camera 1
                try:
                    frame1 = self.frame_queue_cam1.get_nowait()
                    result_image1, confidence1 = self.process_frame(
                        frame1, 1, self.selectRect_cam1, self.followRect_cam1)
                    
                    cv2.normalize(confidence1, confidence1, 1, 0, cv2.NORM_MINMAX)
                    cv2.imshow("Camera 1 - Depth", result_image1)
                    cv2.imshow("Camera 1 - Confidence", confidence1)
                    
                    self.cam1.releaseFrame(frame1)
                except:
                    pass
                
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q'):
                    break
                elif key == ord('s'):
                    self.current_camera = 1 - self.current_camera
                    print(f"Switched focus to Camera {self.current_camera}")
                    
        except KeyboardInterrupt:
            print("\nInterrupted by user")
        finally:
            self.cleanup()

    def cleanup(self):
        """Clean up resources"""
        print("Cleaning up...")
        self.running = False
        
        # Clean up queues
        while not self.frame_queue_cam0.empty():
            try:
                frame = self.frame_queue_cam0.get_nowait()
                if self.cam0:
                    self.cam0.releaseFrame(frame)
            except:
                break
                
        while not self.frame_queue_cam1.empty():
            try:
                frame = self.frame_queue_cam1.get_nowait()
                if self.cam1:
                    self.cam1.releaseFrame(frame)
            except:
                break
        
        # Stop and close cameras
        if hasattr(self, 'cam0') and self.cam0:
            try:
                self.cam0.stop()
                self.cam0.close()
            except:
                pass
            self.cam0 = None
                
        if hasattr(self, 'cam1') and self.cam1:
            try:
                self.cam1.stop()
                self.cam1.close()
            except:
                pass
            self.cam1 = None
        
        cv2.destroyAllWindows()
        print("Cleanup completed.")

def main():
    print("Arducam Dual ToF Camera Demo")
    print("Controls:")
    print("  - Click on depth image to measure distance")
    print("  - Use confidence trackbar to filter low-confidence pixels")
    print("  - Press 's' to switch focus between cameras")
    print("  - Press 'q' to quit")
    
    dual_camera = DualToFCamera()
    dual_camera.run()

if __name__ == "__main__":
    main()