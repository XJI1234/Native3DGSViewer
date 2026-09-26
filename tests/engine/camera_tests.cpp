#include "../render-core/test_support.h"
#include "native3dgs/camera.h"
#include <cmath>

using namespace gs::engine;
TEST(EngineCamera, FitAccountsForTallViewportAndGaussianSupport)
{
    auto scene = std::make_shared<gs::SplatScene>(*render_test::make_scene());
    scene->bounds = {{-10, -2, -3}, {10, 2, 3}};
    scene->maxScale = 2;
    CameraController c;
    ASSERT_FALSE(c.fit_scene(*scene, {}, {100, 1000}));
    ASSERT_TRUE(c.has_scene());
    const auto camera = c.camera();
    const double radius = std::hypot(10, 2, 3) + 6;
    const double horizontal_half = std::atan(std::tan(camera.vertical_fov_radians / 2) * 0.1);
    EXPECT_GE(camera.position_rub.z, radius / std::sin(horizontal_half));
    EXPECT_LT(camera.near_plane, camera.position_rub.z - radius);
    EXPECT_GT(camera.far_plane, camera.position_rub.z + radius);
}

TEST(EngineCamera, OrbitPanDollyResetAndReplayAreDeterministic)
{
    CameraController a, b;
    ASSERT_FALSE(a.fit_scene(*render_test::make_scene(), {}, {800, 600}));
    ASSERT_FALSE(b.fit_scene(*render_test::make_scene(), {}, {800, 600}));
    const auto initial = a.camera();
    for (auto *c : {&a, &b})
    {
        ASSERT_FALSE(c->orbit(100, 50));
        ASSERT_FALSE(c->pan(30, -20));
        ASSERT_FALSE(c->dolly(2));
    }
    EXPECT_DOUBLE_EQ(a.camera().position_rub.x, b.camera().position_rub.x);
    EXPECT_DOUBLE_EQ(a.camera().position_rub.y, b.camera().position_rub.y);
    EXPECT_DOUBLE_EQ(a.camera().position_rub.z, b.camera().position_rub.z);
    a.reset();
    EXPECT_DOUBLE_EQ(a.camera().position_rub.x, initial.position_rub.x);
    EXPECT_DOUBLE_EQ(a.camera().position_rub.y, initial.position_rub.y);
    EXPECT_DOUBLE_EQ(a.camera().position_rub.z, initial.position_rub.z);
    EXPECT_DOUBLE_EQ(a.camera().near_plane, initial.near_plane);
}

TEST(EngineCamera, FlyClampsDeltaAndNormalizesDiagonalMovement)
{
    CameraController a, b, fast;
    for (auto *c : {&a, &b, &fast})
    {
        ASSERT_FALSE(c->fit_scene(*render_test::make_scene(), {}, {800, 600}));
        ASSERT_FALSE(c->set_mode(ViewMode::Fly));
    }
    const auto initial = a.camera();
    ASSERT_FALSE(a.fly({0, 0, 1, false}, 10));
    ASSERT_FALSE(b.fly({0, 0, 1, false}, 0.1));
    ASSERT_FALSE(fast.fly({0, 0, 1, true}, 0.1));
    EXPECT_DOUBLE_EQ(a.camera().position_rub.z, b.camera().position_rub.z);
    EXPECT_NEAR(initial.position_rub.z - fast.camera().position_rub.z,
                4 * (initial.position_rub.z - a.camera().position_rub.z), 1e-12);
    EXPECT_LT(a.camera().position_rub.z, initial.position_rub.z);
    auto before = b.camera().position_rub;
    ASSERT_FALSE(b.fly({1, 1, 1, false}, 0.1));
    auto after = b.camera().position_rub;
    EXPECT_NEAR(std::hypot(after.x - before.x, after.y - before.y, after.z - before.z),
                initial.position_rub.z - a.camera().position_rub.z, 1e-12);
}

TEST(EngineCamera, ModesPreservePoseAndHugeInputNeverFlips)
{
    CameraController c;
    ASSERT_FALSE(c.fit_scene(*render_test::make_scene(), {}, {800, 600}));
    ASSERT_FALSE(c.orbit(10, 1e100));
    const auto pose = c.camera();
    ASSERT_FALSE(c.set_mode(ViewMode::Fly));
    EXPECT_DOUBLE_EQ(c.camera().position_rub.z, pose.position_rub.z);
    ASSERT_FALSE(c.set_mode(ViewMode::Orbit));
    EXPECT_DOUBLE_EQ(c.camera().position_rub.z, pose.position_rub.z);
    for (int i = 0; i < 100; ++i)
        ASSERT_FALSE(c.dolly(i % 2 ? 1e100 : -1e100));
    EXPECT_TRUE(std::isfinite(c.camera().position_rub.z));
    EXPECT_GT(c.camera().near_plane, 0);
}

TEST(EngineCamera, InvalidInputsAndUnrepresentableFitPreserveOldPose)
{
    CameraController c;
    EXPECT_TRUE(c.orbit(1, 1));
    ASSERT_FALSE(c.fit_scene(*render_test::make_scene(), {}, {800, 600}));
    const auto initial = c.camera();
    EXPECT_TRUE(c.pan(INFINITY, 0));
    EXPECT_TRUE(c.resize({0, 0}));
    EXPECT_TRUE(c.fit_scene(*render_test::make_scene(), {}, {0, 0}));
    auto remote = std::make_shared<gs::SplatScene>(*render_test::make_scene());
    remote->bounds = {{1e300, 1e300, 1e300}, {1e300, 1e300, 1e300}};
    remote->worldOrigin = {1e300, 1e300, 1e300};
    EXPECT_TRUE(c.fit_scene(*remote, {}, {800, 600}));
    EXPECT_DOUBLE_EQ(c.camera().position_rub.z, initial.position_rub.z);
}

TEST(EngineCamera, FarOriginAndResizeKeepDoublePose)
{
    auto scene = std::make_shared<gs::SplatScene>(*render_test::make_scene());
    scene->worldOrigin = {1e12, 1e12, 1e12};
    scene->bounds = {{1e12, 1e12, 1e12 - 2}, {1e12, 1e12, 1e12 - 2}};
    CameraController c;
    ASSERT_FALSE(c.fit_scene(*scene, {}, {800, 600}));
    const auto before = c.camera();
    ASSERT_FALSE(c.resize({1600, 1200}));
    EXPECT_DOUBLE_EQ(c.camera().position_rub.z, before.position_rub.z);
    ASSERT_FALSE(c.orbit(100, 0));
    EXPECT_GT(c.camera().position_rub.x, 1e12);
}
