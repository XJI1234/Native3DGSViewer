#include "gs_cuda/compact.h"
#include <cub/cub.cuh>
#include <cuda_runtime.h>
#include <thrust/iterator/counting_iterator.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace gs::cuda_ops
{
namespace
{
void checked(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
struct Visible
{
    const uint32_t *keys;
    __device__ bool operator()(uint32_t i) const { return keys[i] != UINT32_MAX; }
};
__global__ void gather(const uint32_t *keys, const uint32_t *values, const uint32_t *selected,
    const uint32_t *visible_count, uint32_t *compact_keys, uint32_t *compact_values, uint32_t capacity)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= capacity) return;
    if (i < *visible_count)
    {
        const auto source = selected[i];
        compact_keys[i] = keys[source]; compact_values[i] = values[source];
    }
    else { compact_keys[i] = UINT32_MAX; compact_values[i] = UINT32_MAX; }
}
__global__ void choose_bucket(const uint32_t *visible_count, cudaGraphConditionalHandle handle,
    uint32_t first, uint32_t branches)
{
    const uint32_t visible = *visible_count;
    uint32_t branch = 0;
    uint64_t bound = first;
    while (visible > bound && branch + 1 < branches) { ++branch; bound *= 2; }
    cudaGraphSetConditional(handle, branch);
}
__global__ void canonicalize(const uint32_t *source_keys, const uint32_t *source_values,
    uint32_t *target_keys, uint32_t *target_values, uint32_t capacity)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < capacity) { target_keys[i] = source_keys[i]; target_values[i] = source_values[i]; }
}
}
struct CompactWorkspace::Impl
{
    uint32_t capacity = 0;
    CompactMode mode = CompactMode::FullCapacity;
    cudaStream_t stream = nullptr;
    std::unique_ptr<SharedBuffer> count;
    std::vector<uint32_t> buckets;
    std::array<void *, 4> addresses{};
    uint32_t *selected = nullptr;
    void *scratch = nullptr;
    size_t bytes = 0;
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t executable = nullptr;
    ~Impl()
    {
        if (executable) cudaGraphExecDestroy(executable);
        if (graph) cudaGraphDestroy(graph);
        if (scratch) cudaFree(scratch);
        if (selected) cudaFree(selected);
    }
    uint32_t *counter() const { return static_cast<uint32_t *>(count->device_pointer()); }
    void select_and_gather(const std::array<uint32_t *, 4> &p)
    {
        size_t available = bytes;
        checked(cub::DeviceSelect::If(scratch, available, thrust::counting_iterator<uint32_t>(0),
            selected, counter(), int64_t(capacity), Visible{p[0]}, stream));
        gather<<<(capacity + 255) / 256, 256, 0, stream>>>(p[0], p[2], selected, counter(), p[1], p[3], capacity);
        checked(cudaGetLastError());
    }
    uint32_t sort(const std::array<uint32_t *, 4> &p, uint32_t bound)
    {
        cub::DoubleBuffer<uint32_t> keys(p[1], p[0]), values(p[3], p[2]);
        size_t available = bytes;
        checked(cub::DeviceRadixSort::SortPairs(scratch, available, keys, values, int64_t(bound), 0, 32, stream));
        if (keys.selector != values.selector) throw std::runtime_error("Compact sort selectors differ");
        return 1 - uint32_t(keys.selector);
    }
    template<class Work> void capture(cudaGraph_t target, Work work)
    {
        checked(cudaStreamBeginCaptureToGraph(stream, target, nullptr, nullptr, 0, cudaStreamCaptureModeThreadLocal));
        try { work(); }
        catch (...)
        {
            cudaGraph_t unused = nullptr;
            cudaStreamEndCapture(stream, &unused);
            throw;
        }
        cudaGraph_t captured = nullptr;
        checked(cudaStreamEndCapture(stream, &captured));
        if (captured != target) throw std::runtime_error("Unexpected compact capture graph");
    }
    void build_graph(const std::array<uint32_t *, 4> &p)
    {
        checked(cudaGraphCreate(&graph, 0));
        cudaGraphConditionalHandle handle{};
        checked(cudaGraphConditionalHandleCreate(&handle, graph, 0, cudaGraphCondAssignDefault));
        capture(graph, [&] {
            select_and_gather(p);
            choose_bucket<<<1, 1, 0, stream>>>(counter(), handle, buckets.front(), uint32_t(buckets.size()));
            checked(cudaGetLastError());
        });
        size_t nodes_count = 0;
        checked(cudaGraphGetNodes(graph, nullptr, &nodes_count));
        std::vector<cudaGraphNode_t> nodes(nodes_count), leaves;
        checked(cudaGraphGetNodes(graph, nodes.data(), &nodes_count));
        for (auto node : nodes)
        {
            size_t dependents = 0;
            checked(cudaGraphNodeGetDependentNodes(node, nullptr, nullptr, &dependents));
            if (!dependents) leaves.push_back(node);
        }
        cudaGraphNodeParams parameters{};
        parameters.type = cudaGraphNodeTypeConditional;
        parameters.conditional.handle = handle;
        parameters.conditional.type = cudaGraphCondTypeSwitch;
        parameters.conditional.size = uint32_t(buckets.size());
        cudaGraphNode_t conditional = nullptr;
        checked(cudaGraphAddNode(&conditional, graph, leaves.data(), nullptr, leaves.size(), &parameters));
        for (size_t i = 0; i < buckets.size(); ++i)
            capture(parameters.conditional.phGraph_out[i], [&] {
                const auto selector = sort(p, buckets[i]);
                if (selector) canonicalize<<<(buckets[i] + 255) / 256, 256, 0, stream>>>(p[1], p[3], p[0], p[2], buckets[i]);
                checked(cudaGetLastError());
            });
        checked(cudaGraphInstantiate(&executable, graph, 0));
        for (int i = 0; i < 4; ++i) addresses[i] = p[i];
    }
};
CompactWorkspace::CompactWorkspace(Context &context, uint32_t capacity, CompactMode mode) : impl_(std::make_unique<Impl>())
{
    auto &s = *impl_;
    s.capacity = capacity; s.mode = mode;
    s.stream = static_cast<cudaStream_t>(context.native_stream());
    s.count = std::make_unique<SharedBuffer>(context, 4);
    if (!capacity) return;
    for (uint64_t bound = (std::min)(32768u, capacity); ; bound = (std::min)(bound * 2, uint64_t(capacity)))
    {
        s.buckets.push_back(uint32_t(bound));
        if (bound == capacity) break;
    }
    size_t needed = 0;
    checked(cub::DeviceSelect::If(nullptr, needed, thrust::counting_iterator<uint32_t>(0),
        static_cast<uint32_t *>(nullptr), s.counter(), int64_t(capacity), Visible{nullptr}, s.stream));
    s.bytes = needed;
    for (auto bound : s.buckets)
    {
        cub::DoubleBuffer<uint32_t> keys(nullptr, nullptr), values(nullptr, nullptr);
        checked(cub::DeviceRadixSort::SortPairs(nullptr, needed, keys, values, int64_t(bound), 0, 32, s.stream));
        s.bytes = (std::max)(s.bytes, needed);
    }
    checked(cudaMalloc(&s.scratch, s.bytes));
    checked(cudaMalloc(reinterpret_cast<void **>(&s.selected), uint64_t(capacity) * 4));
}
CompactWorkspace::~CompactWorkspace() = default;
uint64_t CompactWorkspace::scratch_bytes() const { return impl_->bytes + uint64_t(impl_->capacity) * 4 + 65536; }
SharedBuffer &CompactWorkspace::visible_count_buffer() const { return *impl_->count; }
uint32_t CompactWorkspace::submit(Context &context, SharedBuffer &keys0, SharedBuffer &keys1,
    SharedBuffer &values0, SharedBuffer &values1, uint32_t count)
{
    auto &s = *impl_;
    if (count != s.capacity) throw std::out_of_range("Compact workspace requires its fixed source capacity");
    if (context.native_stream() != s.stream) throw std::invalid_argument("Compact context changed");
    SharedBuffer *buffers[]{&keys0, &keys1, &values0, &values1};
    std::array<uint32_t *, 4> p{};
    for (int i = 0; i < 4; ++i)
    {
        if (!context.owns(*buffers[i])) throw std::invalid_argument("Foreign compact buffer");
        if (buffers[i]->bytes() < uint64_t(count) * 4) throw std::out_of_range("Compact buffer capacity");
        for (int j = 0; j < i; ++j) if (buffers[j] == buffers[i]) throw std::invalid_argument("Aliased compact buffers");
        p[i] = static_cast<uint32_t *>(buffers[i]->device_pointer());
    }
    if (!count) { checked(cudaMemsetAsync(s.counter(), 0, 4, s.stream)); return 0; }
    if (s.mode == CompactMode::FullCapacity) { s.select_and_gather(p); return s.sort(p, count); }
    if (!s.executable) s.build_graph(p);
    for (int i = 0; i < 4; ++i)
        if (s.addresses[i] != p[i]) throw std::invalid_argument("Compact graph resource addresses changed; rebuild workspace");
    checked(cudaGraphLaunch(s.executable, s.stream));
    return 0;
}
}
