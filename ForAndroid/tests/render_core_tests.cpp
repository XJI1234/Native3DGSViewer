#include "render-core/device_probe.h"
#include "render-core/gpu_timing.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <gtest/gtest.h>

int run_render_cases()
{
    int failures = 0;
    auto expect = [&](bool condition, const char *name) {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL %s\n", name);
            ++failures;
        }
    };
    const auto device = gs::android::render::probe_device();
    std::printf("Vulkan device=%s api=%u supported=%d result=%d range=%llu budget=%llu\n",
                device.device_name.c_str(), device.api_version, device.supported, device.result,
                static_cast<unsigned long long>(device.max_storage_buffer_range),
                static_cast<unsigned long long>(device.heap_budget_bytes));
    expect(!device.supported || device.max_storage_buffer_range > 0,
           "supported device has storage buffer range");
    expect(!device.supported || device.heap_budget_bytes > 0,
           "supported device has memory budget");
    expect(!gs::android::render::assess_scene({}, device).accepted,
           "null scene rejected");

    std::array<float, 3> center{0, 0, 0}, scale{1, 1, 1}, color{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1};
    std::array<float, 1> opacity{1};
    auto scene = std::make_shared<gs::SplatScene>();
    scene->storage = std::make_shared<int>(1);
    scene->count = 1;
    scene->maxScale = 1;
    scene->centerLocal = center;
    scene->scale = scale;
    scene->rotation = rotation;
    scene->opacity = opacity;
    scene->rgb0 = color;
    auto ample = device;
    ample.supported = true;
    ample.max_storage_buffer_range = 1ull << 30;
    ample.heap_budget_bytes = 1ull << 30;
    ample.heap_usage_bytes = 0;
    expect(gs::android::render::assess_scene(scene, ample).accepted,
           "valid scene admitted");
    scene->shDegree = 3;
    expect(!gs::android::render::assess_scene(scene, ample).accepted,
           "missing SH data rejected");
    scene->shDegree = 0;
    auto constrained = ample;
    constrained.heap_budget_bytes = 4096;
    expect(!gs::android::render::assess_scene(scene, constrained).accepted,
           "device budget rejects full scene");
    std::printf("render-core independent: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}

TEST(AndroidRenderCore, DeviceAndAdmissionContracts)
{
    EXPECT_EQ(run_render_cases(), 0);
}

TEST(AndroidRenderCore, GpuStableRadixSelfTest)
{
    const auto device = gs::android::render::probe_device();
    if (!device.supported)
        GTEST_SKIP() << device.diagnostic;
    const char *directory = std::getenv("GS_SHADER_DIRECTORY");
    const auto diagnostic = gs::android::render::sort_self_test(
        directory ? directory : "/data/local/tmp/shaders");
    EXPECT_TRUE(diagnostic.empty()) << diagnostic;
    EXPECT_TRUE(gs::android::render::sort_self_test({}).empty());
}

TEST(AndroidRenderCore, GpuStableRadixMillionKeys)
{
    const auto device = gs::android::render::probe_device();
    if (!device.supported)
        GTEST_SKIP() << device.diagnostic;
    constexpr size_t count = 1'179'648;
    std::vector<float> centers(count * 3);
    for (size_t i = 0; i < count; ++i)
        centers[i * 3] = static_cast<float>((i * 7919) % 1009);
    auto scene = std::make_shared<gs::SplatScene>();
    scene->storage = std::make_shared<int>(1);
    scene->count = count;
    scene->centerLocal = centers;
    const char *directory = std::getenv("GS_SHADER_DIRECTORY");
    EXPECT_TRUE(gs::android::render::sort_scene_self_test(
        scene, directory ? directory : "/data/local/tmp/shaders").empty());
    if (device.device_name.find("735") != std::string::npos)
        EXPECT_TRUE(gs::android::render::sort_scene_subgroup_self_test(
            scene, directory ? directory : "/data/local/tmp/shaders").empty());
}

TEST(AndroidRenderCore, GpuSubgroupStableRadixSelfTest)
{
    const auto device = gs::android::render::probe_device();
    if (device.device_name.find("735") == std::string::npos)
        GTEST_SKIP() << "Subgroup candidate is measured on Adreno 735";
    const char *directory = std::getenv("GS_SHADER_DIRECTORY");
    EXPECT_TRUE(gs::android::render::sort_subgroup_self_test(
        directory ? directory : "/data/local/tmp/shaders").empty());
    EXPECT_TRUE(gs::android::render::sort_subgroup_self_test({}).empty());
}

TEST(AndroidRenderCore, TimestampDeltaHandlesCounterWrap)
{
    using gs::android::render::timestamp_delta;
    EXPECT_EQ(timestamp_delta(250, 5, 8), 11u);
    EXPECT_EQ(timestamp_delta(UINT64_MAX - 4, 5, 64), 10u);
    EXPECT_EQ(timestamp_delta(100, 150, 32), 50u);
    EXPECT_EQ(timestamp_delta(100, 150, 0), 0u);
}
