@echo off
echo ===== BUILDING ZALO DATA MOVER =====

where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake không được tìm thấy trên hệ thống.
    echo Vui lòng cài đặt CMake từ https://cmake.org/download/
    pause
    exit /b 1
)

if not exist build mkdir build
cd build

echo Đang tạo project files...
cmake ..
if %ERRORLEVEL% neq 0 (
    echo ERROR: Không thể tạo project files! 
    cd ..
    pause
    exit /b 1
)

echo Đang build dự án...
cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build thất bại!
    cd ..
    pause
    exit /b 1
)

echo.
echo Build hoàn tất! File executable nằm trong thư mục build\Release
echo.

cd ..
pause
exit /b 0
