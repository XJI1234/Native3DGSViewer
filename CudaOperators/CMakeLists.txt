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

add_library(gs-cuda-backend SHARED src/interop/interop.cpp src/operators/fill.cu)
target_include_directories(gs-cuda-backend PUBLIC include)
target_compile_definitions(gs-cuda-backend PRIVATE GS_CUDA_EXPORTS NOMINMAX)
target_link_libraries(gs-cuda-backend PRIVATE CUDA::cudart_static d3d12 dxgi)
set_target_properties(gs-cuda-backend PROPERTIES CUDA_RUNTIME_LIBRARY Static)
cuda_experiment_output(gs-cuda-backend)

add_executable(Native3DGSViewer.CudaTests tests/interop_tests.cpp)
target_include_directories(Native3DGSViewer.CudaTests PRIVATE include "${PROJECT_SOURCE_DIR}/src/render-core/device")
target_compile_definitions(Native3DGSViewer.CudaTests PRIVATE NOMINMAX)
target_link_libraries(Native3DGSViewer.CudaTests PRIVATE gs-cuda-backend render-core GTest::gtest_main)
cuda_experiment_output(Native3DGSViewer.CudaTests)
add_test(NAME CudaInterop COMMAND Native3DGSViewer.CudaTests --gtest_filter=CudaInterop.*)
set_tests_properties(CudaInterop PROPERTIES TIMEOUT 60)
