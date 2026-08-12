#!/usr/bin/env python3
"""
RAW Dual Arducam ToF Camera System
Minimal version - just raw camera feeds, no UI complications
"""

import cv2
import numpy as np
import ArducamDepthCamera as ac
import threading
import time
from queue import Queue, Empty

class RawDualToFCamera:
    def __init__(self):
        self.cam0 = None
        self.cam1 = None
        self.confidence_value = 30
        
        # Simple threading
        self.running = False
        self.frame_queue_cam0 = Queue(maxsize=2)
        self.frame_queue_cam1 = Queue(maxsize=2)
        
        # Pre-computed ranges for speed
        self.cam0_range = None
        self.cam1_range = None
        
    def initialize_cameras(self):
        """Initialize both cameras"""
        print("🚀 Initializing Raw Dual ToF System...")
        print("SDK version:", ac.__version__)
        
        # Camera 0
        self.cam0 = ac.ArducamCamera()
        ret0 = self.cam0.open(ac.Connection.CSI, 0)
        if ret0 != 0:
            raise Exception(f"Failed to open Camera 0. Error: {ret0}")
        
        # Camera 1  
        self.cam1 = ac.ArducamCamera()
        ret1 = self.cam1.open(ac.Connection.CSI, 8)
        if ret1 != 0:
            raise Exception(f"Failed to open Camera 1. Error: {ret1}")
        
        # Set ranges
        self.cam0.setControl(ac.Control.RANGE, 4000)
        self.cam1.setControl(ac.Control.RANGE, 4000)
        
        # Pre-compute ranges
        self.cam0_range = self.cam0.getControl(ac.Control.RANGE)
        self.cam1_range = self.cam1.getControl(ac.Control.RANGE)
        
        # Start cameras
        ret0 = self.cam0.start(ac.FrameType.DEPTH)
        ret1 = self.cam1.start(ac.FrameType.DEPTH)
        
        if ret0 != 0 or ret1 != 0:
            raise Exception(f"Failed to start cameras. Cam0: {ret0}, Cam1: {ret1}")
        
        print("✅ Both cameras initialized and started")
        return True
    
    def capture_cam0(self):
        """Capture thread for camera 0"""
        while self.running:
            try:
                frame = self.cam0.requestFrame(50)
                if frame is not None:
                    if not self.frame_queue_cam0.full():
                        self.frame_queue_cam0.put(frame)
                    else:
                        # Drop old frame
                        try:
                            old_frame = self.frame_queue_cam0.get_nowait()
                            self.cam0.releaseFrame(old_frame)
                        except:
                            pass
                        self.frame_queue_cam0.put(frame)
                else:
                    time.sleep(0.001)
            except Exception as e:
                print(f"Cam0 error: {e}")
                time.sleep(0.005)
    
    def capture_cam1(self):
        """Capture thread for camera 1"""
        while self.running:
            try:
                frame = self.cam1.requestFrame(50)
                if frame is not None:
                    if not self.frame_queue_cam1.full():
                        self.frame_queue_cam1.put(frame)
                    else:
                        # Drop old frame
                        try:
                            old_frame = self.frame_queue_cam1.get_nowait()
                            self.cam1.releaseFrame(old_frame)
                        except:
                            pass
                        self.frame_queue_cam1.put(frame)
                else:
                    time.sleep(0.001)
            except Exception as e:
                print(f"Cam1 error: {e}")
                time.sleep(0.005)
    
    def process_frame_simple(self, frame, camera_range):
        """Simple frame processing - just convert to viewable image"""
        depth_buf = frame.depth_data
        confidence_buf = frame.confidence_data
        
        # Convert to 8-bit image
        result_image = (depth_buf * (255.0 / camera_range)).astype(np.uint8)
        result_image = cv2.applyColorMap(result_image, cv2.COLORMAP_RAINBOW)
        
        # Simple confidence filtering
        mask = confidence_buf < self.confidence_value
        result_image[mask] = (0, 0, 0)
        
        return result_image
    
    def run(self):
        """Run raw dual camera system"""
        if not self.initialize_cameras():
            return
        
        # Single window for side-by-side display
        cv2.namedWindow("Dual Raw ToF", cv2.WINDOW_NORMAL)
        
        # Start capture threads
        self.running = True
        thread0 = threading.Thread(target=self.capture_cam0, daemon=True)
        thread1 = threading.Thread(target=self.capture_cam1, daemon=True)
        thread0.start()
        thread1.start()
        
        print("\n🎥 Raw Dual Camera System Started!")
        print("Side-by-side display - pure camera feeds")
        print("Press 'q' to quit")
        print("Press 'c' to adjust confidence filter")
        
        # Store latest images
        latest_image0 = None
        latest_image1 = None
        
        try:
            while True:
                # Process Camera 0
                try:
                    frame0 = self.frame_queue_cam0.get_nowait()
                    latest_image0 = self.process_frame_simple(frame0, self.cam0_range)
                    self.cam0.releaseFrame(frame0)
                except Empty:
                    pass
                
                # Process Camera 1
                try:
                    frame1 = self.frame_queue_cam1.get_nowait()
                    latest_image1 = self.process_frame_simple(frame1, self.cam1_range)
                    self.cam1.releaseFrame(frame1)
                except Empty:
                    pass
                
                # Display side-by-side if we have both images
                if latest_image0 is not None and latest_image1 is not None:
                    # Simple horizontal concatenation - no extra processing
                    combined = np.hstack((latest_image0, latest_image1))
                    cv2.imshow("Dual Raw ToF", combined)
                
                # Simple keyboard handling
                key = cv2.waitKey(1) & 0xFF
                if key == ord('q'):
                    break
                elif key == ord('c'):
                    # Simple confidence adjustment
                    self.confidence_value = (self.confidence_value + 10) % 100
                    print(f"Confidence filter: {self.confidence_value}")
        
        except KeyboardInterrupt:
            print("\nInterrupted")
        finally:
            self.cleanup()
    
    def cleanup(self):
        """Simple cleanup"""
        print("🧹 Cleaning up...")
        self.running = False
        time.sleep(0.1)
        
        if self.cam0:
            self.cam0.stop()
            self.cam0.close()
        if self.cam1:
            self.cam1.stop()
            self.cam1.close()
        
        cv2.destroyAllWindows()
        print("✅ Done!")

def main():
    dual_camera = RawDualToFCamera()
    dual_camera.run()

if __name__ == "__main__":
    main()
