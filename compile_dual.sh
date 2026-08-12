#!/bin/sh
# Dual camera compile script
workpath=$(cd "$(dirname "$0")" && pwd)

echo "workpath: $workpath"
echo "Building dual camera applications..."

if ! cmake -B $workpath/build -S $workpath; then
    echo "== CMake failed"
    exit 1
fi

if cmake --build $workpath/build --config Release --target dual_preview_depth -j 4 &&
   cmake --build $workpath/build --config Release --target dual_preview_pointcloud -j 4; then
    echo "== Dual camera build success"
    echo "== Run $workpath/build/example/cpp/dual_preview_depth"
    echo "== Run $workpath/build/open3d_preview/dual_preview_pointcloud"
    echo "== Run python3 $workpath/example/python/dual_preview_depth.py"
else
    echo "== Retry build without -j 4"
    if cmake --build $workpath/build --config Release --target dual_preview_depth &&
       cmake --build $workpath/build --config Release --target dual_preview_pointcloud; then
        echo "== Dual camera build success"
        echo "== Run $workpath/build/example/cpp/dual_preview_depth"
        echo "== Run $workpath/build/open3d_preview/dual_preview_pointcloud"
        echo "== Run python3 $workpath/example/python/dual_preview_depth.py"
    else
        echo "== Dual camera build failed"
    fi
fi