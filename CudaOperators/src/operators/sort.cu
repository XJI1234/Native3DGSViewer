#include "gs_cuda/sort.h"
#include <cub/cub.cuh>
#include <cuda_runtime.h>
#include <stdexcept>
#include <array>

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
    cudaStream_t stream = nullptr;
    bool capture = false;
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t executable = nullptr;
    std::array<void *, 4> addresses{};
    uint32_t captured_count = 0, selector = 0;
    ~Impl()
    {
        if (executable) cudaGraphExecDestroy(executable);
        if (graph) cudaGraphDestroy(graph);
        if (scratch) cudaFree(scratch);
    }
};
SortWorkspace::SortWorkspace(Context &context, uint32_t capacity, bool capture) : impl_(std::make_unique<Impl>())
{
    impl_->capacity = capacity;
    impl_->capture = capture;
    impl_->stream = static_cast<cudaStream_t>(context.native_stream());
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
    if (context.native_stream() != impl_->stream) throw std::invalid_argument("Sort context changed");
    for (auto *buffer : {&keys0, &keys1, &values0, &values1})
    {
        if (!context.owns(*buffer)) throw std::invalid_argument("Foreign sort buffer");
        if (buffer->bytes() < uint64_t(count) * 4) throw std::out_of_range("Sort buffer capacity");
    }
    if (!count)
    {
        if (impl_->executable) throw std::invalid_argument("Sort graph count changed; rebuild workspace");
        return 0;
    }
    if (&keys0 == &keys1 || &values0 == &values1 || &keys0 == &values0 || &keys1 == &values1 ||
        &keys0 == &values1 || &keys1 == &values0) throw std::invalid_argument("Aliased sort buffers");
    const std::array<void *, 4> addresses{keys0.device_pointer(), keys1.device_pointer(), values0.device_pointer(), values1.device_pointer()};
    if (impl_->executable)
    {
        if (impl_->addresses != addresses || impl_->captured_count != count) throw std::invalid_argument("Sort graph addresses/count changed; rebuild workspace");
        checked(cudaGraphLaunch(impl_->executable, impl_->stream));
        return impl_->selector;
    }
    cub::DoubleBuffer<uint32_t> keys(static_cast<uint32_t *>(keys0.device_pointer()), static_cast<uint32_t *>(keys1.device_pointer()));
    cub::DoubleBuffer<uint32_t> values(static_cast<uint32_t *>(values0.device_pointer()), static_cast<uint32_t *>(values1.device_pointer()));
    size_t available = impl_->bytes;
    if (impl_->capture) checked(cudaStreamBeginCapture(impl_->stream, cudaStreamCaptureModeThreadLocal));
    try
    {
        checked(cub::DeviceRadixSort::SortPairs(impl_->scratch, available, keys, values,
            int64_t(count), 0, 32, impl_->stream));
    }
    catch (...)
    {
        if (impl_->capture) { cudaGraph_t discarded = nullptr; cudaStreamEndCapture(impl_->stream, &discarded); if (discarded) cudaGraphDestroy(discarded); }
        throw;
    }
    if (impl_->capture)
    {
        checked(cudaStreamEndCapture(impl_->stream, &impl_->graph));
        checked(cudaGraphInstantiate(&impl_->executable, impl_->graph, 0));
        impl_->addresses = addresses; impl_->captured_count = count; impl_->selector = uint32_t(keys.selector);
        checked(cudaGraphLaunch(impl_->executable, impl_->stream));
    }
    if (keys.selector != values.selector) throw std::runtime_error("CUB selector mismatch");
    return uint32_t(keys.selector);
}
}
