#pragma once

#include "engine/camera.h"
#include "splat-types/scene.h"

#include <android/native_window.h>
#include <cstdint>
#include <memory>
#include <string>

namespace gs::android::render
{

enum class FrameStatus : uint8_t { Presented, NoScene, SurfaceChanged, SurfaceLost, DeviceLost, Failed };

struct FrameResult
{
    FrameStatus status = FrameStatus::Failed;
    int32_t platform_result = 0;
    std::string diagnostic;
    uint64_t cpu_frame_us = 0;
    uint64_t frame_id = 0;
    uint64_t present_call_ns = 0;
    uint64_t gpu_frame_id = 0;
    uint64_t gpu_project_us = 0;
    uint64_t gpu_sort_us = 0;
    uint64_t gpu_draw_us = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t submitted_splats = 0;
};

// All methods, including destruction, must run on the owning render thread.
class SceneRenderer
{
  public:
    explicit SceneRenderer(ANativeWindow *window);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer &) = delete;
    SceneRenderer &operator=(const SceneRenderer &) = delete;

    bool upload(const SceneHandle &scene, std::string &diagnostic);
    void commit_pending();
    void rollback_pending();
    void clear_scene();
    FrameResult render(const engine::CameraPose &camera);
    [[nodiscard]] uint64_t scene_count() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gs::android::render
