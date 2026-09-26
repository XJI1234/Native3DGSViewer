#include "../../GUI/camera.h"
#include "model-io/model_loader.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <cmath>
#include <limits>

using namespace gs;
using namespace gs::desktop;

namespace
{
Double3 camera_local(render::CameraState camera, Double3 point)
{
    const auto q = camera.orientation_xyzw;
    const Double3 u{-q.x, -q.y, -q.z};
    const Double3 v{point.x - camera.position_rub.x, point.y - camera.position_rub.y,
                    point.z - camera.position_rub.z};
    const Double3 t{2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z),
                    2 * (u.x * v.y - u.y * v.x)};
    return {v.x + q.w * t.x + u.y * t.z - u.z * t.y,
            v.y + q.w * t.y + u.z * t.x - u.x * t.z,
            v.z + q.w * t.z + u.x * t.y - u.y * t.x};
}
}

TEST(DesktopViewer, FitAndResetCamera)
{
    SplatScene scene;
    scene.bounds = {{-2, -1, -3}, {2, 1, 3}};
    scene.maxScale = 0.1;
    CameraController camera;
    ASSERT_TRUE(camera.fit(scene, {1600, 900}, 3));
    const auto initial = camera.camera();
    camera.rotate(100, 40);
    camera.zoom(2);
    camera.reset();
    EXPECT_DOUBLE_EQ(camera.camera().position_rub.z, initial.position_rub.z);
    EXPECT_DOUBLE_EQ(camera.camera().orientation_xyzw.w, initial.orientation_xyzw.w);
}

TEST(DesktopViewer, FlyMovementStopsWhenInactive)
{
    SplatScene scene;
    scene.bounds = {{-1, -1, -1}, {1, 1, 1}};
    scene.maxScale = 0.1;
    CameraController camera;
    ASSERT_TRUE(camera.fit(scene, {800, 600}, 3));
    auto before = camera.camera().position_rub;
    camera.move(1, {true, false, false, false, false, false}, false);
    EXPECT_DOUBLE_EQ(camera.camera().position_rub.z, before.z);
    camera.set_mode(ViewMode::Fly);
    camera.move(1, {true, false, false, false, false, false}, false);
    EXPECT_LT(camera.camera().position_rub.z, before.z);
}

TEST(DesktopViewer, FlipZPreservesTheUnflippedView)
{
    SplatScene scene;
    scene.bounds = {{-2, -1, 8}, {2, 1, 12}};
    scene.maxScale = 0.1;
    CameraController camera;
    ASSERT_TRUE(camera.fit(scene, {800, 600}, 3));
    const auto original = camera.camera();
    camera.set_flip_z(true);
    const auto flipped = camera.camera();
    EXPECT_DOUBLE_EQ(flipped.position_rub.x, original.position_rub.x);
    EXPECT_DOUBLE_EQ(flipped.position_rub.y, original.position_rub.y);
    EXPECT_DOUBLE_EQ(flipped.position_rub.z, 20 - original.position_rub.z);
    EXPECT_DOUBLE_EQ(flipped.orientation_xyzw.y, 1);
    camera.reset();
    EXPECT_TRUE(camera.flip_z());
    camera.set_flip_z(false);
    EXPECT_DOUBLE_EQ(camera.camera().position_rub.z, original.position_rub.z);
    EXPECT_DOUBLE_EQ(camera.camera().orientation_xyzw.w, original.orientation_xyzw.w);
}

TEST(DesktopViewer, FlipZMirrorsProjectedGeometryAtRotatedCamera)
{
    SplatScene scene;
    scene.bounds = {{-2, -1, 8}, {2, 1, 12}};
    scene.maxScale = 0.1;
    CameraController camera;
    ASSERT_TRUE(camera.fit(scene, {800, 600}, 3));
    camera.rotate(37, -12);
    auto target_camera = camera.camera();
    target_camera.position_rub.z -= 20;
    const auto target = camera_local(target_camera, {0.2, 0.3, -11.5});
    camera.set_flip_z(true);
    const auto source = camera_local(camera.camera(), {0.2, 0.3, 11.5});
    EXPECT_NEAR(source.x, -target.x, 1e-10);
    EXPECT_NEAR(source.y, target.y, 1e-10);
    EXPECT_NEAR(source.z, target.z, 1e-10);
}

TEST(DesktopViewer, FailedFitKeepsTheCurrentCamera)
{
    SplatScene scene;
    scene.bounds = {{-1, -1, -1}, {1, 1, 1}};
    scene.maxScale = 0.1;
    CameraController camera;
    ASSERT_TRUE(camera.fit(scene, {800, 600}, 3));
    const auto before = camera.camera();
    scene.bounds.max.x = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(camera.fit(scene, {800, 600}, 3));
    camera.rotate(10, 0);
    EXPECT_TRUE(std::isfinite(camera.camera().position_rub.x));
    camera.reset();
    EXPECT_DOUBLE_EQ(camera.camera().position_rub.z, before.position_rub.z);
}

TEST(DesktopViewer, FitsSpzSampleBounds)
{
    auto loader = gs::io::make_model_loader();
    const auto sample = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                        "third_party/spz/samples/hornedlizard.spz";
    if (!std::filesystem::exists(sample)) GTEST_SKIP() << "SPZ sample unavailable";
    auto loaded = loader->load({sample}, {}, {});
    ASSERT_TRUE(std::holds_alternative<SceneHandle>(loaded));
    auto scene = std::get<SceneHandle>(loaded);
    CameraController camera;
    ASSERT_TRUE(camera.fit(*scene, {1600, 900}, 3));
    auto c = camera.camera();
    const auto extent_x = (scene->bounds.max.x - scene->bounds.min.x) / 2 + scene->maxScale * 3;
    const auto extent_y = (scene->bounds.max.y - scene->bounds.min.y) / 2 + scene->maxScale * 3;
    const auto nearest = c.position_rub.z - scene->bounds.max.z - scene->maxScale * 3;
    const auto fy = 900 / (2 * std::tan(c.vertical_fov_radians / 2));
    EXPECT_LT(extent_x * fy / nearest, 800);
    EXPECT_LT(extent_y * fy / nearest, 450);
}
