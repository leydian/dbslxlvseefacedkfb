cmake_minimum_required(VERSION 3.20)
project(VsfCloneNative LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VSFCLONE_BUILD_CONSOLE "Build CLI/console entrypoint" ON)

if(MSVC)
    # Use static CRT to reduce runtime dependency issues for host-side nativecore loading.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(vsfclone_core STATIC
    src/avatar/avatar_loader_facade.cpp
    src/avatar/vrm_loader.cpp
    src/avatar/vxavatar_loader.cpp
    src/avatar/vxa2_loader.cpp
    src/avatar/xav2_loader.cpp
    src/avatar/vsfavatar_loader.cpp
    src/vsf/unityfs_reader.cpp
    src/vsf/serialized_file_reader.cpp
    src/stream/spout_sender.cpp
    src/osc/osc_endpoint.cpp
    src/common/miniz_impl.cpp
    third_party/LzmaDec.c
)

target_include_directories(vsfclone_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_library(nativecore SHARED
    src/nativecore/native_core.cpp
)
target_link_libraries(nativecore PRIVATE vsfclone_core)
if(WIN32)
    target_link_libraries(nativecore PRIVATE d3d11 d3dcompiler dxgi ole32 windowscodecs ws2_32)
endif()
target_include_directories(nativecore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(nativecore PRIVATE VSFCLONE_NATIVECORE_EXPORTS)

if(VSFCLONE_BUILD_CONSOLE)
    add_executable(vsfclone_cli src/main.cpp)
    target_link_libraries(vsfclone_cli PRIVATE vsfclone_core)

    add_executable(avatar_tool tools/avatar_tool.cpp)
    target_link_libraries(avatar_tool PRIVATE vsfclone_core)

    add_executable(vrm_to_xav2 tools/vrm_to_xav2.cpp)
    target_link_libraries(vrm_to_xav2 PRIVATE vsfclone_core)

    add_executable(vsfavatar_sidecar tools/vsfavatar_sidecar.cpp)
    target_link_libraries(vsfavatar_sidecar PRIVATE vsfclone_core)
endif()
