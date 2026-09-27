include_guard(GLOBAL)
get_filename_component(_gs_android_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Native3DGS Android 0.2.0 supports Android arm64-v8a only")
endif()
add_library(Native3DGSAndroid::Engine SHARED IMPORTED GLOBAL)
set_target_properties(Native3DGSAndroid::Engine PROPERTIES
    IMPORTED_LOCATION "${_gs_android_root}/lib/arm64-v8a/libgs_android_decoder.so"
    INTERFACE_INCLUDE_DIRECTORIES "${_gs_android_root}/include")
