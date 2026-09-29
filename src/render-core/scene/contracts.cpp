#include "contracts.h"
#include <cmath>

namespace gs::render::detail
{
namespace
{
constexpr uint8_t max_sh_degree = 3;
bool finite3(Double3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
RenderError invalid(RenderErrorCode code)
{
    return {code, E_INVALIDARG, "Input contract"};
}
} // namespace
std::optional<RenderError> validate_quality(const QualityConfig &q)
{
    if (q.sort_mode > SortMode::ViewDepth || q.sh_degree_cap > 3 || !std::isfinite(q.max_stddev) ||
        q.max_stddev <= 0 || q.max_stddev > 8 || !std::isfinite(q.min_alpha) || q.min_alpha < 0 ||
        q.min_alpha >= 1 || !std::isfinite(q.covariance_blur_px2) || q.covariance_blur_px2 < 0 ||
        q.covariance_blur_px2 > 64 || !std::isfinite(q.max_pixel_radius_px) ||
        q.max_pixel_radius_px <= 0 || q.max_pixel_radius_px > 16384 || !q.premultiplied_alpha)
        return invalid(RenderErrorCode::InvalidQualityConfig);
    return {};
}
std::optional<RenderError> validate_camera(const CameraState &c)
{
    auto q = c.orientation_xyzw;
    const double norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!finite3(c.position_rub) || !std::isfinite(norm) || std::abs(norm - 1) > 1e-6 ||
        !std::isfinite(c.vertical_fov_radians) || c.vertical_fov_radians <= 0.001 ||
        c.vertical_fov_radians >= 3.13 || !std::isfinite(c.near_plane) || c.near_plane < 1e-6 ||
        !std::isfinite(c.far_plane) || c.far_plane <= c.near_plane || c.far_plane > 1e30)
        return invalid(RenderErrorCode::InvalidCamera);
    return {};
}
uint64_t scene_bytes(const SplatScene &s)
{
    if (s.count > UINT32_MAX || s.shDegree > max_sh_degree)
        return UINT64_MAX;
    const uint64_t sh_width = 3ull * ((s.shDegree + 1) * (s.shDegree + 1) - 1);
    if (s.count * 56 > UINT32_MAX || s.count * sh_width * sizeof(float) > UINT32_MAX)
        return UINT64_MAX;
    return s.count * (14ull + 3ull * ((s.shDegree + 1) * (s.shDegree + 1) - 1)) * sizeof(float);
}
uint64_t incremental_bytes(const SplatScene &s, uint8_t sh_degree_cap,
                           uint32_t point_stride)
{
    if (s.count > UINT32_MAX || s.shDegree > max_sh_degree || sh_degree_cap > max_sh_degree ||
        point_stride == 0 || point_stride > 16 || (point_stride & (point_stride - 1)))
        return UINT64_MAX;
    const auto n = (s.count + point_stride - 1) / point_stride;
    const auto base_bytes = n * 56ull;
    const auto degree = (std::min)(s.shDegree, sh_degree_cap);
    const auto sh_floats = 3ull * ((degree + 1) * (degree + 1) - 1);
    const auto sh_bytes = n * sh_floats * sizeof(float);
    if (base_bytes > UINT32_MAX || sh_bytes > UINT32_MAX)
        return UINT64_MAX;
    const auto attributes = base_bytes + sh_bytes;
    if (attributes < base_bytes)
        return UINT64_MAX;
    // Attributes, projected ellipses, two key/value pairs, sort scratch and copy pages.
    return attributes + n * (40ull + 16ull) + 16ull * ((n + 511) / 512) * 4 +
           upload_reserve_bytes;
}
bool fits_budget(uint64_t required, uint64_t budget, uint64_t usage)
{
    if (usage > budget)
        return false;
    const auto available = budget - usage;
    return required <= (available / 5) * 4 + (available % 5) * 4 / 5;
}
bool fits_scene_budgets(uint64_t scene_required, uint64_t upload_required,
                        uint64_t local_budget, uint64_t local_usage,
                        uint64_t nonlocal_budget, uint64_t nonlocal_usage, bool uma)
{
    return fits_budget(scene_required, local_budget, local_usage) &&
           (uma || fits_budget(upload_required, nonlocal_budget, nonlocal_usage));
}
std::optional<RenderError> validate_scene(const SceneHandle &s)
{
    if (!s || s->count == 0 || scene_bytes(*s) == UINT64_MAX ||
        !s->storage || !finite3(s->worldOrigin) || !finite3(s->bounds.min) ||
        !finite3(s->bounds.max) || !std::isfinite(s->maxScale) || s->maxScale <= 0 ||
        s->bounds.min.x > s->bounds.max.x || s->bounds.min.y > s->bounds.max.y ||
        s->bounds.min.z > s->bounds.max.z)
        return invalid(RenderErrorCode::InvalidScene);
    const auto n = s->count;
    if (s->centerLocal.size() != 3 * n || s->scale.size() != 3 * n || s->rotation.size() != 4 * n ||
        s->opacity.size() != n || s->rgb0.size() != 3 * n ||
        s->shRest.size() != 3 * n * ((s->shDegree + 1) * (s->shDegree + 1) - 1))
        return invalid(RenderErrorCode::InvalidScene);
    return {};
}
std::array<float, 3> relative_camera(const SplatScene &s, const CameraState &c)
{
    return {float(c.position_rub.x - s.worldOrigin.x), float(c.position_rub.y - s.worldOrigin.y),
            float(c.position_rub.z - s.worldOrigin.z)};
}
std::array<float, 9> world_to_camera(const Quaterniond &q)
{
    return {float(1 - 2 * (q.y * q.y + q.z * q.z)), float(2 * (q.x * q.y + q.z * q.w)),
            float(2 * (q.x * q.z - q.y * q.w)),     float(2 * (q.x * q.y - q.z * q.w)),
            float(1 - 2 * (q.x * q.x + q.z * q.z)), float(2 * (q.y * q.z + q.x * q.w)),
            float(2 * (q.x * q.z + q.y * q.w)),     float(2 * (q.y * q.z - q.x * q.w)),
            float(1 - 2 * (q.x * q.x + q.y * q.y))};
}
} // namespace gs::render::detail
