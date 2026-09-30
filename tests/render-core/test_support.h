#pragma once
#include "splat.h"
#include "test_renderer.h"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <gtest/gtest.h>

namespace render_test
{
using namespace gs::render;
using namespace gs::render::detail;
struct SceneData
{
    std::vector<float> centers, scales, rotations, opacity, rgb, sh;
};
inline gs::SceneHandle make_scene(uint32_t n = 1, uint8_t degree = 0,
                                  std::function<void(SceneData &)> change = {})
{
    auto data = std::make_shared<SceneData>();
    data->centers.resize(n * 3);
    data->scales.assign(n * 3, 0.2f);
    data->rotations.resize(n * 4);
    data->opacity.assign(n, 0.5f);
    data->rgb.resize(n * 3);
    data->sh.resize(n * 3 * ((degree + 1) * (degree + 1) - 1));
    for (uint32_t i = 0; i < n; ++i)
    {
        data->centers[i * 3 + 2] = -2;
        data->rotations[i * 4 + 3] = 1;
        data->rgb[i * 3] = 1;
    }
    if (change)
        change(*data);
    auto scene = std::make_shared<gs::SplatScene>();
    scene->count = n;
    scene->shDegree = degree;
    scene->storage = data;
    scene->centerLocal = data->centers;
    scene->scale = data->scales;
    scene->rotation = data->rotations;
    scene->opacity = data->opacity;
    scene->rgb0 = data->rgb;
    scene->shRest = data->sh;
    scene->maxScale = 0.2;
    scene->bounds = {{0, 0, -2}, {0, 0, -2}};
    return scene;
}
struct Image
{
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bgra;
    std::vector<float> projected;
    uint32_t candidates = 0;
    uint32_t rejected = 0;
};
Image render_image(gs::SceneHandle, CameraState = {}, QualityConfig = {}, Viewport = {128, 128});
void save_bmp(const Image &, const std::filesystem::path &);
class Session
{
  public:
    std::unique_ptr<IRenderer> renderer;
    ComPtr<IDXGISwapChain3> surface;
    std::vector<RendererEvent> events;
    Session(std::shared_ptr<RendererTestControl> = {}, QualityConfig = {});
    ~Session();
    void bind(Viewport = {128, 128});
    bool pump_until(std::function<bool()>, int timeout_ms = 10000);
    UploadTicket upload(gs::SceneHandle, CameraState = {});
    bool ready(UploadTicket) const;
};
} // namespace render_test
