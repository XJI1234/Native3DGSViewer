#include "gs_server/primitives.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace gs::server
{
std::vector<uint8_t> raster_reference(std::span<const Ellipse> splats, uint32_t width,
                                      uint32_t height)
{
    if (!width || !height || width > 4096 || height > 4096)
        throw std::runtime_error("Invalid reference dimensions");
    std::vector<float> colors(size_t(width) * height * 3, 0);
    for (const auto &ellipse : splats)
    {
        for (float value :
             {ellipse.center_x, ellipse.center_y, ellipse.axis0_x, ellipse.axis0_y, ellipse.axis1_x,
              ellipse.axis1_y, ellipse.red, ellipse.green, ellipse.blue, ellipse.opacity})
            if (!std::isfinite(value))
                throw std::runtime_error("Nonfinite reference ellipse");
        const float determinant =
            ellipse.axis0_x * ellipse.axis1_y - ellipse.axis1_x * ellipse.axis0_y;
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12f)
            throw std::runtime_error("Singular reference ellipse");
        const double extent_x =
            std::abs(double(ellipse.axis0_x)) + std::abs(double(ellipse.axis1_x));
        const double extent_y =
            std::abs(double(ellipse.axis0_y)) + std::abs(double(ellipse.axis1_y));
        const int left =
            int(std::clamp(std::floor(ellipse.center_x - extent_x), 0.0, double(width)));
        const int right =
            int(std::clamp(std::ceil(ellipse.center_x + extent_x), 0.0, double(width)));
        const int top =
            int(std::clamp(std::floor(ellipse.center_y - extent_y), 0.0, double(height)));
        const int bottom =
            int(std::clamp(std::ceil(ellipse.center_y + extent_y), 0.0, double(height)));
        for (int pixel_y = top; pixel_y < bottom; ++pixel_y)
            for (int pixel_x = left; pixel_x < right; ++pixel_x)
            {
                const float delta_x = pixel_x + 0.5f - ellipse.center_x;
                const float delta_y = pixel_y + 0.5f - ellipse.center_y;
                const float gaussian_x =
                    3 * (delta_x * ellipse.axis1_y - delta_y * ellipse.axis1_x) / determinant;
                const float gaussian_y =
                    3 * (ellipse.axis0_x * delta_y - ellipse.axis0_y * delta_x) / determinant;
                const float radius = gaussian_x * gaussian_x + gaussian_y * gaussian_y;
                if (radius > 9)
                    continue;
                const float alpha = std::min(0.999f, ellipse.opacity * std::exp(-0.5f * radius));
                if (alpha < 0.0039f)
                    continue;
                const size_t offset = (size_t(pixel_y) * width + pixel_x) * 3;
                colors[offset] = ellipse.red * alpha + colors[offset] * (1 - alpha);
                colors[offset + 1] = ellipse.green * alpha + colors[offset + 1] * (1 - alpha);
                colors[offset + 2] = ellipse.blue * alpha + colors[offset + 2] * (1 - alpha);
            }
    }
    std::vector<uint8_t> output(size_t(width) * height * 4);
    for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
    {
        for (size_t channel = 0; channel < 3; ++channel)
            output[pixel * 4 + channel] =
                uint8_t(std::lround(std::clamp(colors[pixel * 3 + channel], 0.0f, 1.0f) * 255));
        output[pixel * 4 + 3] = 255;
    }
    return output;
}
} // namespace gs::server
