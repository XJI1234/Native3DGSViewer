enable_language(CUDA)
find_package(CUDAToolkit 13.3 REQUIRED)
if(NOT CUDAToolkit_VERSION VERSION_LESS 13.4)
  message(FATAL_ERROR "The experiment pins CUDA 13.3; select its explicit toolkit root")
endif()
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES 86)
add_compile_options("$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8,/Zc:preprocessor>"
  "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>" "$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")

function(cuda_experiment_output target)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cuda/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cuda/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cuda/$<CONFIG>")
endfunction()

add_executable(Native3DGSViewer.CudaProbe tests/toolchain_probe.cu)
target_link_libraries(Native3DGSViewer.CudaProbe PRIVATE CUDA::cudart_static)
cuda_experiment_output(Native3DGSViewer.CudaProbe)
add_test(NAME CudaToolchain COMMAND Native3DGSViewer.CudaProbe)
set_tests_properties(CudaToolchain PROPERTIES TIMEOUT 30)

add_library(gs-cuda-backend SHARED src/interop/interop.cpp src/operators/fill.cu src/operators/sort.cu src/operators/project.cu)
target_include_directories(gs-cuda-backend PUBLIC include "${PROJECT_SOURCE_DIR}/src/render-core/passes")
target_compile_definitions(gs-cuda-backend PRIVATE GS_CUDA_EXPORTS NOMINMAX)
target_link_libraries(gs-cuda-backend PRIVATE CUDA::cudart_static d3d12 dxgi)
set_target_properties(gs-cuda-backend PROPERTIES CUDA_RUNTIME_LIBRARY Static)
cuda_experiment_output(gs-cuda-backend)

add_executable(Native3DGSViewer.CudaTests tests/interop_tests.cpp tests/sort_tests.cpp tests/project_tests.cpp)
target_include_directories(Native3DGSViewer.CudaTests PRIVATE include "${PROJECT_SOURCE_DIR}/src/render-core/device")
target_compile_definitions(Native3DGSViewer.CudaTests PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.CudaTests PRIVATE gs-cuda-backend render-core GTest::gtest_main)
cuda_experiment_output(Native3DGSViewer.CudaTests)
add_custom_command(TARGET Native3DGSViewer.CudaTests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Native3DGSViewer.CudaTests>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RENDER_SHADER_OUTPUTS} "$<TARGET_FILE_DIR:Native3DGSViewer.CudaTests>/shaders"
  VERBATIM)
add_test(NAME CudaInterop COMMAND Native3DGSViewer.CudaTests --gtest_filter=CudaInterop.*)
set_tests_properties(CudaInterop PROPERTIES TIMEOUT 60)
add_executable(Native3DGSViewer.CudaLifecycleTests tests/lifecycle_tests.cpp)
target_include_directories(Native3DGSViewer.CudaLifecycleTests PRIVATE "${PROJECT_SOURCE_DIR}/src/render-core/device")
target_compile_definitions(Native3DGSViewer.CudaLifecycleTests PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.CudaLifecycleTests PRIVATE gs-cuda-backend render-core GTest::gtest_main)
cuda_experiment_output(Native3DGSViewer.CudaLifecycleTests)
add_dependencies(Native3DGSViewer.CudaTests Native3DGSViewer.CudaLifecycleTests)
# Debug-layer activation is process global. Resource accounting must run in a
# fresh process; debug interop fence retirement is recorded separately.
add_test(NAME CudaLifecycleHandles COMMAND Native3DGSViewer.CudaLifecycleTests --gtest_filter=CudaLifecycle.RepeatsHundredContextBufferTextureLifetimesWithoutHandleGrowth)
add_test(NAME CudaLifecycle COMMAND Native3DGSViewer.CudaLifecycleTests --gtest_filter=CudaLifecycle.ReportsBoundedTimeoutAndRecoversAfterInjectedQueueStall)
set_tests_properties(CudaLifecycle CudaLifecycleHandles PROPERTIES TIMEOUT 60)
add_test(NAME CudaSort COMMAND Native3DGSViewer.CudaTests --gtest_filter=CudaSort.*)
set_tests_properties(CudaSort PROPERTIES TIMEOUT 240)
add_test(NAME CudaProject COMMAND Native3DGSViewer.CudaTests --gtest_filter=CudaProject.*)
set_tests_properties(CudaProject PROPERTIES TIMEOUT 60)

add_executable(Native3DGSViewer.CudaBench bench/scene.cpp)
target_include_directories(Native3DGSViewer.CudaBench PRIVATE
  "${PROJECT_SOURCE_DIR}/src/render-core/device" "${PROJECT_SOURCE_DIR}/src/render-core/scene"
  "${PROJECT_SOURCE_DIR}/src/render-core/passes")
target_compile_definitions(Native3DGSViewer.CudaBench PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.CudaBench PRIVATE native3dgs-engine gs-cuda-backend CUDA::cudart_static)
cuda_experiment_output(Native3DGSViewer.CudaBench)
add_dependencies(Native3DGSViewer.CudaBench model-io-helper render-shaders)
add_custom_command(TARGET Native3DGSViewer.CudaBench POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:model-io-helper>" "$<TARGET_FILE_DIR:Native3DGSViewer.CudaBench>"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Native3DGSViewer.CudaBench>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RENDER_SHADER_OUTPUTS} "$<TARGET_FILE_DIR:Native3DGSViewer.CudaBench>/shaders"
  VERBATIM)
