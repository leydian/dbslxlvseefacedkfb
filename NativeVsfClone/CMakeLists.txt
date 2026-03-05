cmake_minimum_required(VERSION 3.20)
project(VsfCloneNative LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VSFCLONE_BUILD_CONSOLE "Build CLI/console entrypoint" ON)
option(VSFCLONE_ENABLE_SPOUT2 "Enable Spout2 GPU sender integration when SDK is available" ON)

if(MSVC)
    # Use static CRT to reduce runtime dependency issues for host-side nativecore loading.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(vsfclone_core STATIC
    src/avatar/avatar_loader_facade.cpp
    src/avatar/vrm_loader.cpp
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

if(WIN32 AND VSFCLONE_ENABLE_SPOUT2)
    set(VSFCLONE_SPOUT2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Spout2")
    set(VSFCLONE_SPOUT2_INCLUDE_DIR "${VSFCLONE_SPOUT2_ROOT}/include")
    if(EXISTS "${VSFCLONE_SPOUT2_INCLUDE_DIR}")
        message(STATUS "Spout2 include path detected: ${VSFCLONE_SPOUT2_INCLUDE_DIR}")
        target_include_directories(vsfclone_core PRIVATE "${VSFCLONE_SPOUT2_INCLUDE_DIR}")
        target_compile_definitions(vsfclone_core PRIVATE VSFCLONE_SPOUT2_ENABLED=1)

        set(VSFCLONE_SPOUT2_LIB_CANDIDATES
            "SpoutDX.lib"
            "SpoutLibrary.lib")
        set(VSFCLONE_SPOUT2_LIB_SEARCH_DIRS
            "${VSFCLONE_SPOUT2_ROOT}/lib"
            "${VSFCLONE_SPOUT2_ROOT}/lib/x64"
            "${VSFCLONE_SPOUT2_ROOT}/Lib/x64")
        set(VSFCLONE_SPOUT2_LINKED FALSE)
        foreach(_spout_name IN LISTS VSFCLONE_SPOUT2_LIB_CANDIDATES)
            foreach(_spout_dir IN LISTS VSFCLONE_SPOUT2_LIB_SEARCH_DIRS)
                set(_spout_lib "${_spout_dir}/${_spout_name}")
                if(EXISTS "${_spout_lib}")
                    message(STATUS "Spout2 library detected: ${_spout_lib}")
                    target_link_libraries(vsfclone_core PRIVATE "${_spout_lib}")
                    set(VSFCLONE_SPOUT2_LINKED TRUE)
                    break()
                endif()
            endforeach()
            if(VSFCLONE_SPOUT2_LINKED)
                break()
            endif()
        endforeach()
        if(NOT VSFCLONE_SPOUT2_LINKED)
            message(STATUS "Spout2 include found but no import library detected; runtime will keep legacy fallback.")
        endif()
    else()
        message(STATUS "Spout2 SDK not found at ${VSFCLONE_SPOUT2_INCLUDE_DIR}; falling back to legacy shared-memory sender.")
    endif()
endif()

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
