#include "engine/camera.h"
#include "engine/transactions.h"

#include <gtest/gtest.h>

#include <cmath>
#include <future>
#include <thread>

namespace
{
gs::SplatScene scene()
{
    gs::SplatScene value;
    value.count = 1;
    value.worldOrigin = {0, 0, 0};
    value.bounds = {{-1, -1, -1}, {1, 1, 1}};
    value.maxScale = 0.1;
    return value;
}

double screen_x(const gs::android::engine::CameraPose &camera, gs::Double3 point)
{
    const auto &q = camera.orientation;
    const gs::Double3 delta{point.x - camera.position.x, point.y - camera.position.y,
                            point.z - camera.position.z};
    const double right_x = 1 - 2 * (q[1] * q[1] + q[2] * q[2]);
    const double right_y = 2 * (q[0] * q[1] + q[2] * q[3]);
    const double right_z = 2 * (q[0] * q[2] - q[1] * q[3]);
    const double horizontal = right_x * delta.x + right_y * delta.y + right_z * delta.z;
    return camera.horizontal_mirror ? -horizontal : horizontal;
}

double screen_y(const gs::android::engine::CameraPose &camera, gs::Double3 point)
{
    const auto &q = camera.orientation;
    const gs::Double3 delta{point.x - camera.position.x, point.y - camera.position.y,
                            point.z - camera.position.z};
    const double up_x = 2 * (q[0] * q[1] - q[2] * q[3]);
    const double up_y = 1 - 2 * (q[0] * q[0] + q[2] * q[2]);
    const double up_z = 2 * (q[1] * q[2] + q[0] * q[3]);
    return up_x * delta.x + up_y * delta.y + up_z * delta.z;
}
} // namespace

TEST(AndroidCamera, LeftDragMovesSurfacePointLeftWithAnyFlip)
{
    for (uint8_t flip = 0; flip < 8; ++flip)
    {
        gs::android::engine::CameraController camera;
        ASSERT_TRUE(camera.fit(scene(), 800, 600));
        camera.set_flip_axes(flip);
        const gs::Double3 surface{0, 0, flip & 4 ? -1.0 : 1.0};
        const auto before = screen_x(camera.pose(), surface);
        ASSERT_TRUE(camera.orbit(-100, 0));
        const auto after = screen_x(camera.pose(), surface);
        // An odd reflection is compensated by the host's horizontal image mirror.
        EXPECT_LT(after - before, 0) << static_cast<int>(flip);
    }
}

TEST(AndroidCamera, VerticalDragChangesPitchWithoutYawInLandscape)
{
    gs::android::engine::CameraController camera;
    ASSERT_TRUE(camera.fit(scene(), 1920, 900));
    const auto before = camera.pose();
    const gs::Double3 point{0, 1, 0};
    ASSERT_TRUE(camera.orbit(0, 100));
    const auto after = camera.pose();
    EXPECT_NEAR(screen_x(after, point), screen_x(before, point), 1e-10);
    EXPECT_NE(screen_y(after, point), screen_y(before, point));
    EXPECT_NE(after.orientation[0], before.orientation[0]);
    EXPECT_NEAR(after.orientation[1], before.orientation[1], 1e-10);
}

TEST(AndroidCamera, FlyMotionIsBoundedAndRejectsNonFiniteInput)
{
    gs::android::engine::CameraController camera;
    ASSERT_TRUE(camera.fit(scene(), 800, 600));
    ASSERT_TRUE(camera.set_mode(gs::android::engine::ViewMode::Fly));
    const auto before = camera.pose().position;
    ASSERT_TRUE(camera.fly({0, 0, 1, false}, 1000));
    const auto after = camera.pose().position;
    EXPECT_LT(after.z, before.z);
    EXPECT_LT(before.z - after.z, 1);
    EXPECT_FALSE(camera.look(NAN, 0));
    EXPECT_FALSE(camera.fly({NAN, 0, 0, false}, 0.02));
    EXPECT_FALSE(camera.resize(0, 600));
}

TEST(AndroidCamera, FitResetAndModePreservePose)
{
    gs::android::engine::CameraController camera;
    ASSERT_TRUE(camera.fit(scene(), 400, 600));
    const auto baseline = camera.pose();
    ASSERT_TRUE(camera.orbit(40, 20));
    const auto moved = camera.pose();
    ASSERT_TRUE(camera.set_mode(gs::android::engine::ViewMode::Fly));
    EXPECT_DOUBLE_EQ(camera.pose().position.x, moved.position.x);
    ASSERT_TRUE(camera.resize(600, 400));
    EXPECT_DOUBLE_EQ(camera.pose().position.x, moved.position.x);
    camera.reset();
    EXPECT_DOUBLE_EQ(camera.pose().position.x, baseline.position.x);
    EXPECT_DOUBLE_EQ(camera.pose().orientation[3], baseline.orientation[3]);
}

