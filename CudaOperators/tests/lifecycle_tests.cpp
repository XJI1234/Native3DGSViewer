#include "gs_cuda/interop.h"
#include "gpu.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <vector>

using namespace gs::cuda_ops;
using namespace gs::render::detail;

static void verify_lifetimes(bool debug)
{
    GpuDevice gpu(debug);
    auto cycle = [&] {
        Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
        SharedBuffer buffer(context, 4096);
        SharedTexture texture(context, 17, 9);
        context.begin(gpu.direct.Get());
        fill_u32(context, buffer, 123, 1024);
        fill_rgba(context, texture, 0x44332211);
        context.end(gpu.direct.Get());
        check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Cycle complete");
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
        if (!context.wait_idle()) throw std::runtime_error("Cycle timeout");
    };
    for (int i = 0; i < 5; ++i) cycle();
    DWORD before = 0, after = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 100; ++i) cycle();
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_LE(after, before + 4) << "before=" << before << " after=" << after;
    EXPECT_TRUE(gpu.debug_errors().empty());
}

TEST(CudaLifecycle, RepeatsHundredContextBufferTextureLifetimesWithoutHandleGrowth)
{
    verify_lifetimes(false);
}

TEST(CudaLifecycle, RepeatsHundredDebugContextBufferTextureLifetimesWithoutHandleGrowth)
{
    verify_lifetimes(true);
}

TEST(CudaLifecycle, RepeatsHundredOwnedInputLifetimesWithoutHandleGrowth)
{
    GpuDevice gpu;
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    const std::vector<float> values(1024, .125f);
    auto cycle = [&] {
        OwnedBuffer attributes(context, values.size() * sizeof(float));
        attributes.upload(context, 0, values);
    };
    for (int i = 0; i < 5; ++i) cycle();
    DWORD before = 0, after = 0;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 100; ++i) cycle();
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_LE(after, before + 4) << "before=" << before << " after=" << after;
}

TEST(CudaLifecycle, ReportsBoundedTimeoutAndRecoversAfterInjectedQueueStall)
{
    GpuDevice gpu(true);
    Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
    SharedBuffer buffer(context, 4);
    // Load the module before deliberately blocking its stream. CUDA's lazy
    // module initialization can synchronize before a first kernel submission.
    context.begin(gpu.direct.Get());
    fill_u32(context, buffer, 0, 1);
    context.end(gpu.direct.Get());
    ASSERT_TRUE(context.wait_idle());
    ComPtr<ID3D12Fence> gate;
    check(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "Lifecycle gate");
    check(gpu.direct->Wait(gate.Get(), 1), "Inject queue stall");
    context.begin(gpu.direct.Get());
    fill_u32(context, buffer, 123, 1);
    context.end(gpu.direct.Get());
    const auto start = std::chrono::steady_clock::now();
    const bool completed = context.wait_idle(10);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // Always release the injected stall before assertions or owner destruction.
    check(gate->Signal(1), "Release queue stall");
    EXPECT_FALSE(completed);
    EXPECT_LT(elapsed, 1);
    check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Lifecycle complete");
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    ASSERT_TRUE(context.wait_idle());
    const auto bytes = gpu.readback(buffer.resource(), 4, D3D12_RESOURCE_STATE_COMMON);
    uint32_t value = 0;
    std::memcpy(&value, bytes.data(), 4);
    EXPECT_EQ(value, 123u);
    EXPECT_TRUE(gpu.debug_errors().empty());
}
