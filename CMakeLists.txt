cmake_minimum_required(VERSION 3.10)
project(ZaloDataMover)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")

add_executable(ZaloDataMover WIN32 ZaloDataMover.cpp resources.rc resource.h)

target_link_libraries(ZaloDataMover shell32 ole32 comctl32)

if(MSVC)
    add_custom_command(
        TARGET ZaloDataMover
        POST_BUILD
        COMMAND mt.exe -manifest "${APP_MANIFEST}" -outputresource:"$<TARGET_FILE:ZaloDataMover>;1"
        COMMENT "Adding UAC manifest..."
    )
endif()
