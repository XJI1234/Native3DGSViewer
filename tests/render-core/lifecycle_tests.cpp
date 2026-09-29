#include "test_support.h"
#include <thread>
using namespace render_test;
namespace
{
uint64_t local_headroom_for(const gs::SplatScene &scene, uint8_t degree, uint32_t stride)
{
    static const bool uma = GpuDevice{}.uma;
    const auto scene_bytes = incremental_bytes(scene, degree, stride);
    const auto upload_bytes = uma ? upload_reserve_bytes(scene, degree, stride) : 0;
    return (scene_bytes + upload_bytes) * 5 / 4 + 1024;
}
}
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
TEST(RenderLifecycle, FrameStatsWaitForActiveScenesCompletedFrame)
{
    Session s;
    const auto first = s.upload(make_scene(1));
    ASSERT_TRUE(s.pump_until([&] { return s.ready(first); }));
    ASSERT_TRUE(s.pump_until([&] {
        const auto stats = s.renderer->get_stats();
        return stats.gpu_frame_ms.has_value() && stats.drawn_splats == 1;
    }));
    const auto second = s.upload(make_scene(2));
    ASSERT_TRUE(s.pump_until([&] { return s.ready(second); }));
    const auto activated = s.renderer->get_stats();
    EXPECT_EQ(activated.active_ticket, second);
    EXPECT_EQ(activated.active_source_splats, 2u);
    EXPECT_EQ(activated.active_source_sh_degree, 0u);
    EXPECT_EQ(activated.active_splats, 2u);
    EXPECT_EQ(activated.presented_frame_id, 0u);
    EXPECT_FALSE(activated.gpu_frame_ms.has_value());
    EXPECT_EQ(activated.drawn_splats, 0u);
    ASSERT_TRUE(s.pump_until([&] {
        const auto stats = s.renderer->get_stats();
        return stats.gpu_frame_ms.has_value() && stats.drawn_splats == 2;
    }));
    s.renderer->clear_scene();
    ASSERT_TRUE(s.pump_until([&] {
        return s.renderer->get_stats().active_ticket == 0;
    }));
    const auto cleared = s.renderer->get_stats();
    EXPECT_EQ(cleared.active_source_splats, 0u);
    EXPECT_EQ(cleared.active_splats, 0u);
    EXPECT_EQ(cleared.presented_frame_id, 0u);
    EXPECT_FALSE(cleared.gpu_frame_ms.has_value());
}
TEST(RenderLifecycle, BudgetMitigationSelectsShZeroAndStrictModeRejects)
{
    auto scene = make_scene(1000, 3);
    auto control = std::make_shared<RendererTestControl>();
    control->local_headroom_override = local_headroom_for(*scene, 0, 1);
    {
        Session s(control);
        auto ticket = s.upload(scene);
        ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
        const auto stats = s.renderer->get_stats();
        EXPECT_TRUE(stats.memory_mitigation);
        EXPECT_EQ(stats.active_sh_degree, 0);
        EXPECT_EQ(scene->shDegree, 3);
    }
    QualityConfig quality;
    quality.allow_memory_mitigation = false;
    Session strict(control, quality);
    auto rejected = strict.renderer->upload_scene(scene, {});
    ASSERT_TRUE(std::holds_alternative<RenderError>(rejected));
    EXPECT_EQ(std::get<RenderError>(rejected).code, RenderErrorCode::OutOfVideoMemory);
}
TEST(RenderLifecycle, BudgetMitigationSamplesPointsOnlyAfterShZero)
{
    auto scene = make_scene(1000, 3);
    auto control = std::make_shared<RendererTestControl>();
    control->local_headroom_override = local_headroom_for(*scene, 0, 2);
    Session s(control);
    const auto ticket = s.upload(scene);
    ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
    const auto stats = s.renderer->get_stats();
    EXPECT_TRUE(stats.memory_mitigation);
    EXPECT_EQ(stats.active_sh_degree, 0);
    EXPECT_EQ(stats.active_point_stride, 2u);
    EXPECT_EQ(stats.active_splats, 500u);
    EXPECT_EQ(scene->count, 1000u);
}
TEST(RenderLifecycle, ManualSamplingWorksWithoutAutomaticMitigation)
{
    QualityConfig quality;
    quality.sh_degree_cap = 2;
    quality.point_stride = quality.max_point_stride = 4;
    quality.allow_memory_mitigation = false;
    Session s({}, quality);
    const auto ticket = s.upload(make_scene(1001, 3));
    ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
    const auto stats = s.renderer->get_stats();
    EXPECT_EQ(stats.active_ticket, ticket);
    EXPECT_EQ(stats.active_source_splats, 1001u);
    EXPECT_EQ(stats.active_source_sh_degree, 3u);
    EXPECT_EQ(stats.active_splats, 251u);
    EXPECT_EQ(stats.active_sh_degree, 2u);
    EXPECT_EQ(stats.active_point_stride, 4u);
    EXPECT_FALSE(stats.memory_mitigation);
}
TEST(RenderLifecycle, AutomaticSamplingStopsAtConfiguredMaximum)
{
    auto scene = make_scene(1000, 3);
    auto control = std::make_shared<RendererTestControl>();
    control->local_headroom_override = local_headroom_for(*scene, 0, 4);
    QualityConfig quality;
    quality.max_point_stride = 2;
    Session s(control, quality);
    auto rejected = s.renderer->upload_scene(scene, {});
    ASSERT_TRUE(std::holds_alternative<RenderError>(rejected));
    EXPECT_EQ(std::get<RenderError>(rejected).code, RenderErrorCode::OutOfVideoMemory);
    quality.max_point_stride = 4;
    Session allowed(control, quality);
    const auto ticket = allowed.upload(scene);
    ASSERT_TRUE(allowed.pump_until([&] { return allowed.ready(ticket); }));
    const auto stats = allowed.renderer->get_stats();
    EXPECT_TRUE(stats.memory_mitigation);
    EXPECT_EQ(stats.active_point_stride, 4u);
}
TEST(RenderLifecycle, SmallSceneFitsBelowOldFixedUploadReserve)
{
    auto control = std::make_shared<RendererTestControl>();
    control->local_headroom_override = 10ull << 20;
    Session s(control);
    const auto ticket = s.upload(make_scene(1000, 3));
    ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
    EXPECT_EQ(s.renderer->get_stats().active_splats, 1000u);
    EXPECT_FALSE(s.renderer->get_stats().memory_mitigation);
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

TEST(RenderLifecycle, ResizeBeforeAttachSurvivesInitialSurfaceBinding)
{
    Session s;
    auto g = s.renderer->surface_generation();
    ASSERT_FALSE(s.renderer->resize(g, 1, {257, 129}));
    ASSERT_FALSE(s.renderer->attach_swapchain(g, s.surface.Get()));
    s.renderer->render_frame();
    DXGI_SWAP_CHAIN_DESC1 desc{};
    ASSERT_EQ(s.surface->GetDesc1(&desc), S_OK);
    EXPECT_EQ(desc.Width, 257u);
    EXPECT_EQ(desc.Height, 129u);
}

TEST(RenderLifecycle, IdenticalCameraAndViewportReuseExistingSort)
{
    Session s;
    auto ticket = s.upload(make_scene());
    ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
    const auto before = s.renderer->get_stats();
    for (uint64_t i = 1; i <= 10; ++i)
    {
        ASSERT_FALSE(s.renderer->set_camera(ticket, {}));
        ASSERT_FALSE(s.renderer->resize(s.renderer->surface_generation(), i, {128, 128}));
        s.renderer->render_frame();
    }
    EXPECT_EQ(s.renderer->get_stats().sort_pass_count, before.sort_pass_count);
    EXPECT_GT(s.renderer->get_stats().sort_reuse_count, before.sort_reuse_count);
}
TEST(RenderLifecycle, WrongThreadFrameIsRejectedAndReported)
{
    Session s;
    s.renderer->render_frame();
    auto before = s.renderer->get_stats();
    std::thread wrong([&] { s.renderer->render_frame(); });
    wrong.join();
    auto after = s.renderer->get_stats();
    EXPECT_EQ(after.wrong_thread_frame_calls, before.wrong_thread_frame_calls + 1);
    EXPECT_EQ(after.sort_pass_count, before.sort_pass_count);
}
