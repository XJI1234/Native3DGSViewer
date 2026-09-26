cmake_minimum_required(VERSION 3.30)
project(Native3DGSViewer LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Native3DGSViewer requires Windows x64")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/$<CONFIG>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/spz/src/cc/load-spz.cc" OR
   NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/miniply/miniply.cpp" OR
   NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/googletest/googletest/src/gtest-all.cc" OR
   NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/parallel-sort/ffx-parallelsort/FFX_ParallelSort.h")
  message(FATAL_ERROR "Initialize pinned submodules: git submodule update --init --recursive")
endif()

add_library(zlibstatic STATIC
  third_party/zlib/adler32.c third_party/zlib/compress.c third_party/zlib/crc32.c
  third_party/zlib/deflate.c third_party/zlib/gzclose.c third_party/zlib/gzlib.c
  third_party/zlib/gzread.c third_party/zlib/gzwrite.c third_party/zlib/inflate.c
  third_party/zlib/infback.c third_party/zlib/inftrees.c third_party/zlib/inffast.c
  third_party/zlib/trees.c third_party/zlib/uncompr.c third_party/zlib/zutil.c)
target_include_directories(zlibstatic PUBLIC third_party/zlib)
target_compile_definitions(zlibstatic PRIVATE NO_FSEEKO _CRT_SECURE_NO_DEPRECATE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/zstd/build/cmake EXCLUDE_FROM_ALL)
add_library(spz STATIC
  third_party/spz/src/cc/load-spz.cc
  third_party/spz/src/cc/splat-c-types.cc
  third_party/spz/src/cc/splat-types.cc)
target_include_directories(spz PUBLIC third_party/spz/src/cc)
target_link_libraries(spz PUBLIC zlibstatic libzstd_static)

# The pinned miniply accepts only narrow filenames. Build a generated adapter
# with its constructor taking the already-open CRT stream; keep upstream intact.
set(MINIPLY_GENERATED "${CMAKE_BINARY_DIR}/generated/miniply")
file(MAKE_DIRECTORY "${MINIPLY_GENERATED}")
file(READ "${CMAKE_SOURCE_DIR}/third_party/miniply/miniply.h" MINI_H)
string(REPLACE "PLYReader(const char* filename);" "PLYReader(FILE* stream);" MINI_H "${MINI_H}")
file(WRITE "${MINIPLY_GENERATED}/miniply.h" "${MINI_H}")
file(READ "${CMAKE_SOURCE_DIR}/third_party/miniply/miniply.cpp" MINI_CPP)
string(REPLACE "PLYReader::PLYReader(const char* filename)" "PLYReader::PLYReader(FILE* stream)" MINI_CPP "${MINI_CPP}")
string(REPLACE "if (file_open(&m_f, filename, \"rb\") != 0)" "m_f = stream;\n    if (m_f == nullptr)" MINI_CPP "${MINI_CPP}")
file(WRITE "${MINIPLY_GENERATED}/miniply.cpp" "${MINI_CPP}")
add_library(miniply STATIC "${MINIPLY_GENERATED}/miniply.cpp")
target_include_directories(miniply PUBLIC "${MINIPLY_GENERATED}")

add_library(model-io STATIC
  src/model-io/client/model_loader.cpp
  src/model-io/common/probe.cpp
  src/model-io/normalize/normalize.cpp)
target_include_directories(model-io PUBLIC include PRIVATE
  src/model-io/common third_party/spz/src/cc)
target_link_libraries(model-io PRIVATE zlibstatic)

add_executable(model-io-helper
  src/model-io/helper/main.cpp
  src/model-io/codecs/decode.cpp
  src/model-io/common/probe.cpp
  src/model-io/normalize/normalize.cpp)
target_include_directories(model-io-helper PRIVATE include src/model-io/common
  third_party/spz/src/cc "${MINIPLY_GENERATED}")
target_link_libraries(model-io-helper PRIVATE miniply spz zlibstatic)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
enable_testing()
set(DXC "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe")
if(NOT EXISTS "${DXC}")
  message(FATAL_ERROR "Install Windows SDK 10.0.26100.0 DXC")
endif()
set(RENDER_SHADER_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${RENDER_SHADER_DIRECTORY}")
set(RENDER_SHADER_OUTPUTS)
foreach(ENTRY count reduce scan scan_add scatter)
  set(OUTPUT "${RENDER_SHADER_DIRECTORY}/${ENTRY}.cso")
  add_custom_command(OUTPUT "${OUTPUT}"
    COMMAND "${DXC}" -T cs_6_0 -E ${ENTRY} -O3 -WX -Ges
      -I "${CMAKE_SOURCE_DIR}/third_party/parallel-sort/ffx-parallelsort"
      -Fo "${OUTPUT}" "${CMAKE_SOURCE_DIR}/shaders/parallel_sort.hlsl"
    DEPENDS shaders/parallel_sort.hlsl third_party/parallel-sort/ffx-parallelsort/FFX_ParallelSort.h VERBATIM)
  list(APPEND RENDER_SHADER_OUTPUTS "${OUTPUT}")
endforeach()
foreach(ENTRY reset_args project vertex pixel)
  if(ENTRY STREQUAL "vertex")
    set(PROFILE vs_6_0)
  elseif(ENTRY STREQUAL "pixel")
    set(PROFILE ps_6_0)
  else()
    set(PROFILE cs_6_0)
  endif()
  set(OUTPUT "${RENDER_SHADER_DIRECTORY}/${ENTRY}.cso")
  add_custom_command(OUTPUT "${OUTPUT}"
    COMMAND "${DXC}" -T ${PROFILE} -E ${ENTRY} -O3 -WX -Ges -Fo "${OUTPUT}" "${CMAKE_SOURCE_DIR}/shaders/splat.hlsl"
    DEPENDS shaders/splat.hlsl VERBATIM)
  list(APPEND RENDER_SHADER_OUTPUTS "${OUTPUT}")
endforeach()
add_custom_target(render-shaders DEPENDS ${RENDER_SHADER_OUTPUTS})
add_custom_command(TARGET render-shaders POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/out/$<CONFIG>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RENDER_SHADER_OUTPUTS} "${CMAKE_SOURCE_DIR}/out/$<CONFIG>/shaders" VERBATIM)
add_library(render-core STATIC src/render-core/scene/contracts.cpp src/render-core/device/gpu.cpp src/render-core/device/renderer.cpp src/render-core/passes/sort.cpp src/render-core/passes/splat.cpp)
target_include_directories(render-core PUBLIC include PRIVATE src/render-core/scene src/render-core/device src/render-core/passes third_party/parallel-sort/ffx-parallelsort)
target_compile_definitions(render-core PRIVATE NOMINMAX)
target_link_libraries(render-core PRIVATE d3d12 dxgi dxguid)
add_dependencies(render-core render-shaders)
add_executable(Native3DGSViewer.SortBench bench/gpu-sort.cpp)
target_include_directories(Native3DGSViewer.SortBench PRIVATE src/render-core/device)
target_compile_definitions(Native3DGSViewer.SortBench PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.SortBench PRIVATE render-core)
add_executable(Native3DGSViewer.Tests
  tests/model-io/model_io_tests.cpp tests/render-core/contracts_tests.cpp tests/render-core/sort_tests.cpp
  tests/render-core/test_support.cpp tests/render-core/image_tests.cpp tests/render-core/lifecycle_tests.cpp tests/render-core/recovery_tests.cpp tests/render-core/integration_tests.cpp)
add_executable(model-io-fault-helper tests/model-io/fault_helper.cpp)
target_include_directories(model-io-fault-helper PRIVATE include src/model-io/common)
target_include_directories(Native3DGSViewer.Tests PRIVATE include src/model-io/common src/model-io/normalize src/render-core/scene src/render-core/device src/render-core/passes)
target_compile_definitions(Native3DGSViewer.Tests PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.Tests PRIVATE model-io render-core spz GTest::gtest_main)
add_dependencies(Native3DGSViewer.Tests model-io-helper model-io-fault-helper)
add_test(NAME ModelIo COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>" --gtest_filter=ModelIo*)
add_test(NAME RenderCore COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>" --gtest_filter=Render*)
add_test(NAME ModelRenderIntegration COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>" --gtest_filter=ModelRenderIntegration.*)

set(VS_UNITTEST "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/VS/UnitTest")
add_library(Native3DGSViewer.TestBridge SHARED tests/model-io/vstest_bridge.cpp)
set_target_properties(Native3DGSViewer.TestBridge PROPERTIES OUTPUT_NAME "Native3DGSViewer.Tests")
target_include_directories(Native3DGSViewer.TestBridge PRIVATE "${VS_UNITTEST}/include")
target_link_libraries(Native3DGSViewer.TestBridge PRIVATE
  "${VS_UNITTEST}/lib/x64/Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")
add_dependencies(Native3DGSViewer.TestBridge Native3DGSViewer.Tests)
