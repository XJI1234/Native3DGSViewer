#include "splat.h"
#include <algorithm>
#include <cmath>

namespace gs::render::detail
{
SplatPass::SplatPass(GpuDevice &gpu, QualityConfig quality)
    : gpu_(gpu), quality_(quality), sort_(gpu)
{
    std::array<D3D12_ROOT_PARAMETER, 9> p{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants = {0, 0, 40};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[1].Descriptor = {0, 0};
    for (UINT i = 0; i < 4; ++i)
    {
        p[2 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[2 + i].Descriptor = {i, 0};
    }
    p[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[6].Descriptor = {1, 0};
    p[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[7].Descriptor = {2, 0};
    p[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[8].Descriptor = {3, 0};
    root_ = root_signature(gpu.device.Get(), p);
    for (auto pair : {std::pair{"project", &project_}, std::pair{"reset_args", &reset_}})
    {
        auto bytes = shader(pair.first);
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
        d.pRootSignature = root_.Get();
        d.CS = {bytes.data(), bytes.size()};
        check(gpu.device->CreateComputePipelineState(&d, IID_PPV_ARGS(pair.second)),
              "Projection pipeline");
    }
    auto vs = shader("vertex"), ps = shader("pixel");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root_.Get();
    d.VS = {vs.data(), vs.size()};
    d.PS = {ps.data(), ps.size()};
    d.BlendState.RenderTarget[0].BlendEnable = TRUE;
    d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    d.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    d.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    d.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    d.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthEnable = FALSE;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    check(gpu.device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&draw_)), "Splat draw pipeline");
    D3D12_INDIRECT_ARGUMENT_DESC arg{};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC cmd{};
    cmd.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    cmd.NumArgumentDescs = 1;
    cmd.pArgumentDescs = &arg;
    check(gpu.device->CreateCommandSignature(&cmd, nullptr, IID_PPV_ARGS(&indirect_)),
          "Draw signature");
}
std::shared_ptr<SceneGpu> SplatPass::allocate(SceneHandle scene, UploadTicket ticket,
                                              CameraState camera)
{
    auto s = std::make_shared<SceneGpu>();
    s->cpu = std::move(scene);
    s->ticket = ticket;
    s->camera = camera;
    const uint64_t n = s->cpu->count;
    s->attributes = gpu_.buffer(n * 56, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    s->attributes->SetName(L"Scene attributes");
    const uint64_t sh_bytes = scene_bytes(*s->cpu) - n * 56;
    if (sh_bytes)
    {
        s->sh_attributes = gpu_.buffer(sh_bytes, D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_STATE_COMMON);
        s->sh_attributes->SetName(L"Scene SH attributes");
    }
    s->offsets = {0,
                  uint32_t(n * 12),
                  uint32_t(n * 24),
                  uint32_t(n * 40),
                  uint32_t(n * 44), 0};
    s->projected = gpu_.buffer(n * sizeof(ProjectedEllipse), D3D12_HEAP_TYPE_DEFAULT,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    s->projected->SetName(L"Projected ellipses");
    s->arguments = gpu_.buffer(sizeof(DrawCounters), D3D12_HEAP_TYPE_DEFAULT,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    s->arguments->SetName(L"Draw arguments");
    s->sorting = sort_.allocate(uint32_t(n));
    return s;
}
FrameConstants SplatPass::constants(const SceneGpu &s, Viewport viewport) const
{
    FrameConstants c;
    auto rows = world_to_camera(s.camera.orientation_xyzw);
    auto camera = relative_camera(*s.cpu, s.camera);
    for (int r = 0; r < 3; ++r)
        for (int j = 0; j < 3; ++j)
            c.view_rows[r * 4 + j] = rows[r * 3 + j];
    std::copy(camera.begin(), camera.end(), c.camera);
    double f = viewport.physical_height / (2 * std::tan(s.camera.vertical_fov_radians / 2));
    c.viewport[0] = c.viewport[1] = float(f);
    c.viewport[2] = float(viewport.physical_width);
    c.viewport[3] = float(viewport.physical_height);
    c.quality[0] = quality_.max_stddev;
    c.quality[1] = quality_.min_alpha;
    c.quality[2] = quality_.covariance_blur_px2;
    c.quality[3] = quality_.max_pixel_radius_px;
    c.clip[0] = float(s.camera.near_plane);
    c.clip[1] = float(s.camera.far_plane);
    c.meta[0] = uint32_t(s.cpu->count);
    c.meta[1] = (std::min)(s.cpu->shDegree, quality_.sh_degree_cap);
    c.meta[2] = 3 * ((s.cpu->shDegree + 1) * (s.cpu->shDegree + 1) - 1);
    c.meta[3] = uint32_t(quality_.sort_mode);
    std::copy(s.offsets.begin(), s.offsets.end(), c.offsets);
    return c;
}
void SplatPass::project_sort(ID3D12GraphicsCommandList *list, SceneGpu &s, Viewport viewport)
{
    auto c = constants(s, viewport);
    list->SetComputeRootSignature(root_.Get());
    list->SetComputeRoot32BitConstants(0, 40, &c, 0);
    list->SetComputeRootShaderResourceView(1, s.attributes->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(8, s.sh_attributes ? s.sh_attributes->GetGPUVirtualAddress()
                                                           : s.attributes->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(2, s.projected->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(3, s.sorting.keys[0]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(4, s.sorting.values[0]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(5, s.arguments->GetGPUVirtualAddress());
    list->SetPipelineState(reset_.Get());
    list->Dispatch(1, 1, 1);
    uav_barrier(list);
    list->SetPipelineState(project_.Get());
    const uint32_t groups = (c.meta[0] + 255) / 256;
    list->Dispatch((std::min)(groups, 65535u), (groups + 65534) / 65535, 1);
    uav_barrier(list);
    sort_.record(list, s.sorting);
    s.sorted = true;
}
void SplatPass::draw(ID3D12GraphicsCommandList *list, SceneGpu &s, Viewport viewport,
                     D3D12_CPU_DESCRIPTOR_HANDLE target)
{
    auto c = constants(s, viewport);
    transition(list, s.projected.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(list, s.sorting.values[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(list, s.arguments.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    list->SetGraphicsRootSignature(root_.Get());
    list->SetGraphicsRoot32BitConstants(0, 40, &c, 0);
    list->SetGraphicsRootShaderResourceView(1, s.attributes->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(8, s.sh_attributes ? s.sh_attributes->GetGPUVirtualAddress()
                                                           : s.attributes->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(6, s.projected->GetGPUVirtualAddress());
    list->SetGraphicsRootShaderResourceView(7, s.sorting.values[0]->GetGPUVirtualAddress());
    list->SetPipelineState(draw_.Get());
    list->OMSetRenderTargets(1, &target, FALSE, nullptr);
    D3D12_VIEWPORT vp{0, 0, float(viewport.physical_width), float(viewport.physical_height), 0, 1};
    D3D12_RECT rect{0, 0, LONG(viewport.physical_width), LONG(viewport.physical_height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &rect);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->ExecuteIndirect(indirect_.Get(), 1, s.arguments.Get(), 0, nullptr, 0);
    transition(list, s.projected.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(list, s.sorting.values[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(list, s.arguments.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
} // namespace gs::render::detail
