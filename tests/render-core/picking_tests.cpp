#include "test_support.h"
#include "picking.h"

namespace
{
using namespace render_test;
TEST(RenderPick, ResolvesVisibleContributionMedianAndOffAxisRay)
{
    PickRequest request;
    request.id = 3;
    request.viewport = {128, 128};
    request.x_px = 90;
    request.y_px = 64;
    auto pick = resolve_pick(request, {{4, 0.9f, 0, 0}, {2, 0.2f, 1, 1}});
    ASSERT_EQ(pick.status, PickStatus::Hit);
    EXPECT_EQ(pick.depth, 4); // Transparent foreground does not hide the opaque background.
    EXPECT_NE(pick.position_rub.x, 0);
    auto pixel = project_world(pick.position_rub, request.camera, request.viewport);
    ASSERT_TRUE(pixel);
    EXPECT_NEAR(pixel->x_px, 90.5, 1e-6);
    EXPECT_NEAR(pixel->y_px, 64.5, 1e-6);
    pick = resolve_pick(request, {{4, 0.9f, 0, 0}, {2, 0.9f, 1, 1}});
    EXPECT_EQ(pick.depth, 2);
    EXPECT_EQ(resolve_pick(request, {{2, 0.01f, 0, 0}}).status, PickStatus::Miss);
    EXPECT_EQ(resolve_pick(request, {}).status, PickStatus::Miss);
    // An invalid clipped foreground can occlude, but cannot supply a fake near-plane hit.
    EXPECT_EQ(resolve_pick(request, {{2, 0.9f, 0, 0}, {0, 0.999f, 1, 1}}).status, PickStatus::Miss);
}
TEST(RenderPick, GpuUsesProjectedEllipseAndRejectsStaleView)
{
    Session session;
    auto ticket = session.upload(make_scene());
    ASSERT_TRUE(session.pump_until([&] { return session.ready(ticket); }));
    PickRequest request{1,  ticket, session.renderer->surface_generation(), 0, {}, {128, 128},
                        64, 64};
    ASSERT_FALSE(session.renderer->request_pick(request));
    std::optional<PickResult> result;
    ASSERT_TRUE(session.pump_until([&] {
        result = session.renderer->take_pick_result();
        return result.has_value();
    }));
    ASSERT_EQ(result->status, PickStatus::Hit);
    EXPECT_NEAR(result->position_rub.z, -2, 1e-5);
    request.id++;
    request.x_px = 0;
    request.y_px = 0;
    ASSERT_FALSE(session.renderer->request_pick(request));
    ASSERT_TRUE(session.pump_until([&] {
        result = session.renderer->take_pick_result();
        return result.has_value();
    }));
    EXPECT_EQ(result->status, PickStatus::Miss);
    request.id++;
    request.x_px = 64;
    request.y_px = 64;
    ASSERT_FALSE(session.renderer->request_pick(request));
    CameraState camera;
    camera.position_rub.x = 1;
    ASSERT_FALSE(session.renderer->set_camera(ticket, camera));
    ASSERT_TRUE(session.pump_until([&] {
        result = session.renderer->take_pick_result();
        return result.has_value();
    }));
    EXPECT_EQ(result->status, PickStatus::Stale);
}
TEST(RenderPick, ClipsOverlaySegmentAtNearFarAndViewportPlanes)
{
    CameraState camera;
    camera.near_plane = 1;
    camera.far_plane = 10;
    const Viewport viewport{128, 128};
    auto line = project_segment({0, 0, 1}, {0, 0, -20}, camera, viewport);
    ASSERT_TRUE(line);
    EXPECT_NEAR((*line)[0].depth, 1, 1e-12);
    EXPECT_NEAR((*line)[1].depth, 10, 1e-12);
    line = project_segment({-100, 0, -2}, {100, 0, -2}, camera, viewport);
    ASSERT_TRUE(line);
    EXPECT_NEAR((*line)[0].x_px, 0, 1e-9);
    EXPECT_NEAR((*line)[1].x_px, 128, 1e-9);
    EXPECT_FALSE(project_segment({0, 0, 1}, {1, 0, 1}, camera, viewport));
    EXPECT_FALSE(project_segment({100, 0, -2}, {101, 0, -2}, camera, viewport));
}
TEST(RenderPick, GpuRejectsCandidateOverflowAndUsesSampledScene)
{
    {
        Session session;
        auto ticket = session.upload(make_scene(pick_capacity + 1));
        ASSERT_TRUE(session.pump_until([&] { return session.ready(ticket); }));
        ASSERT_FALSE(session.renderer->request_pick(
            {1, ticket, session.renderer->surface_generation(), 0, {}, {128, 128}, 64, 64}));
        std::optional<PickResult> result;
        ASSERT_TRUE(session.pump_until([&] {
            result = session.renderer->take_pick_result();
            return result.has_value();
        }));
        EXPECT_EQ(result->status, PickStatus::Overflow);
    }
    QualityConfig quality;
    quality.point_stride = quality.max_point_stride = 2;
    quality.allow_memory_mitigation = false;
    Session session({}, quality);
    auto ticket =
        session.upload(make_scene(2, 0, [](SceneData &data) { data.opacity = {0.05f, 0.99f}; }));
    ASSERT_TRUE(session.pump_until([&] { return session.ready(ticket); }));
    ASSERT_FALSE(session.renderer->request_pick(
        {1, ticket, session.renderer->surface_generation(), 0, {}, {128, 128}, 64, 64}));
    std::optional<PickResult> result;
    ASSERT_TRUE(session.pump_until([&] {
        result = session.renderer->take_pick_result();
        return result.has_value();
    }));
    // The invisible unsampled opaque splat must not supply an endpoint.
    EXPECT_EQ(result->status, PickStatus::Miss);
}
TEST(RenderPick, GpuFollowsVisibleAlphaAcrossSeparatedLayers)
{
    for (float foreground_alpha : {0.2f, 0.9f})
    {
        Session session;
        auto ticket = session.upload(make_scene(2, 0, [&](SceneData &data) {
            data.centers[2] = -4;
            data.opacity = {0.9f, foreground_alpha};
        }));
        ASSERT_TRUE(session.pump_until([&] { return session.ready(ticket); }));
        ASSERT_FALSE(session.renderer->request_pick(
            {1, ticket, session.renderer->surface_generation(), 0, {}, {128, 128}, 64, 64}));
        std::optional<PickResult> result;
        ASSERT_TRUE(session.pump_until([&] {
            result = session.renderer->take_pick_result();
            return result.has_value();
        }));
        ASSERT_EQ(result->status, PickStatus::Hit);
        EXPECT_NEAR(result->depth, foreground_alpha < 0.5f ? 4 : 2, 1e-5);
    }
}
} // namespace
