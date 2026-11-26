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

function(sdl3_gpu_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Copy or compile one shader directory into <exe>/shaders.
# Remaining args are pairs: shader-name stage  (Text.vert vertex Text.frag fragment)
function(sdl3_gpu_shaders target shader_dir)
    set(shader_out "$<TARGET_FILE_DIR:${target}>/shaders")
    if(COMPILE_SHADERS AND SHADERCROSS)
        set(pairs ${ARGN})
        while(pairs)
            list(POP_FRONT pairs name stage)
            foreach(fmt_pair "SPIRV;spv" "DXIL;dxil" "MSL;msl")
                list(GET fmt_pair 0 fmt)
                list(GET fmt_pair 1 ext)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_out}"
                    COMMAND "${SHADERCROSS}" "${shader_dir}/${name}.hlsl"
                            -s HLSL -d ${fmt} -t ${stage} -e main -o "${shader_out}/${name}.${ext}"
                    VERBATIM)
            endforeach()
        endwhile()
    else()
        file(GLOB prebuilt CONFIGURE_DEPENDS
            "${shader_dir}/*.spv" "${shader_dir}/*.dxil" "${shader_dir}/*.msl")
        if(prebuilt)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_out}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${prebuilt} "${shader_out}"
                VERBATIM)
        else()
            message(WARNING
                "No precompiled shaders in ${shader_dir} and shadercross is not available.")
        endif()
    endif()
endfunction()

find_program(SHADERCROSS shadercross)
if(COMPILE_SHADERS AND SHADERCROSS)
    message(STATUS "COMPILE_SHADERS: compiling HLSL with ${SHADERCROSS}")
elseif(COMPILE_SHADERS)
    message(STATUS "COMPILE_SHADERS is ON but shadercross was not found; copying precompiled shaders")
else()
    message(STATUS "COMPILE_SHADERS is OFF; copying precompiled shaders")
endif()

add_executable(quad src/main.cpp)
target_link_libraries(quad PRIVATE SDL3::SDL3)
sdl3_gpu_apply_warnings(quad)
sdl3_gpu_shaders(quad "${CMAKE_SOURCE_DIR}/shaders" Quad.vert vertex Quad.frag fragment)

# Part 2. The quad target above is unchanged.
add_executable(text
    parts/02-text/main.cpp
    parts/02-text/third_party/stb_truetype.c)
target_include_directories(text PRIVATE
    parts/02-text
    parts/02-text/third_party)
target_link_libraries(text PRIVATE SDL3::SDL3)
sdl3_gpu_apply_warnings(text)
if(MSVC)
    set_source_files_properties(parts/02-text/third_party/stb_truetype.c PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(parts/02-text/third_party/stb_truetype.c PROPERTIES COMPILE_OPTIONS "-w")
endif()
sdl3_gpu_shaders(text "${CMAKE_SOURCE_DIR}/parts/02-text/shaders" Text.vert vertex Text.frag fragment)
add_custom_command(TARGET text POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:text>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/parts/02-text/fonts"
            "$<TARGET_FILE_DIR:text>/fonts"
    VERBATIM)
