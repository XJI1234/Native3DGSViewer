#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gs::render::detail
{
// Mirrors Ellipse and arguments[] in splat.hlsl; no compiler vector alignment.
struct ProjectedEllipse
{
    float center[2], axis0[2], axis1[2], color[4], reserved[2];
};
struct DrawCounters
{
    uint32_t vertices_per_instance, instance_count, start_vertex, start_instance, rejected;
};
struct FrameReadback
{
    uint64_t ticks[3];
    DrawCounters draw;
};
static_assert(std::is_standard_layout_v<ProjectedEllipse> && sizeof(ProjectedEllipse) == 48);
static_assert(offsetof(ProjectedEllipse, axis0) == 8 && offsetof(ProjectedEllipse, axis1) == 16);
static_assert(offsetof(ProjectedEllipse, color) == 24 &&
              offsetof(ProjectedEllipse, reserved) == 40);
static_assert(sizeof(DrawCounters) == 20 && offsetof(DrawCounters, rejected) == 16);
static_assert(offsetof(FrameReadback, draw) == 24);
} // namespace gs::render::detail
