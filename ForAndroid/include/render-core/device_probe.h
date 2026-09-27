#pragma once

#include "splat-types/scene.h"
#include <cstdint>
#include <string>

namespace gs::android::render
{

struct DeviceCapabilities
{
    bool supported = false;
    int32_t result = 0;
    uint32_t api_version = 0;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint32_t max_storage_buffer_range = 0;
    uint64_t heap_size_bytes = 0;
    uint64_t heap_budget_bytes = 0;
    uint64_t heap_usage_bytes = 0;
    bool memory_budget_extension = false;
    std::string device_name;
    std::string diagnostic;
};

struct SceneAdmission
{
    bool accepted = false;
    uint64_t required_bytes = 0;
    uint64_t available_bytes = 0;
    std::string diagnostic;
};

DeviceCapabilities probe_device();
SceneAdmission assess_scene(const gs::SceneHandle &scene, const DeviceCapabilities &device);

// Runs the same subgroup-independent radix passes intended for scene sorting.
// Returns a diagnostic on shader, dispatch, or CPU-reference mismatch.
std::string sort_self_test(const std::string &shader_directory);
std::string sort_scene_self_test(const gs::SceneHandle &scene,
                                  const std::string &shader_directory);

} // namespace gs::android::render