TEST(AndroidEngine, ReplacementRequiresMatchingFirstPresent)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    auto first = engine.begin_open();
    ASSERT_TRUE(first);
    auto original = std::make_shared<gs::SplatScene>(scene());
    ASSERT_TRUE(engine.decoded(first, original, 800, 600));
    ASSERT_TRUE(engine.upload_started(first, 10));
    EXPECT_FALSE(engine.presented(first, 10, 0));
    ASSERT_TRUE(engine.presented(first, 10, 1));
    EXPECT_EQ(engine.active_scene(), original);

    auto replaced = engine.begin_open();
    auto newer = engine.begin_open();
    EXPECT_FALSE(engine.decoded(replaced, original, 800, 600));
    auto replacement = std::make_shared<gs::SplatScene>(scene());
    ASSERT_TRUE(engine.decoded(newer, replacement, 800, 600));
    ASSERT_TRUE(engine.upload_started(newer, 11));
    ASSERT_TRUE(engine.attach_surface(2));
    EXPECT_FALSE(engine.detach_surface(1));
    EXPECT_FALSE(engine.presented(newer, 11, 1));
    EXPECT_EQ(engine.active_scene(), original);
    ASSERT_TRUE(engine.presented(newer, 11, 2));
    EXPECT_EQ(engine.active_scene(), replacement);
    EXPECT_EQ(engine.snapshot().active_request_id, newer);
}

TEST(AndroidEngine, FailedReplacementRetainsOldSceneAndShutdownRejectsWork)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto first = engine.begin_open();
    auto original = std::make_shared<gs::SplatScene>(scene());
    ASSERT_TRUE(engine.decoded(first, original, 800, 600));
    ASSERT_TRUE(engine.upload_started(first, 1));
    ASSERT_TRUE(engine.presented(first, 1, 1));
    const auto second = engine.begin_open();
    ASSERT_TRUE(engine.cancel(second));
    EXPECT_EQ(engine.active_scene(), original);
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Ready);
    EXPECT_EQ(engine.snapshot().error, gs::android::engine::Error::Cancelled);
    EXPECT_FALSE(engine.fail(second, gs::android::engine::Error::Decode));
    ASSERT_TRUE(engine.camera_command([](auto &camera) { return camera.orbit(-50, 0); }));
    engine.shutdown();
    EXPECT_EQ(engine.begin_open(), 0);
    EXPECT_EQ(engine.active_scene(), nullptr);
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Stopped);
}

TEST(AndroidEngine, SurfaceRecoveryKeepsLoadingRequestAndDecodeError)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    auto first = engine.begin_open();
    auto original = std::make_shared<gs::SplatScene>(scene());
    ASSERT_TRUE(engine.decoded(first, original, 800, 600));
    ASSERT_TRUE(engine.upload_started(first, 1));
    ASSERT_TRUE(engine.presented(first, 1, 1));
    const auto next = engine.begin_open();
    ASSERT_TRUE(engine.render_failure(1, gs::android::engine::Error::Device));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Loading);
    ASSERT_TRUE(engine.attach_surface(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Loading);
    ASSERT_TRUE(engine.decoded(next, original, 800, 600));
    ASSERT_TRUE(engine.fail(next, gs::android::engine::Error::Decode));
    EXPECT_EQ(engine.snapshot().error, gs::android::engine::Error::Decode);
    ASSERT_TRUE(engine.attach_surface(3));
    EXPECT_EQ(engine.snapshot().error, gs::android::engine::Error::Decode);
    engine.clear_scene();
    EXPECT_DOUBLE_EQ(engine.snapshot().camera.position.z, 0);
}

