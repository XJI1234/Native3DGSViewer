#pragma once
#include "render-core/renderer.h"
#include <array>

namespace gs::render::detail
{
std::optional<RenderError> validate_quality(const QualityConfig &);
std::optional<RenderError> validate_camera(const CameraState &);
std::optional<RenderError> validate_scene(const SceneHandle &);
uint64_t scene_bytes(const SplatScene &);
uint64_t incremental_bytes(const SplatScene &);
bool fits_budget(uint64_t required, uint64_t budget, uint64_t usage);
std::array<float, 3> relative_camera(const SplatScene &, const CameraState &);
std::array<float, 9> world_to_camera(const Quaterniond &);
} // namespace gs::render::detail
