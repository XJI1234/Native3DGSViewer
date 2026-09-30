#include "test_support.h"
#include "upload.h"
#include <cmath>
#include <thread>
using namespace render_test;
TEST(RenderImage, SampledUploadPacksSelectedSplatsAndSh)
{
    auto scene = make_scene(4, 3, [](SceneData &s) {
        for (int i = 0; i < 4; ++i)
            s.centers[i * 3] = float(i);
        s.sh[3] = 0.2f;
        s.sh[2 * 45 + 3] = 0.7f;
    });
    GpuDevice gpu(true);
    SplatPass pass(gpu, {});
    UploadTransaction upload(gpu, pass, scene, 1, {}, 1, true, 2);
    while (!upload.ready)
    {
        upload.advance(gpu);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(upload.scene->count, 2u);
    auto attributes = gpu.readback(upload.scene->attributes.Get(), 2 * 56,
                                   D3D12_RESOURCE_STATE_COMMON);
    auto sh = gpu.readback(upload.scene->sh_attributes.Get(), 2 * 9 * 4,
                           D3D12_RESOURCE_STATE_COMMON);
    auto value_at = [](const std::vector<uint8_t> &bytes, size_t float_index) {
        float value;
        memcpy(&value, bytes.data() + float_index * 4, sizeof(value));
        return value;
    };
    EXPECT_FLOAT_EQ(value_at(attributes, 0), 0);
    EXPECT_FLOAT_EQ(value_at(attributes, 3), 2);
    EXPECT_FLOAT_EQ(value_at(sh, 3), 0.2f);
    EXPECT_FLOAT_EQ(value_at(sh, 12), 0.7f);
    EXPECT_EQ(scene->count, 4u);
    for (const auto &error : gpu.debug_errors())
        ADD_FAILURE() << error;
}
TEST(RenderImage, GaussianPixelsMatchAnalyticReference)
{
    auto image = render_image(make_scene());
    ASSERT_EQ(image.candidates, 1u);
    const double focal = 64 / std::tan(CameraState{}.vertical_fov_radians / 2);
    const double sigma = focal * 0.2 / 2;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
        {
            double radius2 = ((x + 0.5 - 64) * (x + 0.5 - 64) + (y + 0.5 - 64) * (y + 0.5 - 64)) /
                             (sigma * sigma);
            double expected = radius2 <= 9 ? 0.5 * std::exp(-0.5 * radius2) * 255 : 0;
            auto offset = (y * 128 + x) * 4;
            ASSERT_NEAR(image.bgra[offset + 2], expected, 1.5) << x << "," << y;
            ASSERT_EQ(image.bgra[offset], 0);
            ASSERT_EQ(image.bgra[offset + 1], 0);
        }
    save_bmp(image, "out/render-tests/gaussian.bmp");
}
TEST(RenderImage, FarToNearPremultipliedOverlap)
{
    auto image = render_image(make_scene(2, 0, [](SceneData &s) {
        s.centers[5] = -3;
        s.rgb[3] = 0;
        s.rgb[5] = 1;
    }));
    ASSERT_EQ(image.candidates, 2u);
    const auto pixel = (64 * 128 + 64) * 4;
    EXPECT_NEAR(image.bgra[pixel + 2], 127, 2);
    EXPECT_NEAR(image.bgra[pixel], 64, 2);
    EXPECT_NEAR(image.bgra[pixel + 3], 191, 2);
    save_bmp(image, "out/render-tests/overlap.bmp");
}
TEST(RenderImage, ShZeroThroughThreeAndFarOriginPreserveTrainingColor)
{
    for (uint8_t degree = 0; degree <= 3; ++degree)
    {
        auto scene = make_scene(1, degree, [&](SceneData &s) {
            s.rgb = {0.25f, 0.25f, 0.25f};
            if (degree)
                s.sh[3] = 0.2f;
            if (degree >= 2)
                s.sh[15] = 0.1f;
            if (degree >= 3)
                s.sh[33] = 0.1f;
        });
        auto image = render_image(scene);
        double expected = 0.25;
        if (degree)
            expected -= 0.2 * 0.4886025119029199;
        if (degree >= 2)
            expected += 0.1 * 2 * 0.31539156525252005;
        if (degree >= 3)
            expected -= 0.1 * 2 * 0.3731763325901154;
        EXPECT_NEAR(image.projected[6], expected, 1e-5) << int(degree);
        auto relocated = std::make_shared<gs::SplatScene>(*scene);
        relocated->worldOrigin = {1e12, 1e12, 1e12};
        CameraState c;
        c.position_rub = relocated->worldOrigin;
        EXPECT_EQ(render_image(relocated, c).bgra, image.bgra);
    }
}
TEST(RenderImage, ConservativeEdgesNearPlaneAndOverflowHaveFiniteResults)
{
    auto edge = render_image(make_scene(1, 0, [](SceneData &s) { s.centers[0] = 1.3f; }));
    EXPECT_EQ(edge.candidates, 1u);
    auto near_image = render_image(make_scene(1, 0, [](SceneData &s) { s.centers[2] = -0.005f; }));
    EXPECT_EQ(near_image.candidates, 1u);
    auto behind = render_image(make_scene(1, 0, [](SceneData &s) { s.centers[2] = 1; }));
    EXPECT_EQ(behind.candidates, 0u);
    auto overflow = render_image(make_scene(1, 0, [](SceneData &s) {
        s.centers[2] = -1e30f;
        s.scales.assign(3, 1e18f);
    }));
    EXPECT_EQ(overflow.candidates, 0u);
    auto singular = render_image(make_scene(1, 0, [](SceneData &s) { s.scales.assign(3, 0); }));
    EXPECT_EQ(singular.candidates, 0u);
    EXPECT_EQ(singular.rejected, 1u);
    EXPECT_EQ(edge.rejected, 0u);
}
TEST(RenderImage, RotatedAnisotropicCovarianceAndBlurMatchPrincipalAxes)
{
    auto scene = make_scene(1, 0, [](SceneData &s) {
        s.scales = {0.4f, 0.1f, 0.2f};
        s.rotations = {0, 0, float(std::sin(3.141592653589793 / 8)),
                       float(std::cos(3.141592653589793 / 8))};
    });
    QualityConfig q;
    q.covariance_blur_px2 = 4;
    auto image = render_image(scene, {}, q);
    const double f = 64 / std::tan(CameraState{}.vertical_fov_radians / 2);
    const double major = 3 * std::sqrt((f * 0.4 / 2) * (f * 0.4 / 2) + 4) / std::sqrt(2.0);
    const double minor = 3 * std::sqrt((f * 0.1 / 2) * (f * 0.1 / 2) + 4) / std::sqrt(2.0);
    EXPECT_NEAR(std::abs(image.projected[2]), major, 1e-4);
    EXPECT_NEAR(std::abs(image.projected[3]), major, 1e-4);
    EXPECT_NEAR(std::abs(image.projected[4]), minor, 1e-4);
    EXPECT_NEAR(std::abs(image.projected[5]), minor, 1e-4);
    EXPECT_LT(image.projected[2] * image.projected[3], 0);
}
TEST(RenderImage, DepthAndRadialModesProduceTheirDeclaredOverlapOrder)
{
    auto scene = make_scene(2, 0, [](SceneData &s) {
        s.centers = {1.6f, 0, -2, 0, 0, -2.4f};
        s.scales.assign(6, 1);
        s.rgb = {1, 0, 0, 0, 0, 1};
    });
    QualityConfig radial, depth;
    depth.sort_mode = SortMode::ViewDepth;
    auto r = render_image(scene, {}, radial), d = render_image(scene, {}, depth);
    EXPECT_GT(d.bgra[(64 * 128 + 64) * 4 + 2], r.bgra[(64 * 128 + 64) * 4 + 2]);
}
TEST(RenderImage, ColorClampAndShCapAreAppliedAfterEvaluation)
{
    auto scene = make_scene(1, 3, [](SceneData &s) {
        s.rgb = {2, -1, 0.4f};
        s.sh[3] = -4;
    });
    QualityConfig q;
    q.sh_degree_cap = 0;
    auto image = render_image(scene, {}, q);
    EXPECT_FLOAT_EQ(image.projected[6], 1);
    EXPECT_FLOAT_EQ(image.projected[7], 0);
    EXPECT_FLOAT_EQ(image.projected[8], 0.4f);
}

TEST(RenderImage, ShCapUsesEachSplatsOwnCoefficients)
{
    auto scene = make_scene(2, 3, [](SceneData &s) {
        s.centers[3] = 0.8f;
        s.rgb.assign(6, 0.3f);
        s.sh[3] = 0.2f;
        s.sh[45 + 3] = 0.6f;
        s.sh[45 + 15] = 1.0f;
    });
    QualityConfig quality;
    quality.sh_degree_cap = 1;
    const auto image = render_image(scene, {}, quality);
    ASSERT_EQ(image.candidates, 2u);
    EXPECT_NEAR(image.projected[6], 0.3 - 0.2 * 0.4886025119029199, 1e-5);
    EXPECT_NEAR(image.projected[16],
                0.3 - 0.6 * 0.4886025119029199 * 2 / std::sqrt(4.0 + 0.8 * 0.8),
                1e-5);
}

TEST(RenderImage, ThinAnisotropicSplatRetainsPositiveMinorAxis)
{
    auto image = render_image(make_scene(1, 0, [](SceneData &s) {
        s.scales = {0.4f, 0.00004f, 0.2f};
        s.rotations = {0, 0, float(std::sin(0.37)), float(std::cos(0.37))};
    }));
    ASSERT_EQ(image.candidates, 1u);
    const double f = 64 / std::tan(CameraState{}.vertical_fov_radians / 2);
    EXPECT_NEAR(std::hypot(image.projected[4], image.projected[5]), 3 * f * 0.00004 / 2, 1e-6);
    EXPECT_EQ(image.rejected, 0u);
}
