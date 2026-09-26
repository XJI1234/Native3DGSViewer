#pragma once
#include "contracts.h"
#include "gpu.h"
#include "layout.h"

namespace gs::render::detail
{
struct FrameConstants
{
    float view_rows[12]{};
    float camera[4]{};
    float viewport[4]{};
    float quality[4]{};
    float clip[4]{};
    uint32_t meta[4]{};
    uint32_t offsets[8]{};
};
static_assert(sizeof(FrameConstants) == 160);
struct SceneGpu
{
    SceneHandle cpu;
    UploadTicket ticket = 0;
    CameraState camera;
    ComPtr<ID3D12Resource> attributes, projected, arguments;
    SortBuffers sorting;
    std::array<uint32_t, 6> offsets{};
    bool sorted = false;
};
class SplatPass
{
  public:
    SplatPass(GpuDevice &, QualityConfig);
    SortPass &sort() { return sort_; }
    std::shared_ptr<SceneGpu> allocate(SceneHandle, UploadTicket, CameraState);
    FrameConstants constants(const SceneGpu &, Viewport) const;
    void project_sort(ID3D12GraphicsCommandList *, SceneGpu &, Viewport);
    void draw(ID3D12GraphicsCommandList *, SceneGpu &, Viewport, D3D12_CPU_DESCRIPTOR_HANDLE);

  private:
    GpuDevice &gpu_;
    QualityConfig quality_;
    SortPass sort_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> project_, reset_, draw_;
    ComPtr<ID3D12CommandSignature> indirect_;
};
} // namespace gs::render::detail
