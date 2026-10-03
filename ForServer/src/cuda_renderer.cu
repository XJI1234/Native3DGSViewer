#include "gs_server/renderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cub/cub.cuh>
#include <cuda_runtime.h>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gs::server
{
namespace
{
void checked(cudaError_t status, const char *stage)
{
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(stage) + ": " + cudaGetErrorString(status));
}
struct Accounting
{
    uint64_t live = 0, peak = 0;
};
template <class Type> class DeviceBuffer
{
  public:
    DeviceBuffer() = default;
    DeviceBuffer(size_t count, Accounting &account)
        : bytes_(count * sizeof(Type)), account_(&account)
    {
        if (!bytes_)
            return;
        size_t free_bytes = 0, total = 0;
        checked(cudaMemGetInfo(&free_bytes, &total), "GPU budget query");
        if (bytes_ > free_bytes * 0.95)
            throw std::runtime_error("GPU ResourceLimit");
        checked(cudaMalloc(reinterpret_cast<void **>(&data_), bytes_), "GPU allocation");
        account.live += bytes_;
        account.peak = std::max(account.peak, account.live);
    }
    ~DeviceBuffer()
    {
        release();
    }
    DeviceBuffer(const DeviceBuffer &) = delete;
    DeviceBuffer &operator=(const DeviceBuffer &) = delete;
    DeviceBuffer(DeviceBuffer &&other) noexcept
    {
        swap(other);
    }
    DeviceBuffer &operator=(DeviceBuffer &&other) noexcept
    {
        release();
        swap(other);
        return *this;
    }
    Type *get() const
    {
        return data_;
    }
    size_t bytes() const
    {
        return bytes_;
    }

  private:
    void release()
    {
        if (data_)
        {
            cudaFree(data_);
            account_->live -= bytes_;
        }
        data_ = nullptr;
        bytes_ = 0;
    }
    void swap(DeviceBuffer &other)
    {
        std::swap(data_, other.data_);
        std::swap(bytes_, other.bytes_);
        std::swap(account_, other.account_);
    }
    Type *data_ = nullptr;
    size_t bytes_ = 0;
    Accounting *account_ = nullptr;
};
class Timer
{
  public:
    Timer()
    {
        checked(cudaEventCreate(&begin_), "Event create");
        try
        {
            checked(cudaEventCreate(&end_), "Event create");
        }
        catch (...)
        {
            cudaEventDestroy(begin_);
            throw;
        }
    }
    ~Timer()
    {
        cudaEventDestroy(begin_);
        cudaEventDestroy(end_);
    }
    void begin()
    {
        checked(cudaEventRecord(begin_), "Timer begin");
    }
    double end()
    {
        checked(cudaGetLastError(), "Kernel launch");
        checked(cudaEventRecord(end_), "Timer end");
        checked(cudaEventSynchronize(end_), "GPU completion");
        float value = 0;
        checked(cudaEventElapsedTime(&value, begin_, end_), "Elapsed time");
        return value;
    }

  private:
    cudaEvent_t begin_{}, end_{};
};
struct GpuScene
{
    const float *center, *scale, *rotation, *opacity, *rgb, *sh;
    uint32_t count, terms;
};
struct Camera
{
    float3 rows[3];
    float3 position;
    float focal, width, height;
};
__host__ __device__ float3 add3(float3 first, float3 second)
{
    return make_float3(first.x + second.x, first.y + second.y, first.z + second.z);
}
__host__ __device__ float3 mul3(float3 value, float scalar)
{
    return make_float3(value.x * scalar, value.y * scalar, value.z * scalar);
}
__host__ __device__ float dot3(float3 first, float3 second)
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}
__host__ __device__ float3 cross3(float3 first, float3 second)
{
    return make_float3(first.y * second.z - first.z * second.y,
                       first.z * second.x - first.x * second.z,
                       first.x * second.y - first.y * second.x);
}
__host__ __device__ float3 normalize3(float3 value)
{
    return mul3(value, 1.0f / sqrtf(dot3(value, value)));
}
__device__ float3 load3(const float *array, uint32_t index)
{
    return make_float3(array[index * 3], array[index * 3 + 1], array[index * 3 + 2]);
}
__device__ float3 view3(const Camera &camera, float3 value)
{
    return make_float3(dot3(camera.rows[0], value), dot3(camera.rows[1], value),
                       dot3(camera.rows[2], value));
}
__device__ float3 rotate3(float4 rotation, float3 value)
{
    const float3 axis = make_float3(rotation.x, rotation.y, rotation.z);
    return add3(value, mul3(cross3(axis, add3(cross3(axis, value), mul3(value, rotation.w))), 2));
}
__device__ float3 color3(GpuScene scene, uint32_t index, float3 direction)
{
    const float axis_x = direction.x, axis_y = direction.y, axis_z = direction.z;
    const float basis[15]{
        -0.4886025119f * axis_y,
        0.4886025119f * axis_z,
        -0.4886025119f * axis_x,
        1.092548431f * axis_x * axis_y,
        -1.092548431f * axis_y * axis_z,
        0.3153915653f * (2 * axis_z * axis_z - axis_x * axis_x - axis_y * axis_y),
        -1.092548431f * axis_x * axis_z,
        0.5462742153f * (axis_x * axis_x - axis_y * axis_y),
        -0.5900435899f * axis_y * (3 * axis_x * axis_x - axis_y * axis_y),
        2.890611443f * axis_x * axis_y * axis_z,
        -0.4570457995f * axis_y * (4 * axis_z * axis_z - axis_x * axis_x - axis_y * axis_y),
        0.3731763326f * axis_z * (2 * axis_z * axis_z - 3 * axis_x * axis_x - 3 * axis_y * axis_y),
        -0.4570457995f * axis_x * (4 * axis_z * axis_z - axis_x * axis_x - axis_y * axis_y),
        1.445305721f * axis_z * (axis_x * axis_x - axis_y * axis_y),
        -0.5900435899f * axis_x * (axis_x * axis_x - 3 * axis_y * axis_y)};
    float3 color = load3(scene.rgb, index);
    for (uint32_t coefficient = 0; coefficient < scene.terms; ++coefficient)
        color = add3(color,
                     mul3(load3(scene.sh, index * scene.terms + coefficient), basis[coefficient]));
    return make_float3(fminf(1, fmaxf(0, color.x)), fminf(1, fmaxf(0, color.y)),
                       fminf(1, fmaxf(0, color.z)));
}
__global__ void project_kernel(GpuScene scene, Camera camera, Ellipse *projected, uint32_t *keys,
                               uint32_t *indices, uint32_t *visible)
{
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= scene.count)
        return;
    keys[index] = UINT32_MAX;
    indices[index] = index;
    float3 delta = add3(load3(scene.center, index), mul3(camera.position, -1));
    float3 position = view3(camera, delta), scale = load3(scene.scale, index);
    const float support = 3 * fmaxf(scale.x, fmaxf(scale.y, scale.z));
    const float depth = -position.z;
    if (scene.opacity[index] < 0.0039f || depth + support < 0.01f || depth - support > 1e8f)
        return;
    const float distance = fmaxf(depth, 0.01f);
    float3 jacobian_x =
        make_float3(camera.focal / distance, 0, camera.focal * position.x / (distance * distance));
    float3 jacobian_y = make_float3(0, -camera.focal / distance,
                                    -camera.focal * position.y / (distance * distance));
    const float4 rotation =
        make_float4(scene.rotation[index * 4], scene.rotation[index * 4 + 1],
                    scene.rotation[index * 4 + 2], scene.rotation[index * 4 + 3]);
    const float3 axes[3]{view3(camera, rotate3(rotation, make_float3(scale.x, 0, 0))),
                         view3(camera, rotate3(rotation, make_float3(0, scale.y, 0))),
                         view3(camera, rotate3(rotation, make_float3(0, 0, scale.z)))};
    const float3 transform_x = make_float3(dot3(jacobian_x, axes[0]), dot3(jacobian_x, axes[1]),
                                           dot3(jacobian_x, axes[2]));
    const float3 transform_y = make_float3(dot3(jacobian_y, axes[0]), dot3(jacobian_y, axes[1]),
                                           dot3(jacobian_y, axes[2]));
    const float covariance_x = dot3(transform_x, transform_x) + 0.01f;
    const float covariance_y = dot3(transform_y, transform_y) + 0.01f;
    const float covariance_xy = dot3(transform_x, transform_y);
    const float covariance_scale = fmaxf(covariance_x, covariance_y);
    const float normalized_x = covariance_x / covariance_scale,
                normalized_y = covariance_y / covariance_scale,
                normalized_xy = covariance_xy / covariance_scale;
    const float middle = (normalized_x + normalized_y) * 0.5f;
    const float discriminant = hypotf((normalized_x - normalized_y) * 0.5f, normalized_xy);
    const float eigen_major = middle + discriminant;
    const float3 gram = cross3(mul3(transform_x, rsqrtf(covariance_scale)),
                               mul3(transform_y, rsqrtf(covariance_scale)));
    const float blur = 0.01f / covariance_scale;
    const float eigen_minor =
        (dot3(gram, gram) + blur * (normalized_x + normalized_y - blur)) / eigen_major;
    const float major = fminf(1024, 3 * sqrtf(covariance_scale * eigen_major));
    const float minor = fminf(1024, 3 * sqrtf(covariance_scale * eigen_minor));
    if (!isfinite(major) || !isfinite(minor) || minor <= 0)
        return;
    float2 axis = covariance_x >= covariance_y ? make_float2(1, 0) : make_float2(0, 1);
    if (fabsf(normalized_xy) > 1e-10f)
    {
        const float length = hypotf(normalized_xy, eigen_major - normalized_x);
        axis = make_float2(normalized_xy / length, (eigen_major - normalized_x) / length);
    }
    Ellipse ellipse;
    ellipse.axis0_x = axis.x * major;
    ellipse.axis0_y = axis.y * major;
    ellipse.axis1_x = -axis.y * minor;
    ellipse.axis1_y = axis.x * minor;
    ellipse.center_x = camera.width * 0.5f + camera.focal * position.x / distance;
    ellipse.center_y = camera.height * 0.5f - camera.focal * position.y / distance;
    const float extent_x = fabsf(ellipse.axis0_x) + fabsf(ellipse.axis1_x),
                extent_y = fabsf(ellipse.axis0_y) + fabsf(ellipse.axis1_y);
    if (ellipse.center_x + extent_x < 0 || ellipse.center_y + extent_y < 0 ||
        ellipse.center_x - extent_x > camera.width || ellipse.center_y - extent_y > camera.height)
        return;
    const float metric = dot3(delta, delta);
    if (!isfinite(metric) || !isfinite(ellipse.center_x) || !isfinite(ellipse.center_y))
        return;
    const float3 color =
        color3(scene, index, metric > 0 ? normalize3(delta) : make_float3(0, 0, 1));
    ellipse.red = color.x;
    ellipse.green = color.y;
    ellipse.blue = color.z;
    ellipse.opacity = scene.opacity[index];
    projected[index] = ellipse;
    keys[index] = ~__float_as_uint(metric);
    if (keys[index] == UINT32_MAX)
        --keys[index];
    atomicAdd(visible, 1);
}
__global__ void gather_kernel(const Ellipse *source, const uint32_t *indices, Ellipse *output,
                              uint32_t count)
{
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index < count)
        output[index] = source[indices[index]];
}
struct TileBounds
{
    int left, top, right, bottom;
};
__device__ TileBounds tile_bounds(Ellipse ellipse, uint32_t width, uint32_t height)
{
    const float extent_x = fabsf(ellipse.axis0_x) + fabsf(ellipse.axis1_x),
                extent_y = fabsf(ellipse.axis0_y) + fabsf(ellipse.axis1_y);
    return {max(0, min(int((width - 1) / 16), int(floorf((ellipse.center_x - extent_x) / 16)))),
            max(0, min(int((height - 1) / 16), int(floorf((ellipse.center_y - extent_y) / 16)))),
            max(0, min(int((width - 1) / 16), int(floorf((ellipse.center_x + extent_x) / 16)))),
            max(0, min(int((height - 1) / 16), int(floorf((ellipse.center_y + extent_y) / 16))))};
}
__global__ void count_tiles(const Ellipse *ellipses, uint64_t *counts, uint32_t count,
                            uint32_t width, uint32_t height)
{
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index == count)
        counts[index] = 0;
    if (index >= count)
        return;
    const auto bounds = tile_bounds(ellipses[index], width, height);
    counts[index] = uint64_t(bounds.right - bounds.left + 1) * (bounds.bottom - bounds.top + 1);
}
__global__ void emit_tiles(const Ellipse *ellipses, const uint64_t *offsets, uint64_t *keys,
                           uint32_t count, uint32_t width, uint32_t height)
{
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count)
        return;
    const auto bounds = tile_bounds(ellipses[index], width, height);
    uint64_t offset = offsets[index];
    for (int tile_y = bounds.top; tile_y <= bounds.bottom; ++tile_y)
        for (int tile_x = bounds.left; tile_x <= bounds.right; ++tile_x)
            keys[offset++] = (uint64_t(tile_y * ((width + 15) / 16) + tile_x) << 32) | index;
}
__global__ void ranges_kernel(const uint64_t *keys, uint2 *ranges, uint32_t count)
{
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count)
        return;
    const uint32_t tile = uint32_t(keys[index] >> 32);
    if (index == 0 || uint32_t(keys[index - 1] >> 32) != tile)
        ranges[tile].x = index;
    if (index + 1 == count || uint32_t(keys[index + 1] >> 32) != tile)
        ranges[tile].y = index + 1;
}
__global__ void raster_kernel(const Ellipse *ellipses, const uint64_t *keys, const uint2 *ranges,
                              uchar4 *pixels, uint32_t width, uint32_t height)
{
    __shared__ Ellipse batch[128];
    const uint32_t lane = threadIdx.y * 16 + threadIdx.x;
    const uint32_t pixel_x = blockIdx.x * 16 + threadIdx.x, pixel_y = blockIdx.y * 16 + threadIdx.y;
    const uint32_t tile = blockIdx.y * gridDim.x + blockIdx.x;
    const auto range = ranges[tile];
    float3 color = make_float3(0, 0, 0);
    for (uint32_t start = range.x; start < range.y; start += 128)
    {
        const uint32_t size = min(128u, range.y - start);
        if (lane < size)
            batch[lane] = ellipses[uint32_t(keys[start + lane])];
        __syncthreads();
        if (pixel_x < width && pixel_y < height)
            for (uint32_t index = 0; index < size; ++index)
            {
                const auto ellipse = batch[index];
                const float delta_x = pixel_x + 0.5f - ellipse.center_x,
                            delta_y = pixel_y + 0.5f - ellipse.center_y;
                const float determinant =
                    ellipse.axis0_x * ellipse.axis1_y - ellipse.axis0_y * ellipse.axis1_x;
                const float gaussian_x =
                    3 * (delta_x * ellipse.axis1_y - delta_y * ellipse.axis1_x) / determinant;
                const float gaussian_y =
                    3 * (ellipse.axis0_x * delta_y - ellipse.axis0_y * delta_x) / determinant;
                const float radius = gaussian_x * gaussian_x + gaussian_y * gaussian_y;
                if (radius > 9)
                    continue;
                const float alpha = fminf(0.999f, ellipse.opacity * expf(-0.5f * radius));
                if (alpha < 0.0039f)
                    continue;
                color = add3(mul3(make_float3(ellipse.red, ellipse.green, ellipse.blue), alpha),
                             mul3(color, 1 - alpha));
            }
        __syncthreads();
    }
    if (pixel_x < width && pixel_y < height)
        pixels[size_t(pixel_y) * width + pixel_x] =
            make_uchar4(uint8_t(floorf(fminf(1, fmaxf(0, color.x)) * 255 + 0.5f)),
                        uint8_t(floorf(fminf(1, fmaxf(0, color.y)) * 255 + 0.5f)),
                        uint8_t(floorf(fminf(1, fmaxf(0, color.z)) * 255 + 0.5f)), 255);
}
Camera fit_camera(const SplatScene &scene, const RenderRequest &request)
{
    const double center[3]{(scene.bounds.min.x + scene.bounds.max.x) * 0.5 - scene.worldOrigin.x,
                           (scene.bounds.min.y + scene.bounds.max.y) * 0.5 - scene.worldOrigin.y,
                           (scene.bounds.min.z + scene.bounds.max.z) * 0.5 - scene.worldOrigin.z};
    const double diagonal =
        std::hypot(scene.bounds.max.x - scene.bounds.min.x, scene.bounds.max.y - scene.bounds.min.y,
                   scene.bounds.max.z - scene.bounds.min.z);
    const double radius = std::max(diagonal * 0.5 + scene.maxScale * 3, 0.01);
    const double aspect = double(request.width) / request.height;
    const double distance = radius /
                            std::sin(std::atan(std::min(1.0, aspect) * std::tan(0.5235987756))) *
                            1.1 * request.zoom;
    const double angle = request.yaw * 0.017453292519943295;
    const double pitch = request.pitch * 0.017453292519943295;
    const float3 backward =
        make_float3(float(std::sin(angle) * std::cos(pitch)), float(std::sin(pitch)),
                    float(std::cos(angle) * std::cos(pitch)));
    const float orientation = request.flip_y ? -1.0f : 1.0f;
    const float3 right =
        make_float3(float(std::cos(angle)) * orientation, 0, float(-std::sin(angle)) * orientation);
    Camera camera{};
    camera.rows[0] = right;
    camera.rows[1] = cross3(backward, right);
    camera.rows[2] = backward;
    camera.position = make_float3(float(center[0] + distance * backward.x),
                                  float(center[1] + distance * backward.y),
                                  float(center[2] + distance * backward.z));
    camera.width = float(request.width);
    camera.height = float(request.height);
    camera.focal = float(request.height / (2 * std::tan(0.5235987756)));
    return camera;
}
} // namespace

