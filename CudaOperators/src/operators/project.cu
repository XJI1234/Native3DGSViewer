#include "gs_cuda/project.h"
#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gs::cuda_ops
{
namespace
{
using render::detail::FrameConstants;
using render::detail::ProjectedEllipse;
using render::detail::DrawCounters;

struct V3 { float x, y, z; };
__device__ V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
__device__ V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
__device__ V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
template<int math> __device__ V3 divide(V3 a, float s)
{
    if constexpr (math & 1)
    {
        const float inverse = 1.f / s;
        if (isfinite(inverse)) return a * inverse;
    }
    return {a.x / s, a.y / s, a.z / s};
}
__device__ float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
__device__ V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
__device__ bool finite(V3 a) { return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }
template<int math> __device__ V3 normalize(V3 a)
{
    if constexpr (math & 2) return a * rsqrtf(dot(a, a));
    return divide<math>(a, sqrtf(dot(a, a)));
}
__device__ V3 load3(const float *data, uint32_t offset, uint32_t i)
{
    const float *p = data + uint64_t(offset) / 4 + uint64_t(i) * 3;
    return {p[0], p[1], p[2]};
}
__device__ V3 to_view(const FrameConstants &c, V3 p)
{
    return {dot({c.view_rows[0], c.view_rows[1], c.view_rows[2]}, p),
            dot({c.view_rows[4], c.view_rows[5], c.view_rows[6]}, p),
            dot({c.view_rows[8], c.view_rows[9], c.view_rows[10]}, p)};
}
__device__ V3 rotate(V3 q, float w, V3 p) { return p + cross(q, cross(q, p) + p * w) * 2; }
__device__ void reject(DrawCounters *arguments) { atomicAdd(&arguments->rejected, 1u); }
__device__ V3 sh_color(V3 rgb, V3 dir, const float *sh, uint32_t stride, uint32_t degree, uint32_t i)
{
    const float x = dir.x, y = dir.y, z = dir.z;
    // Same real SH convention and coefficient ordering as shaders/splat.hlsl.
    const float b[15]{
        -.4886025119029199f * y, .4886025119029199f * z, -.4886025119029199f * x,
        1.0925484305920792f * x * y, -1.0925484305920792f * y * z,
        .31539156525252005f * (2 * z * z - x * x - y * y), -1.0925484305920792f * x * z,
        .5462742152960396f * (x * x - y * y), -.5900435899266435f * y * (3 * x * x - y * y),
        2.890611442640554f * x * y * z, -.4570457994644658f * y * (4 * z * z - x * x - y * y),
        .3731763325901154f * z * (2 * z * z - 3 * x * x - 3 * y * y),
        -.4570457994644658f * x * (4 * z * z - x * x - y * y), 1.445305721320277f * z * (x * x - y * y),
        -.5900435899266435f * x * (x * x - 3 * y * y)};
    const uint32_t n = (degree + 1) * (degree + 1) - 1;
    for (uint32_t j = 0; j < n; ++j)
    {
        const float *p = sh + uint64_t(i) * stride + j * 3;
        rgb = rgb + V3{p[0], p[1], p[2]} * b[j];
    }
    return {fminf(1, fmaxf(0, rgb.x)), fminf(1, fmaxf(0, rgb.y)), fminf(1, fmaxf(0, rgb.z))};
}
template<int math> __global__ void project_kernel(FrameConstants c, const float *data, const float *sh,
    ProjectedEllipse *ellipses, uint32_t *keys, uint32_t *indexes, DrawCounters *arguments)
{
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= c.meta[0]) return;
    keys[i] = UINT32_MAX;
    indexes[i] = i;
    ellipses[i] = {};
    const V3 delta = load3(data, c.offsets[0], i) - V3{c.camera[0], c.camera[1], c.camera[2]};
    const V3 p = to_view(c, delta), s = load3(data, c.offsets[1], i);
    const float *qp = data + c.offsets[2] / 4 + uint64_t(i) * 4;
    const V3 q{qp[0], qp[1], qp[2]};
    const float w = qp[3], alpha = data[c.offsets[3] / 4 + i];
    const float support = fmaxf(s.x, fmaxf(s.y, s.z)) * c.quality[0], depth = -p.z;
    if (!finite(p) || !finite(s) || s.x <= 0 || s.y <= 0 || s.z <= 0 || !finite(q) || !isfinite(w) ||
        fabsf(dot(q, q) + w * w - 1) > .001f || !isfinite(alpha) || alpha < 0 || alpha > 1)
    { reject(arguments); return; }
    if (alpha <= c.quality[1] || depth + support < c.clip[0] || depth - support > c.clip[1]) return;
    const float d = fmaxf(depth, c.clip[0]);
    const V3 jx{c.viewport[0] / d, 0, c.viewport[0] * p.x / (d * d)};
    const V3 jy{0, -c.viewport[1] / d, -c.viewport[1] * p.y / (d * d)};
    const V3 a0 = to_view(c, rotate(q, w, {s.x, 0, 0}));
    const V3 a1 = to_view(c, rotate(q, w, {0, s.y, 0}));
    const V3 a2 = to_view(c, rotate(q, w, {0, 0, s.z}));
    const V3 tx{dot(jx, a0), dot(jx, a1), dot(jx, a2)}, ty{dot(jy, a0), dot(jy, a1), dot(jy, a2)};
    const float xx = dot(tx, tx) + c.quality[2], yy = dot(ty, ty) + c.quality[2], xy = dot(tx, ty);
    const float scale = fmaxf(xx, yy);
    if (!isfinite(scale) || scale <= 0) { reject(arguments); return; }
    const float xxn = xx / scale, yyn = yy / scale, xyn = xy / scale;
    const float diff = (xxn - yyn) * .5f;
    const float major_normalized = (xxn + yyn) * .5f + sqrtf(diff * diff + xyn * xyn);
    V3 gram;
    if constexpr (math & 2) gram = cross(tx * rsqrtf(scale), ty * rsqrtf(scale));
    else gram = cross(divide<math>(tx, sqrtf(scale)), divide<math>(ty, sqrtf(scale)));
    const float blur = c.quality[2] / scale;
    const float determinant = dot(gram, gram) + blur * (xxn + yyn - blur);
    const float major = scale * major_normalized, minor = scale * (determinant / major_normalized);
    if (!isfinite(major) || !isfinite(minor) || minor <= 0) { reject(arguments); return; }
    float ax = xx >= yy ? 1.f : 0.f, ay = xx >= yy ? 0.f : 1.f;
    if (fabsf(xyn) > 1e-10f)
    {
        const float length = sqrtf(xyn * xyn + (major_normalized - xxn) * (major_normalized - xxn));
        ax = xyn / length; ay = (major_normalized - xxn) / length;
    }
    const float r0 = fminf(c.quality[0] * sqrtf(major), c.quality[3]);
    const float r1 = fminf(c.quality[0] * sqrtf(minor), c.quality[3]);
    ProjectedEllipse e{};
    e.axis0[0] = ax * r0; e.axis0[1] = ay * r0;
    e.axis1[0] = -ay * r1; e.axis1[1] = ax * r1;
    e.center[0] = c.viewport[2] * .5f + c.viewport[0] * p.x / d;
    e.center[1] = c.viewport[3] * .5f - c.viewport[1] * p.y / d;
    const float ex = fabsf(e.axis0[0]) + fabsf(e.axis1[0]), ey = fabsf(e.axis0[1]) + fabsf(e.axis1[1]);
    if (e.center[0] + ex < 0 || e.center[1] + ey < 0 || e.center[0] - ex > c.viewport[2] || e.center[1] - ey > c.viewport[3])
    { ellipses[i] = e; return; }
    const float largest = fmaxf(fabsf(delta.x), fmaxf(fabsf(delta.y), fabsf(delta.z)));
    if (c.meta[3] == 0 && largest > 1e19f) { reject(arguments); return; }
    const float metric = c.meta[3] == 0 ? dot(delta, delta) : fmaxf(depth, 0);
    if (!isfinite(metric)) { reject(arguments); return; }
    const V3 direction = largest > 0 ? normalize<math>(divide<math>(delta, largest)) : V3{0, 0, 1};
    const V3 rgb = sh_color(load3(data, c.offsets[4], i), direction, sh, c.meta[2], c.meta[1], i);
    e.color[0] = rgb.x; e.color[1] = rgb.y; e.color[2] = rgb.z; e.color[3] = alpha;
    ellipses[i] = e;
    keys[i] = ~__float_as_uint(metric);
    if (keys[i] == UINT32_MAX) keys[i] = UINT32_MAX - 1;
    atomicAdd(&arguments->instance_count, 1u);
}
__global__ void reset(DrawCounters *arguments) { *arguments = {4, 0, 0, 0, 0}; }
void checked(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
}

void project(Context &context, const FrameConstants &c, SharedBuffer &attributes, SharedBuffer &sh,
    SharedBuffer &ellipses, SharedBuffer &keys, SharedBuffer &indexes, SharedBuffer &arguments, uint32_t block_size, ProjectMath math)
{
    if ((block_size != 128 && block_size != 256) || c.meta[1] > 3 ||
        c.meta[2] < 3 * ((c.meta[1] + 1) * (c.meta[1] + 1) - 1) || c.meta[3] > 1 ||
        !std::isfinite(c.clip[0]) || c.clip[0] <= 0 || uint32_t(math) > 3)
        throw std::invalid_argument("Invalid projection layout or launch configuration");
    const uint64_t count = c.meta[0];
    for (auto pair : {std::pair{&ellipses, count * sizeof(ProjectedEllipse)},
                      std::pair{&keys, count * 4}, std::pair{&indexes, count * 4},
                      std::pair{&arguments, uint64_t(sizeof(DrawCounters))}})
        if (pair.first->bytes() < pair.second) throw std::out_of_range("Projection output capacity");
    const uint32_t sizes[]{12, 12, 16, 4, 12};
    for (uint32_t field = 0; field < 5; ++field)
        if (c.offsets[field] % 4 || uint64_t(c.offsets[field]) + count * sizes[field] > attributes.bytes())
            throw std::out_of_range("Projection attribute capacity or alignment");
    if (count * c.meta[2] * 4 > sh.bytes()) throw std::out_of_range("Projection SH capacity");
    SharedBuffer *buffers[]{&attributes, &sh, &ellipses, &keys, &indexes, &arguments};
    for (int i = 0; i < 6; ++i) for (int j = i + 1; j < 6; ++j)
        if (buffers[i] == buffers[j]) throw std::invalid_argument("Aliased projection buffers");
    auto stream = static_cast<cudaStream_t>(context.native_stream());
    auto *counters = static_cast<DrawCounters *>(arguments.device_pointer());
    reset<<<1, 1, 0, stream>>>(counters);
    checked(cudaGetLastError());
    if (!count) return;
    const auto launch = [&]<int mode>() {
        project_kernel<mode><<<uint32_t((count + block_size - 1) / block_size), block_size, 0, stream>>>(c,
            static_cast<const float *>(attributes.device_pointer()), static_cast<const float *>(sh.device_pointer()),
            static_cast<ProjectedEllipse *>(ellipses.device_pointer()), static_cast<uint32_t *>(keys.device_pointer()),
            static_cast<uint32_t *>(indexes.device_pointer()), counters);
    };
    switch (math)
    {
    case ProjectMath::Reference: launch.operator()<0>(); break;
    case ProjectMath::FactoredReciprocal: launch.operator()<1>(); break;
    case ProjectMath::ReciprocalSqrt: launch.operator()<2>(); break;
    case ProjectMath::FactoredAndRsqrt: launch.operator()<3>(); break;
    }
    checked(cudaGetLastError());
}
}
