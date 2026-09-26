#pragma once
#include "render-core/renderer.h"
#include <array>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h>

namespace gs::render::detail
{
template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
struct GpuFailure
{
    HRESULT hr;
    const char *operation;
    RenderErrorCode code = RenderErrorCode::InternalFailure;
};
void check(HRESULT, const char *, RenderErrorCode = RenderErrorCode::InternalFailure);
void transition(ID3D12GraphicsCommandList *, ID3D12Resource *, D3D12_RESOURCE_STATES,
                D3D12_RESOURCE_STATES);
void uav_barrier(ID3D12GraphicsCommandList *);
class GpuDevice
{
  public:
    explicit GpuDevice(bool diagnostics = false, std::optional<LUID> required_adapter = {});
    ~GpuDevice();
    GpuDevice(const GpuDevice &) = delete;
    GpuDevice &operator=(const GpuDevice &) = delete;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter3> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> direct, copy;
    ComPtr<ID3D12Fence> direct_fence, copy_fence;
    uint64_t direct_value = 0, copy_value = 0;
    uint64_t timestamp_frequency = 0;
    DXGI_ADAPTER_DESC1 adapter_description{};
    uint32_t wave_min = 0, wave_max = 0;
    ComPtr<ID3D12Resource> buffer(uint64_t, D3D12_HEAP_TYPE, D3D12_RESOURCE_STATES,
                                  D3D12_RESOURCE_FLAGS = D3D12_RESOURCE_FLAG_NONE);
    uint64_t submit(ID3D12CommandQueue *, ID3D12CommandList *, ID3D12Fence *, uint64_t &);
    void wait(ID3D12Fence *, uint64_t);
    std::vector<uint8_t> readback(ID3D12Resource *, uint64_t, D3D12_RESOURCE_STATES);
    std::vector<std::string> debug_errors() const;

  private:
    HANDLE event_ = nullptr;
};
struct SortBuffers
{
    uint32_t count = 0;
    std::array<ComPtr<ID3D12Resource>, 2> keys, values;
    ComPtr<ID3D12Resource> sums, reduced;
};
class SortPass
{
  public:
    explicit SortPass(GpuDevice &);
    SortBuffers allocate(uint32_t);
    void record(ID3D12GraphicsCommandList *, SortBuffers &);

  private:
    GpuDevice &gpu_;
    ComPtr<ID3D12RootSignature> root_;
    std::array<ComPtr<ID3D12PipelineState>, 5> pipelines_;
};
std::vector<uint8_t> shader(const char *);
ComPtr<ID3D12RootSignature> root_signature(ID3D12Device *, std::span<const D3D12_ROOT_PARAMETER>);
} // namespace gs::render::detail