struct CudaRenderer::Impl
{
    SceneHandle scene;
    int device;
    Accounting accounting;
    DeviceBuffer<float> arrays[6];
    GpuScene gpu{};
    Impl(SceneHandle source, int selected) : scene(std::move(source)), device(selected)
    {
        checked(cudaSetDevice(device), "Select GPU");
        if (!scene || scene->count == 0 || scene->count > 32'000'000)
            throw std::runtime_error("Invalid scene count");
        const size_t terms = (scene->shDegree + 1) * (scene->shDegree + 1) - 1;
        if (scene->shDegree > 3 || scene->centerLocal.size() != scene->count * 3 ||
            scene->scale.size() != scene->count * 3 || scene->rotation.size() != scene->count * 4 ||
            scene->opacity.size() != scene->count || scene->rgb0.size() != scene->count * 3 ||
            scene->shRest.size() != scene->count * terms * 3)
            throw std::runtime_error("Invalid scene array layout");
        const std::span<const float> spans[]{scene->centerLocal, scene->scale, scene->rotation,
                                             scene->opacity,     scene->rgb0,  scene->shRest};
        for (size_t index = 0; index < 6; ++index)
        {
            arrays[index] = DeviceBuffer<float>(spans[index].size(), accounting);
            if (!spans[index].empty())
                checked(cudaMemcpy(arrays[index].get(), spans[index].data(), arrays[index].bytes(),
                                   cudaMemcpyHostToDevice),
                        "Scene upload");
        }
        gpu = {arrays[0].get(),        arrays[1].get(),
               arrays[2].get(),        arrays[3].get(),
               arrays[4].get(),        arrays[5].get(),
               uint32_t(scene->count), uint32_t((scene->shDegree + 1) * (scene->shDegree + 1) - 1)};
    }
};
CudaRenderer::CudaRenderer(SceneHandle scene, int device)
    : impl_(std::make_unique<Impl>(std::move(scene), device))
{
}
CudaRenderer::~CudaRenderer() = default;
std::string CudaRenderer::devices()
{
    int count = 0;
    checked(cudaGetDeviceCount(&count), "Enumerate GPUs");
    std::ostringstream output;
    for (int index = 0; index < count; ++index)
    {
        cudaDeviceProp properties{};
        checked(cudaGetDeviceProperties(&properties, index), "GPU properties");
        output << index << '\t' << properties.name << '\t' << properties.totalGlobalMem << '\t'
               << properties.major << '.' << properties.minor << '\n';
    }
    return output.str();
}
RenderOutput CudaRenderer::render(const RenderRequest &request)
{
    if (!request.width || !request.height || request.width > 4096 || request.height > 4096 ||
        !std::isfinite(request.yaw) || std::abs(request.yaw) > 36000 ||
        !std::isfinite(request.pitch) || std::abs(request.pitch) > 36000 ||
        !std::isfinite(request.zoom) || request.zoom < 0.1 || request.zoom > 10)
        throw std::runtime_error("Invalid render request");
    profile_name(request.profile);
    checked(cudaSetDevice(impl_->device), "Select GPU");
    auto &account = impl_->accounting;
    account.peak = account.live;
    RenderOutput output;
    output.frame.width = request.width;
    output.frame.height = request.height;
    output.frame.profile = request.profile;
    output.stats.source_count = impl_->scene->count;
    output.stats.resident_bytes = account.live;
    const uint32_t count = impl_->gpu.count;
    DeviceBuffer<Ellipse> ordered;
    uint32_t visible = 0;
    Timer timer;
    {
        DeviceBuffer<Ellipse> projected(count, account);
        DeviceBuffer<uint32_t> keys_in(count, account), keys_out(count, account),
            indices_in(count, account), indices_out(count, account), counter(1, account);
        checked(cudaMemset(counter.get(), 0, 4), "Visible counter reset");
        timer.begin();
        project_kernel<<<(count + 255) / 256, 256>>>(impl_->gpu, fit_camera(*impl_->scene, request),
                                                     projected.get(), keys_in.get(),
                                                     indices_in.get(), counter.get());
        output.stats.project_ms = timer.end();
        checked(cudaMemcpy(&visible, counter.get(), 4, cudaMemcpyDeviceToHost),
                "Visible count readback");
        size_t scratch_bytes = 0;
        checked(cub::DeviceRadixSort::SortPairs(nullptr, scratch_bytes, keys_in.get(),
                                                keys_out.get(), indices_in.get(), indices_out.get(),
                                                count),
                "Sort scratch");
        DeviceBuffer<uint8_t> scratch(scratch_bytes, account);
        timer.begin();
        checked(cub::DeviceRadixSort::SortPairs(scratch.get(), scratch_bytes, keys_in.get(),
                                                keys_out.get(), indices_in.get(), indices_out.get(),
                                                count),
                "Stable radix sort");
        output.stats.sort_ms = timer.end();
        ordered = DeviceBuffer<Ellipse>(visible, account);
        timer.begin();
        if (visible)
            gather_kernel<<<(visible + 255) / 256, 256>>>(projected.get(), indices_out.get(),
                                                          ordered.get(), visible);
        output.stats.gather_ms = timer.end();
    }
    output.stats.visible_count = visible;
    if (request.capture_projection)
    {
        if (visible > 8'000'000)
            throw std::runtime_error("Diagnostic projection exceeds capture budget");
        output.debug_projection.resize(visible);
        timer.begin();
        if (visible)
            checked(cudaMemcpy(output.debug_projection.data(), ordered.get(), ordered.bytes(),
                               cudaMemcpyDeviceToHost),
                    "Projected readback");
        output.stats.readback_ms = timer.end();
    }
    {
        timer.begin();
        DeviceBuffer<uint64_t> counts(size_t(visible) + 1, account),
            offsets(size_t(visible) + 1, account);
        count_tiles<<<(visible + 256) / 256, 256>>>(ordered.get(), counts.get(), visible,
                                                    request.width, request.height);
        size_t scan_bytes = 0;
        checked(cub::DeviceScan::ExclusiveSum(nullptr, scan_bytes, counts.get(), offsets.get(),
                                              size_t(visible) + 1),
                "Tile prefix scratch");
        DeviceBuffer<uint8_t> scan_scratch(scan_bytes, account);
        checked(cub::DeviceScan::ExclusiveSum(scan_scratch.get(), scan_bytes, counts.get(),
                                              offsets.get(), size_t(visible) + 1),
                "Tile prefix");
        uint64_t references = 0;
        checked(cudaMemcpy(&references, offsets.get() + visible, 8, cudaMemcpyDeviceToHost),
                "Tile reference count");
        if (references > 64'000'000)
            throw std::runtime_error("Tile references exceed 64 million limit");
        output.stats.tile_references = references;
        DeviceBuffer<uint64_t> keys_in(size_t(references), account),
            keys_out(size_t(references), account);
        if (visible)
            emit_tiles<<<(visible + 255) / 256, 256>>>(ordered.get(), offsets.get(), keys_in.get(),
                                                       visible, request.width, request.height);
        size_t sort_bytes = 0;
        if (references)
            checked(cub::DeviceRadixSort::SortKeys(nullptr, sort_bytes, keys_in.get(),
                                                   keys_out.get(), references),
                    "Tile sort scratch");
        DeviceBuffer<uint8_t> sort_scratch(sort_bytes, account);
        if (references)
            checked(cub::DeviceRadixSort::SortKeys(sort_scratch.get(), sort_bytes, keys_in.get(),
                                                   keys_out.get(), references),
                    "Tile/rank sort");
        const uint32_t tiles_x = (request.width + 15) / 16, tiles_y = (request.height + 15) / 16;
        DeviceBuffer<uint2> ranges(size_t(tiles_x) * tiles_y, account);
        checked(cudaMemset(ranges.get(), 0, ranges.bytes()), "Tile ranges reset");
        if (references)
            ranges_kernel<<<(uint32_t(references) + 255) / 256, 256>>>(keys_out.get(), ranges.get(),
                                                                       uint32_t(references));
        DeviceBuffer<uchar4> pixels(size_t(request.width) * request.height, account);
        raster_kernel<<<dim3(tiles_x, tiles_y), dim3(16, 16)>>>(ordered.get(), keys_out.get(),
                                                                ranges.get(), pixels.get(),
                                                                request.width, request.height);
        output.stats.draw_ms = timer.end();
        output.frame.rgba.resize(pixels.bytes());
        timer.begin();
        checked(cudaMemcpy(output.frame.rgba.data(), pixels.get(), pixels.bytes(),
                           cudaMemcpyDeviceToHost),
                "Image readback");
        output.stats.readback_ms = timer.end();
    }
    output.stats.peak_gpu_bytes = account.peak;
    return output;
}
void CudaRenderer::self_test(int device)
{
    checked(cudaSetDevice(device), "Select GPU");
    Accounting accounting;
    std::vector<uint32_t> keys(1000003), indices(keys.size());
    for (size_t index = 0; index < keys.size(); ++index)
    {
        keys[index] = index % 65537 == 0 ? UINT32_MAX : uint32_t((index * 2654435761ull) % 257);
        indices[index] = uint32_t(index);
    }
    auto expected = indices;
    std::stable_sort(expected.begin(), expected.end(),
                     [&](uint32_t left, uint32_t right) { return keys[left] < keys[right]; });
    DeviceBuffer<uint32_t> keys_in(keys.size(), accounting), keys_out(keys.size(), accounting),
        values_in(keys.size(), accounting), values_out(keys.size(), accounting);
    checked(cudaMemcpy(keys_in.get(), keys.data(), keys_in.bytes(), cudaMemcpyHostToDevice),
            "Self-test input");
    checked(cudaMemcpy(values_in.get(), indices.data(), values_in.bytes(), cudaMemcpyHostToDevice),
            "Self-test input");
    size_t size = 0;
    checked(cub::DeviceRadixSort::SortPairs(nullptr, size, keys_in.get(), keys_out.get(),
                                            values_in.get(), values_out.get(), keys.size()),
            "Self-test scratch");
    DeviceBuffer<uint8_t> scratch(size, accounting);
    checked(cub::DeviceRadixSort::SortPairs(scratch.get(), size, keys_in.get(), keys_out.get(),
                                            values_in.get(), values_out.get(), keys.size()),
            "Self-test sort");
    std::vector<uint32_t> output(keys.size());
    checked(cudaMemcpy(output.data(), values_out.get(), values_out.bytes(), cudaMemcpyDeviceToHost),
            "Self-test result");
    if (output != expected)
        throw std::runtime_error("Stable GPU sort self-test failed");
}
} // namespace gs::server
