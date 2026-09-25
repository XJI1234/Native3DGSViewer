cmake_minimum_required(VERSION 3.30)
project(Native3DGSViewer LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "model-io requires Windows x64")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/$<CONFIG>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/spz/src/cc/load-spz.cc" OR
   NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/miniply/miniply.cpp" OR
   NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/googletest/googletest/src/gtest-all.cc")
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
add_executable(Native3DGSViewer.Tests
  tests/model-io/model_io_tests.cpp)
target_include_directories(Native3DGSViewer.Tests PRIVATE include src/model-io/common src/model-io/normalize)
target_link_libraries(Native3DGSViewer.Tests PRIVATE model-io spz GTest::gtest_main)
add_dependencies(Native3DGSViewer.Tests model-io-helper)
add_test(NAME ModelIo COMMAND "$<TARGET_FILE:Native3DGSViewer.Tests>")

set(VS_UNITTEST "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/VS/UnitTest")
add_library(Native3DGSViewer.TestBridge SHARED tests/model-io/vstest_bridge.cpp)
set_target_properties(Native3DGSViewer.TestBridge PROPERTIES OUTPUT_NAME "Native3DGSViewer.Tests")
target_include_directories(Native3DGSViewer.TestBridge PRIVATE "${VS_UNITTEST}/include")
target_link_libraries(Native3DGSViewer.TestBridge PRIVATE
  "${VS_UNITTEST}/lib/x64/Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")
add_dependencies(Native3DGSViewer.TestBridge Native3DGSViewer.Tests)
