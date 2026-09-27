cmake_minimum_required(VERSION 3.30)
project(Native3DGSViewer VERSION 0.2.0 LANGUAGES C CXX)

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
target_include_directories(zlibstatic PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/zlib>)
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

add_library(model-io STATIC
  src/model-io/client/model_loader.cpp
  src/model-io/common/probe.cpp
  src/model-io/normalize/normalize.cpp)
target_include_directories(model-io PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include> PRIVATE
  src/model-io/common third_party/spz/src/cc)
target_link_libraries(model-io PRIVATE zlibstatic)

add_executable(model-io-helper
  src/model-io/helper/main.cpp
  src/model-io/codecs/decode.cpp
  src/model-io/common/probe.cpp
  src/model-io/normalize/normalize.cpp)
target_include_directories(model-io-helper PRIVATE include src/model-io/common
  third_party/spz/src/cc)
target_link_libraries(model-io-helper PRIVATE spz zlibstatic)

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
  foreach(VARIANT wave32 wave_agnostic)
    set(VARIANT_OUTPUT "${RENDER_SHADER_DIRECTORY}/${ENTRY}_${VARIANT}.cso")
    if(VARIANT STREQUAL "wave32")
      set(VARIANT_PROFILE cs_6_6)
      set(VARIANT_DEFINE GS_FORCE_WAVE32)
    else()
      set(VARIANT_PROFILE cs_6_0)
      set(VARIANT_DEFINE GS_WAVE_AGNOSTIC)
    endif()
    add_custom_command(OUTPUT "${VARIANT_OUTPUT}"
      COMMAND "${DXC}" -T ${VARIANT_PROFILE} -E ${ENTRY} -O3 -WX -Ges
        -D ${VARIANT_DEFINE}
        -I "${CMAKE_SOURCE_DIR}/shaders"
        -I "${CMAKE_SOURCE_DIR}/third_party/parallel-sort/ffx-parallelsort"
        -Fo "${VARIANT_OUTPUT}" "${CMAKE_SOURCE_DIR}/shaders/parallel_sort.hlsl"
      DEPENDS shaders/parallel_sort.hlsl shaders/FFX_ParallelSort_WaveAgnostic.h
        third_party/parallel-sort/ffx-parallelsort/FFX_ParallelSort.h VERBATIM)
    list(APPEND RENDER_SHADER_OUTPUTS "${VARIANT_OUTPUT}")
  endforeach()
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
add_library(render-core STATIC src/render-core/scene/contracts.cpp src/render-core/scene/upload.cpp src/render-core/device/gpu.cpp src/render-core/device/renderer.cpp src/render-core/passes/sort.cpp src/render-core/passes/splat.cpp)
target_include_directories(render-core PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include> PRIVATE src/render-core/scene src/render-core/device src/render-core/passes third_party/parallel-sort/ffx-parallelsort)
target_compile_definitions(render-core PRIVATE NOMINMAX)
target_link_libraries(render-core PRIVATE d3d12 dxgi dxguid)
add_dependencies(render-core render-shaders)
add_library(render-core-testing STATIC src/render-core/device/renderer.cpp)
target_include_directories(render-core-testing PRIVATE src/render-core/scene src/render-core/device src/render-core/passes)
target_compile_definitions(render-core-testing PRIVATE NOMINMAX GS_RENDER_TEST_HOOKS)
target_link_libraries(render-core-testing PUBLIC render-core)
add_library(native3dgs-engine STATIC src/engine/camera.cpp src/engine/engine.cpp)
target_include_directories(native3dgs-engine PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_definitions(native3dgs-engine PRIVATE NOMINMAX)
target_link_libraries(native3dgs-engine PUBLIC model-io render-core)
add_executable(Native3DGSViewer.SortBench bench/gpu-sort.cpp)
target_include_directories(Native3DGSViewer.SortBench PRIVATE src/render-core/device)
target_compile_definitions(Native3DGSViewer.SortBench PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.SortBench PRIVATE render-core)
add_executable(Native3DGSViewer.SceneBench bench/scene.cpp)
target_include_directories(Native3DGSViewer.SceneBench PRIVATE src/render-core/device src/render-core/scene src/render-core/passes)
target_compile_definitions(Native3DGSViewer.SceneBench PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.SceneBench PRIVATE native3dgs-engine)
add_executable(Native3DGSViewer.Tests
  tests/model-io/model_io_tests.cpp
  tests/render-core/contracts_tests.cpp tests/render-core/sort_tests.cpp
  tests/render-core/test_support.cpp tests/render-core/image_tests.cpp tests/render-core/lifecycle_tests.cpp
  tests/render-core/recovery_tests.cpp tests/render-core/integration_tests.cpp
  tests/engine/camera_tests.cpp tests/engine/engine_tests.cpp)
add_executable(model-io-fault-helper tests/model-io/fault_helper.cpp)
target_include_directories(model-io-fault-helper PRIVATE include src/model-io/common)
target_include_directories(Native3DGSViewer.Tests PRIVATE include src/model-io/common src/model-io/normalize src/render-core/scene src/render-core/device src/render-core/passes)
target_compile_definitions(Native3DGSViewer.Tests PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.Tests PRIVATE native3dgs-engine render-core-testing spz GTest::gtest_main)
add_dependencies(Native3DGSViewer.Tests model-io-helper model-io-fault-helper)
add_test(NAME ModelIo COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>" --gtest_filter=ModelIo*)
add_test(NAME RenderCore COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>" --gtest_filter=Render*)
add_test(NAME ModelRenderIntegration COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>" --gtest_filter=ModelRenderIntegration.*)
add_test(NAME Engine COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>" --gtest_filter=Engine*)
add_test(NAME InstalledSDK COMMAND pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tests/sdk/installed-sdk.ps1"
  -BuildDirectory "${CMAKE_BINARY_DIR}" -Configuration "$<CONFIG>" -Scene "${CMAKE_SOURCE_DIR}/../1.ply")
set_tests_properties(InstalledSDK PROPERTIES CONFIGURATIONS Release TIMEOUT 180)

set(VS_UNITTEST "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/VS/UnitTest")
add_library(Native3DGSViewer.TestBridge SHARED tests/model-io/vstest_bridge.cpp)
set_target_properties(Native3DGSViewer.TestBridge PROPERTIES OUTPUT_NAME "Native3DGSViewer.Tests")
target_include_directories(Native3DGSViewer.TestBridge PRIVATE "${VS_UNITTEST}/include")
target_link_libraries(Native3DGSViewer.TestBridge PRIVATE
  "${VS_UNITTEST}/lib/x64/Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")
add_dependencies(Native3DGSViewer.TestBridge Native3DGSViewer.Tests)

include(CMakePackageConfigHelpers)
set_target_properties(native3dgs-engine PROPERTIES EXPORT_NAME Engine)
set_target_properties(render-core PROPERTIES EXPORT_NAME RenderCore)
set_target_properties(model-io PROPERTIES EXPORT_NAME ModelIo)
set_target_properties(zlibstatic PROPERTIES EXPORT_NAME Compression)
install(TARGETS native3dgs-engine render-core model-io zlibstatic EXPORT Native3DGSTargets
  ARCHIVE DESTINATION lib COMPONENT SDK)
install(TARGETS model-io-helper RUNTIME DESTINATION bin COMPONENT SDK)
install(DIRECTORY include/ DESTINATION include COMPONENT SDK)
install(FILES ${RENDER_SHADER_OUTPUTS} DESTINATION bin/shaders COMPONENT SDK)
install(EXPORT Native3DGSTargets NAMESPACE Native3DGS:: DESTINATION lib/cmake/Native3DGS COMPONENT SDK)
configure_package_config_file(cmake/Native3DGSConfig.cmake.in
  "${CMAKE_BINARY_DIR}/Native3DGSConfig.cmake" INSTALL_DESTINATION lib/cmake/Native3DGS)
write_basic_package_version_file("${CMAKE_BINARY_DIR}/Native3DGSConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_BINARY_DIR}/Native3DGSConfig.cmake" "${CMAKE_BINARY_DIR}/Native3DGSConfigVersion.cmake"
  DESTINATION lib/cmake/Native3DGS COMPONENT SDK)
install(DIRECTORY examples/sdk-host/ DESTINATION examples/sdk-host COMPONENT SDK)
install(FILES docs/SPEC-engine-sdk.md docs/SDK-guide.md README.md docs/engine-sdk-verification.md
  DESTINATION share/Native3DGS COMPONENT SDK)
install(DIRECTORY docs/ DESTINATION share/Native3DGS/docs COMPONENT SDK)
install(FILES GUI/README.md DESTINATION share/Native3DGS/GUI COMPONENT SDK)
install(FILES third_party/README.md DESTINATION share/Native3DGS RENAME dependencies.md COMPONENT SDK)
foreach(DEP miniply spz zlib zstd parallel-sort)
  if(DEP STREQUAL "miniply")
    set(LICENSE_FILE LICENSE.md)
  elseif(DEP STREQUAL "parallel-sort")
    set(LICENSE_FILE LICENSE.txt)
  else()
    set(LICENSE_FILE LICENSE)
  endif()
  install(FILES "third_party/${DEP}/${LICENSE_FILE}" DESTINATION "share/Native3DGS/licenses/${DEP}" COMPONENT SDK)
endforeach()
set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME Native3DGS-SDK)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "Native3DGS-SDK-${PROJECT_VERSION}-windows-x64")
set(CPACK_COMPONENTS_ALL SDK)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
include(CPack)

option(NATIVE3DGS_ENABLE_ASAN "Instrument engine, decoders and tests with MSVC AddressSanitizer" OFF)
if(NATIVE3DGS_ENABLE_ASAN)
  foreach(TARGET_NAME native3dgs-engine render-core render-core-testing model-io model-io-helper
      Native3DGSViewer.Tests Native3DGSViewer.SortBench Native3DGSViewer.SceneBench
      spz zlibstatic libzstd_static gtest gtest_main)
    target_compile_options(${TARGET_NAME} PRIVATE /fsanitize=address /Zi)
  endforeach()
  # Fault helper deliberately exhausts a 128 MiB Job; instrument the actual decoder instead.
endif()
