#include "gs_cuda/sort.h"
#include "gpu.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>

using namespace gs::cuda_ops;
using namespace gs::render::detail;

static void verify_sort(uint32_t count, uint32_t distribution, bool capture = false,
    SortOffsets offsets = SortOffsets::Int64)
{
    GpuDevice gpu;
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    const uint64_t bytes = uint64_t((std::max)(count, 1u)) * 4;
    SharedBuffer keys0(context, bytes), keys1(context, bytes);
    SharedBuffer values0(context, bytes), values1(context, bytes);
    SortWorkspace workspace(context, count, capture, offsets);
    for (uint32_t trial = 0; trial < (capture ? 3u : 1u); ++trial)
    {
        std::vector<uint32_t> keys(count), expected(count);
        std::mt19937 random(42 + trial);
        for (uint32_t i = 0; i < count; ++i)
            keys[i] = distribution == 0 ? random() : distribution == 1 ? 7u :
                      distribution == 2 ? count - i : (i % 7 == 0 ? UINT32_MAX : i % 257);
        std::iota(expected.begin(), expected.end(), 0);
        std::stable_sort(expected.begin(), expected.end(), [&](auto a, auto b) { return keys[a] < keys[b]; });
        auto upload = gpu.buffer(bytes * 2, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        check(upload->Map(0, &empty, &mapped), "Sort test upload");
        if (count) std::memcpy(mapped, keys.data(), size_t(count) * 4);
        auto *indices = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(mapped) + bytes);
        std::iota(indices, indices + count, 0u);
        upload->Unmap(0, nullptr);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Sort test allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Sort test commands");
        for (auto [target, offset] : {std::pair{keys0.resource(), uint64_t{0}}, std::pair{values0.resource(), bytes}})
        {
            transition(list.Get(), target, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyBufferRegion(target, 0, upload.Get(), offset, bytes);
            transition(list.Get(), target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        }
        check(list->Close(), "Sort test close");
        gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value);
        context.begin(gpu.direct.Get());
        const auto selected = workspace.submit(context, keys0, keys1, values0, values1, count);
        context.end(gpu.direct.Get());
        check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Sort test complete");
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
        auto actual_keys = gpu.readback(selected ? keys1.resource() : keys0.resource(), bytes, D3D12_RESOURCE_STATE_COMMON);
        auto actual_values = gpu.readback(selected ? values1.resource() : values0.resource(), bytes, D3D12_RESOURCE_STATE_COMMON);
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t key = 0, value = 0;
            std::memcpy(&key, actual_keys.data() + uint64_t(i) * 4, 4);
            std::memcpy(&value, actual_values.data() + uint64_t(i) * 4, 4);
            ASSERT_EQ(value, expected[i]) << "position=" << i << " distribution=" << distribution;
            ASSERT_EQ(key, keys[expected[i]]) << i;
        }
    }
}

TEST(CudaSort, StablePairsAtBlockBoundaries)
{
    for (auto count : {0u, 1u, 511u, 512u, 513u, 65537u})
        for (uint32_t distribution = 0; distribution < 4; ++distribution)
            verify_sort(count, distribution);
}
TEST(CudaSort, StableMillionPairs) { verify_sort(1000000, 3); }
TEST(CudaSort, StableEightMillionRandomPairs) { verify_sort(8000000, 0); }
TEST(CudaSort, StableFullLargeSceneCount) { verify_sort(22480361, 0); }
TEST(CudaSort, Int32OffsetsPreserveStablePairs)
{
    for (auto count : {0u, 1u, 511u, 512u, 513u, 65537u})
        for (uint32_t distribution = 0; distribution < 4; ++distribution)
            verify_sort(count, distribution, false, SortOffsets::Int32);
    verify_sort(1000000, 3, false, SortOffsets::Int32);
    verify_sort(8000000, 0, false, SortOffsets::Int32);
    verify_sort(22480361, 3, false, SortOffsets::Int32);
}
TEST(CudaSort, Int32OffsetsRejectOversizedCapacityBeforeAllocation)
{
    GpuDevice gpu;
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    EXPECT_THROW(SortWorkspace(context, 0x80000000u, false, SortOffsets::Int32), std::out_of_range);
    EXPECT_THROW(SortWorkspace(context, 1, false, static_cast<SortOffsets>(99)), std::invalid_argument);
}
TEST(CudaSort, GraphReplaysChangingInputsWithStablePairs)
{
    for (auto count : {0u, 1u, 513u, 1000000u}) for (auto distribution : {0u, 3u}) verify_sort(count, distribution, true);
}

TEST(CudaSort, Int32GraphReplaysChangingInputsWithStablePairs)
{
    for (auto count : {0u, 1u, 513u, 1000000u})
        for (auto distribution : {0u, 3u}) verify_sort(count, distribution, true, SortOffsets::Int32);
}

TEST(CudaSort, GraphRejectsChangedBindingsAndCapacity)
{
    GpuDevice gpu;
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    SharedBuffer keys0(context, 4096), keys1(context, 4096);
    SharedBuffer values0(context, 4096), values1(context, 4096), replacement(context, 4096);
    SharedBuffer undersized(context, 4);
    SortWorkspace workspace(context, 513, true);
    EXPECT_THROW(workspace.submit(context, keys0, keys1, values0, values1, 514), std::out_of_range);
    EXPECT_THROW(workspace.submit(context, undersized, keys1, values0, values1, 513), std::out_of_range);
    EXPECT_THROW(workspace.submit(context, keys0, keys0, values0, values1, 513), std::invalid_argument);
    context.begin(gpu.direct.Get());
    workspace.submit(context, keys0, keys1, values0, values1, 513);
    EXPECT_THROW(workspace.submit(context, replacement, keys1, values0, values1, 513), std::invalid_argument);
    EXPECT_THROW(workspace.submit(context, keys0, keys1, values0, values1, 512), std::invalid_argument);
    EXPECT_THROW(workspace.submit(context, keys0, keys1, values0, values1, 0), std::invalid_argument);
    EXPECT_NO_THROW(workspace.submit(context, keys0, keys1, values0, values1, 513));
    context.end(gpu.direct.Get());
    ASSERT_TRUE(context.wait_idle());
    Context other(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    EXPECT_THROW(workspace.submit(other, keys0, keys1, values0, values1, 513), std::invalid_argument);
}
