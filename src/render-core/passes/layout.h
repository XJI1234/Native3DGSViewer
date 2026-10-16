#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gs::render::detail
{
// Shared by the native pass and optional CUDA experiment; mirrors Frame in splat.hlsl.
struct FrameConstants
{
    float view_rows[12]{};
    float camera[4]{};
    float viewport[4]{};
    float quality[4]{};
    float clip[4]{};
    uint32_t meta[4]{};
    uint32_t offsets[8]{};
};
static_assert(sizeof(FrameConstants) == 160 && offsetof(FrameConstants, meta) == 112);
// Mirrors Ellipse and arguments[] in splat.hlsl; no compiler vector alignment.
struct ProjectedEllipse
{
    float center[2], axis0[2], axis1[2], color[4];
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
static_assert(std::is_standard_layout_v<ProjectedEllipse> && sizeof(ProjectedEllipse) == 40);
static_assert(offsetof(ProjectedEllipse, axis0) == 8 && offsetof(ProjectedEllipse, axis1) == 16);
static_assert(offsetof(ProjectedEllipse, color) == 24);
static_assert(sizeof(DrawCounters) == 20 && offsetof(DrawCounters, rejected) == 16);
static_assert(offsetof(FrameReadback, draw) == 24);
} // namespace gs::render::detail
