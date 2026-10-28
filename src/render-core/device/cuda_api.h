#pragma once
#include <d3d12.h>
#include <cstdint>

// Private, versioned, exception-free boundary. No CUDA headers or link dependency.
namespace gs::render::detail
{
struct CudaSortApi
{
    uint32_t size, version;
    HRESULT (*create_device)(ID3D12Device *, void **) noexcept;
    void (*destroy_device)(void *) noexcept;
    HRESULT (*create_scene)(void *, uint32_t, void **, ID3D12Resource **, uint32_t *) noexcept;
    void (*destroy_scene)(void *) noexcept;
    HRESULT (*submit)(void *, ID3D12CommandQueue *) noexcept;
};
using GetCudaSortApi = const CudaSortApi *(__cdecl *)(uint32_t) noexcept;
}
