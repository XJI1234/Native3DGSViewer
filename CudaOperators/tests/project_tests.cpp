#include "gs_cuda/project.h"
#include "gpu.h"
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace gs::cuda_ops;
using namespace gs::render::detail;

namespace
{
struct Fixture
{
    static constexpr uint32_t count = 513;
    GpuDevice gpu{true};
    Context context{gpu.device.Get(), gpu.adapter_description.AdapterLuid};
    FrameConstants constants;
    std::vector<float> attributes = std::vector<float>(count * 14);
    std::vector<float> sh = std::vector<float>(count * 45);
    SharedBuffer input{context, attributes.size() * 4}, harmonics{context, sh.size() * 4};
    SharedBuffer output{context, count * sizeof(ProjectedEllipse)}, keys{context, count * 4};
    SharedBuffer indexes{context, count * 4}, arguments{context, sizeof(DrawCounters)};

    Fixture()
    {
        constants.view_rows[0] = constants.view_rows[5] = constants.view_rows[10] = 1;
        constants.viewport[0] = constants.viewport[1] = 100;
        constants.viewport[2] = constants.viewport[3] = 64;
        constants.quality[0] = 3;
        constants.quality[3] = 1024;
        constants.clip[0] = .1f;
        constants.clip[1] = 100;
        constants.meta[0] = count;
        constants.meta[2] = 45;
        const uint32_t offsets[]{0, count * 12, count * 24, count * 40, count * 44};
        std::copy(std::begin(offsets), std::end(offsets), constants.offsets);
        for (uint32_t i = 0; i < count; ++i)
        {
            set(0, i, 2, -10);
            for (int j = 0; j < 3; ++j) { set(1, i, j, 1); set(4, i, j, .5f); }
            set(2, i, 3, 1);
            set(3, i, 0, .8f);
        }
    }
    void set(int field, uint32_t i, int component, float value)
    {
        const uint32_t stride[]{3, 3, 4, 1, 3};
        attributes[constants.offsets[field] / 4 + i * stride[field] + component] = value;
    }
    void run(uint32_t block_size, ProjectMath math = ProjectMath::Reference)
    {
        auto upload = gpu.buffer(input.bytes() + harmonics.bytes(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        check(upload->Map(0, &empty, &mapped), "Project test upload");
        std::memcpy(mapped, attributes.data(), size_t(input.bytes()));
        std::memcpy(static_cast<uint8_t *>(mapped) + input.bytes(), sh.data(), size_t(harmonics.bytes()));
        upload->Unmap(0, nullptr);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Project allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Project commands");
        uint64_t offset = 0;
        for (auto *buffer : {&input, &harmonics})
        {
            transition(list.Get(), buffer->resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyBufferRegion(buffer->resource(), 0, upload.Get(), offset, buffer->bytes());
            transition(list.Get(), buffer->resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
            offset += buffer->bytes();
        }
        check(list->Close(), "Project close");
        gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value);
        context.begin(gpu.direct.Get());
        gs::cuda_ops::project(context, constants, input, harmonics, output, keys, indexes, arguments, block_size, math);
        context.end(gpu.direct.Get());
        check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Project completion");
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    }
    template<class T> std::vector<T> read(SharedBuffer &buffer)
    {
        auto bytes = gpu.readback(buffer.resource(), buffer.bytes(), D3D12_RESOURCE_STATE_COMMON);
        std::vector<T> data(bytes.size() / sizeof(T));
        std::memcpy(data.data(), bytes.data(), bytes.size());
        return data;
    }
    void run_hlsl()
    {
        std::array<D3D12_ROOT_PARAMETER, 7> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants = {0, 0, 40};
        parameters[1].ParameterType = parameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].Descriptor = {0, 0}; parameters[6].Descriptor = {3, 0};
        for (UINT i = 0; i < 4; ++i)
        {
            parameters[i + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            parameters[i + 2].Descriptor = {i, 0};
        }
        auto root = root_signature(gpu.device.Get(), parameters);
        auto pipeline = [&](const char *entry) {
            auto bytes = shader(entry);
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = root.Get(); desc.CS = {bytes.data(), bytes.size()};
            ComPtr<ID3D12PipelineState> state;
            check(gpu.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&state)), "HLSL oracle pipeline");
            return state;
        };
        auto reset = pipeline("reset_args"), reference = pipeline("project");
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "HLSL oracle allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "HLSL oracle list");
        SharedBuffer *outputs[]{&output, &keys, &indexes, &arguments};
        for (auto *buffer : outputs) transition(list.Get(), buffer->resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->SetComputeRootSignature(root.Get());
        list->SetComputeRoot32BitConstants(0, 40, &constants, 0);
        list->SetComputeRootShaderResourceView(1, input.resource()->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(6, harmonics.resource()->GetGPUVirtualAddress());
        for (UINT i = 0; i < 4; ++i) list->SetComputeRootUnorderedAccessView(i + 2, outputs[i]->resource()->GetGPUVirtualAddress());
        list->SetPipelineState(reset.Get()); list->Dispatch(1, 1, 1); uav_barrier(list.Get());
        list->SetPipelineState(reference.Get()); list->Dispatch((constants.meta[0] + 255) / 256, 1, 1);
        for (auto *buffer : outputs) transition(list.Get(), buffer->resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        check(list->Close(), "HLSL oracle close");
        gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value));
    }
};
}

TEST(CudaProject, AnalyticalCenteredEllipsesAndShDegrees)
{
    // Independent double precision oracle at direction (0,0,-1).
    const double z_basis[]{-.4886025119029199, .6307831305050401, -.7463526651802308};
    for (auto math : {ProjectMath::Reference, ProjectMath::FactoredReciprocal, ProjectMath::ReciprocalSqrt, ProjectMath::FactoredAndRsqrt})
    for (uint32_t block : {128u, 256u}) for (uint32_t degree = 0; degree <= 3; ++degree)
    {
        Fixture f;
        f.constants.meta[1] = degree;
        for (uint32_t i = 0; i < Fixture::count; ++i)
            for (uint32_t j : {1u, 5u, 11u}) f.sh[i * 45 + j * 3] = .1f;
        f.run(block, math);
        auto ellipses = f.read<ProjectedEllipse>(f.output);
        auto keys = f.read<uint32_t>(f.keys), indexes = f.read<uint32_t>(f.indexes);
        double red = .5;
        for (uint32_t j = 0; j < degree; ++j) red += .1 * z_basis[j];
        for (uint32_t i = 0; i < Fixture::count; ++i)
        {
            EXPECT_NEAR(ellipses[i].center[0], 32, 1e-5);
            EXPECT_NEAR(ellipses[i].center[1], 32, 1e-5);
            EXPECT_NEAR(ellipses[i].axis0[0], 30, 1e-4);
            EXPECT_NEAR(ellipses[i].axis1[1], 30, 1e-4);
            EXPECT_NEAR(ellipses[i].color[0], red, 1e-6);
            EXPECT_FLOAT_EQ(ellipses[i].color[3], .8f);
            EXPECT_EQ(keys[i], ~std::bit_cast<uint32_t>(100.f));
            EXPECT_EQ(indexes[i], i);
        }
        auto counters = f.read<DrawCounters>(f.arguments)[0];
        EXPECT_EQ(counters.vertices_per_instance, 4u);
        EXPECT_EQ(counters.instance_count, Fixture::count);
        EXPECT_EQ(counters.rejected, 0u);
        EXPECT_TRUE(f.gpu.debug_errors().empty());
    }
}

TEST(CudaProject, RejectsInvalidInputsAndCullsWithoutCountingAsNumericErrors)
{
    Fixture f;
    f.set(0, 0, 0, std::numeric_limits<float>::quiet_NaN());
    f.set(1, 1, 0, 0);
    f.set(2, 2, 3, 2);
    f.set(3, 3, 0, -1);
    f.set(1, 4, 1, std::numeric_limits<float>::infinity());
    f.set(3, 5, 0, 0); // ordinary alpha culling
    f.set(0, 6, 2, -200); // ordinary far plane culling
    f.set(0, 7, 0, 10000); // outside viewport
    f.run(128);
    auto counters = f.read<DrawCounters>(f.arguments)[0];
    EXPECT_EQ(counters.rejected, 5u);
    EXPECT_EQ(counters.instance_count, Fixture::count - 8);
    auto keys = f.read<uint32_t>(f.keys);
    for (uint32_t i = 0; i < 8; ++i) EXPECT_EQ(keys[i], UINT32_MAX);
    EXPECT_TRUE(f.gpu.debug_errors().empty());
}

TEST(CudaProject, ValidatesLayoutCapacityAndLaunchSize)
{
    Fixture f;
    auto submit = [&](uint32_t block) { gs::cuda_ops::project(f.context, f.constants, f.input, f.harmonics,
        f.output, f.keys, f.indexes, f.arguments, block); };
    EXPECT_THROW(submit(64), std::invalid_argument);
    f.constants.meta[1] = 4;
    EXPECT_THROW(submit(256), std::invalid_argument);
    f.constants.meta[1] = 0;
    f.constants.meta[0] = Fixture::count + 1;
    EXPECT_THROW(submit(256), std::out_of_range);
}

TEST(CudaProject, MatchesHlslForAnisotropyRotationsShAndClipping)
{
    std::mt19937 random(42);
    std::uniform_real_distribution<float> unit(0, 1);
    for (auto math : {ProjectMath::Reference, ProjectMath::FactoredReciprocal, ProjectMath::ReciprocalSqrt, ProjectMath::FactoredAndRsqrt})
    for (uint32_t degree = 0; degree <= 3; ++degree)
    {
        Fixture f;
        f.constants.meta[1] = degree;
        f.constants.quality[2] = .01f;
        for (uint32_t i = 0; i < Fixture::count; ++i)
        {
            f.set(0, i, 0, (unit(random) - .5f) * 30);
            f.set(0, i, 1, (unit(random) - .5f) * 30);
            f.set(0, i, 2, -unit(random) * 40);
            f.set(1, i, 0, .01f + unit(random) * 2);
            f.set(1, i, 1, .001f + unit(random) * .1f);
            f.set(1, i, 2, .0001f + unit(random) * .01f);
            const float angle = unit(random) * 3.14f;
            f.set(2, i, 2, std::sin(angle)); f.set(2, i, 3, std::cos(angle));
            f.set(3, i, 0, unit(random));
            for (uint32_t j = 0; j < 45; ++j) f.sh[i * 45 + j] = (unit(random) - .5f) * .1f;
        }
        f.run(256, math);
        auto actual = f.read<ProjectedEllipse>(f.output);
        auto keys = f.read<uint32_t>(f.keys);
        auto counters = f.read<DrawCounters>(f.arguments)[0];
        f.run_hlsl();
        auto expected = f.read<ProjectedEllipse>(f.output);
        auto expected_keys = f.read<uint32_t>(f.keys);
        auto expected_counters = f.read<DrawCounters>(f.arguments)[0];
        EXPECT_EQ(counters.instance_count, expected_counters.instance_count);
        EXPECT_EQ(counters.rejected, expected_counters.rejected);
        for (uint32_t i = 0; i < Fixture::count; ++i)
        {
            ASSERT_EQ(keys[i] == UINT32_MAX, expected_keys[i] == UINT32_MAX) << i;
            if (keys[i] == UINT32_MAX) continue;
            EXPECT_LE(std::abs(int64_t(keys[i]) - expected_keys[i]), 2) << i;
            for (int j = 0; j < 2; ++j)
            {
                EXPECT_NEAR(actual[i].center[j], expected[i].center[j], .002f) << i;
                EXPECT_NEAR(actual[i].axis0[j], expected[i].axis0[j], .002f) << i;
                EXPECT_NEAR(actual[i].axis1[j], expected[i].axis1[j], .002f) << i;
            }
            for (int j = 0; j < 4; ++j) EXPECT_NEAR(actual[i].color[j], expected[i].color[j], 1e-5f) << i;
        }
        EXPECT_TRUE(f.gpu.debug_errors().empty());
    }
}

TEST(CudaProject, EmptyFrameResetsCounters)
{
    Fixture f;
    f.constants.meta[0] = 0;
    f.run(128);
    const auto counters = f.read<DrawCounters>(f.arguments)[0];
    EXPECT_EQ(counters.vertices_per_instance, 4u);
    EXPECT_EQ(counters.instance_count, 0u);
    EXPECT_EQ(counters.rejected, 0u);
}

TEST(CudaProject, PreservesNearPlaneCrossingThinSplatsAndZeroDistance)
{
    for (auto math : {ProjectMath::Reference, ProjectMath::FactoredReciprocal, ProjectMath::ReciprocalSqrt, ProjectMath::FactoredAndRsqrt})
    {
        Fixture f;
        f.set(0, 0, 2, -.05f); // center in front of near plane; support crosses it
        f.set(0, 1, 2, 0); // valid radial zero, distinct from reserved culled key
        f.set(1, 2, 1, 1e-6f); f.set(1, 2, 2, 1e-6f); // stable Gram minor eigenvalue
        f.set(1, 3, 0, 1e30f); // covariance overflow must reject
        f.run(256, math);
        auto ellipses = f.read<ProjectedEllipse>(f.output);
        auto keys = f.read<uint32_t>(f.keys);
        auto counters = f.read<DrawCounters>(f.arguments)[0];
        EXPECT_NE(keys[0], UINT32_MAX);
        EXPECT_EQ(keys[1], UINT32_MAX - 1);
        EXPECT_NEAR(ellipses[0].axis0[0], 1024, 1e-3);
        EXPECT_NEAR(ellipses[2].axis1[1], 3e-5, 1e-9);
        EXPECT_EQ(keys[3], UINT32_MAX);
        EXPECT_EQ(counters.rejected, 1u);
        EXPECT_EQ(counters.instance_count, Fixture::count - 1);
        f.run_hlsl();
        auto expected_keys = f.read<uint32_t>(f.keys);
        for (uint32_t i = 0; i < 4; ++i) EXPECT_EQ(keys[i], expected_keys[i]);
        EXPECT_TRUE(f.gpu.debug_errors().empty());
    }
}

TEST(CudaProject, FactoredReciprocalHandlesSubnormalCameraDelta)
{
    for (auto math : {ProjectMath::FactoredReciprocal, ProjectMath::FactoredAndRsqrt})
    {
        Fixture f;
        f.constants.meta[1] = 1;
        f.set(0, 0, 0, 1e-42f); f.set(0, 0, 2, 0);
        f.sh[2 * 3] = .1f; // direction +X, first-degree X basis is negative
        f.run(256, math);
        const auto ellipses = f.read<ProjectedEllipse>(f.output);
        EXPECT_NEAR(ellipses[0].color[0], .5 - .04886025119029199, 1e-6);
        EXPECT_EQ(f.read<uint32_t>(f.keys)[0], UINT32_MAX - 1);
    }
}
