#pragma once

#include "splat-types/scene.h"

#include <array>
#include <cstdint>

namespace gs::android::engine
{

enum class ViewMode : uint8_t { Orbit, Fly };

struct CameraPose
{
    Double3 position{};
    std::array<double, 4> orientation{0, 0, 0, 1};
    double vertical_fov = 0.8726646259971648;
    double near_plane = 0.01;
    double far_plane = 1000;
    bool horizontal_mirror = false;
};

struct FlyMotion
{
    double right = 0;
    double up = 0;
    double forward = 0;
    bool fast = false;
};

class CameraController
{
  public:
    bool fit(const SplatScene &scene, uint32_t width, uint32_t height);
    bool resize(uint32_t width, uint32_t height);
    bool orbit(double dx_pixels, double dy_pixels);
    bool pan(double dx_pixels, double dy_pixels);
    bool dolly(double steps);
    bool look(double dx_pixels, double dy_pixels);
    bool fly(FlyMotion motion, double seconds);
    bool set_mode(ViewMode mode);
    void set_flip_axes(uint8_t mask);
    void reset();

    [[nodiscard]] CameraPose pose() const;
    [[nodiscard]] ViewMode mode() const { return mode_; }
    [[nodiscard]] uint8_t flip_axes() const { return flip_axes_; }
    [[nodiscard]] bool has_scene() const { return has_scene_; }

  private:
    bool commit(const CameraController &next);
    void orient();
    void clip_planes();

    CameraPose camera_{};
    CameraPose initial_{};
    Double3 center_{};
    Double3 origin_{};
    Double3 focus_{};
    double radius_ = 1;
    double distance_ = 1;
    double yaw_ = 0;
    double pitch_ = 0;
    uint32_t width_ = 1;
    uint32_t height_ = 1;
    uint8_t flip_axes_ = 0;
    ViewMode mode_ = ViewMode::Orbit;
    bool has_scene_ = false;
};

} // namespace gs::android::engine
