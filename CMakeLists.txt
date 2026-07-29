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

# --- ZeroMQ (광대역 WB Viewer 스트림 수신용) ---------------------------------
# 단일 바이너리 운용을 위해 libzmq 를 소스에서 정적으로 빌드해 정적 링크한다.
# libzmq 는 자체 옵션 BUILD_SHARED/BUILD_STATIC 을 쓴다(표준 BUILD_SHARED_LIBS 아님).
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_DOCS OFF CACHE BOOL "" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
set(ENABLE_CPACK OFF CACHE BOOL "" FORCE)
set(WITH_LIBSODIUM OFF CACHE BOOL "" FORCE)
set(ENABLE_CURVE OFF CACHE BOOL "" FORCE)
set(ENABLE_DRAFTS OFF CACHE BOOL "" FORCE)
# libzmq v4.3.5 의 cmake_minimum_required 가 낮아 최신 CMake 에서 막히는 것을 방지.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_Declare(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG v4.3.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libzmq)

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
    src/IqStream.cpp
    src/BinStreamer.cpp
    src/IqReceiver.cpp
    src/WbViewerReceiver.cpp
    src/Fft.cpp
    src/TcpSender.cpp
    src/TcpReceiver.cpp
    src/main.cpp
)

target_include_directories(BinTcpSpectrum PRIVATE
    src
    ${libzmq_SOURCE_DIR}/include
)

target_compile_definitions(BinTcpSpectrum PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    ZMQ_STATIC   # 정적 libzmq 사용 시 zmq.h 가 dllimport 를 붙이지 않도록
)

target_link_libraries(BinTcpSpectrum PRIVATE
    imgui_backend
    implot_backend
    libzmq-static
    comdlg32
    ws2_32
    iphlpapi
)

if(MSVC)
    target_compile_options(BinTcpSpectrum PRIVATE /W4 /permissive-)
else()
    target_compile_options(BinTcpSpectrum PRIVATE -Wall -Wextra -Wpedantic)
endif()