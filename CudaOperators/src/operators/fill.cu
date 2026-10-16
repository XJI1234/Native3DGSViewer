#include "gs_cuda/interop.h"
#include <cuda_runtime.h>
#include <stdexcept>

namespace gs::cuda_ops
{
__global__ void fill_kernel(uint32_t *buffer, uint32_t value, uint32_t count)
{
    const auto i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) buffer[i] = value;
}
void fill_u32(Context &context, SharedBuffer &buffer, uint32_t value, uint32_t count)
{
    if (!context.owns(buffer)) throw std::invalid_argument("Foreign fill buffer");
    if (uint64_t(count) * sizeof(uint32_t) > buffer.bytes()) throw std::out_of_range("Fill capacity");
    if (!count) return;
    fill_kernel<<<uint32_t((uint64_t(count) + 255u) / 256u), 256, 0, static_cast<cudaStream_t>(context.native_stream())>>>(
        static_cast<uint32_t *>(buffer.device_pointer()), value, count);
    const auto result = cudaGetLastError();
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}

__global__ void fill_surface(cudaSurfaceObject_t surface, uint32_t rgba, uint32_t width, uint32_t height)
{
    const uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < width && y < height) surf2Dwrite(rgba, surface, x * 4, y);
}
void fill_rgba(Context &context, SharedTexture &texture, uint32_t rgba)
{
    if (!context.owns(texture)) throw std::invalid_argument("Foreign fill texture");
    fill_surface<<<dim3((texture.width() + 15) / 16, (texture.height() + 15) / 16),
        dim3(16, 16), 0, static_cast<cudaStream_t>(context.native_stream())>>>(
            texture.surface(), rgba, texture.width(), texture.height());
    const auto result = cudaGetLastError();
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
}
