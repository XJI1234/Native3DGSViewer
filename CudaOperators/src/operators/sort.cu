#include "gs_cuda/sort.h"
#include <cub/cub.cuh>
#include <cuda_runtime.h>
#include <stdexcept>

namespace gs::cuda_ops
{
namespace
{
void checked(cudaError_t result)
{
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
}
struct SortWorkspace::Impl
{
    uint32_t capacity = 0;
    size_t bytes = 0;
    void *scratch = nullptr;
    ~Impl() { if (scratch) cudaFree(scratch); }
};
SortWorkspace::SortWorkspace(Context &context, uint32_t capacity) : impl_(std::make_unique<Impl>())
{
    impl_->capacity = capacity;
    if (!capacity) return;
    cub::DoubleBuffer<uint32_t> keys(nullptr, nullptr), values(nullptr, nullptr);
    checked(cub::DeviceRadixSort::SortPairs(nullptr, impl_->bytes, keys, values,
        int64_t(capacity), 0, 32, static_cast<cudaStream_t>(context.native_stream())));
    checked(cudaMalloc(&impl_->scratch, impl_->bytes));
}
SortWorkspace::~SortWorkspace() = default;
uint64_t SortWorkspace::scratch_bytes() const { return impl_->bytes; }
uint32_t SortWorkspace::submit(Context &context, SharedBuffer &keys0, SharedBuffer &keys1,
    SharedBuffer &values0, SharedBuffer &values1, uint32_t count)
{
    if (count > impl_->capacity) throw std::out_of_range("Sort workspace capacity");
    for (auto *buffer : {&keys0, &keys1, &values0, &values1})
        if (buffer->bytes() < uint64_t(count) * 4) throw std::out_of_range("Sort buffer capacity");
    if (!count) return 0;
    if (&keys0 == &keys1 || &values0 == &values1 || &keys0 == &values0 || &keys1 == &values1 ||
        &keys0 == &values1 || &keys1 == &values0) throw std::invalid_argument("Aliased sort buffers");
    cub::DoubleBuffer<uint32_t> keys(static_cast<uint32_t *>(keys0.device_pointer()), static_cast<uint32_t *>(keys1.device_pointer()));
    cub::DoubleBuffer<uint32_t> values(static_cast<uint32_t *>(values0.device_pointer()), static_cast<uint32_t *>(values1.device_pointer()));
    size_t available = impl_->bytes;
    checked(cub::DeviceRadixSort::SortPairs(impl_->scratch, available, keys, values,
        int64_t(count), 0, 32, static_cast<cudaStream_t>(context.native_stream())));
    if (keys.selector != values.selector) throw std::runtime_error("CUB selector mismatch");
    return uint32_t(keys.selector);
}
}
