#include "gs_cuda/interop.h"
#include "gpu.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace gs::cuda_ops;
using namespace gs::render::detail;

TEST(CudaInterop, MatchesAdapterAndWritesSharedBuffer)
{
    GpuDevice gpu(true);
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    SharedBuffer buffer(context, 513 * sizeof(uint32_t));
    context.begin(gpu.direct.Get());
    fill_u32(context, buffer, 0x12345678u, 513);
    context.end(gpu.direct.Get());
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Interop completion");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    const auto bytes = gpu.readback(buffer.resource(), buffer.bytes(), D3D12_RESOURCE_STATE_COMMON);
    for (size_t offset = 0; offset < bytes.size(); offset += sizeof(uint32_t))
    {
        uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        ASSERT_EQ(value, 0x12345678u) << offset;
    }
    EXPECT_THROW(fill_u32(context, buffer, 0, 514), std::out_of_range);
    EXPECT_TRUE(gpu.debug_errors().empty());
}

TEST(CudaInterop, RejectsInvalidLuidAndEmptyAllocation)
{
    GpuDevice gpu;
    LUID missing{0xffffffffu, 0x7fffffff};
    EXPECT_THROW(Context(gpu.device.Get(), missing), std::runtime_error);
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    EXPECT_THROW(SharedBuffer(context, 0), std::invalid_argument);
}

TEST(CudaInterop, RepeatsTenThousandFenceHandoffs)
{
    GpuDevice gpu(true);
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    SharedBuffer buffer(context, 4);
    EXPECT_THROW(context.end(gpu.direct.Get()), std::logic_error);
    for (uint32_t i = 0; i < 10000; ++i)
    {
        context.begin(gpu.direct.Get());
        if (i == 0) EXPECT_THROW(context.begin(gpu.direct.Get()), std::logic_error);
        fill_u32(context, buffer, i, 1);
        context.end(gpu.direct.Get());
    }
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Handoff completion");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    ASSERT_TRUE(context.wait_idle());
    auto bytes = gpu.readback(buffer.resource(), 4, D3D12_RESOURCE_STATE_COMMON);
    uint32_t last = 0;
    std::memcpy(&last, bytes.data(), 4);
    EXPECT_EQ(last, 9999u);
    EXPECT_TRUE(gpu.debug_errors().empty());
}

TEST(CudaInterop, WritesSharedTextureWithPaddedRows)
{
    GpuDevice gpu(true);
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    SharedTexture texture(context, 17, 9);
    context.begin(gpu.direct.Get());
    fill_rgba(context, texture, 0x44332211u);
    context.end(gpu.direct.Get());
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    auto desc = texture.resource()->GetDesc();
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    auto readback = gpu.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Texture allocator");
    check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Texture commands");
    transition(list.Get(), texture.resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture.resource();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION target{};
    target.pResource = readback.Get();
    target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    target.PlacedFootprint = footprint;
    list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    transition(list.Get(), texture.resource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    check(list->Close(), "Texture close");
    gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value));
    void *mapped = nullptr;
    D3D12_RANGE range{0, size_t(bytes)};
    check(readback->Map(0, &range, &mapped), "Texture map");
    for (uint32_t y = 0; y < 9; ++y)
        for (uint32_t x = 0; x < 17; ++x)
        {
            uint32_t value = 0;
            std::memcpy(&value, static_cast<uint8_t *>(mapped) + y * footprint.Footprint.RowPitch + x * 4, 4);
            EXPECT_EQ(value, 0x44332211u);
        }
    D3D12_RANGE empty{};
    readback->Unmap(0, &empty);
    EXPECT_TRUE(gpu.debug_errors().empty());
}
