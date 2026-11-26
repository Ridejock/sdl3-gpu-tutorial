cmake_minimum_required(VERSION 3.24)
project(sdl3_gpu_quad LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDL3: pinned release archive by default; -DUSE_SYSTEM_SDL3=ON uses an installed SDL3 instead.
option(USE_SYSTEM_SDL3 "Use an installed SDL3 (find_package) instead of downloading it" OFF)
# When ON and the shadercross CLI is on PATH, HLSL is compiled to SPIR-V, DXIL, and MSL
# after the build. Otherwise the precompiled files in shaders/ are copied next to the exe.
option(COMPILE_SHADERS "Compile HLSL with SDL_shadercross when the shadercross CLI is available" ON)

if(USE_SYSTEM_SDL3)
    find_package(SDL3 REQUIRED CONFIG)
else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
        URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

add_executable(quad src/main.cpp)
target_link_libraries(quad PRIVATE SDL3::SDL3)
if(MSVC)
    target_compile_options(quad PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(quad PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Shaders land in <exe dir>/shaders, which is where src/main.cpp loads them from.
find_program(SHADERCROSS shadercross)
set(SHADER_OUT "$<TARGET_FILE_DIR:quad>/shaders")

if(COMPILE_SHADERS AND SHADERCROSS)
    message(STATUS "COMPILE_SHADERS: compiling HLSL with ${SHADERCROSS}")
    foreach(stage_pair "Quad.vert;vertex" "Quad.frag;fragment")
        list(GET stage_pair 0 name)
        list(GET stage_pair 1 stage)
        foreach(fmt_pair "SPIRV;spv" "DXIL;dxil" "MSL;msl")
            list(GET fmt_pair 0 fmt)
            list(GET fmt_pair 1 ext)
            add_custom_command(TARGET quad POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT}"
                COMMAND "${SHADERCROSS}" "${CMAKE_SOURCE_DIR}/shaders/${name}.hlsl"
                        -s HLSL -d ${fmt} -t ${stage} -e main -o "${SHADER_OUT}/${name}.${ext}"
                VERBATIM)
        endforeach()
    endforeach()
else()
    if(COMPILE_SHADERS)
        message(STATUS "COMPILE_SHADERS is ON but shadercross was not found; copying precompiled shaders from shaders/")
    else()
        message(STATUS "COMPILE_SHADERS is OFF; copying precompiled shaders from shaders/")
    endif()
    file(GLOB prebuilt CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/shaders/*.spv"
        "${CMAKE_SOURCE_DIR}/shaders/*.dxil"
        "${CMAKE_SOURCE_DIR}/shaders/*.msl")
    if(prebuilt)
        add_custom_command(TARGET quad POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${prebuilt} "${SHADER_OUT}"
            VERBATIM)
    else()
        message(WARNING
            "No precompiled shaders in shaders/ (.spv, .dxil, .msl) and shadercross is not available. "
            "The sample builds, but it has nothing to draw until those files exist. "
            "Install SDL_shadercross and reconfigure, or see docs/guide.md.")
    endif()
endif()
