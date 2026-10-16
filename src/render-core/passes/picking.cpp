#include "picking.h"
#include <algorithm>
#include <cmath>

namespace gs::render
{
namespace
{
std::array<double, 9> view_rows(Quaterniond q)
{
    return {1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y + q.z * q.w),
            2 * (q.x * q.z - q.y * q.w),     2 * (q.x * q.y - q.z * q.w),
            1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z + q.x * q.w),
            2 * (q.x * q.z + q.y * q.w),     2 * (q.y * q.z - q.x * q.w),
            1 - 2 * (q.x * q.x + q.y * q.y)};
}
bool finite(Double3 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
} // namespace
std::optional<std::array<ScreenPoint, 2>> project_segment(Double3 a, Double3 b, CameraState camera,
                                                          Viewport viewport)
{
    if (!viewport.physical_width || !viewport.physical_height || !finite(a) || !finite(b) ||
        !(camera.near_plane > 0) || !(camera.far_plane > camera.near_plane))
        return {};
    const double f = viewport.physical_height / (2 * std::tan(camera.vertical_fov_radians / 2));
    if (!(f > 0) || !std::isfinite(f))
        return {};
    const auto m = view_rows(camera.orientation_xyzw);
    const auto to_view = [&](Double3 p) {
        const double x = p.x - camera.position_rub.x, y = p.y - camera.position_rub.y,
                     z = p.z - camera.position_rub.z;
        return Double3{m[0] * x + m[1] * y + m[2] * z, m[3] * x + m[4] * y + m[5] * z,
                       -(m[6] * x + m[7] * y + m[8] * z)};
    };
    a = to_view(a);
    b = to_view(b);
    if (!finite(a) || !finite(b))
        return {};
    const double half_width = viewport.physical_width * 0.5;
    const double half_height = viewport.physical_height * 0.5;
    const auto planes = [&](Double3 p) {
        return std::array{p.z - camera.near_plane,     camera.far_plane - p.z,
                          half_width * p.z + f * p.x,  half_width * p.z - f * p.x,
                          half_height * p.z + f * p.y, half_height * p.z - f * p.y};
    };
    const auto start = planes(a), end = planes(b);
    double lo = 0, hi = 1;
    for (size_t i = 0; i < start.size(); ++i)
    {
        if (!std::isfinite(start[i]) || !std::isfinite(end[i]))
            return {};
        if (start[i] < 0 && end[i] < 0)
            return {};
        if (start[i] < 0)
            lo = (std::max)(lo, start[i] / (start[i] - end[i]));
        else if (end[i] < 0)
            hi = (std::min)(hi, start[i] / (start[i] - end[i]));
        if (lo > hi)
            return {};
    }
    const auto project = [&](double t) {
        const double x = std::lerp(a.x, b.x, t), y = std::lerp(a.y, b.y, t),
                     depth = std::lerp(a.z, b.z, t);
        return ScreenPoint{half_width + f * x / depth, half_height - f * y / depth, depth};
    };
    return std::array{project(lo), project(hi)};
}
std::optional<ScreenPoint> project_world(Double3 point, CameraState camera, Viewport viewport)
{
    if (!viewport.physical_width || !viewport.physical_height || !finite(point))
        return {};
    const auto m = view_rows(camera.orientation_xyzw);
    const double x = point.x - camera.position_rub.x, y = point.y - camera.position_rub.y,
                 z = point.z - camera.position_rub.z;
    const double depth = -(m[6] * x + m[7] * y + m[8] * z);
    if (depth < camera.near_plane || depth > camera.far_plane)
        return {};
    const double f = viewport.physical_height / (2 * std::tan(camera.vertical_fov_radians / 2));
    ScreenPoint result{
        viewport.physical_width * 0.5 + f * (m[0] * x + m[1] * y + m[2] * z) / depth,
        viewport.physical_height * 0.5 - f * (m[3] * x + m[4] * y + m[5] * z) / depth, depth};
    if (!std::isfinite(result.x_px) || !std::isfinite(result.y_px) || !std::isfinite(depth))
        return {};
    return result;
}
std::optional<Double3> unproject_pixel(double x, double y, double depth, CameraState camera,
                                       Viewport viewport)
{
    if (!viewport.physical_width || !viewport.physical_height || !std::isfinite(x) ||
        !std::isfinite(y) || !std::isfinite(depth) || depth < camera.near_plane ||
        depth > camera.far_plane)
        return {};
    const double f = viewport.physical_height / (2 * std::tan(camera.vertical_fov_radians / 2));
    const double vx = (std::floor(x) + 0.5 - viewport.physical_width * 0.5) * depth / f;
    const double vy = -(std::floor(y) + 0.5 - viewport.physical_height * 0.5) * depth / f;
    const auto m = view_rows(camera.orientation_xyzw);
    Double3 result{camera.position_rub.x + m[0] * vx + m[3] * vy - m[6] * depth,
                   camera.position_rub.y + m[1] * vx + m[4] * vy - m[7] * depth,
                   camera.position_rub.z + m[2] * vx + m[5] * vy - m[8] * depth};
    return finite(result) ? std::optional{result} : std::nullopt;
}
namespace detail
{
PickResult resolve_pick(PickRequest request, std::vector<PickCandidate> candidates)
{
    PickResult result{request.id};
    std::sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.rank > b.rank; });
    struct Weighted
    {
        double weight;
        float depth;
        uint32_t index;
    };
    std::vector<Weighted> valid;
    valid.reserve(candidates.size());
    double transmittance = 1, total = 0;
    for (auto candidate : candidates)
    {
        const double weight = transmittance * candidate.alpha;
        transmittance *= 1 - candidate.alpha;
        if (candidate.depth > 0 && std::isfinite(candidate.depth))
        {
            valid.push_back({weight, candidate.depth, candidate.source_index});
            total += weight;
        }
    }
    if (total < 0.1 || !std::isfinite(total))
        return result;
    std::sort(valid.begin(), valid.end(), [](auto a, auto b) {
        return a.depth == b.depth ? a.index < b.index : a.depth < b.depth;
    });
    double cumulative = 0;
    for (auto candidate : valid)
    {
        cumulative += candidate.weight;
        if (cumulative >= total * 0.5)
        {
            if (auto point = unproject_pixel(request.x_px, request.y_px, candidate.depth,
                                             request.camera, request.viewport))
            {
                result.status = PickStatus::Hit;
                result.depth = candidate.depth;
                result.position_rub = *point;
            }
            break;
        }
    }
    return result;
}
PickPass::PickPass(GpuDevice &gpu) : gpu_(gpu)
{
    std::array<D3D12_ROOT_PARAMETER, 6> p{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants = {0, 0, 40};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[1].Constants = {1, 0, 2};
    for (uint32_t i = 0; i < 3; ++i)
    {
        p[i + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        p[i + 2].Descriptor = {i, 0};
    }
    p[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    p[5].Descriptor = {0, 0};
    root_ = root_signature(gpu.device.Get(), p);
    for (auto pair : {std::pair{"pick_collect", &pipeline_}, std::pair{"pick_reset", &reset_}})
    {
        auto bytes = shader(pair.first);
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
        d.pRootSignature = root_.Get();
        d.CS = {bytes.data(), bytes.size()};
        check(gpu.device->CreateComputePipelineState(&d, IID_PPV_ARGS(pair.second)),
              "Pick pipeline");
    }
    output_ = gpu.buffer((pick_capacity + 1) * 16, D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    readback_ = gpu.buffer((pick_capacity + 1) * 16, D3D12_HEAP_TYPE_READBACK,
                           D3D12_RESOURCE_STATE_COPY_DEST);
}
void PickPass::record(ID3D12GraphicsCommandList *list, const SceneGpu &scene,
                      const FrameConstants &constants, const PickRequest &request)
{
    transition(list, scene.attributes.Get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(list, scene.projected.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(list, scene.sorting.values[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->SetComputeRootSignature(root_.Get());
    list->SetComputeRoot32BitConstants(0, 40, &constants, 0);
    const float pixel[2] = {float(std::floor(request.x_px) + 0.5),
                            float(std::floor(request.y_px) + 0.5)};
    list->SetComputeRoot32BitConstants(1, 2, pixel, 0);
    list->SetComputeRootShaderResourceView(2, scene.attributes->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(3, scene.projected->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(4, scene.sorting.values[0]->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(5, output_->GetGPUVirtualAddress());
    list->SetPipelineState(reset_.Get());
    list->Dispatch(1, 1, 1);
    uav_barrier(list);
    list->SetPipelineState(pipeline_.Get());
    const uint32_t groups = (scene.count + 255) / 256;
    list->Dispatch((std::min)(groups, 65535u), (groups + 65534) / 65535, 1);
    uav_barrier(list);
    transition(list, output_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readback_.Get(), 0, output_.Get(), 0, (pick_capacity + 1) * 16);
    transition(list, output_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(list, scene.attributes.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_COMMON);
    transition(list, scene.projected.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(list, scene.sorting.values[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
PickResult PickPass::read(const PickRequest &request)
{
    void *data = nullptr;
    D3D12_RANGE range{0, (pick_capacity + 1) * 16};
    check(readback_->Map(0, &range, &data), "Pick readback");
    const uint32_t count = *static_cast<const uint32_t *>(data);
    std::vector<PickCandidate> candidates;
    try
    {
        if (count <= pick_capacity)
        {
            auto start =
                reinterpret_cast<const PickCandidate *>(static_cast<const char *>(data) + 16);
            candidates.assign(start, start + count);
        }
    }
    catch (...)
    {
        D3D12_RANGE empty{};
        readback_->Unmap(0, &empty);
        throw;
    }
    D3D12_RANGE empty{};
    readback_->Unmap(0, &empty);
    if (count > pick_capacity)
        return {request.id, PickStatus::Overflow};
    return resolve_pick(request, std::move(candidates));
}
} // namespace detail
} // namespace gs::render
