#include "gs_cuda/compact.h"
#include "gpu.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>

using namespace gs::cuda_ops;
using namespace gs::render::detail;

static void verify_compact(uint32_t count, uint32_t percent, CompactMode mode)
{
    GpuDevice gpu(true);
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    const uint64_t bytes = uint64_t((std::max)(count, 1u)) * 4;
    SharedBuffer keys0(context, bytes), keys1(context, bytes), values0(context, bytes), values1(context, bytes);
    CompactWorkspace workspace(context, count, mode);
    std::vector<uint32_t> keys(count), expected;
    std::mt19937 random(42);
    for (uint32_t trial = 0; trial < 3; ++trial)
    {
        expected.clear();
        const auto visible_percent = trial == 1 ? 100 - percent : percent;
        // Non-prefix visibility and many equal keys detect unstable selection.
        for (uint32_t i = 0; i < count; ++i)
        {
            keys[i] = i % 100 < visible_percent ? random() % 17 : UINT32_MAX;
            if (keys[i] != UINT32_MAX) expected.push_back(i);
        }
        std::stable_sort(expected.begin(), expected.end(), [&](auto a, auto b) { return keys[a] < keys[b]; });
        auto upload = gpu.buffer(bytes * 2, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        check(upload->Map(0, &empty, &mapped), "Compact upload");
        if (count) std::memcpy(mapped, keys.data(), count * 4ull);
        auto *indices = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(mapped) + bytes);
        std::iota(indices, indices + count, 0u);
        upload->Unmap(0, nullptr);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Compact allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Compact list");
        for (auto [target, offset] : {std::pair{keys0.resource(), uint64_t{0}}, std::pair{values0.resource(), bytes}})
        {
            transition(list.Get(), target, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyBufferRegion(target, 0, upload.Get(), offset, bytes);
            transition(list.Get(), target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        }
        check(list->Close(), "Compact close");
        gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value);
        context.begin(gpu.direct.Get());
        const auto selected = workspace.submit(context, keys0, keys1, values0, values1, count);
        context.end(gpu.direct.Get());
        check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Compact complete");
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
        auto actual_count = gpu.readback(workspace.visible_count_buffer().resource(), 4, D3D12_RESOURCE_STATE_COMMON);
        uint32_t visible = UINT32_MAX;
        std::memcpy(&visible, actual_count.data(), 4);
        ASSERT_EQ(visible, expected.size());
        auto actual_keys = gpu.readback(selected ? keys1.resource() : keys0.resource(), bytes, D3D12_RESOURCE_STATE_COMMON);
        auto actual_values = gpu.readback(selected ? values1.resource() : values0.resource(), bytes, D3D12_RESOURCE_STATE_COMMON);
        for (uint32_t i = 0; i < expected.size(); ++i)
        {
            uint32_t key = 0, value = 0;
            std::memcpy(&key, actual_keys.data() + i * 4ull, 4);
            std::memcpy(&value, actual_values.data() + i * 4ull, 4);
            ASSERT_EQ(value, expected[i]) << "position=" << i << " percent=" << percent;
            ASSERT_EQ(key, keys[expected[i]]) << i;
        }
        ASSERT_TRUE(context.wait_idle());
        EXPECT_TRUE(gpu.debug_errors().empty());
    }
}

TEST(CudaCompact, StableVisiblePrefixForAllVisibilityFractions)
{
    for (auto mode : {CompactMode::FullCapacity, CompactMode::DeviceBuckets})
        for (uint32_t count : {0u, 1u, 513u, 65537u})
            for (uint32_t percent : {0u, 1u, 10u, 50u, 100u}) verify_compact(count, percent, mode);
}
TEST(CudaCompact, StableMillionVisibleSubset)
{
    for (auto mode : {CompactMode::FullCapacity, CompactMode::DeviceBuckets}) verify_compact(1000000, 10, mode);
}

TEST(CudaCompact, RejectsAliasesCapacityChangesAndStaleGraphAddresses)
{
    GpuDevice gpu;
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    SharedBuffer keys0(context, 4), keys1(context, 4), values0(context, 4), values1(context, 4), replacement(context, 4);
    CompactWorkspace full(context, 1, CompactMode::FullCapacity);
    EXPECT_THROW(full.submit(context, keys0, keys0, values0, values1, 1), std::invalid_argument);
    EXPECT_THROW(full.submit(context, keys0, keys1, values0, values1, 0), std::out_of_range);
    CompactWorkspace too_large(context, 2, CompactMode::FullCapacity);
    EXPECT_THROW(too_large.submit(context, keys0, keys1, values0, values1, 2), std::out_of_range);
    CompactWorkspace buckets(context, 1, CompactMode::DeviceBuckets);
    context.begin(gpu.direct.Get());
    fill_u32(context, keys0, 7, 1); fill_u32(context, values0, 33, 1);
    buckets.submit(context, keys0, keys1, values0, values1, 1);
    context.end(gpu.direct.Get());
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Graph address test complete");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    EXPECT_THROW(buckets.submit(context, replacement, keys1, values0, values1, 1), std::invalid_argument);
}
