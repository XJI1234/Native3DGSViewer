cmake_minimum_required(VERSION 3.22.1)
project(Native3DGSAndroidCore LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "This target requires the Android NDK toolchain")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_subdirectory("${ROOT}/third_party/googletest" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/third_party/zstd/build/cmake" "${CMAKE_CURRENT_BINARY_DIR}/zstd" EXCLUDE_FROM_ALL)

add_library(gs_spz STATIC
    "${ROOT}/third_party/spz/src/cc/load-spz.cc"
    "${ROOT}/third_party/spz/src/cc/splat-c-types.cc"
    "${ROOT}/third_party/spz/src/cc/splat-types.cc")
target_include_directories(gs_spz PUBLIC "${ROOT}/third_party/spz/src/cc")
target_compile_options(gs_spz PRIVATE -include android/log.h)
target_link_libraries(gs_spz PUBLIC libzstd_static z log)

add_library(gs_android_model_io STATIC
    "${ROOT}/src/model-io/common/probe.cpp"
    "${ROOT}/src/model-io/normalize/normalize.cpp"
    "${ROOT}/src/model-io/codecs/decode.cpp"
    src/model-io/fd_loader.cpp)
target_include_directories(gs_android_model_io PUBLIC include "${ROOT}/include"
    PRIVATE "${ROOT}/src/model-io/common" "${ROOT}/third_party/spz/src/cc")
target_link_libraries(gs_android_model_io PUBLIC gs_spz android z)

add_library(gs_android_decoder SHARED src/android-bridge/decoder_jni.cpp
    src/android-bridge/engine_c.cpp src/android-bridge/engine_jni.cpp)
target_link_libraries(gs_android_decoder PRIVATE gs_android_model_io)

if(CMAKE_HOST_WIN32)
    set(GLSLC_HOST_DIR windows-x86_64)
elseif(CMAKE_HOST_APPLE)
    set(GLSLC_HOST_DIR darwin-x86_64)
else()
    set(GLSLC_HOST_DIR linux-x86_64)
endif()
find_program(GLSLC glslc HINTS "${ANDROID_NDK}/shader-tools/${GLSLC_HOST_DIR}" REQUIRED)
set(ANDROID_SHADERS histogram scan add_prefix scatter project)
foreach(SHADER IN LISTS ANDROID_SHADERS)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv")
    add_custom_command(OUTPUT "${OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${GLSLC}" --target-env=vulkan1.1 -O
                -o "${OUTPUT}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.comp"
        DEPENDS "shaders/${SHADER}.comp" VERBATIM)
    list(APPEND SHADER_OUTPUTS "${OUTPUT}")
endforeach()
foreach(SHADER triangle splat)
    foreach(STAGE vert frag)
        set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}_${STAGE}.spv")
        add_custom_command(OUTPUT "${OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND "${GLSLC}" --target-env=vulkan1.1 -O -o "${OUTPUT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.${STAGE}"
            DEPENDS "shaders/${SHADER}.${STAGE}" VERBATIM)
        list(APPEND TRIANGLE_OUTPUTS "${OUTPUT}")
    endforeach()
endforeach()
add_custom_target(gs_android_shaders DEPENDS ${SHADER_OUTPUTS} ${TRIANGLE_OUTPUTS})
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/embedded_shaders.h"
    COMMAND ${CMAKE_COMMAND} "-DDIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/embedded_shaders.h"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed-shaders.cmake"
    DEPENDS ${TRIANGLE_OUTPUTS} ${SHADER_OUTPUTS} cmake/embed-shaders.cmake VERBATIM)

add_library(gs_android_render_core STATIC src/render-core/device_probe.cpp
    src/render-core/gpu_sort.cpp src/render-core/surface_probe.cpp
    src/render-core/scene_renderer.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/embedded_shaders.h")
add_dependencies(gs_android_render_core gs_android_shaders)
target_include_directories(gs_android_render_core PUBLIC include "${ROOT}/include")
target_include_directories(gs_android_render_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(gs_android_render_core PUBLIC vulkan android)
target_link_libraries(gs_android_decoder PRIVATE gs_android_render_core)

add_library(gs_android_engine STATIC src/engine/camera.cpp src/engine/transactions.cpp)
target_include_directories(gs_android_engine PUBLIC include "${ROOT}/include")
target_link_libraries(gs_android_decoder PRIVATE gs_android_engine)

add_executable(gs_android_model_io_tests tests/model_io_tests.cpp)
target_link_libraries(gs_android_model_io_tests PRIVATE gs_android_model_io GTest::gtest_main)

add_executable(gs_android_render_core_tests tests/render_core_tests.cpp)
target_link_libraries(gs_android_render_core_tests PRIVATE gs_android_render_core GTest::gtest_main)

add_executable(gs_android_core_tests tests/core_tests.cpp)
target_link_libraries(gs_android_core_tests PRIVATE gs_android_model_io gs_android_render_core GTest::gtest_main)

add_executable(gs_android_engine_tests tests/engine_tests.cpp)
target_link_libraries(gs_android_engine_tests PRIVATE gs_android_engine GTest::gtest_main)
