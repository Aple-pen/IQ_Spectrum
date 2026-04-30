cmake_minimum_required(VERSION 3.24)

project(BinTcpSpectrum LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BTS_FETCH_DEPS "Fetch GUI dependencies with CMake FetchContent" ON)

include(FetchContent)

if(BTS_FETCH_DEPS)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.90.9
        GIT_SHALLOW TRUE
    )

    FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG v0.16
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(glfw imgui implot)
endif()

find_package(OpenGL REQUIRED)

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_backend PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_backend PUBLIC glfw OpenGL::GL)

add_library(implot_backend STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

target_include_directories(implot_backend PUBLIC
    ${implot_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)

target_link_libraries(implot_backend PUBLIC imgui_backend)

add_executable(BinTcpSpectrum
    src/App.cpp
    src/BinStreamer.cpp
    src/Fft.cpp
    src/TcpSender.cpp
    src/main.cpp
)

target_include_directories(BinTcpSpectrum PRIVATE src)

target_compile_definitions(BinTcpSpectrum PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(BinTcpSpectrum PRIVATE
    imgui_backend
    implot_backend
    comdlg32
    ws2_32
)

if(MSVC)
    target_compile_options(BinTcpSpectrum PRIVATE /W4 /permissive-)
else()
    target_compile_options(BinTcpSpectrum PRIVATE -Wall -Wextra -Wpedantic)
endif()