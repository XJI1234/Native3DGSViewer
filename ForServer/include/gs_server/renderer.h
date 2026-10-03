#pragma once
#include "gs_server/frame.h"
#include "gs_server/primitives.h"
#include "splat-types/scene.h"
#include <memory>

namespace gs::server
{
struct RenderRequest
{
    uint32_t width = 640, height = 360;
    Profile profile = Profile::Rgba;
    double yaw = 0, pitch = 14.0362434679, zoom = 1;
    bool capture_projection = false;
    bool flip_y = false;
};
struct RenderStats
{
    double project_ms = 0, sort_ms = 0, gather_ms = 0, draw_ms = 0, readback_ms = 0;
    uint64_t source_count = 0, visible_count = 0, tile_references = 0;
    uint64_t resident_bytes = 0, peak_gpu_bytes = 0;
};
struct RenderOutput
{
    Frame frame;
    RenderStats stats;
    std::vector<Ellipse> debug_projection;
};
SceneHandle load_scene(const std::string &path);
class CudaRenderer
{
  public:
    CudaRenderer(SceneHandle scene, int device);
    ~CudaRenderer();
    CudaRenderer(const CudaRenderer &) = delete;
    CudaRenderer &operator=(const CudaRenderer &) = delete;
    RenderOutput render(const RenderRequest &request);
    static std::string devices();
    static void self_test(int device);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gs::server
