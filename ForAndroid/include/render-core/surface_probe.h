#pragma once

#include <android/native_window.h>
#include <cstdint>
#include <string>
#include <functional>

namespace gs::android::render
{
struct SurfaceProbeResult
{
    bool presented = false;
    int32_t result = 0;
    std::string diagnostic;
};

// Synchronous diagnostic only. Borrows window; the caller must run off the UI thread.
SurfaceProbeResult probe_surface(ANativeWindow *window,
                                  const std::function<void()> &on_presented = {});
}
