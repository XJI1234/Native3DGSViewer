#pragma once
#include "splat.h"
#include "render-core/picking.h"

namespace gs::render::detail
{
inline constexpr uint32_t pick_capacity = 16384;
struct PickCandidate
{
    float depth = 0, alpha = 0;
    uint32_t rank = 0, source_index = 0;
};
static_assert(sizeof(PickCandidate) == 16);
PickResult resolve_pick(PickRequest, std::vector<PickCandidate>);
// Render-thread owned. Fixed output/readback buffers are reused only after fence completion.
class PickPass
{
  public:
    explicit PickPass(GpuDevice &);
    void record(ID3D12GraphicsCommandList *, const SceneGpu &, const FrameConstants &,
                const PickRequest &);
    PickResult read(const PickRequest &);

  private:
    GpuDevice &gpu_;
    ComPtr<ID3D12RootSignature> root_;
    ComPtr<ID3D12PipelineState> pipeline_, reset_;
    ComPtr<ID3D12Resource> output_, readback_;
};
} // namespace gs::render::detail
