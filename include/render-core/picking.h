#pragma once
#include "renderer.h"

namespace gs::render
{
struct ScreenPoint
{
    double x_px = 0, y_px = 0, depth = 0;
};
// Pixel centers use (floor(x)+0.5, floor(y)+0.5). No display mirroring is applied.
std::optional<ScreenPoint> project_world(Double3, CameraState, Viewport);
std::optional<std::array<ScreenPoint, 2>> project_segment(Double3 a, Double3 b, CameraState,
                                                          Viewport);
std::optional<Double3> unproject_pixel(double x_px, double y_px, double depth, CameraState,
                                       Viewport);
} // namespace gs::render
