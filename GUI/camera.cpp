#include "camera.h"

#include <algorithm>
#include <cmath>

namespace gs::desktop
{
namespace
{
constexpr double pi = 3.14159265358979323846;
Double3 add(Double3 a, Double3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Double3 scale(Double3 a, double n) { return {a.x * n, a.y * n, a.z * n}; }
Double3 right(double yaw) { return {std::cos(yaw), 0, -std::sin(yaw)}; }
Double3 up(double yaw, double pitch)
{
    return {-std::sin(yaw) * std::sin(pitch), std::cos(pitch),
            -std::cos(yaw) * std::sin(pitch)};
}
Double3 back(double yaw, double pitch)
{
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
            std::cos(yaw) * std::cos(pitch)};
}
} // namespace

bool CameraController::fit(const SplatScene &scene, render::Viewport viewport, float max_stddev)
{
    if (!viewport.physical_width || !viewport.physical_height ||
        !std::isfinite(scene.maxScale) || scene.maxScale <= 0 ||
        !std::isfinite(max_stddev) || max_stddev <= 0)
        return false;
    const auto &b = scene.bounds;
    const Double3 focus{b.min.x / 2 + b.max.x / 2, b.min.y / 2 + b.max.y / 2,
                        b.min.z / 2 + b.max.z / 2};
    const double support = scene.maxScale * max_stddev;
    const double hx = (b.max.x - b.min.x) / 2 + support;
    const double hy = (b.max.y - b.min.y) / 2 + support;
    const double hz = (b.max.z - b.min.z) / 2 + support;
    const double aspect = double(viewport.physical_width) / viewport.physical_height;
    const double half_fov = camera_.vertical_fov_radians / 2;
    const double horizontal = std::atan(std::tan(half_fov) * aspect);
    const double distance = std::max(hx / std::tan(horizontal), hy / std::tan(half_fov)) * 1.25 + hz;
    const double scene_scale = std::max({hx, hy, hz, 0.01});
    if (!std::isfinite(distance) || distance <= 0 || distance > 1e25 ||
        !std::isfinite(focus.x) || !std::isfinite(focus.y) || !std::isfinite(focus.z))
        return false;
    focus_ = focus;
    distance_ = distance;
    scene_scale_ = scene_scale;
    yaw_ = pitch_ = 0;
    camera_.near_plane = std::max(1e-5, (distance_ - hz) * 0.01);
    camera_.far_plane = std::min(1e30, std::max(distance_ + hz + scene_scale_ * 8,
                                                camera_.near_plane * 1000));
    update_camera();
    if (!valid_)
    {
        initial_ = camera_;
        initial_focus_ = focus_;
        initial_yaw_ = yaw_;
        initial_pitch_ = pitch_;
        initial_distance_ = distance_;
    }
    valid_ = true;
    return true;
}

void CameraController::reset()
{
    if (!valid_) return;
    camera_ = initial_;
    focus_ = initial_focus_;
    yaw_ = initial_yaw_;
    pitch_ = initial_pitch_;
    distance_ = initial_distance_;
}

void CameraController::set_mode(ViewMode mode) { mode_ = mode; }
void CameraController::set_flip_z(bool flip) { flip_z_ = flip; }

void CameraController::rotate(double dx, double dy)
{
    if (!valid_) return;
    yaw_ += dx * 0.004;
    pitch_ = std::clamp(pitch_ + dy * 0.004, -pi / 2 + 0.01, pi / 2 - 0.01);
    update_camera();
}

void CameraController::pan(double dx, double dy, double viewport_height)
{
    if (!valid_ || viewport_height <= 0) return;
    const double unit = 2 * distance_ * std::tan(camera_.vertical_fov_radians / 2) /
                        viewport_height;
    focus_ = add(focus_, add(scale(right(yaw_), -dx * unit),
                              scale(up(yaw_, pitch_), dy * unit)));
    update_camera();
}

void CameraController::zoom(double wheel_steps)
{
    if (!valid_) return;
    distance_ = std::clamp(distance_ * std::exp(-wheel_steps * 0.14),
                           scene_scale_ * 0.001, scene_scale_ * 1e6);
    update_camera();
}

void CameraController::move(double dt, const std::array<bool, 6> &directions, bool fast)
{
    if (!valid_ || mode_ != ViewMode::Fly) return;
    dt = std::clamp(dt, 0.0, 0.05);
    Double3 velocity{};
    auto accumulate = [&](Double3 axis, double sign) {
        velocity = add(velocity, scale(axis, sign));
    };
    if (directions[0]) accumulate(back(yaw_, pitch_), -1);
    if (directions[1]) accumulate(back(yaw_, pitch_), 1);
    if (directions[2]) accumulate(right(yaw_), -1);
    if (directions[3]) accumulate(right(yaw_), 1);
    if (directions[4]) accumulate({0, 1, 0}, -1);
    if (directions[5]) accumulate({0, 1, 0}, 1);
    const double length = std::hypot(velocity.x, velocity.y, velocity.z);
    if (length == 0) return;
    const double step = scene_scale_ * (fast ? 8.0 : 1.6) * dt / length;
    focus_ = add(focus_, scale(velocity, step));
    update_camera();
}

render::CameraState CameraController::camera() const
{
    if (!flip_z_) return camera_;
    auto mirrored = camera_;
    mirrored.position_rub.z = 2 * focus_.z - camera_.position_rub.z;
    const auto q = camera_.orientation_xyzw;
    mirrored.orientation_xyzw = {-q.z, q.w, -q.x, q.y};
    return mirrored;
}

void CameraController::update_camera()
{
    camera_.position_rub = add(focus_, scale(back(yaw_, pitch_), distance_));
    const double sy = std::sin(yaw_ / 2), cy = std::cos(yaw_ / 2);
    const double sx = std::sin(-pitch_ / 2), cx = std::cos(-pitch_ / 2);
    camera_.orientation_xyzw = {sx * cy, cx * sy, -sx * sy, cx * cy};
}
} // namespace gs::desktop
