#pragma once
#include "gs_cuda/tile.h"
#include "gpu.h"

class HlslTilePass
{
    using Gpu = gs::render::detail::GpuDevice;
    template<class T> using ComPtr = gs::render::detail::ComPtr<T>;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> init_, composite_, store_;
    ComPtr<ID3D12DescriptorHeap> heap_;
    ComPtr<ID3D12Resource> pixels_, commands_, counts_;
    ComPtr<ID3D12CommandSignature> indirect_;
    gs::cuda_ops::TileConfig config_;
    ID3D12Resource *output_;
  public:
    static uint64_t required_bytes(const gs::cuda_ops::TileConfig &c)
    {
        return uint64_t(c.width) * c.height * 16 + ((uint64_t(c.source_count) + 511) / 512) * 16 + (3ull << 16);
    }
    HlslTilePass(Gpu &gpu, gs::cuda_ops::TileConfig config, ID3D12Resource *output) : config_(config), output_(output)
    {
        using namespace gs::render::detail;
        std::array<D3D12_ROOT_PARAMETER, 10> p{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p[0].Constants = {0, 0, 8};
        for (uint32_t i = 0; i < 4; ++i) { p[i+1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; p[i+1].Descriptor = {i, 0}; }
        p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; p[5].Descriptor = {5, 0};
        p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; p[6].Descriptor = {0, 0};
        D3D12_DESCRIPTOR_RANGE range{}; range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; range.NumDescriptors = 1; range.BaseShaderRegister = 1;
        p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[7].DescriptorTable = {1, &range};
        p[8].ParameterType = p[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[8].Descriptor = {2, 0}; p[9].Descriptor = {3, 0};
        root_ = root_signature(gpu.device.Get(), p);
        auto pipeline = [&](const char *entry) {
            auto bytes = shader(entry); D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = root_.Get(); desc.CS = {bytes.data(), bytes.size()}; ComPtr<ID3D12PipelineState> result;
            check(gpu.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&result)), "HLSL tile pipeline"); return result;
        };
        init_ = pipeline("tile_init"); store_ = pipeline("tile_store");
        composite_ = pipeline(config.tile_size == 8 ? "tile_composite_8" : "tile_composite_16");
        const uint64_t chunks = (uint64_t(config.source_count) + 511) / 512;
        pixels_ = gpu.buffer(uint64_t(config.width) * config.height * 16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        commands_ = gpu.buffer((std::max)(chunks * 12, uint64_t{12}), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        counts_ = gpu.buffer((std::max)(chunks * 4, uint64_t{4}), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 1; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_)), "HLSL tile descriptors");
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        gpu.device->CreateUnorderedAccessView(output, nullptr, &uav, heap_->GetCPUDescriptorHandleForHeapStart());
        D3D12_INDIRECT_ARGUMENT_DESC argument{}; argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC desc{}; desc.ByteStride = 12; desc.NumArgumentDescs = 1; desc.pArgumentDescs = &argument;
        check(gpu.device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&indirect_)), "HLSL tile indirect signature");
    }
    void record(ID3D12GraphicsCommandList *list, gs::cuda_ops::TileWorkspace &tiles, gs::cuda_ops::SharedBuffer &ellipses,
        gs::cuda_ops::SharedBuffer &order, gs::cuda_ops::TileCompositeOptions options)
    {
        using namespace gs::render::detail;
        auto *heap = heap_.Get(); list->SetDescriptorHeaps(1, &heap); list->SetComputeRootSignature(root_.Get());
        struct Constants { uint32_t width, height, tile, chunk; float stddev, cutoff; uint32_t quantize, count; };
        const Constants constants{config_.width, config_.height, config_.tile_size, 0, options.stddev, options.alpha_cutoff, uint32_t(options.quantize_each_splat), config_.source_count};
        list->SetComputeRoot32BitConstants(0, 8, &constants, 0);
        ID3D12Resource *sources[]{ellipses.resource(), order.resource(), tiles.keys().resource(), tiles.ranges().resource()};
        for (uint32_t i = 0; i < 4; ++i)
        {
            transition(list, sources[i], i < 2 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            list->SetComputeRootShaderResourceView(i+1, sources[i]->GetGPUVirtualAddress());
        }
        transition(list, tiles.status().resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(list, output_, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->SetComputeRootUnorderedAccessView(5, tiles.status().resource()->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(6, pixels_->GetGPUVirtualAddress());
        list->SetComputeRootDescriptorTable(7, heap_->GetGPUDescriptorHandleForHeapStart());
        list->SetComputeRootUnorderedAccessView(8, commands_->GetGPUVirtualAddress());
        list->SetComputeRootUnorderedAccessView(9, counts_->GetGPUVirtualAddress());
        list->SetPipelineState(init_.Get());
        const uint32_t chunks = uint32_t((uint64_t(config_.source_count) + 511) / 512);
        list->Dispatch(((std::max)(config_.width * config_.height, chunks) + 255) / 256, 1, 1); uav_barrier(list);
        transition(list, commands_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transition(list, counts_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        list->SetPipelineState(composite_.Get());
        for (uint64_t offset = 0, i = 0; offset < config_.source_count; offset += 512, ++i)
        {
            list->SetComputeRoot32BitConstant(0, uint32_t(offset), 3);
            list->ExecuteIndirect(indirect_.Get(), 1, commands_.Get(), i*12, counts_.Get(), i*4);
            uav_barrier(list); // ExecuteIndirect cannot encode this dependency internally.
        }
        list->SetPipelineState(store_.Get()); list->Dispatch((config_.width * config_.height + 255) / 256, 1, 1); uav_barrier(list);
        transition(list, commands_.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(list, counts_.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(list, output_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        transition(list, tiles.status().resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        for (uint32_t i = 0; i < 4; ++i) transition(list, sources[i], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            i < 2 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON);
    }
};
