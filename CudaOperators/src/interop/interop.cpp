#include "gs_cuda/interop.h"
#include <cuda_runtime_api.h>
#include <wrl/client.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace gs::cuda_ops
{
namespace
{
void checked(cudaError_t result)
{
    if (result != cudaSuccess)
        throw std::runtime_error(cudaGetErrorString(result));
}
void checked(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("D3D12 CUDA interop HRESULT=" + std::to_string(result));
}
struct Handle
{
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};
bool wait_stream(cudaStream_t stream, uint32_t timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;)
    {
        const auto result = cudaStreamQuery(stream);
        if (result == cudaSuccess) return true;
        checked(result == cudaErrorNotReady ? cudaSuccess : result);
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}

struct ContextState
{
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    cudaStream_t stream = nullptr;
    cudaExternalSemaphore_t semaphore = nullptr;
    uint64_t value = 0;
    bool active = false;
    ~ContextState()
    {
        // Never block indefinitely during device failure. An unfinished import is
        // retained by CUDA; normal owners call wait_idle before releasing buffers.
        try
        {
            if (stream && !wait_stream(stream, 5000)) return;
            if (semaphore) cudaDestroyExternalSemaphore(semaphore);
            if (stream) cudaStreamDestroy(stream);
        }
        catch (...) { OutputDebugStringA("CUDA interop shutdown failed; imports retained\n"); }
    }
};

Context::Context(ID3D12Device *device, LUID adapter) : state_(std::make_shared<ContextState>())
{
    if (!device) throw std::invalid_argument("Null D3D12 device");
    int count = 0, selected = -1;
    checked(cudaGetDeviceCount(&count));
    for (int i = 0; i < count; ++i)
    {
        cudaDeviceProp properties{};
        checked(cudaGetDeviceProperties(&properties, i));
        if (properties.luidDeviceNodeMask && std::memcmp(properties.luid, &adapter, sizeof(adapter)) == 0)
        { selected = i; break; }
    }
    if (selected < 0) throw std::runtime_error("No CUDA device matches D3D12 adapter LUID");
    checked(cudaSetDevice(selected));
    state_->device = device;
    checked(cudaStreamCreateWithFlags(&state_->stream, cudaStreamNonBlocking));
    checked(device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&state_->fence)));
    Handle handle;
    checked(device->CreateSharedHandle(state_->fence.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value));
    cudaExternalSemaphoreHandleDesc desc{};
    desc.type = cudaExternalSemaphoreHandleTypeD3D12Fence;
    desc.handle.win32.handle = handle.value;
    checked(cudaImportExternalSemaphore(&state_->semaphore, &desc));
}
Context::~Context() = default;
void *Context::native_stream() const { return state_->stream; }
bool Context::wait_idle(uint32_t timeout_ms) { return wait_stream(state_->stream, timeout_ms); }
void Context::begin(ID3D12CommandQueue *queue)
{
    if (!queue || state_->active) throw std::logic_error("Invalid interop begin");
    const auto value = ++state_->value;
    checked(queue->Signal(state_->fence.Get(), value));
    cudaExternalSemaphoreWaitParams params{};
    params.params.fence.value = value;
    checked(cudaWaitExternalSemaphoresAsync(&state_->semaphore, &params, 1, state_->stream));
    state_->active = true;
}
void Context::end(ID3D12CommandQueue *queue)
{
    if (!queue || !state_->active) throw std::logic_error("Invalid interop end");
    cudaExternalSemaphoreSignalParams params{};
    params.params.fence.value = ++state_->value;
    checked(cudaSignalExternalSemaphoresAsync(&state_->semaphore, &params, 1, state_->stream));
    checked(queue->Wait(state_->fence.Get(), state_->value));
    state_->active = false;
}

