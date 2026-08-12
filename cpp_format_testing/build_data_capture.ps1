# PowerShell build script for ToF Data Capture & Labeling Tool
# This script is designed for Windows environments

Write-Host "🔨 Building ToF Data Capture & Labeling Tool..." -ForegroundColor Green
Write-Host "📁 Source directory: $PWD" -ForegroundColor Cyan

# Check if this is a Windows environment
if ($IsWindows -or $env:OS -eq "Windows_NT") {
    Write-Host "⚠️  Windows Environment Detected" -ForegroundColor Yellow
    Write-Host "" 
    Write-Host "This C++ project is designed for Linux/Raspberry Pi environments." -ForegroundColor Yellow
    Write-Host "To build and run this tool, you need:" -ForegroundColor White
    Write-Host "" 
    Write-Host "📋 Required Dependencies:" -ForegroundColor Cyan
    Write-Host "   • ArducamDepthCamera SDK" -ForegroundColor White
    Write-Host "   • OpenCV (libopencv-dev)" -ForegroundColor White
    Write-Host "   • jsoncpp (libjsoncpp-dev)" -ForegroundColor White
    Write-Host "   • Open3D (libopen3d-dev) - optional for 3D viewer" -ForegroundColor White
    Write-Host "   • CMake and Make build tools" -ForegroundColor White
    Write-Host "" 
    Write-Host "🚀 Recommended Setup Options:" -ForegroundColor Cyan
    Write-Host "" 
    Write-Host "1️⃣  Transfer to Raspberry Pi 5:" -ForegroundColor Green