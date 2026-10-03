#include "gs_server/renderer.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

using namespace gs::server;
TEST(CudaRenderer, StableGpuSorting)
{
    EXPECT_NO_THROW(CudaRenderer::self_test(0));
}
TEST(CudaRenderer, ProjectionAndTileRasterMatchCpu)
{
    auto scene = std::make_shared<gs::SplatScene>();
    std::vector<float> centers{0, 0, 0, 0.1f, 0.1f, -0.2f, -0.1f, 0, 0.1f};
    std::vector<float> scales(9, 0.25f), rotations{0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<float> opacity{0.7f, 0.8f, 0.6f}, rgb{1, 0, 0, 0, 1, 0, 0, 0, 1};
    scene->count = 3;
    scene->bounds = {{-1, -1, -1}, {1, 1, 1}};
    scene->centerLocal = centers;
    scene->scale = scales;
    scene->rotation = rotations;
    scene->opacity = opacity;
    scene->rgb0 = rgb;
    CudaRenderer renderer(scene, 0);
    const auto projected = renderer.render({67, 43, Profile::Rgba, 0, 14.0362434679, 1, true});
    ASSERT_EQ(projected.debug_projection.size(), 3u);
    const auto image = renderer.render({67, 43, Profile::Rgba});
    const auto reference = raster_reference(projected.debug_projection, 67, 43);
    ASSERT_EQ(image.frame.rgba.size(), reference.size());
    int maximum = 0;
    for (size_t index = 0; index < reference.size(); ++index)
        maximum = std::max(maximum, std::abs(int(reference[index]) - int(image.frame.rgba[index])));
    EXPECT_LE(maximum, 1);
    EXPECT_THROW(renderer.render({0, 43, Profile::Rgba, 0, 14.0362434679, 1, true}),
                 std::exception);
}
TEST(CudaRenderer, IsotropicProjectionAndShBandsAnalytic)
{
    auto scene = std::make_shared<gs::SplatScene>();
    std::vector<float> center{0, 0, 0}, scale{0.25f, 0.25f, 0.25f}, rotation{0, 0, 0, 1};
    std::vector<float> opacity{0.8f}, rgb{0.5f, 0.5f, 0.5f};
    scene->count = 1;
    scene->bounds = {{-1, -1, -1}, {1, 1, 1}};
    scene->centerLocal = center;
    scene->scale = scale;
    scene->rotation = rotation;
    scene->opacity = opacity;
    scene->rgb0 = rgb;
    const double distance = std::sqrt(3.0) / 0.5 * 1.1;
    const double focal = 64 / (2 * std::tan(std::acos(-1.0) / 6));
    const double radius = 3 * std::sqrt(std::pow(focal * 0.25 / distance, 2) + 0.01);
    const double direction_y = -1 / std::sqrt(17.0), direction_z = -4 / std::sqrt(17.0);
    for (uint8_t degree = 1; degree <= 3; ++degree)
    {
        const size_t terms = (degree + 1) * (degree + 1) - 1;
        std::vector<float> sh(terms * 3, 0);
        const size_t coefficient = degree == 1 ? 0 : degree == 2 ? 5 : 11;
        sh[coefficient * 3] = 0.2f;
        scene->shDegree = degree;
        scene->shRest = sh;
        CudaRenderer renderer(scene, 0);
        const auto output = renderer.render({64, 64, Profile::Rgba, 0, 14.0362434679, 1, true});
        ASSERT_EQ(output.debug_projection.size(), 1u);
        const auto &ellipse = output.debug_projection[0];
        EXPECT_NEAR(ellipse.center_x, 32, 0.0001);
        EXPECT_NEAR(ellipse.center_y, 32, 0.0001);
        EXPECT_NEAR(std::hypot(ellipse.axis0_x, ellipse.axis0_y), radius, 0.0001);
        EXPECT_NEAR(std::hypot(ellipse.axis1_x, ellipse.axis1_y), radius, 0.0001);
        const double basis =
            degree == 1   ? -std::sqrt(3 / (4 * std::acos(-1.0))) * direction_y
            : degree == 2 ? std::sqrt(5 / (16 * std::acos(-1.0))) *
                                (2 * direction_z * direction_z - direction_y * direction_y)
                          : std::sqrt(7 / (16 * std::acos(-1.0))) * direction_z *
                                (2 * direction_z * direction_z - 3 * direction_y * direction_y);
        EXPECT_NEAR(ellipse.red, 0.5 + 0.2 * basis, 0.00001);
        EXPECT_EQ(ellipse.green, 0.5f);
    }
}
TEST(CudaRenderer, FullyCulledSceneProducesBlackAndRecovers)
{
    auto scene = std::make_shared<gs::SplatScene>();
    std::vector<float> center{0, 0, 0}, scale{0.25f, 0.25f, 0.25f}, rotation{0, 0, 0, 1};
    std::vector<float> opacity{0}, rgb{1, 0, 0};
    scene->count = 1;
    scene->bounds = {{-1, -1, -1}, {1, 1, 1}};
    scene->centerLocal = center;
    scene->scale = scale;
    scene->rotation = rotation;
    scene->opacity = opacity;
    scene->rgb0 = rgb;
    CudaRenderer renderer(scene, 0);
    EXPECT_TRUE(renderer.render({17, 13, Profile::Rgba, 0, 14.0362434679, 1, true})
                    .debug_projection.empty());
    EXPECT_EQ(renderer.render({17, 13, Profile::Rgba, 0, 90}).frame.rgba,
              raster_reference({}, 17, 13));
    EXPECT_EQ(renderer.render({17, 13, Profile::Rgba}).frame.rgba, raster_reference({}, 17, 13));
}

TEST(CudaRenderer, OrbitZoomIdentityAndInvalidParameters)
{
    auto scene = std::make_shared<gs::SplatScene>();
    std::vector<float> centers{0, 0, 0}, scales(3, 0.25f), rotations{0, 0, 0, 1};
    std::vector<float> opacity{0.8f}, rgb{1, 0, 0};
    scene->count = 1;
    scene->bounds = {{-1, -1, -1}, {1, 1, 1}};
    scene->maxScale = 0.25;
    scene->centerLocal = centers;
    scene->scale = scales;
    scene->rotation = rotations;
    scene->opacity = opacity;
    scene->rgb0 = rgb;
    CudaRenderer renderer(scene, 0);
    RenderRequest request{67, 43, Profile::Rgba, 123, -25, 0.5, true};
    auto first = renderer.render(request);
    ASSERT_EQ(first.debug_projection.size(), 1u);
    EXPECT_NEAR(first.debug_projection[0].center_x, 33.5, 0.0001);
    EXPECT_NEAR(first.debug_projection[0].center_y, 21.5, 0.0001);
    EXPECT_EQ(first.frame.rgba, renderer.render(request).frame.rgba);
    const auto radius =
        std::hypot(first.debug_projection[0].axis0_x, first.debug_projection[0].axis0_y);
    request.zoom = 1;
    auto further = renderer.render(request);
    ASSERT_EQ(further.debug_projection.size(), 1u);
    EXPECT_GT(radius,
              std::hypot(further.debug_projection[0].axis0_x, further.debug_projection[0].axis0_y));
    for (double invalid : {0.0, 11.0, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()})
    {
        request.zoom = invalid;
        EXPECT_THROW(renderer.render(request), std::exception);
    }
    request.zoom = 1;
    request.yaw = NAN;
    EXPECT_THROW(renderer.render(request), std::exception);
}
TEST(CudaRenderer, PoleAndFlipUseStableWorldBasis)
{
    auto scene = std::make_shared<gs::SplatScene>();
    std::vector<float> centers{0.3f, 0.6f, 0.1f}, scales(3, 0.25f), rotations{0, 0, 0, 1};
    std::vector<float> opacity{0.8f}, rgb{0.2f, 0.8f, 0.1f};
    scene->count = 1;
    scene->bounds = {{-1, -1, -1}, {1, 1, 1}};
    scene->maxScale = 0.25;
    scene->centerLocal = centers;
    scene->scale = scales;
    scene->rotation = rotations;
    scene->opacity = opacity;
    scene->rgb0 = rgb;
    CudaRenderer renderer(scene, 0);
    RenderRequest request{64, 48, Profile::Rgba, 22, 14, 1, true};
    const auto original = renderer.render(request);
    request.flip_y = true;
    const auto flipped = renderer.render(request);
    for (size_t pixel = 0; pixel < 64 * 48; ++pixel)
        for (size_t channel = 0; channel < 4; ++channel)
            EXPECT_NEAR(original.frame.rgba[pixel * 4 + channel],
                        flipped.frame.rgba[(64 * 48 - 1 - pixel) * 4 + channel], 1);
    for (double pitch : {-90.0, 90.0, 180.0, 270.0})
    {
        request.pitch = pitch;
        const auto pole = renderer.render(request);
        ASSERT_EQ(pole.debug_projection.size(), 1);
        EXPECT_TRUE(std::isfinite(pole.debug_projection.front().center_x));
        EXPECT_TRUE(std::isfinite(pole.debug_projection.front().center_y));
    }
}
