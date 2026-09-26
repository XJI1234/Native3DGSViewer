#include "test_support.h"
#include <thread>
using namespace render_test;
TEST(RenderLifecycle, PresentsOnlyCompletedUploadsAndKeepsOldSceneOnCancellation)
{
    Session s;
    auto first = s.upload(make_scene());
    ASSERT_TRUE(s.pump_until([&] { return s.ready(first); }));
    auto second = s.upload(make_scene(10000));
    s.renderer->cancel_upload(second);
    ASSERT_TRUE(s.pump_until([&] {
        return std::any_of(s.events.begin(), s.events.end(), [&](const auto &e) {
            return e.ticket == second && e.kind == RendererEvent::Kind::SceneFailed;
        });
    }));
    EXPECT_FALSE(s.ready(second));
    EXPECT_FALSE(s.renderer->set_camera(first, {}));
    auto third = s.upload(make_scene());
    ASSERT_TRUE(s.pump_until([&] { return s.ready(third); }));
    EXPECT_TRUE(s.renderer->set_camera(first, {}));
    ASSERT_TRUE(s.pump_until([&] { return s.renderer->get_stats().gpu_frame_ms.has_value(); }));
    EXPECT_GT(s.renderer->get_stats().completed_upload_bytes, 0u);
}
TEST(RenderLifecycle, ZeroViewportWaitsForFirstPresentAndStaleRevisionCannotOverrideResize)
{
    Session s;
    auto g = s.renderer->surface_generation();
    ASSERT_FALSE(s.renderer->resize(g, 1, {0, 0}));
    s.renderer->render_frame();
    auto ticket = s.upload(make_scene());
    for (int i = 0; i < 10; ++i)
        s.renderer->render_frame();
    EXPECT_FALSE(s.ready(ticket));
    ASSERT_FALSE(s.renderer->resize(g, 3, {256, 128}));
    ASSERT_FALSE(s.renderer->resize(g, 2, {0, 0}));
    ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    s.surface->GetDesc1(&desc);
    EXPECT_EQ(desc.Width, 256u);
    EXPECT_TRUE(s.renderer->resize(g + 1, 4, {1, 1}));
}
TEST(RenderLifecycle, RepeatedReplaceCancelClearAndDetach)
{
    Session s;
    for (int i = 0; i < 100; ++i)
    {
        auto ticket = s.upload(make_scene());
        if (i % 2)
        {
            s.renderer->cancel_upload(ticket);
            s.renderer->render_frame();
        }
        else
            ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
        s.renderer->clear_scene();
        s.renderer->render_frame();
    }
    auto g = s.renderer->surface_generation();
    s.renderer->detach_swapchain(g);
    s.renderer->render_frame();
    EXPECT_TRUE(std::any_of(s.events.begin(), s.events.end(), [&](const auto &e) {
        return e.kind == RendererEvent::Kind::SurfaceDetached && e.surface_generation == g;
    }));
}
TEST(RenderLifecycle, CommandsAreSafeWhileRenderThreadSubmits)
{
    Session s;
    auto ticket = s.upload(make_scene());
    std::jthread worker([&](std::stop_token stop) {
        while (!stop.stop_requested())
        {
            s.renderer->render_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool active = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!s.renderer->set_camera(ticket, {}))
        {
            active = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(active);
    for (uint64_t i = 1; i <= 100; ++i)
    {
        CameraState c;
        c.position_rub.x = double(i) * 0.001;
        EXPECT_FALSE(s.renderer->set_camera(ticket, c));
        EXPECT_FALSE(
            s.renderer->resize(s.renderer->surface_generation(), i, {128 + uint32_t(i % 2), 128}));
        (void)s.renderer->get_stats();
    }
    s.renderer->clear_scene();
    s.renderer->detach_swapchain(s.renderer->surface_generation());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    worker.request_stop();
    worker.join();
}
