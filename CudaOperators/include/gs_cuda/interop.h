#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>

#ifdef GS_CUDA_EXPORTS
#define GS_CUDA_API __declspec(dllexport)
#else
#define GS_CUDA_API __declspec(dllimport)
#endif

namespace gs::cuda_ops
{
struct ContextState;
class SharedBuffer;
class SharedTexture;

class GS_CUDA_API Context
{
  public:
    Context(ID3D12Device *device, LUID adapter);
    ~Context();
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    void begin(ID3D12CommandQueue *queue);
    void end(ID3D12CommandQueue *queue);
    bool wait_idle(uint32_t timeout_ms = 5000);
    void *native_stream() const;

  private:
    std::shared_ptr<ContextState> state_;
    friend class SharedBuffer;
    friend class SharedTexture;
};

class GS_CUDA_API SharedBuffer
{
  public:
    SharedBuffer(Context &context, uint64_t bytes);
    ~SharedBuffer();
    SharedBuffer(const SharedBuffer &) = delete;
    SharedBuffer &operator=(const SharedBuffer &) = delete;
    ID3D12Resource *resource() const;
    void *device_pointer() const;
    uint64_t bytes() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

GS_CUDA_API void fill_u32(Context &, SharedBuffer &, uint32_t value, uint32_t count);

class GS_CUDA_API SharedTexture
{
  public:
    SharedTexture(Context &, uint32_t width, uint32_t height);
    ~SharedTexture();
    SharedTexture(const SharedTexture &) = delete;
    SharedTexture &operator=(const SharedTexture &) = delete;
    ID3D12Resource *resource() const;
    uint64_t surface() const;
    uint32_t width() const;
    uint32_t height() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
GS_CUDA_API void fill_rgba(Context &, SharedTexture &, uint32_t rgba);
} // namespace gs::cuda_ops
