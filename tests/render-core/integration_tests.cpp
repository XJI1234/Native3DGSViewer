#include "model-io/model_loader.h"
#include "test_support.h"
#include <cmath>

using namespace render_test;
TEST(ModelRenderIntegration, LoadsUploadsPresentsAndRendersWorkspacePlyAndSpz)
{
    const auto workspace =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const std::array samples{workspace / "1.ply",
                             workspace / "Viewer_android/public/scene/jidaoshan.spz",
                             workspace / "Viewer_android/public/scene/zhihuizhimen.spz"};
    for (const auto &path : samples)
        if (!exists(path))
            GTEST_SKIP() << "External corpus unavailable";
    Session session;
    session.renderer->resize(session.renderer->surface_generation(), 1, {512, 512});
    for (size_t i = 0; i < samples.size(); ++i)
    {
        SCOPED_TRACE(i);
        auto decoded = gs::io::make_model_loader()->load({samples[i]}, {}, {});
        ASSERT_TRUE(std::holds_alternative<gs::SceneHandle>(decoded))
            << "code=" << static_cast<int>(std::get<gs::io::LoadError>(decoded).code)
            << " stage=" << static_cast<int>(std::get<gs::io::LoadError>(decoded).stage)
            << " diagnostic=" << std::get<gs::io::LoadError>(decoded).diagnostic;
        auto scene = std::get<gs::SceneHandle>(decoded);
        CameraState camera;
        const auto &a = scene->bounds.min;
        const auto &b = scene->bounds.max;
        double radius = std::max({b.x - a.x, b.y - a.y, b.z - a.z}) * 0.5 + scene->maxScale * 3;
        camera.position_rub = {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5,
                               (a.z + b.z) * 0.5 +
                                   radius / std::sin(camera.vertical_fov_radians * 0.5) + radius};
        camera.far_plane = std::max(10000.0, radius * 10);
        auto ticket = session.upload(scene, camera);
        ASSERT_TRUE(session.pump_until([&] { return session.ready(ticket); }, 20000));
        const auto bytes = scene_bytes(*scene);
        EXPECT_TRUE(std::any_of(session.events.begin(), session.events.end(), [&](const auto &e) {
            return e.kind == RendererEvent::Kind::UploadProgress && e.ticket == ticket &&
                   e.bytes_done == bytes && e.bytes_total == bytes;
        }));
        auto image = render_image(scene, camera, {}, {512, 512});
        EXPECT_GT(image.candidates, 0u);
        EXPECT_GT(
            std::count_if(image.bgra.begin(), image.bgra.end(), [](auto v) { return v != 0; }),
            100u);
        save_bmp(image, std::filesystem::path("out/render-tests") /
                            ("sample-" + std::to_string(i) + ".bmp"));
        std::cout << "Corpus " << i << " count=" << scene->count << " SH=" << int(scene->shDegree)
                  << " candidates=" << image.candidates << "\n";
    }
}
