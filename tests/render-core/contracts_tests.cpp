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
    EXPECT_TRUE(fits_scene_budgets(800, 8, 1100, 100, 0, 0, true));
    EXPECT_FALSE(fits_scene_budgets(801, 8, 1100, 100, 0, 0, true));
    EXPECT_FALSE(fits_scene_budgets(800, 8, 1100, 100, 0, 0, false));
    EXPECT_TRUE(fits_scene_budgets(800, 8, 1100, 100, 110, 100, false));
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
TEST(RenderContracts, RotatedCameraUsesInverseQuaternionRows)
{
    const double half = std::sqrt(0.5);
    const auto rows = world_to_camera({0, half, 0, half});
    const std::array<float, 9> expected{0, 0, -1, 0, 1, 0, 1, 0, 0};
    for (size_t i = 0; i < rows.size(); ++i)
        EXPECT_NEAR(rows[i], expected[i], 1e-6);
}
