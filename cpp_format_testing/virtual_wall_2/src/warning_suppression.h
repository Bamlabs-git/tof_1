#pragma once

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

/**
 * @brief Utility class for suppressing and restoring system warnings
 * 
 * This class provides cross-platform warning suppression functionality
 * to reduce noise from camera initialization and other system operations.
 */
class WarningSuppressionManager {
private:
#ifdef _WIN32
    int original_stderr_;
    int null_fd_;
#else
    int original_stderr_;
    int null_fd_;
#endif
    bool warnings_suppressed_;

public:
    WarningSuppressionManager() : 
        original_stderr_(-1), 
        null_fd_(-1), 
        warnings_suppressed_(false) {}
    
    ~WarningSuppressionManager() {
        if (warnings_suppressed_) {
            restoreWarnings();
        }
    }
    
    /**
     * @brief Suppress system warnings and debug output
     * 
     * Redirects stderr to null device and sets environment variables
     * to reduce various system warnings during camera operations.
     */
    void suppressWarnings() {
        if (warnings_suppressed_) {
            return; // Already suppressed
        }
        
#ifdef _WIN32
        // Set environment variables to reduce various warnings
        SetEnvironmentVariableA("QT_LOGGING_RULES", "*.debug=false");
        SetEnvironmentVariableA("OPENCV_LOG_LEVEL", "ERROR");
        SetEnvironmentVariableA("GLOG_minloglevel", "3");
        SetEnvironmentVariableA("XDG_RUNTIME_DIR", "C:\\temp\\runtime");
        
        // Create temp directory if it doesn't exist
        CreateDirectoryA("C:\\temp", NULL);
        CreateDirectoryA("C:\\temp\\runtime", NULL);
        
        // Redirect stderr to NUL on Windows
        original_stderr_ = _dup(_fileno(stderr));
        null_fd_ = _open("NUL", _O_WRONLY);
        if (null_fd_ != -1) {
            _dup2(null_fd_, _fileno(stderr));
        }
#else
        // Set environment variables to reduce various warnings
        setenv("QT_LOGGING_RULES", "*.debug=false", 1);
        setenv("OPENCV_LOG_LEVEL", "ERROR", 1);
        setenv("GLOG_minloglevel", "3", 1);
        setenv("XDG_RUNTIME_DIR", "/tmp/runtime-root", 1);
        setenv("UVC_QUIRKS", "0x80", 1);
        setenv("LIBUSB_DEBUG", "0", 1);
        
        // Create directory if it doesn't exist
        system("mkdir -p /tmp/runtime-root");
        
        // Redirect stderr to /dev/null temporarily
        original_stderr_ = dup(STDERR_FILENO);
        null_fd_ = open("/dev/null", O_WRONLY);
        if (null_fd_ != -1) {
            dup2(null_fd_, STDERR_FILENO);
        }
#endif
        warnings_suppressed_ = true;
    }
    
    /**
     * @brief Restore normal warning output
     * 
     * Restores stderr to its original state, allowing normal
     * warning and error messages to be displayed.
     */
    void restoreWarnings() {
        if (!warnings_suppressed_) {
            return; // Not suppressed
        }
        
#ifdef _WIN32
        // Restore stderr
        if (original_stderr_ != -1) {
            _dup2(original_stderr_, _fileno(stderr));
            _close(original_stderr_);
            original_stderr_ = -1;
        }
        if (null_fd_ != -1) {
            _close(null_fd_);
            null_fd_ = -1;
        }
#else
        // Restore stderr
        if (original_stderr_ != -1) {
            dup2(original_stderr_, STDERR_FILENO);
            close(original_stderr_);
            original_stderr_ = -1;
        }
        if (null_fd_ != -1) {
            close(null_fd_);
            null_fd_ = -1;
        }
#endif
        warnings_suppressed_ = false;
    }
    
    /**
     * @brief Check if warnings are currently suppressed
     * @return true if warnings are suppressed, false otherwise
     */
    bool isWarningsSuppressed() const {
        return warnings_suppressed_;
    }
};