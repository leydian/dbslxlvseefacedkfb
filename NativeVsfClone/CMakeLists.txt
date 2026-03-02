cmake_minimum_required(VERSION 3.20)
project(VsfCloneNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VSFCLONE_BUILD_CONSOLE "Build CLI/console entrypoint" ON)

add_library(vsfclone_core STATIC
    src/avatar/avatar_loader_facade.cpp
    src/avatar/vrm_loader.cpp
    src/avatar/vxavatar_loader.cpp
    src/avatar/vsfavatar_loader.cpp
    src/vsf/unityfs_reader.cpp
    src/stream/spout_sender_stub.cpp
    src/osc/osc_endpoint_stub.cpp
)

target_include_directories(vsfclone_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(nativecore SHARED
    src/nativecore/native_core.cpp
)
target_link_libraries(nativecore PRIVATE vsfclone_core)
target_include_directories(nativecore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(nativecore PRIVATE VSFCLONE_NATIVECORE_EXPORTS)

if(VSFCLONE_BUILD_CONSOLE)
    add_executable(vsfclone_cli src/main.cpp)
    target_link_libraries(vsfclone_cli PRIVATE vsfclone_core)

    add_executable(avatar_tool tools/avatar_tool.cpp)
    target_link_libraries(avatar_tool PRIVATE nativecore)
endif()
