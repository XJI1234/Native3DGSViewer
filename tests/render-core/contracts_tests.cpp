#include "contracts.h"
#include "test_support.h"
#include <gtest/gtest.h>
#include <limits>

using namespace gs::render;
using namespace gs::render::detail;

TEST(RenderContracts, QualityRejectsNonFiniteAndUnsupportedValues)
{
    QualityConfig q;
    EXPECT_FALSE(validate_quality(q));
    q.max_stddev = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(validate_quality(q));
    q = {};
    q.sh_degree_cap = 4;
    EXPECT_TRUE(validate_quality(q));
    q = {};
    q.min_alpha = 1;
    EXPECT_TRUE(validate_quality(q));
    q = {};
    q.premultiplied_alpha = false;
    EXPECT_TRUE(validate_quality(q));
    q = {};
    q.point_stride = 3;
    EXPECT_TRUE(validate_quality(q));
    q = {};
    q.max_point_stride = 32;
    EXPECT_TRUE(validate_quality(q));
    q = {};
    q.point_stride = 4;
    q.max_point_stride = 2;
    EXPECT_TRUE(validate_quality(q));
    q.max_point_stride = 8;
    EXPECT_FALSE(validate_quality(q));
}
TEST(RenderContracts, CameraRejectsInvalidProjectionAndQuaternion)
{
    CameraState c;
    EXPECT_FALSE(validate_camera(c));
    c.orientation_xyzw.w = 0;
    EXPECT_TRUE(validate_camera(c));
    c = {};
    c.near_plane = c.far_plane;
    EXPECT_TRUE(validate_camera(c));
    c = {};
    c.position_rub.x = INFINITY;
    EXPECT_TRUE(validate_camera(c));
}
TEST(RenderContracts, BudgetUsesIncrementalHeadroomWithoutCountingOldSceneTwice)
{
    EXPECT_TRUE(fits_budget(80, 200, 100));
    EXPECT_FALSE(fits_budget(81, 200, 100));
    EXPECT_FALSE(fits_budget(1, 100, 101));
    EXPECT_TRUE(fits_budget(0, UINT64_MAX, 0));
}
TEST(RenderContracts, UmaUploadDoesNotRequireNonlocalSegment)
{
    EXPECT_TRUE(fits_scene_budgets(792, 8, 1100, 100, 0, 0, true));
    EXPECT_FALSE(fits_scene_budgets(793, 8, 1100, 100, 0, 0, true));
    EXPECT_FALSE(fits_scene_budgets(800, 8, 1100, 100, 0, 0, false));
    EXPECT_TRUE(fits_scene_budgets(800, 8, 1100, 100, 110, 100, false));
}
TEST(RenderContracts, SmallUploadReserveUsesPackedSceneSize)
{
    auto scene = render_test::make_scene(1000, 3);
    const auto packed = 1000ull * (56 + 180);
    EXPECT_EQ(upload_reserve_bytes(*scene, 3, 1), packed + (4ull << 20));
    EXPECT_EQ(upload_reserve_bytes(*scene, 0, 4), 250ull * 56 + (4ull << 20));
    EXPECT_EQ(upload_reserve_bytes(*scene, 3, 1'000), UINT64_MAX);
    gs::SplatScene large;
    large.count = 2'000'000;
    EXPECT_EQ(upload_reserve_bytes(large, 0, 1), (64ull << 20) + (4ull << 20));
}
TEST(RenderContracts, RelativeCameraSubtractsDoubleOriginBeforeFloatConversion)
{
    gs::SplatScene scene;
    scene.worldOrigin = {1e12, -1e12, 1e12};
    CameraState c;
    c.position_rub = {1e12 + 3, -1e12 + 4, 1e12 + 5};
    EXPECT_EQ(relative_camera(scene, c), (std::array<float, 3>{3, 4, 5}));
    EXPECT_EQ(world_to_camera({}), (std::array<float, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1}));
}
TEST(RenderContracts, SceneRejectsCountOverflowMissingStorageAndShLength)
{
    auto scene = std::make_shared<gs::SplatScene>(*render_test::make_scene());
    EXPECT_FALSE(validate_scene(scene));
    scene->count = UINT64_MAX;
    EXPECT_TRUE(validate_scene(scene));
    EXPECT_EQ(scene_bytes(*scene), UINT64_MAX);
    EXPECT_EQ(incremental_bytes(*scene), UINT64_MAX);
    scene->count = 1;
    scene->shDegree = 3;
    EXPECT_TRUE(validate_scene(scene));
    scene->shDegree = 0;
    scene->storage.reset();
    EXPECT_TRUE(validate_scene(scene));
}
TEST(RenderContracts, LargeSceneUsesMeasuredResourceSize)
{
    gs::SplatScene scene;
    scene.count = 22'480'361;
    scene.shDegree = 3;
    EXPECT_EQ(scene_bytes(scene), 5'305'365'196ull);
    EXPECT_NE(incremental_bytes(scene), UINT64_MAX);
    EXPECT_LT(incremental_bytes(scene, 0), incremental_bytes(scene, 1));
    EXPECT_LT(incremental_bytes(scene, 1), incremental_bytes(scene, 2));
    EXPECT_LT(incremental_bytes(scene, 2), incremental_bytes(scene, 3));
    EXPECT_EQ(incremental_bytes(scene, 0), 2'520'610'480ull);
    scene.count = UINT32_MAX / 180 + 1;
    EXPECT_EQ(scene_bytes(scene), UINT64_MAX);
}
TEST(RenderContracts, RotatedCameraUsesInverseQuaternionRows)
{
    const double half = std::sqrt(0.5);
    const auto rows = world_to_camera({0, half, 0, half});
    const std::array<float, 9> expected{0, 0, -1, 0, 1, 0, 1, 0, 0};
    for (size_t i = 0; i < rows.size(); ++i)
        EXPECT_NEAR(rows[i], expected[i], 1e-6);
}
