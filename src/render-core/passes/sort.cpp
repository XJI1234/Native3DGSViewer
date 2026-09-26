#include "gpu.h"
#define FFX_CPP
#include "FFX_ParallelSort.h"

namespace gs::render::detail
{
SortPass::SortPass(GpuDevice &gpu) : gpu_(gpu)
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
    const char *names[]{"count", "reduce", "scan", "scan_add", "scatter"};
    for (int i = 0; i < 5; ++i)
    {
        auto bytes = shader(names[i]);
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_.Get();
        desc.CS = {bytes.data(), bytes.size()};
        check(gpu.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelines_[i])),
              "Sort pipeline");
    }
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
