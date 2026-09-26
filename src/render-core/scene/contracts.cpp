#include "contracts.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace gs::render::detail
{
namespace
{
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
    return s.count * (14ull + 3ull * ((s.shDegree + 1) * (s.shDegree + 1) - 1)) * sizeof(float);
}
uint64_t incremental_bytes(const SplatScene &s)
{
    // Attributes, projected ellipses, two key/value pairs, sort scratch and copy pages.
    return scene_bytes(s) + s.count * (48ull + 16ull) + 16ull * ((s.count + 511) / 512) * 4 +
           (8ull << 20);
}
bool fits_budget(uint64_t required, uint64_t budget, uint64_t usage)
{
    if (usage > budget)
        return false;
    const auto available = budget - usage;
    return required <= (available / 5) * 4 + (available % 5) * 4 / 5;
}
std::optional<RenderError> validate_scene(const SceneHandle &s)
{
    if (!s || s->count == 0 || s->count > 8'000'000 || s->shDegree > 3 || !s->storage ||
        !finite3(s->worldOrigin) || !finite3(s->bounds.min) || !finite3(s->bounds.max) ||
        !std::isfinite(s->maxScale) || s->maxScale <= 0 || s->bounds.min.x > s->bounds.max.x ||
        s->bounds.min.y > s->bounds.max.y || s->bounds.min.z > s->bounds.max.z)
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
