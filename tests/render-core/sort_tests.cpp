#include "gpu.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <numeric>
#include <random>

using namespace gs::render::detail;
TEST(RenderGpu, Wave8CompatibleSortModesPassGpuSelfTest)
{
    try
    {
        GpuDevice gpu(true);
        SortPass wave_agnostic(gpu, gs::render::SortShaderMode::WaveAgnostic);
        wave_agnostic.validate();
        EXPECT_EQ(wave_agnostic.mode(), gs::render::SortShaderMode::WaveAgnostic);

        if (gpu.shader_model_6_6 && gpu.wave_max >= 32)
        {
            SortPass wave32(gpu, gs::render::SortShaderMode::FixedWave32);
            wave32.validate();
            EXPECT_TRUE(wave32.mode() == gs::render::SortShaderMode::FixedWave32 ||
                        wave32.mode() == gs::render::SortShaderMode::WaveAgnostic);
            if (wave32.mode() == gs::render::SortShaderMode::WaveAgnostic)
                EXPECT_NE(wave32.wave32_fallback_hr(), S_OK);
        }

        if (gpu.wave_min >= 16)
        {
            SortPass standard(gpu);
            standard.validate();
            EXPECT_EQ(standard.mode(), gs::render::SortShaderMode::Standard);
        }

        gpu.wave_min = 8;
        gpu.wave_max = 32;
        SortPass automatic(gpu);
        automatic.validate();
        EXPECT_TRUE(automatic.mode() == gs::render::SortShaderMode::FixedWave32 ||
                    automatic.mode() == gs::render::SortShaderMode::WaveAgnostic);
        gpu.shader_model_6_6 = false;
        SortPass automatic_fallback(gpu);
        automatic_fallback.validate();
        EXPECT_EQ(automatic_fallback.mode(), gs::render::SortShaderMode::WaveAgnostic);
        for (const auto &error : gpu.debug_errors())
            ADD_FAILURE() << error;
    }
    catch (const GpuFailure &failure)
    {
        FAIL() << failure.operation << " HRESULT=" << std::hex << failure.hr;
    }
}
TEST(RenderGpu, StableRadixMatchesCpuIncludingEightMillionKeys)
{
    try
    {
        GpuDevice gpu(true);
        SortPass sort(gpu);
        std::mt19937 random(0x12345);
        for (uint32_t n :
             {0u, 1u, 127u, 128u, 129u, 511u, 512u, 513u, 4097u, 1'000'000u, 8'000'000u})
        {
            SCOPED_TRACE(n);
            auto buffers = sort.allocate(n);
            std::vector<uint32_t> keys(n), values(n), expected(n);
            for (uint32_t i = 0; i < n; ++i)
                keys[i] = i % 3 ? random() % 257 : UINT32_MAX;
            if (n == 4097)
                std::fill(keys.begin(), keys.end(), 42);
            std::iota(values.begin(), values.end(), 0);
            expected = values;
            std::stable_sort(expected.begin(), expected.end(),
                             [&](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });
            if (!n)
                continue;
            auto upload = gpu.buffer(uint64_t(n) * 8, D3D12_HEAP_TYPE_UPLOAD,
                                     D3D12_RESOURCE_STATE_GENERIC_READ);
            void *mapped = nullptr;
            D3D12_RANGE empty{};
            check(upload->Map(0, &empty, &mapped), "Test upload map");
            memcpy(mapped, keys.data(), size_t(n) * 4);
            memcpy(static_cast<uint8_t *>(mapped) + size_t(n) * 4, values.data(), size_t(n) * 4);
            upload->Unmap(0, nullptr);
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12GraphicsCommandList> list;
            check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                     IID_PPV_ARGS(&allocator)),
                  "Test allocator");
            check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                                nullptr, IID_PPV_ARGS(&list)),
                  "Test list");
            for (int i = 0; i < 2; ++i)
            {
                auto *r = i ? buffers.values[0].Get() : buffers.keys[0].Get();
                transition(list.Get(), r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_COPY_DEST);
                list->CopyBufferRegion(r, 0, upload.Get(), uint64_t(i) * n * 4, uint64_t(n) * 4);
                transition(list.Get(), r, D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            sort.record(list.Get(), buffers);
            check(list->Close(), "Test close");
            gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(),
                                                        gpu.direct_fence.Get(), gpu.direct_value));
            auto result = gpu.readback(buffers.values[0].Get(), uint64_t(n) * 4,
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            EXPECT_EQ(memcmp(result.data(), expected.data(), result.size()), 0);
            auto sorted_keys = gpu.readback(buffers.keys[0].Get(), uint64_t(n) * 4,
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            std::vector<uint32_t> expected_keys(n);
            std::transform(expected.begin(), expected.end(), expected_keys.begin(),
                           [&](uint32_t index) { return keys[index]; });
            EXPECT_EQ(memcmp(sorted_keys.data(), expected_keys.data(), sorted_keys.size()), 0);
        }
        for (const auto &error : gpu.debug_errors())
            ADD_FAILURE() << error;
    }
    catch (const GpuFailure &failure)
    {
        FAIL() << failure.operation << " HRESULT=" << std::hex << failure.hr;
    }
}
