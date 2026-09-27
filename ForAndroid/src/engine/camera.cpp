#include "engine/camera.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <numeric>

namespace gs::android::engine
{
namespace
{
constexpr double pi = std::numbers::pi;
bool finite(Double3 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
Double3 add(Double3 a, Double3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Double3 subtract(Double3 a, Double3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Double3 scale(Double3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Double3 cross(Double3 a, Double3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Double3 rotate(const std::array<double, 4> &q, Double3 v)
{
    const Double3 axis{q[0], q[1], q[2]};
    return add(v, scale(cross(axis, add(cross(axis, v), scale(v, q[3]))), 2));
}
bool viewport_valid(uint32_t width, uint32_t height)
{
    return width > 0 && height > 0 && width <= 16384 && height <= 16384;
}
} // namespace

bool CameraController::fit(const SplatScene &scene, uint32_t width, uint32_t height)
{
    if (!viewport_valid(width, height) || !scene.count || !finite(scene.bounds.min) ||
        !finite(scene.bounds.max) || !finite(scene.worldOrigin) ||
        !std::isfinite(scene.maxScale) || scene.maxScale <= 0)
        return false;
    const auto extent = subtract(scene.bounds.max, scene.bounds.min);
    if (!finite(extent) || extent.x < 0 || extent.y < 0 || extent.z < 0)
        return false;
    auto next = *this;
    next.width_ = width;
    next.height_ = height;
    next.center_ = {std::midpoint(scene.bounds.min.x, scene.bounds.max.x),
                    std::midpoint(scene.bounds.min.y, scene.bounds.max.y),
                    std::midpoint(scene.bounds.min.z, scene.bounds.max.z)};
    next.origin_ = scene.worldOrigin;
    next.focus_ = next.center_;
    next.radius_ = std::max(1e-4, std::hypot(extent.x / 2, extent.y / 2, extent.z / 2) +
                                      scene.maxScale * 3.0);
    const double half_vertical = next.camera_.vertical_fov / 2;
    const double half_horizontal = std::atan(std::tan(half_vertical) * width / height);
    next.distance_ = next.radius_ * 1.1 / std::sin(std::min(half_vertical, half_horizontal));
    next.has_scene_ = true;
    next.mode_ = ViewMode::Orbit;
    next.yaw_ = next.pitch_ = 0;
    next.orient();
    next.clip_planes();
    next.initial_ = next.camera_;
    return commit(next);
}

bool CameraController::resize(uint32_t width, uint32_t height)
{
    if (!viewport_valid(width, height)) return false;
    width_ = width;
    height_ = height;
    return true;
}

void CameraController::orient()
{
    const double sy = std::sin(yaw_ / 2), cy = std::cos(yaw_ / 2);
    const double sp = std::sin(pitch_ / 2), cp = std::cos(pitch_ / 2);
    camera_.orientation = {cy * sp, sy * cp, -sy * sp, cy * cp};
    if (mode_ == ViewMode::Orbit)
        camera_.position = add(focus_, rotate(camera_.orientation, {0, 0, distance_}));
}

void CameraController::clip_planes()
{
    const auto relative = subtract(camera_.position, center_);
    const double distance = std::hypot(relative.x, relative.y, relative.z);
    camera_.near_plane = std::max(1e-6, std::min(radius_ * 0.001,
                                      std::max(1e-6, (distance - radius_) * 0.5)));
    camera_.far_plane = std::max(camera_.near_plane * 2, distance + radius_ * 2 + 1);
}

bool CameraController::commit(const CameraController &next)
{
    const auto relative = subtract(next.camera_.position, next.origin_);
    if (!finite(relative) || std::max({std::abs(relative.x), std::abs(relative.y),
                                      std::abs(relative.z)}) > 1e19 ||
        !std::isfinite(next.radius_) || !std::isfinite(next.camera_.near_plane) ||
        !std::isfinite(next.camera_.far_plane) || next.camera_.far_plane > 1e30 ||
        next.camera_.far_plane <= next.camera_.near_plane)
        return false;
    *this = next;
    return true;
}

bool CameraController::orbit(double dx, double dy)
{
    if (!has_scene_ || mode_ != ViewMode::Orbit || !std::isfinite(dx) || !std::isfinite(dy))
        return false;
    auto next = *this;
    next.yaw_ = std::remainder(yaw_ - std::clamp(dx, -4096.0, 4096.0) * 0.005, 2 * pi);
    next.pitch_ = std::clamp(pitch_ - std::clamp(dy, -4096.0, 4096.0) * 0.005,
                             -pi / 2 + 0.001, pi / 2 - 0.001);
    next.orient();
    next.clip_planes();
    return commit(next);
}

bool CameraController::pan(double dx, double dy)
{
    if (!has_scene_ || mode_ != ViewMode::Orbit || !std::isfinite(dx) || !std::isfinite(dy))
        return false;
    auto next = *this;
    const double world_per_pixel = 2 * distance_ * std::tan(camera_.vertical_fov / 2) / height_;
    const auto shift = rotate(camera_.orientation, {-dx * world_per_pixel,
                                                     dy * world_per_pixel, 0});
    next.focus_ = add(focus_, shift);
    next.camera_.position = add(camera_.position, shift);
    next.clip_planes();
    return commit(next);
}

bool CameraController::dolly(double steps)
{
    if (!has_scene_ || mode_ != ViewMode::Orbit || !std::isfinite(steps)) return false;
    auto next = *this;
    next.distance_ = std::clamp(distance_ * std::exp(-std::clamp(steps, -100.0, 100.0) * 0.15),
                                radius_ * 1e-4, std::min(1e19, radius_ * 1e6));
    next.orient();
    next.clip_planes();
    return commit(next);
}

bool CameraController::look(double dx, double dy)
{
    if (!has_scene_ || mode_ != ViewMode::Fly || !std::isfinite(dx) || !std::isfinite(dy))
        return false;
    auto next = *this;
    next.yaw_ = std::remainder(yaw_ + std::clamp(dx, -4096.0, 4096.0) * 0.005, 2 * pi);
    next.pitch_ = std::clamp(pitch_ + std::clamp(dy, -4096.0, 4096.0) * 0.005,
                             -pi / 2 + 0.001, pi / 2 - 0.001);
    next.orient();
    return commit(next);
}

bool CameraController::fly(FlyMotion motion, double seconds)
{
    if (!has_scene_ || mode_ != ViewMode::Fly || !std::isfinite(seconds) || seconds < 0 ||
        !std::isfinite(motion.right) || !std::isfinite(motion.up) ||
        !std::isfinite(motion.forward))
        return false;
    auto next = *this;
    Double3 movement{std::clamp(motion.right, -1.0, 1.0),
                     std::clamp(motion.up, -1.0, 1.0),
                     -std::clamp(motion.forward, -1.0, 1.0)};
    const double norm = std::hypot(movement.x, movement.y, movement.z);
    if (norm > 1) movement = scale(movement, 1 / norm);
    next.camera_.position = add(camera_.position, rotate(camera_.orientation,
        scale(movement, radius_ * 0.5 * (motion.fast ? 4 : 1) * std::min(seconds, 0.1))));
    next.clip_planes();
    return commit(next);
}

bool CameraController::set_mode(ViewMode mode)
{
    if (mode != ViewMode::Orbit && mode != ViewMode::Fly) return false;
    if (mode == ViewMode::Orbit && mode_ == ViewMode::Fly)
        focus_ = add(camera_.position, rotate(camera_.orientation, {0, 0, -distance_}));
    mode_ = mode;
    return true;
}

void CameraController::set_flip_axes(uint8_t mask) { flip_axes_ = mask & 7; }

CameraPose CameraController::pose() const
{
    if (!has_scene_ || !flip_axes_) return camera_;
    auto mirrored = camera_;
    if (flip_axes_ & 1)
        mirrored.position.x = center_.x + (center_.x - camera_.position.x);
    if (flip_axes_ & 2)
        mirrored.position.y = center_.y + (center_.y - camera_.position.y);
    if (flip_axes_ & 4)
        mirrored.position.z = center_.z + (center_.z - camera_.position.z);
    if (!finite(mirrored.position)) return camera_;
    const auto &q = camera_.orientation;
    switch (flip_axes_)
    {
    case 1: mirrored.orientation = {q[0], -q[1], -q[2], q[3]}; break;
    case 2: mirrored.orientation = {q[1], q[0], q[3], q[2]}; break;
    case 3: mirrored.orientation = {-q[1], q[0], q[3], -q[2]}; break;
    case 4: mirrored.orientation = {-q[2], q[3], -q[0], q[1]}; break;
    case 5: mirrored.orientation = {q[2], q[3], -q[0], -q[1]}; break;
    case 6: mirrored.orientation = {q[3], -q[2], q[1], -q[0]}; break;
    case 7: mirrored.orientation = {q[3], q[2], -q[1], -q[0]}; break;
    }
    mirrored.horizontal_mirror = (std::popcount(static_cast<unsigned>(flip_axes_)) & 1) != 0;
    return mirrored;
}

void CameraController::reset()
{
    if (!has_scene_) return;
    camera_ = initial_;
    focus_ = center_;
    const auto offset = subtract(initial_.position, center_);
    distance_ = std::hypot(offset.x, offset.y, offset.z);
    yaw_ = pitch_ = 0;
}

} // namespace gs::android::engine
