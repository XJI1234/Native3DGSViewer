#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace gs::server
{
struct Ellipse
{
    float center_x = 0, center_y = 0;
    float axis0_x = 0, axis0_y = 0, axis1_x = 0, axis1_y = 0;
    float red = 0, green = 0, blue = 0, opacity = 0;
};
static_assert(sizeof(Ellipse) == 40);
std::vector<uint8_t> raster_reference(std::span<const Ellipse> splats, uint32_t width,
                                      uint32_t height);
} // namespace gs::server
