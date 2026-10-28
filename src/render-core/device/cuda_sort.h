#pragma once
#include "gpu.h"
#include "cuda_api.h"
#include <memory>

namespace gs::render::detail
{
// Conservative boundary, measured on RTX 3080; not an architectural CUDA limit.
inline constexpr uint32_t cuda_sort_threshold = 2000000;
constexpr bool use_cuda_sort(bool available, uint32_t count)
{
    return available && count >= cuda_sort_threshold;
}
class CudaSortDevice;
class CudaSortScene
{
  public:
    ~CudaSortScene();
    void barriers(ID3D12GraphicsCommandList *, bool release);
    bool submit(ID3D12CommandQueue *);
    uint32_t selector = 0;
    bool initialized = false;
    std::array<ComPtr<ID3D12Resource>, 4> buffers;

  private:
    friend class CudaSortDevice;
    std::shared_ptr<CudaSortDevice> device_;
    void *scene_ = nullptr;
};
class CudaSortDevice : public std::enable_shared_from_this<CudaSortDevice>
{
  public:
    static std::shared_ptr<CudaSortDevice> probe(ID3D12Device *);
    ~CudaSortDevice();
    std::shared_ptr<CudaSortScene> prepare(GpuDevice &, SortBuffers &);

  private:
    friend class CudaSortScene;
    HMODULE module_ = nullptr;
    const CudaSortApi *api_ = nullptr;
    void *device_ = nullptr;
    bool failed_ = false;
};
}
