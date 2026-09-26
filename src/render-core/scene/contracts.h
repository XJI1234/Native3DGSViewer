#pragma once
#include "render-core/renderer.h"
#include <array>

namespace gs::render::detail
{
inline constexpr uint64_t upload_reserve_bytes = 8ull << 20;
std::optional<RenderError> validate_quality(const QualityConfig &);
std::optional<RenderError> validate_camera(const CameraState &);
std::optional<RenderError> validate_scene(const SceneHandle &);
uint64_t scene_bytes(const SplatScene &);
uint64_t incremental_bytes(const SplatScene &);
bool fits_budget(uint64_t required, uint64_t budget, uint64_t usage);
bool fits_scene_budgets(uint64_t scene_required, uint64_t upload_required,
                        uint64_t local_budget, uint64_t local_usage,
                        uint64_t nonlocal_budget, uint64_t nonlocal_usage, bool uma);
std::array<float, 3> relative_camera(const SplatScene &, const CameraState &);
std::array<float, 9> world_to_camera(const Quaterniond &);
} // namespace gs::render::detail
