#pragma once

#include "render-core/renderer.h"

#include <array>

namespace gs::desktop
{
enum class ViewMode : uint8_t { Orbit, Fly };

class CameraController
{
  public:
    bool fit(const SplatScene &scene, render::Viewport viewport, float max_stddev);
    void reset();
    void set_mode(ViewMode mode);
    void set_flip_z(bool flip);
    void rotate(double dx, double dy);
    void pan(double dx, double dy, double viewport_height);
    void zoom(double wheel_steps);
    void move(double dt, const std::array<bool, 6> &directions, bool fast);
    render::CameraState camera() const;
    ViewMode mode() const { return mode_; }
    bool flip_z() const { return flip_z_; }
    bool valid() const { return valid_; }

  private:
    void update_camera();
    render::CameraState camera_{};
    render::CameraState initial_{};
    Double3 focus_{};
    Double3 initial_focus_{};
    double yaw_ = 0, pitch_ = 0, distance_ = 1;
    double initial_yaw_ = 0, initial_pitch_ = 0, initial_distance_ = 1;
    double scene_scale_ = 1;
    ViewMode mode_ = ViewMode::Orbit;
    bool flip_z_ = false;
    bool valid_ = false;
};
} // namespace gs::desktop
