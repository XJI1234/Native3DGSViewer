#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace gs
{

struct Double3
{
    double x;
    double y;
    double z;
};

struct Bounds3
{
    Double3 min;
    Double3 max;
};

enum class SourceFormat : uint8_t
{
    Ply,
    Spz
};

struct SplatScene
{
    uint64_t count = 0;
    uint8_t shDegree = 0;
    SourceFormat sourceFormat = SourceFormat::Ply;
    Double3 worldOrigin{};
    Bounds3 bounds{};
    double maxScale = 0;
    std::span<const float> centerLocal;
    std::span<const float> scale;
    std::span<const float> rotation;
    std::span<const float> opacity;
    std::span<const float> rgb0;
    std::span<const float> shRest;

    // Keeps the read-only shared mapping alive for all spans.
    std::shared_ptr<const void> storage;
};

using SceneHandle = std::shared_ptr<const SplatScene>;

} // namespace gs
