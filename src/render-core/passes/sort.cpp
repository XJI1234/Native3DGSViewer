#include "gpu.h"
#define FFX_CPP
#include "FFX_ParallelSort.h"
#include <algorithm>
#include <numeric>
#include <random>

namespace gs::render::detail
{
SortPass::SortPass(GpuDevice &gpu, std::optional<SortShaderMode> requested_mode) : gpu_(gpu)
{
    std::array<D3D12_ROOT_PARAMETER, 11> params{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, 6};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants = {1, 0, 1};
    for (UINT i = 0; i < 9; ++i)
    {
        params[i + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[i + 2].Descriptor = {i, 0};
    }
    root_ = root_signature(gpu.device.Get(), params);
    if (requested_mode)
    {
        // Forced variants are used by tests and deliberately fail on unsupported hardware.
        load_pipelines(*requested_mode);
        return;
    }
    if (gpu.wave_min >= 16)
        load_pipelines(SortShaderMode::Standard);
    else if (gpu.shader_model_6_6 && gpu.wave_max >= 32)
    {
        try { load_pipelines(SortShaderMode::FixedWave32); }
        catch (const GpuFailure &failure)
        {
            wave32_fallback_hr_ = failure.hr;
            load_pipelines(SortShaderMode::WaveAgnostic);
        }
    }
    else load_pipelines(SortShaderMode::WaveAgnostic);
}
void SortPass::load_pipelines(SortShaderMode mode)
{
    const char *names[]{"count", "reduce", "scan", "scan_add", "scatter"};
    std::array<ComPtr<ID3D12PipelineState>, 5> prepared;
    for (int i = 0; i < 5; ++i)
    {
        const std::string name = std::string(names[i]) +
            (mode == SortShaderMode::FixedWave32 ? "_wave32" :
             mode == SortShaderMode::WaveAgnostic ? "_wave_agnostic" : "");
        auto bytes = shader(name.c_str());
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_.Get();
        desc.CS = {bytes.data(), bytes.size()};
        check(gpu_.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&prepared[i])),
              "Sort pipeline");
    }
    pipelines_ = std::move(prepared);
    mode_ = mode;
}
void SortPass::validate()
{
    try
    {
        if (self_test()) return;
    }
    catch (const GpuFailure &failure)
    {
        if (mode_ != SortShaderMode::FixedWave32 ||
            failure.code == RenderErrorCode::GpuTimeout ||
            FAILED(gpu_.device->GetDeviceRemovedReason()))
            throw;
        wave32_fallback_hr_ = failure.hr;
    }
    if (mode_ == SortShaderMode::FixedWave32)
    {
        if (wave32_fallback_hr_ == S_OK) wave32_fallback_hr_ = E_FAIL;
        load_pipelines(SortShaderMode::WaveAgnostic);
        if (self_test()) return;
    }
    throw GpuFailure{E_FAIL,
                     mode_ == SortShaderMode::WaveAgnostic ?
                         "Wave-agnostic GPU sort self-test" : "GPU sort self-test",
                     RenderErrorCode::ShaderFailure};
}
bool SortPass::self_test()
{
    constexpr uint32_t count = 65'537;
    auto buffers = allocate(count);
    std::vector<uint32_t> keys(count), indices(count), expected(count);
    std::mt19937 rng(0x12345);
    for (uint32_t i = 0; i < count; ++i)
        keys[i] = i % 7 == 0 ? UINT32_MAX : (i % 7 == 1 ? rng() % 257 : rng());
    std::iota(indices.begin(), indices.end(), 0);
    expected = indices;
    std::stable_sort(expected.begin(), expected.end(),
                     [&](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });
    auto upload = gpu_.buffer(uint64_t(count) * 8, D3D12_HEAP_TYPE_UPLOAD,
                              D3D12_RESOURCE_STATE_GENERIC_READ);
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    check(upload->Map(0, &empty, &mapped), "Sort self-test upload map");
    memcpy(mapped, keys.data(), count * 4);
    memcpy(static_cast<uint8_t *>(mapped) + count * 4, indices.data(), count * 4);
    upload->Unmap(0, nullptr);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    check(gpu_.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&allocator)), "Sort self-test allocator");
    check(gpu_.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                         nullptr, IID_PPV_ARGS(&list)), "Sort self-test list");
    for (int i = 0; i < 2; ++i)
    {
        auto *buffer = i ? buffers.values[0].Get() : buffers.keys[0].Get();
        transition(list.Get(), buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(buffer, 0, upload.Get(), uint64_t(i) * count * 4,
                               uint64_t(count) * 4);
        transition(list.Get(), buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    record(list.Get(), buffers);
    check(list->Close(), "Sort self-test close");
    gpu_.wait(gpu_.direct_fence.Get(), gpu_.submit(gpu_.direct.Get(), list.Get(),
                                                   gpu_.direct_fence.Get(), gpu_.direct_value));
    const auto result = gpu_.readback(buffers.values[0].Get(), uint64_t(count) * 4,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const auto sorted_keys = gpu_.readback(buffers.keys[0].Get(), uint64_t(count) * 4,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t actual_index = 0, actual_key = 0;
        memcpy(&actual_index, result.data() + uint64_t(i) * 4, 4);
        memcpy(&actual_key, sorted_keys.data() + uint64_t(i) * 4, 4);
        if (actual_index != expected[i] || actual_key != keys[expected[i]]) return false;
    }
    return true;
}
SortBuffers SortPass::allocate(uint32_t count)
{
    SortBuffers b;
    b.count = count;
    uint32_t scratch = 0, reduced = 0;
    FFX_ParallelSort_CalculateScratchResourceSize(count, scratch, reduced);
    // Upstream prefetches a whole 512-key block before masking the tail.
    const uint64_t padded = (uint64_t(count) + 511) / 512 * 512;
    for (int i = 0; i < 2; ++i)
    {
        b.keys[i] =
            gpu_.buffer(padded * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        b.values[i] =
            gpu_.buffer(padded * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
    b.sums = gpu_.buffer(scratch, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    b.reduced = gpu_.buffer(reduced, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    return b;
}
void SortPass::record(ID3D12GraphicsCommandList *list, SortBuffers &b)
{
    if (!b.count)
        return;
    FFX_ParallelSortCB cb{};
    uint32_t groups = 0, reducedGroups = 0;
    FFX_ParallelSort_SetConstantAndDispatchData(b.count, 800, cb, groups, reducedGroups);
    list->SetComputeRootSignature(root_.Get());
    list->SetComputeRoot32BitConstants(0, 6, &cb, 0);
    auto bind = [&](UINT slot, ID3D12Resource *r) {
        list->SetComputeRootUnorderedAccessView(slot + 2, r->GetGPUVirtualAddress());
    };
    for (uint32_t pass = 0; pass < 8; ++pass)
    {
        int src = pass % 2, dst = 1 - src;
        list->SetComputeRoot32BitConstant(1, pass * 4, 0);
        bind(0, b.keys[src].Get());
        bind(1, b.values[src].Get());
        bind(2, b.sums.Get());
        bind(3, b.reduced.Get());
        bind(4, b.keys[dst].Get());
        bind(5, b.values[dst].Get());
        list->SetPipelineState(pipelines_[0].Get());
        list->Dispatch(groups, 1, 1);
        uav_barrier(list);
        list->SetPipelineState(pipelines_[1].Get());
        list->Dispatch(reducedGroups, 1, 1);
        uav_barrier(list);
        bind(6, b.reduced.Get());
        bind(7, b.reduced.Get());
        bind(8, b.reduced.Get());
        list->SetPipelineState(pipelines_[2].Get());
        list->Dispatch(1, 1, 1);
        uav_barrier(list);
        bind(6, b.sums.Get());
        bind(7, b.sums.Get());
        bind(8, b.reduced.Get());
        list->SetPipelineState(pipelines_[3].Get());
        list->Dispatch(reducedGroups, 1, 1);
        uav_barrier(list);
        list->SetPipelineState(pipelines_[4].Get());
        list->Dispatch(groups, 1, 1);
        uav_barrier(list);
    }
}
} // namespace gs::render::detail
