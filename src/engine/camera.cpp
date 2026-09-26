#include "native3dgs/camera.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace gs::engine
{
namespace
{
using namespace render;
constexpr double pi = std::numbers::pi;
std::optional<RenderError> invalid()
{
    return RenderError{RenderErrorCode::InvalidCamera, E_INVALIDARG, "Camera input"};
}
bool finite(Double3 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
Double3 add(Double3 a, Double3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Double3 subtract(Double3 a, Double3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Double3 scale(Double3 a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}
Double3 cross(Double3 a, Double3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Double3 rotate(Quaterniond q, Double3 v)
{
    Double3 axis{q.x, q.y, q.z};
    return add(v, scale(cross(axis, add(cross(axis, v), scale(v, q.w))), 2));
}
bool valid_viewport(Viewport v)
{
    return v.physical_width && v.physical_height && v.physical_width <= 16384 &&
           v.physical_height <= 16384;
}
} // namespace
std::optional<render::RenderError> CameraController::fit_scene(const SplatScene &scene,
                                                               render::QualityConfig quality,
                                                               render::Viewport viewport)
{
    if (!valid_viewport(viewport) || !scene.count || !finite(scene.bounds.min) ||
        !finite(scene.bounds.max) || !finite(scene.worldOrigin) || !std::isfinite(scene.maxScale) ||
        scene.maxScale <= 0 || !std::isfinite(quality.max_stddev) || quality.max_stddev <= 0 ||
        quality.max_stddev > 8)
        return invalid();
    auto extent = subtract(scene.bounds.max, scene.bounds.min);
    if (!finite(extent) || extent.x < 0 || extent.y < 0 || extent.z < 0)
        return invalid();
    CameraController next;
    next.viewport_ = viewport;
    next.center_ = {std::midpoint(scene.bounds.min.x, scene.bounds.max.x),
                    std::midpoint(scene.bounds.min.y, scene.bounds.max.y),
                    std::midpoint(scene.bounds.min.z, scene.bounds.max.z)};
    next.origin_ = scene.worldOrigin;
    next.focus_ = next.center_;
    next.radius_ = std::max(1e-4, std::hypot(extent.x / 2, extent.y / 2, extent.z / 2) +
                                      scene.maxScale * quality.max_stddev);
    const double half_vertical = next.camera_.vertical_fov_radians / 2;
    const double half_horizontal =
        std::atan(std::tan(half_vertical) * viewport.physical_width / viewport.physical_height);
    next.distance_ = next.radius_ * 1.1 / std::sin(std::min(half_vertical, half_horizontal));
    next.has_scene_ = true;
    next.orient();
    const auto actual_offset = subtract(next.camera_.position_rub, next.center_);
    if (std::hypot(actual_offset.x, actual_offset.y, actual_offset.z) < next.distance_ / 1.1)
        return invalid();
    next.clip_planes();
    next.initial_ = has_scene_ ? initial_ : next.camera_;
    next.mode_ = mode_;
    return commit(next);
}
void CameraController::orient()
{
    const double sy = std::sin(yaw_ / 2), cy = std::cos(yaw_ / 2);
    const double sp = std::sin(pitch_ / 2), cp = std::cos(pitch_ / 2);
    camera_.orientation_xyzw = {cy * sp, sy * cp, -sy * sp, cy * cp};
    if (mode_ == ViewMode::Orbit)
        camera_.position_rub = add(focus_, rotate(camera_.orientation_xyzw, {0, 0, distance_}));
}
void CameraController::clip_planes()
{
    const auto relative = subtract(camera_.position_rub, center_);
    const double distance = std::hypot(relative.x, relative.y, relative.z);
    camera_.near_plane =
        std::max(1e-6, std::min(radius_ * 0.001, std::max(1e-6, (distance - radius_) * 0.5)));
    camera_.far_plane = std::max(camera_.near_plane * 2, distance + radius_ * 2 + 1);
}
std::optional<render::RenderError> CameraController::commit(const CameraController &next)
{
    const auto relative = subtract(next.camera_.position_rub, next.origin_);
    if (!finite(relative) ||
        std::max({std::abs(relative.x), std::abs(relative.y), std::abs(relative.z)}) > 1e19 ||
        !std::isfinite(next.radius_) || !std::isfinite(next.camera_.near_plane) ||
        !std::isfinite(next.camera_.far_plane) || next.camera_.far_plane > 1e30 ||
        next.camera_.far_plane <= next.camera_.near_plane)
        return invalid();
    *this = next;
    return {};
}
std::optional<render::RenderError> CameraController::resize(render::Viewport viewport)
{
    if (!valid_viewport(viewport))
        return invalid();
    viewport_ = viewport;
    return {};
}
std::optional<render::RenderError> CameraController::orbit(double dx, double dy)
{
    if (!has_scene_ || mode_ != ViewMode::Orbit || !std::isfinite(dx) || !std::isfinite(dy))
        return invalid();
    auto next = *this;
    next.yaw_ = std::remainder(yaw_ + std::clamp(dx, -4096.0, 4096.0) * 0.005, 2 * pi);
    next.pitch_ = std::clamp(pitch_ + std::clamp(dy, -4096.0, 4096.0) * 0.005, -pi / 2 + 0.001,
                             pi / 2 - 0.001);
    next.orient();
    next.clip_planes();
    return commit(next);
}
std::optional<render::RenderError> CameraController::pan(double dx, double dy)
{
    if (!has_scene_ || mode_ != ViewMode::Orbit || !std::isfinite(dx) || !std::isfinite(dy))
        return invalid();
    auto next = *this;
    const double world_per_pixel =
        2 * distance_ * std::tan(camera_.vertical_fov_radians / 2) / viewport_.physical_height;
    const auto shift =
        rotate(camera_.orientation_xyzw, {-dx * world_per_pixel, dy * world_per_pixel, 0});
    next.focus_ = add(focus_, shift);
    next.camera_.position_rub = add(camera_.position_rub, shift);
    next.clip_planes();
    return commit(next);
}
std::optional<render::RenderError> CameraController::dolly(double steps)
{
    if (!has_scene_ || mode_ != ViewMode::Orbit || !std::isfinite(steps))
        return invalid();
    auto next = *this;
    next.distance_ = std::clamp(distance_ * std::exp(-std::clamp(steps, -100.0, 100.0) * 0.15),
                                radius_ * 1e-4, std::min(1e19, radius_ * 1e6));
    next.orient();
    next.clip_planes();
    return commit(next);
}
std::optional<render::RenderError> CameraController::look(double dx, double dy)
{
    if (!has_scene_ || mode_ != ViewMode::Fly || !std::isfinite(dx) || !std::isfinite(dy))
        return invalid();
    auto next = *this;
    next.yaw_ = std::remainder(yaw_ + std::clamp(dx, -4096.0, 4096.0) * 0.005, 2 * pi);
    next.pitch_ = std::clamp(pitch_ + std::clamp(dy, -4096.0, 4096.0) * 0.005, -pi / 2 + 0.001,
                             pi / 2 - 0.001);
    next.orient();
    return commit(next);
}
std::optional<render::RenderError> CameraController::fly(FlyMotion motion, double seconds)
{
    if (!has_scene_ || mode_ != ViewMode::Fly || !std::isfinite(seconds) || seconds < 0 ||
        !std::isfinite(motion.right) || !std::isfinite(motion.up) || !std::isfinite(motion.forward))
        return invalid();
    auto next = *this;
    Double3 movement{std::clamp(motion.right, -1.0, 1.0), std::clamp(motion.up, -1.0, 1.0),
                     -std::clamp(motion.forward, -1.0, 1.0)};
    const double norm = std::hypot(movement.x, movement.y, movement.z);
    const double speed = radius_ * 0.5 * (motion.fast ? 4 : 1) * std::min(seconds, 0.1);
    if (norm > 1)
        movement = scale(movement, 1 / norm);
    next.camera_.position_rub =
        add(camera_.position_rub, rotate(camera_.orientation_xyzw, scale(movement, speed)));
    next.clip_planes();
    return commit(next);
}
std::optional<render::RenderError> CameraController::set_mode(ViewMode mode)
{
    if (mode > ViewMode::Fly)
        return invalid();
    if (mode == ViewMode::Orbit && mode_ == ViewMode::Fly)
        focus_ = add(camera_.position_rub, rotate(camera_.orientation_xyzw, {0, 0, -distance_}));
    mode_ = mode;
    return {};
}
void CameraController::reset()
{
    if (!has_scene_)
        return;
    camera_ = initial_;
    focus_ = center_;
    auto offset = subtract(initial_.position_rub, center_);
    distance_ = std::hypot(offset.x, offset.y, offset.z);
    yaw_ = pitch_ = 0;
}
} // namespace gs::engine
