#include "gs_cuda/tile.h"
#include <cuda_runtime.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace gs::cuda_ops
{
namespace
{
using render::detail::ProjectedEllipse;
constexpr uint32_t batch_size = 512;
void checked(cudaError_t error) { if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error)); }
struct Parameters { float stddev, cutoff; uint32_t quantize, front; float terminate; };
__global__ void set_parameters(Parameters *target, Parameters value) { *target = value; }
__global__ void clear_pixels(float4 *pixels, const Parameters *parameters, uint32_t count)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count) pixels[i] = make_float4(0, 0, 0, parameters->front ? 1.f : 0.f);
}
__global__ void start_batches(uint32_t *cursor, const TileStatus *status, cudaGraphConditionalHandle handle)
{
    *cursor = 0;
    cudaGraphSetConditional(handle, !status->flags && status->max_tile_length > 0);
}
__global__ void next_batch(uint32_t *cursor, const TileStatus *status, cudaGraphConditionalHandle handle)
{
    *cursor += batch_size;
    cudaGraphSetConditional(handle, !status->flags && *cursor < status->max_tile_length);
}
__device__ float rounded(float value) { return nearbyintf(fminf(fmaxf(value, 0), 1) * 255) / 255; }
template<bool DeviceCursor>
__global__ void composite_batch(const ProjectedEllipse *points, const uint32_t *order, const uint64_t *keys,
    const TileRange *ranges, TileStatus *status, const uint32_t *cursor, const Parameters *parameters,
    float4 *pixels, uint32_t width, uint32_t height, uint32_t tile_size, uint32_t offset)
{
    __shared__ ProjectedEllipse batch[batch_size];
    __shared__ uint32_t invalid;
    const uint32_t columns = (width + tile_size - 1) / tile_size;
    const uint32_t tile = blockIdx.x;
    const auto range = ranges[tile];
    if (!threadIdx.x) invalid = status->flags;
    __syncthreads();
    if (invalid) return;
    const uint32_t chunk = DeviceCursor ? *cursor : offset;
    if (chunk >= range.end - range.begin) return;
    const uint32_t base = range.begin + chunk;
    if (base >= range.end) return; // Uniform across the block; no barrier skipped by a lane.
    const uint32_t count = min(batch_size, range.end - base);
    const auto p = *parameters;
    for (uint32_t i = threadIdx.x; i < count; i += blockDim.x)
        batch[i] = points[order[uint32_t(keys[p.front ? range.end - chunk - i - 1 : base + i])]];
    __syncthreads();
    const uint32_t x = tile % columns * tile_size + threadIdx.x % tile_size;
    const uint32_t y = tile / columns * tile_size + threadIdx.x / tile_size;
    const bool active = x < width && y < height;
    float4 result = active ? pixels[uint64_t(y) * width + x] : make_float4(0, 0, 0, 0);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto e = batch[i];
        // Invert the actual two-axis quad, matching vertex interpolation even
        // when FP32 axes are not exactly orthogonal after rounding.
        const float determinant = e.axis0[0] * e.axis1[1] - e.axis0[1] * e.axis1[0];
        if (!isfinite(determinant) || determinant == 0) { if (!threadIdx.x) atomicOr(&status->flags, 4u); continue; }
        if (!active) continue;
        if (p.front && p.terminate > 0 && result.w <= p.terminate) break;
        const float dx = float(x) + .5f - e.center[0], dy = float(y) + .5f - e.center[1];
        const float u = (dx * e.axis1[1] - dy * e.axis1[0]) / determinant;
        const float v = (dy * e.axis0[0] - dx * e.axis0[1]) / determinant;
        const float gx = u * p.stddev, gy = v * p.stddev;
        const float radius2 = gx * gx + gy * gy;
        if (fabsf(u) > 1 || fabsf(v) > 1 || radius2 > p.stddev * p.stddev) continue;
        const float alpha = fminf(.999f, e.color[3] * expf(-.5f * radius2));
        if (alpha < p.cutoff) continue;
        const float transmittance = 1 - alpha;
        if (p.front)
        {
            result.x += result.w * e.color[0] * alpha;
            result.y += result.w * e.color[1] * alpha;
            result.z += result.w * e.color[2] * alpha;
            result.w *= transmittance;
        }
        else
        {
            result.x = e.color[0] * alpha + transmittance * result.x;
            result.y = e.color[1] * alpha + transmittance * result.y;
            result.z = e.color[2] * alpha + transmittance * result.z;
            result.w = alpha + transmittance * result.w;
        }
        if (p.quantize) result = make_float4(rounded(result.x), rounded(result.y), rounded(result.z), rounded(result.w));
    }
    if (active) pixels[uint64_t(y) * width + x] = result;
}
__global__ void store_surface(const float4 *pixels, const TileStatus *status, const Parameters *parameters, cudaSurfaceObject_t surface, uint32_t width, uint32_t height)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (uint64_t(i) >= uint64_t(width) * height) return;
    auto p = status->flags ? make_float4(0, 0, 0, 0) : pixels[i];
    if (!status->flags && parameters->front) p.w = 1 - p.w;
    const auto rgba = make_uchar4(uint8_t(nearbyintf(fminf(fmaxf(p.x, 0), 1) * 255)),
        uint8_t(nearbyintf(fminf(fmaxf(p.y, 0), 1) * 255)), uint8_t(nearbyintf(fminf(fmaxf(p.z, 0), 1) * 255)),
        uint8_t(nearbyintf(fminf(fmaxf(p.w, 0), 1) * 255)));
    surf2Dwrite(rgba, surface, (i % width) * sizeof(uchar4), i / width);
}
}
struct TileCompositeWorkspace::Impl
{
    TileWorkspace *lists = nullptr;
    cudaStream_t stream = nullptr;
    float4 *pixels = nullptr;
    uint32_t *cursor = nullptr;
    Parameters *parameters = nullptr;
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t executable = nullptr;
    std::array<void *, 3> addresses{};
    uint64_t surface = 0;
    bool captured_direct = false;
    ~Impl()
    {
        if (executable) cudaGraphExecDestroy(executable);
        if (graph) cudaGraphDestroy(graph);
        if (parameters) cudaFree(parameters);
        if (cursor) cudaFree(cursor);
        if (pixels) cudaFree(pixels);
    }
    template<class Work> void capture(cudaGraph_t target, Work work)
    {
        checked(cudaStreamBeginCaptureToGraph(stream, target, nullptr, nullptr, 0, cudaStreamCaptureModeThreadLocal));
        try { work(); }
        catch (...) { cudaGraph_t unused = nullptr; cudaStreamEndCapture(stream, &unused); throw; }
        cudaGraph_t captured = nullptr; checked(cudaStreamEndCapture(stream, &captured));
        if (captured != target) throw std::runtime_error("Unexpected tile capture graph");
    }
    void direct_work(SharedBuffer &ellipses, SharedBuffer &order)
    {
        const auto c = lists->config();
        clear_pixels<<<(uint64_t(c.width) * c.height + 255) / 256, 256, 0, stream>>>(pixels, parameters, c.width * c.height);
        const uint32_t tile_count = ((c.width + c.tile_size - 1) / c.tile_size) * ((c.height + c.tile_size - 1) / c.tile_size);
        for (uint64_t offset = 0; offset < c.source_count; offset += batch_size)
            composite_batch<false><<<tile_count, c.tile_size * c.tile_size, 0, stream>>>(
                static_cast<const ProjectedEllipse *>(ellipses.device_pointer()), static_cast<const uint32_t *>(order.device_pointer()),
                static_cast<const uint64_t *>(lists->keys().device_pointer()), static_cast<const TileRange *>(lists->ranges().device_pointer()),
                static_cast<TileStatus *>(lists->status().device_pointer()), nullptr, parameters, pixels,
                c.width, c.height, c.tile_size, uint32_t(offset));
        checked(cudaGetLastError());
    }
    void build(SharedBuffer &ellipses, SharedBuffer &order, SharedTexture &target, bool direct)
    {
        const auto c = lists->config();
        auto *status = static_cast<TileStatus *>(lists->status().device_pointer());
        checked(cudaGraphCreate(&graph, 0));
        if (direct)
        {
            capture(graph, [&] { direct_work(ellipses, order); });
            checked(cudaGraphInstantiate(&executable, graph, 0));
            addresses = {ellipses.device_pointer(), order.device_pointer(), lists->keys().device_pointer()};
            surface = target.surface(); captured_direct = true; return;
        }
        cudaGraphConditionalHandle handle{};
        checked(cudaGraphConditionalHandleCreate(&handle, graph, 0, cudaGraphCondAssignDefault));
        capture(graph, [&] {
            clear_pixels<<<(uint64_t(c.width) * c.height + 255) / 256, 256, 0, stream>>>(pixels, parameters, c.width * c.height);
            start_batches<<<1, 1, 0, stream>>>(cursor, status, handle); checked(cudaGetLastError());
        });
        // Stream capture is a linear chain here: find its sole final node.
        size_t nodes_count = 0; checked(cudaGraphGetNodes(graph, nullptr, &nodes_count));
        std::unique_ptr<cudaGraphNode_t[]> nodes(new cudaGraphNode_t[nodes_count]);
        checked(cudaGraphGetNodes(graph, nodes.get(), &nodes_count));
        cudaGraphNode_t leaf = nullptr;
        for (size_t i = 0; i < nodes_count; ++i)
        {
            size_t successors = 0; checked(cudaGraphNodeGetDependentNodes(nodes[i], nullptr, nullptr, &successors));
            if (!successors) { if (leaf) throw std::runtime_error("Tile graph has multiple leaves"); leaf = nodes[i]; }
        }
        cudaGraphNodeParams node{}; node.type = cudaGraphNodeTypeConditional;
        node.conditional.handle = handle; node.conditional.type = cudaGraphCondTypeWhile; node.conditional.size = 1;
        cudaGraphNode_t loop = nullptr;
        checked(cudaGraphAddNode(&loop, graph, &leaf, nullptr, 1, &node));
        capture(node.conditional.phGraph_out[0], [&] {
            const uint32_t tile_count = ((c.width + c.tile_size - 1) / c.tile_size) * ((c.height + c.tile_size - 1) / c.tile_size);
            composite_batch<true><<<tile_count, c.tile_size * c.tile_size, 0, stream>>>(
                static_cast<const ProjectedEllipse *>(ellipses.device_pointer()), static_cast<const uint32_t *>(order.device_pointer()),
                static_cast<const uint64_t *>(lists->keys().device_pointer()), static_cast<const TileRange *>(lists->ranges().device_pointer()),
                status, cursor, parameters, pixels, c.width, c.height, c.tile_size, 0);
            next_batch<<<1, 1, 0, stream>>>(cursor, status, handle); checked(cudaGetLastError());
        });
        // The output kernel is submitted after graph launch, on the same stream.
        checked(cudaGraphInstantiate(&executable, graph, 0));
        addresses = {ellipses.device_pointer(), order.device_pointer(), lists->keys().device_pointer()}; surface = target.surface();
    }
};
uint64_t TileCompositeWorkspace::required_bytes(uint32_t width, uint32_t height)
{
    if (!width || !height || width > 16384 || height > 16384) throw std::invalid_argument("Tile composite dimensions");
    return uint64_t(width) * height * sizeof(float4) + sizeof(Parameters) + sizeof(uint32_t);
}
TileCompositeWorkspace::TileCompositeWorkspace(Context &context, TileWorkspace &lists, uint64_t budget) : impl_(std::make_unique<Impl>())
{
    auto &s = *impl_; s.lists = &lists; s.stream = static_cast<cudaStream_t>(context.native_stream());
    const auto &c = lists.config();
    if (required_bytes(c.width, c.height) > budget) throw std::out_of_range("Tile composite exceeds budget");
    checked(cudaMalloc(reinterpret_cast<void **>(&s.pixels), uint64_t(c.width) * c.height * sizeof(float4)));
    checked(cudaMalloc(reinterpret_cast<void **>(&s.cursor), sizeof(uint32_t)));
    checked(cudaMalloc(reinterpret_cast<void **>(&s.parameters), sizeof(Parameters)));
}
TileCompositeWorkspace::~TileCompositeWorkspace() = default;
void TileCompositeWorkspace::submit(Context &context, SharedBuffer &ellipses, SharedBuffer &order, SharedTexture &target, TileCompositeOptions options)
{
    auto &s = *impl_; const auto c = s.lists->config();
    if (context.native_stream() != s.stream) throw std::invalid_argument("Tile composite context changed");
    if (!std::isfinite(options.stddev) || options.stddev <= 0 || options.stddev > 32 || !std::isfinite(options.alpha_cutoff) ||
        options.alpha_cutoff < 0 || options.alpha_cutoff > 1) throw std::invalid_argument("Invalid tile composite quality");
    if (!std::isfinite(options.transmittance_cutoff) || options.transmittance_cutoff < 0 || options.transmittance_cutoff > .001f ||
        (options.front_to_back && options.quantize_each_splat) || (!options.front_to_back && options.transmittance_cutoff != 0) ||
        (options.conditional_while && options.capture_direct)) throw std::invalid_argument("Invalid tile composite experiment combination");
    if (target.width() != c.width || target.height() != c.height || &ellipses == &order) throw std::invalid_argument("Tile composite layout");
    if (ellipses.bytes() < uint64_t(c.source_count) * sizeof(ProjectedEllipse) || order.bytes() < uint64_t(c.source_count) * 4)
        throw std::out_of_range("Tile composite input capacity");
    set_parameters<<<1, 1, 0, s.stream>>>(s.parameters, {options.stddev, options.alpha_cutoff, uint32_t(options.quantize_each_splat), uint32_t(options.front_to_back), options.transmittance_cutoff});
    checked(cudaGetLastError());
    if (options.conditional_while || options.capture_direct)
    {
        if (!s.executable) s.build(ellipses, order, target, options.capture_direct);
        if (s.captured_direct != options.capture_direct) throw std::invalid_argument("Tile graph schedule changed; rebuild workspace");
        if (s.addresses != std::array<void *, 3>{ellipses.device_pointer(), order.device_pointer(), s.lists->keys().device_pointer()} ||
            s.surface != target.surface()) throw std::invalid_argument("Tile composite graph addresses changed; rebuild workspace");
        checked(cudaGraphLaunch(s.executable, s.stream));
    }
    else
    {
        // A tile contains at most one reference per source rank. The fixed host
        // bound requires no device-count readback; inactive blocks return at once.
        s.direct_work(ellipses, order);
    }
    store_surface<<<(uint64_t(c.width) * c.height + 255) / 256, 256, 0, s.stream>>>(s.pixels,
        static_cast<const TileStatus *>(s.lists->status().device_pointer()), s.parameters, target.surface(), c.width, c.height);
    checked(cudaGetLastError());
}
}
