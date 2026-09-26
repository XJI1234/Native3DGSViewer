#pragma once
#include "splat.h"
#include <chrono>

namespace gs::render::detail
{
struct UploadProgress
{
    uint64_t completed, total;
};
class UploadTransaction
{
  public:
    UploadTransaction(GpuDevice &, SplatPass &, SceneHandle, UploadTicket, CameraState);
    bool copy_complete(uint64_t completed_value) const;
    std::optional<UploadProgress> advance(GpuDevice &);
    std::shared_ptr<SceneGpu> scene;
    uint64_t fence = 0;
    bool ready = false;
    std::chrono::steady_clock::time_point deadline{};

  private:
    ComPtr<ID3D12Resource> page_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> list_;
    uint64_t submitted_ = 0, completed_ = 0, page_bytes_ = 0;
};
} // namespace gs::render::detail