TEST(AndroidEngine, SurfaceIsReadyOnlyAfterRestoredPresent)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto request = engine.begin_open();
    auto original = std::make_shared<gs::SplatScene>(scene());
    ASSERT_TRUE(engine.decoded(request, original, 800, 600));
    ASSERT_TRUE(engine.upload_started(request, 1));
    ASSERT_TRUE(engine.presented(request, 1, 1));
    ASSERT_TRUE(engine.detach_surface(1));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Recovering);
    ASSERT_TRUE(engine.attach_surface(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Recovering);
    EXPECT_FALSE(engine.surface_restored(1));
    EXPECT_TRUE(engine.surface_restored(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Ready);
}

TEST(AndroidEngine, DetachedReplacementFailureWaitsForRestoredPresent)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto first = engine.begin_open();
    auto original = std::make_shared<gs::SplatScene>(scene());
    ASSERT_TRUE(engine.decoded(first, original, 800, 600));
    ASSERT_TRUE(engine.upload_started(first, 1));
    ASSERT_TRUE(engine.presented(first, 1, 1));
    const auto replacement = engine.begin_open();
    ASSERT_TRUE(engine.detach_surface(1));
    ASSERT_TRUE(engine.fail(replacement, gs::android::engine::Error::Decode));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Recovering);
    ASSERT_TRUE(engine.attach_surface(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Recovering);
    ASSERT_TRUE(engine.surface_restored(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Ready);
}

TEST(AndroidEngine, ReplacingSurfaceRequiresNewPresent)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto request = engine.begin_open();
    ASSERT_TRUE(engine.decoded(request, std::make_shared<gs::SplatScene>(scene()), 800, 600));
    ASSERT_TRUE(engine.upload_started(request, 1));
    ASSERT_TRUE(engine.presented(request, 1, 1));
    ASSERT_TRUE(engine.attach_surface(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Recovering);
    ASSERT_TRUE(engine.surface_restored(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Ready);
}

TEST(AndroidEngine, CameraCallbackCanInspectSnapshot)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto request = engine.begin_open();
    ASSERT_TRUE(engine.decoded(request, std::make_shared<gs::SplatScene>(scene()), 800, 600));
    ASSERT_TRUE(engine.upload_started(request, 1));
    ASSERT_TRUE(engine.presented(request, 1, 1));
    EXPECT_TRUE(engine.camera_command([&](auto &camera) {
        EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Ready);
        return camera.orbit(-20, 0);
    }));
}

TEST(AndroidEngine, CameraRevisionTracksChangesForOnDemandRendering)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto request = engine.begin_open();
    ASSERT_TRUE(engine.decoded(request, std::make_shared<gs::SplatScene>(scene()), 800, 600));
    ASSERT_TRUE(engine.upload_started(request, 1));
    ASSERT_TRUE(engine.presented(request, 1, 1));
    const auto initial = engine.snapshot().camera_revision;
    EXPECT_EQ(initial, engine.snapshot().camera_revision);
    ASSERT_TRUE(engine.camera_command([](auto &camera) { return camera.orbit(20, 0); }));
    EXPECT_GT(engine.snapshot().camera_revision, initial);
    const auto moved = engine.snapshot().camera_revision;
    ASSERT_TRUE(engine.resize(1, 1, 900, 600));
    EXPECT_GT(engine.snapshot().camera_revision, moved);
}

TEST(AndroidEngine, RestoredOldSceneDuringReplacementIsReadyAfterFailure)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto first = engine.begin_open();
    ASSERT_TRUE(engine.decoded(first, std::make_shared<gs::SplatScene>(scene()), 800, 600));
    ASSERT_TRUE(engine.upload_started(first, 1));
    ASSERT_TRUE(engine.presented(first, 1, 1));
    const auto replacement = engine.begin_open();
    ASSERT_TRUE(engine.attach_surface(2));
    ASSERT_TRUE(engine.surface_restored(2));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Loading);
    ASSERT_TRUE(engine.fail(replacement, gs::android::engine::Error::Decode));
    EXPECT_EQ(engine.snapshot().phase, gs::android::engine::Phase::Ready);
}

TEST(AndroidEngine, ConcurrentCameraCommandsBothApply)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto request = engine.begin_open();
    ASSERT_TRUE(engine.decoded(request, std::make_shared<gs::SplatScene>(scene()), 800, 600));
    ASSERT_TRUE(engine.upload_started(request, 1));
    ASSERT_TRUE(engine.presented(request, 1, 1));
    std::promise<void> entered, release;
    auto released = release.get_future();
    bool first = false, second = false;
    std::thread a([&] {
        first = engine.camera_command([&](auto &camera) {
            entered.set_value();
            released.wait();
            return camera.orbit(-20, 0);
        });
    });
    entered.get_future().wait();
    std::thread b([&] {
        second = engine.camera_command([](auto &camera) { return camera.orbit(-20, 0); });
    });
    release.set_value();
    a.join();
    b.join();
    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
}

TEST(AndroidEngine, CameraCallbackIsNotRepeatedAfterResize)
{
    gs::android::engine::Transactions engine;
    ASSERT_TRUE(engine.attach_surface(1));
    const auto request = engine.begin_open();
    ASSERT_TRUE(engine.decoded(request, std::make_shared<gs::SplatScene>(scene()), 800, 600));
    ASSERT_TRUE(engine.upload_started(request, 1));
    ASSERT_TRUE(engine.presented(request, 1, 1));
    int calls = 0;
    EXPECT_FALSE(engine.camera_command([&](auto &camera) {
        ++calls;
        EXPECT_TRUE(engine.resize(1, 1, 640, 480));
        return camera.orbit(-20, 0);
    }));
    EXPECT_EQ(calls, 1);
}
