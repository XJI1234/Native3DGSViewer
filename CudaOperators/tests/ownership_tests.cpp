#include "gs_cuda/compact.h"
#include "gs_cuda/project.h"
#include "gs_cuda/sort.h"
#include "gs_cuda/tile.h"
#include "gpu.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>

using namespace gs::cuda_ops;
using namespace gs::render::detail;

TEST(CudaInterop, ForeignFillFailsWithoutChangingTheResource)
{
    GpuDevice gpu;
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    Context other(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    SharedBuffer foreign(other, 4);
    SharedTexture texture(other, 8, 8);
    other.begin(gpu.direct.Get());
    fill_u32(other, foreign, 0x12345678u, 1);
    other.end(gpu.direct.Get());
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Foreign fill seed");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    context.begin(gpu.direct.Get());
    EXPECT_THROW(fill_u32(context, foreign, 99, 1), std::invalid_argument);
    EXPECT_THROW(fill_u32(context, foreign, 99, 0), std::invalid_argument);
    EXPECT_THROW(fill_rgba(context, texture, 0), std::invalid_argument);
    context.end(gpu.direct.Get());
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Foreign fill complete");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    const auto bytes = gpu.readback(foreign.resource(), 4, D3D12_RESOURCE_STATE_COMMON);
    uint32_t actual = 0;
    std::memcpy(&actual, bytes.data(), 4);
    EXPECT_EQ(actual, 0x12345678u);
    ASSERT_TRUE(context.wait_idle());
    ASSERT_TRUE(other.wait_idle());
}

TEST(CudaInterop, EveryOperatorRejectsForeignInputsBeforeEnqueue)
{
    GpuDevice gpu;
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    Context other(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    std::array<std::unique_ptr<SharedBuffer>, 6> local;
    for (auto &buffer : local) buffer = std::make_unique<SharedBuffer>(context, 4096);
    SharedBuffer foreign(other, 4096);
    SharedTexture target(context, 8, 8), foreign_target(other, 8, 8);
    SortWorkspace sort(context, 1);
    CompactWorkspace compact(context, 1, CompactMode::FullCapacity);
    TileWorkspace lists(context, {1, 8, 8, 8, 1, 4ull << 20});
    TileCompositeWorkspace composite(context, lists, 4ull << 20);

    // Seed valid bounded zero inputs so a missing guard fails assertions safely.
    other.begin(gpu.direct.Get());
    fill_u32(other, foreign, 0, 1024);
    other.end(gpu.direct.Get());
    context.begin(gpu.direct.Get());
    for (auto &buffer : local) fill_u32(context, *buffer, 0, 1024);
    lists.submit(context, *local[0], *local[1], *local[5]);
    context.end(gpu.direct.Get());
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Ownership seeds");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    EXPECT_THROW(TileCompositeWorkspace(other, lists, 4ull << 20), std::invalid_argument);

    context.begin(gpu.direct.Get());
    for (uint32_t input = 0; input < 4; ++input)
    {
        std::array<SharedBuffer *, 4> buffers{local[0].get(), local[1].get(), local[2].get(), local[3].get()};
        buffers[input] = &foreign;
        EXPECT_THROW(sort.submit(context, *buffers[0], *buffers[1], *buffers[2], *buffers[3], 1), std::invalid_argument);
        EXPECT_THROW(compact.submit(context, *buffers[0], *buffers[1], *buffers[2], *buffers[3], 1), std::invalid_argument);
    }
    FrameConstants frame{};
    frame.meta[0] = 1;
    frame.clip[0] = .01f;
    for (uint32_t input = 0; input < 6; ++input)
    {
        std::array<SharedBuffer *, 6> buffers{};
        for (uint32_t i = 0; i < buffers.size(); ++i) buffers[i] = local[i].get();
        buffers[input] = &foreign;
        EXPECT_THROW(project(context, frame, *buffers[0], *buffers[1], *buffers[2], *buffers[3], *buffers[4], *buffers[5]), std::invalid_argument);
    }
    for (uint32_t input = 0; input < 3; ++input)
    {
        std::array<SharedBuffer *, 3> buffers{local[0].get(), local[1].get(), local[5].get()};
        buffers[input] = &foreign;
        EXPECT_THROW(lists.submit(context, *buffers[0], *buffers[1], *buffers[2]), std::invalid_argument);
    }
    EXPECT_THROW(composite.submit(context, foreign, *local[1], target), std::invalid_argument);
    EXPECT_THROW(composite.submit(context, *local[0], foreign, target), std::invalid_argument);
    EXPECT_THROW(composite.submit(context, *local[0], *local[1], foreign_target), std::invalid_argument);
    context.end(gpu.direct.Get());
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Ownership rejects complete");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    ASSERT_TRUE(context.wait_idle());
    ASSERT_TRUE(other.wait_idle());
}
