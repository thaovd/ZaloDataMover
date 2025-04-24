# Công cụ di chuyển dữ liệu Zalo

Ứng dụng này giúp chuyển dữ liệu của Zalo sang ổ đĩa khác, giải phóng không gian trên ổ C.

## Chức năng

1. Cho phép người dùng chọn vị trí lưu trữ dữ liệu mới
2. Tự động đóng ứng dụng Zalo nếu đang chạy
3. Di chuyển thư mục dữ liệu Zalo từ `C:\Users\{user}\AppData\Local\ZaloPC` sang vị trí mới
4. Tạo symbolic link (junction) từ vị trí cũ tới vị trí mới


## Cách build

### Cài đặt các công cụ cần thiết
1. Cài đặt **Visual Studio** hoặc **Visual Studio Build Tools**
   - Tải từ: https://visualstudio.microsoft.com/downloads/
   - Khi cài đặt, hãy chọn "Desktop development with C++"

2. Cài đặt **CMake**
   - Tải từ: https://cmake.org/download/
   - Đảm bảo tích vào "Add CMake to the system PATH"

### Cách build dự án
Có hai cách để build dự án:

#### Cách 1: Sử dụng file build.bat
1. Đơn giản là double-click vào file `build.bat`
2. Sau khi hoàn tất, file thực thi sẽ nằm ở `build\Release\ZaloDataMover.exe`

#### Cách 2: Sử dụng CMake thủ công
```
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Cách sử dụng

1. Chạy file `ZaloDataMover.exe` với quyền Administrator
   - Right-click > Run as Administrator
2. Chọn thư mục đích để lưu trữ dữ liệu Zalo
3. Ứng dụng sẽ tự động thực hiện các bước còn lại

