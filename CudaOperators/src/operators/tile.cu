#include "gs_cuda/tile.h"
#include <cub/cub.cuh>
#include <cuda_runtime.h>
#include <algorithm>
#include <stdexcept>

namespace gs::cuda_ops
{
namespace
{
using render::detail::ProjectedEllipse;
using render::detail::DrawCounters;
void checked(cudaError_t error) { if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error)); }
uint64_t add(uint64_t a, uint64_t b)
{
    if (a > UINT64_MAX - b) throw std::out_of_range("Tile byte arithmetic overflow");
    return a + b;
}
uint64_t aligned(uint64_t bytes) { return add((std::max)(bytes, uint64_t{1}), 65535) / 65536 * 65536; }
struct Sizing { uint32_t columns, rows; size_t scratch; uint64_t bytes; };
Sizing sizing(cudaStream_t stream, const TileConfig &c)
{
    if (!c.width || !c.height || c.width > 16384 || c.height > 16384 ||
        (c.tile_size != 8 && c.tile_size != 16) || !c.reference_capacity || c.reference_capacity > INT32_MAX)
        throw std::invalid_argument("Tile dimensions, size or reference capacity unsupported");
    Sizing s{(c.width + c.tile_size - 1) / c.tile_size, (c.height + c.tile_size - 1) / c.tile_size, 0, 0};
    size_t scan = 0, sort = 0;
    if (c.source_count) checked(cub::DeviceScan::ExclusiveSum(nullptr, scan,
        static_cast<uint64_t *>(nullptr), static_cast<uint64_t *>(nullptr), int64_t(c.source_count), stream));
    cub::DoubleBuffer<uint64_t> keys(nullptr, nullptr);
    checked(cub::DeviceRadixSort::SortKeys(nullptr, sort, keys, int64_t(c.reference_capacity), 0, 64, stream));
    s.scratch = (std::max)(scan, sort);
    s.bytes = add(uint64_t(c.source_count) * 16, s.scratch);
    s.bytes = add(s.bytes, aligned(uint64_t(c.reference_capacity) * 8) * 2);
    s.bytes = add(s.bytes, aligned(uint64_t(s.columns) * s.rows * sizeof(TileRange)));
    s.bytes = add(s.bytes, aligned(sizeof(TileStatus)));
    return s;
}
struct Bounds { uint32_t x0, y0, x1, y1; bool active; };
__device__ Bounds bounds(const ProjectedEllipse &e, TileConfig c, TileStatus *status)
{
    const float *fields = reinterpret_cast<const float *>(&e);
    for (int i = 0; i < 10; ++i) if (!isfinite(fields[i])) { atomicOr(&status->flags, 1u); return {}; }
    if (e.color[3] < 0 || e.color[3] > 1) { atomicOr(&status->flags, 1u); return {}; }
    if (e.color[3] == 0) return {};
    const float ex = fabsf(e.axis0[0]) + fabsf(e.axis1[0]), ey = fabsf(e.axis0[1]) + fabsf(e.axis1[1]);
    if (!isfinite(ex) || !isfinite(ey) || ex <= 0 || ey <= 0) { atomicOr(&status->flags, 1u); return {}; }
    const float left = e.center[0] - ex, right = e.center[0] + ex;
    const float top = e.center[1] - ey, bottom = e.center[1] + ey;
    if (right < 0 || bottom < 0 || left >= c.width || top >= c.height) return {};
    const uint32_t columns = (c.width + c.tile_size - 1) / c.tile_size, rows = (c.height + c.tile_size - 1) / c.tile_size;
    return {uint32_t(fmaxf(left, 0)) / c.tile_size, uint32_t(fmaxf(top, 0)) / c.tile_size,
        min(uint32_t(fminf(right, float(c.width))) / c.tile_size, columns - 1),
        min(uint32_t(fminf(bottom, float(c.height))) / c.tile_size, rows - 1), true};
}
__global__ void count_references(const ProjectedEllipse *ellipses, const uint32_t *order, const DrawCounters *args,
    uint64_t *counts, TileStatus *status, TileConfig c)
{
    const uint64_t rank = uint64_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (rank >= c.source_count) return;
    counts[rank] = 0;
    if (args->instance_count > c.source_count) { atomicOr(&status->flags, 1u); return; }
    if (rank >= args->instance_count) return;
    const auto index = order[rank];
    if (index >= c.source_count) { atomicOr(&status->flags, 1u); return; }
    const auto b = bounds(ellipses[index], c, status);
    if (b.active) counts[rank] = uint64_t(b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1);
}
__global__ void check_total(const uint64_t *counts, const uint64_t *prefix, const DrawCounters *args,
    TileStatus *status, TileConfig c)
{
    const uint64_t total = c.source_count ? prefix[c.source_count - 1] + counts[c.source_count - 1] : 0;
    status->references = total;
    if (args->instance_count > c.source_count) status->flags |= 1;
    if (total > c.reference_capacity) status->flags |= 2;
}
__global__ void emit_references(const ProjectedEllipse *ellipses, const uint32_t *order, const uint64_t *counts,
    const uint64_t *prefix, uint64_t *keys, TileStatus *status, TileConfig c)
{
    const uint64_t rank = uint64_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (rank >= c.source_count || status->flags || !counts[rank]) return;
    const auto b = bounds(ellipses[order[rank]], c, status);
    const uint32_t columns = (c.width + c.tile_size - 1) / c.tile_size;
    uint64_t offset = prefix[rank];
    for (uint32_t y = b.y0; y <= b.y1; ++y) for (uint32_t x = b.x0; x <= b.x1; ++x)
        keys[offset++] = (uint64_t(y * columns + x) << 32) | rank;
}
__global__ void find_ranges(const uint64_t *keys, TileRange *ranges, const TileStatus *status, uint32_t capacity)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= capacity || status->flags || keys[i] == UINT64_MAX) return;
    const auto tile = uint32_t(keys[i] >> 32);
    if (!i || uint32_t(keys[i - 1] >> 32) != tile) ranges[tile].begin = i;
    if (i + 1 == capacity || uint32_t(keys[i + 1] >> 32) != tile) ranges[tile].end = i + 1;
}
__global__ void maximum_range(const TileRange *ranges, TileStatus *status, uint32_t tiles)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < tiles) atomicMax(&status->max_tile_length, ranges[i].end - ranges[i].begin);
}
}
struct TileWorkspace::Impl
{
    TileConfig config{};
    Sizing size{};
    cudaStream_t stream = nullptr;
    uint64_t *counts = nullptr, *prefix = nullptr;
    void *scratch = nullptr;
    std::unique_ptr<SharedBuffer> key_buffers[2], tile_ranges, tile_status;
    uint32_t selected = 0;
    ~Impl()
    {
        if (scratch) cudaFree(scratch);
        if (prefix) cudaFree(prefix);
        if (counts) cudaFree(counts);
    }
};
uint64_t TileWorkspace::required_bytes(Context &context, const TileConfig &c)
{
    return sizing(static_cast<cudaStream_t>(context.native_stream()), c).bytes;
}
TileWorkspace::TileWorkspace(Context &context, const TileConfig &c) : impl_(std::make_unique<Impl>())
{
    auto &s = *impl_; s.config = c; s.stream = static_cast<cudaStream_t>(context.native_stream()); s.size = sizing(s.stream, c);
    if (s.size.bytes > c.budget_bytes) throw std::out_of_range("Tile allocation exceeds explicit budget");
    // All scratch queries and budget checks precede the first allocation.
    if (c.source_count)
    {
        checked(cudaMalloc(reinterpret_cast<void **>(&s.counts), uint64_t(c.source_count) * 8));
        checked(cudaMalloc(reinterpret_cast<void **>(&s.prefix), uint64_t(c.source_count) * 8));
    }
    checked(cudaMalloc(&s.scratch, s.size.scratch));
    for (auto &keys : s.key_buffers) keys = std::make_unique<SharedBuffer>(context, uint64_t(c.reference_capacity) * 8);
    s.tile_ranges = std::make_unique<SharedBuffer>(context, uint64_t(s.size.columns) * s.size.rows * sizeof(TileRange));
    s.tile_status = std::make_unique<SharedBuffer>(context, sizeof(TileStatus));
}
TileWorkspace::~TileWorkspace() = default;
void TileWorkspace::submit(Context &context, SharedBuffer &ellipses, SharedBuffer &order, SharedBuffer &arguments)
{
    auto &s = *impl_; const auto c = s.config;
    if (context.native_stream() != s.stream) throw std::invalid_argument("Tile context changed");
    for (auto *buffer : {&ellipses, &order, &arguments})
        if (!context.owns(*buffer)) throw std::invalid_argument("Foreign tile input");
    if (&ellipses == &order || &ellipses == &arguments || &order == &arguments) throw std::invalid_argument("Aliased tile inputs");
    if (ellipses.bytes() < uint64_t(c.source_count) * sizeof(ProjectedEllipse) || order.bytes() < uint64_t(c.source_count) * 4 ||
        arguments.bytes() < sizeof(DrawCounters)) throw std::out_of_range("Tile input capacity");
    auto *status = static_cast<TileStatus *>(s.tile_status->device_pointer());
    auto *ranges = static_cast<TileRange *>(s.tile_ranges->device_pointer());
    auto *points = static_cast<const ProjectedEllipse *>(ellipses.device_pointer());
    auto *indexes = static_cast<const uint32_t *>(order.device_pointer());
    auto *args = static_cast<const DrawCounters *>(arguments.device_pointer());
    checked(cudaMemsetAsync(status, 0, sizeof(TileStatus), s.stream));
    checked(cudaMemsetAsync(ranges, 0, s.tile_ranges->bytes(), s.stream));
    checked(cudaMemsetAsync(s.key_buffers[0]->device_pointer(), 255, uint64_t(c.reference_capacity) * 8, s.stream));
    if (c.source_count)
    {
        count_references<<<uint32_t((uint64_t(c.source_count) + 255) / 256), 256, 0, s.stream>>>(points, indexes, args, s.counts, status, c);
        checked(cudaGetLastError()); size_t bytes = s.size.scratch;
        checked(cub::DeviceScan::ExclusiveSum(s.scratch, bytes, s.counts, s.prefix, int64_t(c.source_count), s.stream));
    }
    check_total<<<1, 1, 0, s.stream>>>(s.counts, s.prefix, args, status, c); checked(cudaGetLastError());
    if (c.source_count) emit_references<<<uint32_t((uint64_t(c.source_count) + 255) / 256), 256, 0, s.stream>>>(
        points, indexes, s.counts, s.prefix, static_cast<uint64_t *>(s.key_buffers[0]->device_pointer()), status, c);
    checked(cudaGetLastError());
    cub::DoubleBuffer<uint64_t> keys(static_cast<uint64_t *>(s.key_buffers[0]->device_pointer()), static_cast<uint64_t *>(s.key_buffers[1]->device_pointer()));
    size_t bytes = s.size.scratch;
    checked(cub::DeviceRadixSort::SortKeys(s.scratch, bytes, keys, int64_t(c.reference_capacity), 0, 64, s.stream));
    s.selected = uint32_t(keys.selector);
    find_ranges<<<(c.reference_capacity + 255) / 256, 256, 0, s.stream>>>(keys.Current(), ranges, status, c.reference_capacity);
    const auto tiles = s.size.columns * s.size.rows;
    maximum_range<<<(tiles + 255) / 256, 256, 0, s.stream>>>(ranges, status, tiles); checked(cudaGetLastError());
}
SharedBuffer &TileWorkspace::keys() const { return *impl_->key_buffers[impl_->selected]; }
SharedBuffer &TileWorkspace::ranges() const { return *impl_->tile_ranges; }
SharedBuffer &TileWorkspace::status() const { return *impl_->tile_status; }
uint64_t TileWorkspace::allocated_bytes() const { return impl_->size.bytes; }
const TileConfig &TileWorkspace::config() const { return impl_->config; }
}
