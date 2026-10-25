#pragma once
#include "gpu.h"

// Bench-only RGBA surface -> native BGRA target conversion, included in timing.
class SurfaceBlit
{
    gs::render::detail::ComPtr<ID3D12RootSignature> root_;
    gs::render::detail::ComPtr<ID3D12PipelineState> pipeline_;
    gs::render::detail::ComPtr<ID3D12DescriptorHeap> heap_;
    ID3D12Resource *source_;
  public:
    SurfaceBlit(gs::render::detail::GpuDevice &gpu, ID3D12Resource *source) : source_(source)
    {
        using namespace gs::render::detail;
        D3D12_DESCRIPTOR_RANGE range{}; range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.DescriptorTable = {1, &range};
        root_ = root_signature(gpu.device.Get(), std::span(&parameter, 1));
        auto vs = shader("blit_vertex"), ps = shader("blit_pixel");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root_.Get(); desc.VS = {vs.data(), vs.size()}; desc.PS = {ps.data(), ps.size()};
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask = UINT_MAX; desc.SampleDesc.Count = 1;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE; desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1; desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        check(gpu.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_)), "Surface blit pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap_)), "Surface blit descriptors");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1; gpu.device->CreateShaderResourceView(source, &srv, heap_->GetCPUDescriptorHandleForHeapStart());
    }
    void record(ID3D12GraphicsCommandList *list, D3D12_CPU_DESCRIPTOR_HANDLE target)
    {
        using namespace gs::render::detail;
        transition(list, source_, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        auto *heap = heap_.Get(); list->SetDescriptorHeaps(1, &heap);
        list->SetGraphicsRootSignature(root_.Get()); list->SetPipelineState(pipeline_.Get());
        list->SetGraphicsRootDescriptorTable(0, heap_->GetGPUDescriptorHandleForHeapStart());
        list->OMSetRenderTargets(1, &target, FALSE, nullptr);
        const auto desc = source_->GetDesc();
        D3D12_VIEWPORT viewport{0, 0, float(desc.Width), float(desc.Height), 0, 1};
        D3D12_RECT scissor{0, 0, LONG(desc.Width), LONG(desc.Height)};
        list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list->DrawInstanced(3, 1, 0, 0);
        transition(list, source_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    }
};
