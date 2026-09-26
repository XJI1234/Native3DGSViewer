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

TEST(EngineCamera, AxisFlipsMirrorViewAndSurviveFit)
{
    CameraController c;
    auto scene = std::make_shared<gs::SplatScene>(*render_test::make_scene());
    scene->bounds = {{1, 2, -3}, {3, 4, -1}};
    ASSERT_FALSE(c.fit_scene(*scene, {}, {800, 600}));
    ASSERT_FALSE(c.orbit(130, 45));
    ASSERT_FALSE(c.pan(20, -10));
    const auto original = c.camera();
    const gs::Double3 center{2, 3, -2};
    EXPECT_NE(original.position_rub.x, center.x);
    EXPECT_NE(original.position_rub.y, center.y);
    const auto rotate = [](gs::render::Quaterniond q, gs::Double3 v) {
        const gs::Double3 a{q.x, q.y, q.z};
        const auto cross = [](gs::Double3 u, gs::Double3 w) {
            return gs::Double3{u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z,
                               u.x * w.y - u.y * w.x};
        };
        const auto first = cross(a, v);
        const auto second = cross(a, {first.x + q.w * v.x, first.y + q.w * v.y,
                                      first.z + q.w * v.z});
        return gs::Double3{v.x + 2 * second.x, v.y + 2 * second.y, v.z + 2 * second.z};
    };
    const auto forward = rotate(original.orientation_xyzw, {0, 0, -1});
    const auto right = rotate(original.orientation_xyzw, {1, 0, 0});
    for (uint8_t mask = 1; mask < 8; ++mask)
    {
        c.set_flip_axes(mask);
        const auto mirrored = c.camera();
        const auto f = rotate(mirrored.orientation_xyzw, {0, 0, -1});
        const auto r = rotate(mirrored.orientation_xyzw, {1, 0, 0});
        const int sx = mask & 1 ? -1 : 1, sy = mask & 2 ? -1 : 1,
                  sz = mask & 4 ? -1 : 1;
        const int screen = (int(bool(mask & 1)) + int(bool(mask & 2)) +
                            int(bool(mask & 4))) % 2 ? -1 : 1;
        SCOPED_TRACE(int(mask));
        EXPECT_NEAR(mirrored.position_rub.x,
                    center.x + sx * (original.position_rub.x - center.x), 1e-12);
        EXPECT_NEAR(mirrored.position_rub.y,
                    center.y + sy * (original.position_rub.y - center.y), 1e-12);
        EXPECT_NEAR(mirrored.position_rub.z,
                    center.z + sz * (original.position_rub.z - center.z),
                    1e-12);
        EXPECT_NEAR(f.x, sx * forward.x, 1e-12);
        EXPECT_NEAR(f.y, sy * forward.y, 1e-12);
        EXPECT_NEAR(f.z, sz * forward.z, 1e-12);
        EXPECT_NEAR(r.x, screen * sx * right.x, 1e-12);
        EXPECT_NEAR(r.y, screen * sy * right.y, 1e-12);
        EXPECT_NEAR(r.z, screen * sz * right.z, 1e-12);
        const auto q = mirrored.orientation_xyzw;
        EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1, 1e-12);
    }
    c.set_flip_axes(0);
    const auto restored = c.camera();
    EXPECT_DOUBLE_EQ(restored.position_rub.x, original.position_rub.x);
    EXPECT_DOUBLE_EQ(restored.position_rub.y, original.position_rub.y);
    EXPECT_DOUBLE_EQ(restored.position_rub.z, original.position_rub.z);
    EXPECT_DOUBLE_EQ(restored.orientation_xyzw.w, original.orientation_xyzw.w);
    c.set_flip_axes(2);
    ASSERT_FALSE(c.fit_scene(*scene, {}, {800, 600}));
    const auto fitted_mirror = c.camera();
    CameraController plain;
    ASSERT_FALSE(plain.fit_scene(*scene, {}, {800, 600}));
    EXPECT_EQ(c.flip_axes(), 2);
    EXPECT_DOUBLE_EQ(fitted_mirror.orientation_xyzw.z, 1);
    EXPECT_DOUBLE_EQ(plain.camera().orientation_xyzw.z, 0);
    c.reset();
    c.set_flip_axes(0);
    const auto reset = c.camera();
    EXPECT_DOUBLE_EQ(reset.orientation_xyzw.w, 1);
}
