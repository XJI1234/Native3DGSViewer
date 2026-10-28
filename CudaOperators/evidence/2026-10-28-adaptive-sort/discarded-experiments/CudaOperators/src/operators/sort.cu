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
// Stable merge: one item per thread, lower/upper-bound rank in adjacent run.
// Equal keys from the left run precede equal keys from the right run.
__global__ void merge_pass(const uint32_t *ki, const uint32_t *vi, uint32_t *ko,
                          uint32_t *vo, uint32_t count, uint32_t width)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const uint32_t base = (i / (2 * width)) * (2 * width);
    const uint32_t middle = min(base + width, count), end = min(base + 2 * width, count);
    const bool left = i < middle;
    uint32_t lo = left ? middle : base, hi = left ? end : middle;
    const uint32_t start = lo, key = ki[i];
    while (lo < hi) {
        const uint32_t m = lo + (hi - lo) / 2;
        if (ki[m] < key || (!left && ki[m] == key)) lo = m + 1;
        else hi = m;
    }
    const uint32_t out = base + (i - (left ? base : middle)) + lo - start;
    ko[out] = key; vo[out] = vi[i];
}

}
struct SortWorkspace::Impl
{
    uint32_t capacity = 0;
    size_t bytes = 0;
    void *scratch = nullptr;
    cudaStream_t stream = nullptr;
    bool capture = false;
    SortOffsets offsets = SortOffsets::Int64;
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t executable = nullptr;
    std::array<void *, 4> addresses{};
    uint32_t captured_count = 0, selector = 0;
    void sort(cub::DoubleBuffer<uint32_t> &keys, cub::DoubleBuffer<uint32_t> &values,
        uint32_t count, size_t &available)
    {
        if (offsets == SortOffsets::Merge) {
            if (!scratch) { available = 1; return; }
            for (uint32_t width = 1; width < count; width *= 2) {
                merge_pass<<<(count + 255) / 256, 256, 0, stream>>>(keys.Current(), values.Current(), keys.Alternate(), values.Alternate(), count, width);
                checked(cudaGetLastError());
                keys.selector ^= 1; values.selector ^= 1;
            }
        }
        else if (offsets == SortOffsets::Int32)
            checked(cub::DeviceRadixSort::SortPairs(scratch, available, keys, values,
                int32_t(count), 0, 32, stream));
        else
            checked(cub::DeviceRadixSort::SortPairs(scratch, available, keys, values,
                int64_t(count), 0, 32, stream));
    }
    ~Impl()
    {
        if (executable) cudaGraphExecDestroy(executable);
        if (graph) cudaGraphDestroy(graph);
        if (scratch) cudaFree(scratch);
    }
};
SortWorkspace::SortWorkspace(Context &context, uint32_t capacity, bool capture, SortOffsets offsets) : impl_(std::make_unique<Impl>())
{
    if (offsets != SortOffsets::Int64 && offsets != SortOffsets::Int32 && offsets != SortOffsets::Merge) throw std::invalid_argument("Invalid CUB offset width");
    if (offsets == SortOffsets::Int32 && capacity > uint32_t(INT32_MAX)) throw std::out_of_range("CUB int32 offset capacity");
    impl_->capacity = capacity;
    impl_->capture = capture;
    impl_->offsets = offsets;
    impl_->stream = static_cast<cudaStream_t>(context.native_stream());
    if (!capacity) return;
    cub::DoubleBuffer<uint32_t> keys(nullptr, nullptr), values(nullptr, nullptr);
    impl_->sort(keys, values, capacity, impl_->bytes);
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
        impl_->sort(keys, values, count, available);
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
