cmake_minimum_required(VERSION 3.20)
project(AnimiqCore LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ANIMIQ_BUILD_CONSOLE "Build CLI/console entrypoint" ON)
option(ANIMIQ_ENABLE_SPOUT2 "Enable Spout2 GPU sender integration when SDK is available" ON)

if(MSVC)
    # Use static CRT to reduce runtime dependency issues for host-side nativecore loading.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(animiq_core STATIC
    src/avatar/avatar_loader_facade.cpp
    src/avatar/vrm_loader.cpp
    src/avatar/miq_loader.cpp
    src/avatar/vsfavatar_loader.cpp
    src/vsf/unityfs_reader.cpp
    src/vsf/serialized_file_reader.cpp
    src/stream/spout_sender.cpp
    src/osc/osc_endpoint.cpp
    src/common/miniz_impl.cpp
    third_party/LzmaDec.c
)

target_include_directories(animiq_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(WIN32 AND ANIMIQ_ENABLE_SPOUT2)
    set(ANIMIQ_SPOUT2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Spout2")
    set(ANIMIQ_SPOUT2_INCLUDE_DIR "${ANIMIQ_SPOUT2_ROOT}/include")
    if(EXISTS "${ANIMIQ_SPOUT2_INCLUDE_DIR}")
        message(STATUS "Spout2 include path detected: ${ANIMIQ_SPOUT2_INCLUDE_DIR}")
        target_include_directories(animiq_core PRIVATE "${ANIMIQ_SPOUT2_INCLUDE_DIR}")
        target_compile_definitions(animiq_core PRIVATE ANIMIQ_SPOUT2_ENABLED=1)

        set(ANIMIQ_SPOUT2_LIB_CANDIDATES
            "SpoutDX.lib"
            "SpoutLibrary.lib")
        set(ANIMIQ_SPOUT2_LIB_SEARCH_DIRS
            "${ANIMIQ_SPOUT2_ROOT}/lib"
            "${ANIMIQ_SPOUT2_ROOT}/lib/x64"
            "${ANIMIQ_SPOUT2_ROOT}/Lib/x64")
        set(ANIMIQ_SPOUT2_LINKED FALSE)
        foreach(_spout_name IN LISTS ANIMIQ_SPOUT2_LIB_CANDIDATES)
            foreach(_spout_dir IN LISTS ANIMIQ_SPOUT2_LIB_SEARCH_DIRS)
                set(_spout_lib "${_spout_dir}/${_spout_name}")
                if(EXISTS "${_spout_lib}")
                    message(STATUS "Spout2 library detected: ${_spout_lib}")
                    target_link_libraries(animiq_core PRIVATE "${_spout_lib}")
                    set(ANIMIQ_SPOUT2_LINKED TRUE)
                    break()
                endif()
            endforeach()
            if(ANIMIQ_SPOUT2_LINKED)
                break()
            endif()
        endforeach()
        if(NOT ANIMIQ_SPOUT2_LINKED)
            message(STATUS "Spout2 include found but no import library detected; runtime will keep legacy fallback.")
        endif()
    else()
        message(STATUS "Spout2 SDK not found at ${ANIMIQ_SPOUT2_INCLUDE_DIR}; falling back to legacy shared-memory sender.")
    endif()
endif()

add_library(nativecore SHARED
    src/nativecore/native_core.cpp
)
target_link_libraries(nativecore PRIVATE animiq_core)
if(WIN32)
    target_link_libraries(nativecore PRIVATE d3d11 d3dcompiler dxgi ole32 windowscodecs ws2_32)
endif()
target_include_directories(nativecore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(nativecore PRIVATE ANIMIQ_NATIVECORE_EXPORTS)

if(ANIMIQ_BUILD_CONSOLE)
    add_executable(animiq_cli src/main.cpp)
    target_link_libraries(animiq_cli PRIVATE animiq_core)

    add_executable(avatar_tool tools/avatar_tool.cpp)
    target_link_libraries(avatar_tool PRIVATE animiq_core)

    add_executable(vrm_to_miq tools/vrm_to_miq.cpp)
    target_link_libraries(vrm_to_miq PRIVATE animiq_core)

    add_executable(vsfavatar_sidecar tools/vsfavatar_sidecar.cpp)
    target_link_libraries(vsfavatar_sidecar PRIVATE animiq_core)
endif()