struct SharedBuffer::Impl
{
    std::shared_ptr<ContextState> context;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    cudaExternalMemory_t memory = nullptr;
    void *pointer = nullptr;
    uint64_t size = 0;
    ~Impl()
    {
        if (pointer) cudaFree(pointer);
        if (memory) cudaDestroyExternalMemory(memory);
    }
};
SharedBuffer::SharedBuffer(Context &context, uint64_t bytes) : impl_(std::make_unique<Impl>())
{
    if (!bytes || bytes > (uint64_t{1} << 34)) throw std::invalid_argument("Invalid shared buffer size");
    impl_->context = context.state_;
    impl_->size = bytes;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = bytes;
    resource.Height = resource.DepthOrArraySize = resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto *device = context.state_->device.Get();
    checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &resource,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&impl_->resource)));
    const auto allocation = device->GetResourceAllocationInfo(0, 1, &resource);
    if (allocation.SizeInBytes == UINT64_MAX) throw std::runtime_error("Invalid shared resource allocation");
    Handle handle;
    checked(device->CreateSharedHandle(impl_->resource.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value));
    cudaExternalMemoryHandleDesc desc{};
    desc.type = cudaExternalMemoryHandleTypeD3D12Resource;
    desc.handle.win32.handle = handle.value;
    desc.size = allocation.SizeInBytes;
    desc.flags = cudaExternalMemoryDedicated;
    checked(cudaImportExternalMemory(&impl_->memory, &desc));
    cudaExternalMemoryBufferDesc mapping{};
    mapping.size = bytes;
    checked(cudaExternalMemoryGetMappedBuffer(&impl_->pointer, impl_->memory, &mapping));
}
SharedBuffer::~SharedBuffer()
{
    if (!impl_) return;
    try
    {
        if (!wait_stream(impl_->context->stream, 5000))
        {
            OutputDebugStringA("CUDA timeout: retaining in-flight shared allocation\n");
            impl_.release();
        }
    }
    catch (...) { impl_.release(); }
}
ID3D12Resource *SharedBuffer::resource() const { return impl_->resource.Get(); }
void *SharedBuffer::device_pointer() const { return impl_->pointer; }
uint64_t SharedBuffer::bytes() const { return impl_->size; }

struct SharedTexture::Impl
{
    std::shared_ptr<ContextState> context;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    cudaExternalMemory_t memory = nullptr;
    cudaMipmappedArray_t mipmap = nullptr;
    cudaSurfaceObject_t surface = 0;
    uint32_t width = 0, height = 0;
    ~Impl()
    {
        if (surface) cudaDestroySurfaceObject(surface);
        if (mipmap) cudaFreeMipmappedArray(mipmap);
        if (memory) cudaDestroyExternalMemory(memory);
    }
};
SharedTexture::SharedTexture(Context &context, uint32_t width, uint32_t height)
    : impl_(std::make_unique<Impl>())
{
    if (!width || !height || width > 16384 || height > 16384)
        throw std::invalid_argument("Invalid shared texture dimensions");
    impl_->context = context.state_;
    impl_->width = width;
    impl_->height = height;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource.Width = width;
    resource.Height = height;
    resource.DepthOrArraySize = resource.MipLevels = 1;
    resource.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource.SampleDesc.Count = 1;
    resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto *device = context.state_->device.Get();
    checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &resource,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&impl_->resource)));
    const auto allocation = device->GetResourceAllocationInfo(0, 1, &resource);
    Handle handle;
    checked(device->CreateSharedHandle(impl_->resource.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value));
    cudaExternalMemoryHandleDesc desc{};
    desc.type = cudaExternalMemoryHandleTypeD3D12Resource;
    desc.handle.win32.handle = handle.value;
    desc.size = allocation.SizeInBytes;
    desc.flags = cudaExternalMemoryDedicated;
    checked(cudaImportExternalMemory(&impl_->memory, &desc));
    cudaExternalMemoryMipmappedArrayDesc mapping{};
    mapping.extent = {width, height, 0};
    mapping.formatDesc = {8, 8, 8, 8, cudaChannelFormatKindUnsigned};
    mapping.flags = cudaArraySurfaceLoadStore;
    mapping.numLevels = 1;
    checked(cudaExternalMemoryGetMappedMipmappedArray(&impl_->mipmap, impl_->memory, &mapping));
    cudaResourceDesc surface{};
    surface.resType = cudaResourceTypeArray;
    checked(cudaGetMipmappedArrayLevel(&surface.res.array.array, impl_->mipmap, 0));
    checked(cudaCreateSurfaceObject(&impl_->surface, &surface));
}
SharedTexture::~SharedTexture()
{
    try
    {
        if (!wait_stream(impl_->context->stream, 5000)) impl_.release();
    }
    catch (...) { impl_.release(); }
}
ID3D12Resource *SharedTexture::resource() const { return impl_->resource.Get(); }
uint64_t SharedTexture::surface() const { return impl_->surface; }
uint32_t SharedTexture::width() const { return impl_->width; }
uint32_t SharedTexture::height() const { return impl_->height; }
} // namespace gs::cuda_ops
