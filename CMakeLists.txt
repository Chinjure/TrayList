cmake_minimum_required(VERSION 3.20)
project(TrayVerticalList VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Unicode 构建
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# Windows GUI 子系统(无控制台)
if(MSVC)
    add_compile_options(/utf-8)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup")
    # 静态链接 C 运行时(可选,避免依赖 vcruntime)
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_executable(TrayVerticalList WIN32
    src/main.cpp
    src/app.cpp
    src/common.cpp
    src/settings.cpp
    src/hotkey.cpp
    src/tray_enum.cpp
    src/click.cpp
    src/popup.cpp
    src/settings_window.cpp
)

target_include_directories(TrayVerticalList PRIVATE src)

# 嵌入应用清单(启用 comctl v6 视觉样式、DPI 感知、Windows 10/11 兼容)
if(MSVC)
    set_target_properties(TrayVerticalList PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")
endif()

target_link_libraries(TrayVerticalList
    user32 gdi32 comctl32 shell32 ole32 oleaut32 psapi uxtheme
)
