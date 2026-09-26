#pragma once
#include "render-core/renderer.h"
#include <cstdint>
#include <optional>

namespace gs::engine
{
enum class ViewMode : uint8_t
{
    Orbit,
    Fly
};
enum class FlipAxis : uint8_t
{
    X = 1,
    Y = 2,
    Z = 4
};
struct FlyMotion
{
    double right = 0, up = 0, forward = 0;
    bool fast = false;
};
class CameraController
{
  public:
    std::optional<render::RenderError> fit_scene(const SplatScene &, render::QualityConfig,
                                                 render::Viewport);
    std::optional<render::RenderError> resize(render::Viewport);
    std::optional<render::RenderError> orbit(double dx_px, double dy_px);
    std::optional<render::RenderError> pan(double dx_px, double dy_px);
    std::optional<render::RenderError> dolly(double wheel_steps);
    std::optional<render::RenderError> look(double dx_px, double dy_px);
    std::optional<render::RenderError> fly(FlyMotion, double seconds);
    std::optional<render::RenderError> set_mode(ViewMode);
    void set_flip_axes(uint8_t mask);
    uint8_t flip_axes() const { return flip_axes_; }
    void reset();
    // View camera, including the display-only axis reflections.
    render::CameraState camera() const;
    ViewMode mode() const
    {
        return mode_;
    }
    bool has_scene() const
    {
        return has_scene_;
    }

  private:
    void orient();
    void clip_planes();
    std::optional<render::RenderError> commit(const CameraController &);
    render::CameraState camera_, initial_;
    render::Viewport viewport_{};
    Double3 focus_{}, center_{}, origin_{};
    double radius_ = 1, distance_ = 1, yaw_ = 0, pitch_ = 0;
    ViewMode mode_ = ViewMode::Orbit;
    bool has_scene_ = false;
    uint8_t flip_axes_ = 0;
};
} // namespace gs::engine
