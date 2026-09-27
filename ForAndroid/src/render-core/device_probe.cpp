#include "render-core/device_probe.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include <vulkan/vulkan.h>

namespace gs::android::render
{
namespace
{
struct InstanceOwner
{
    VkInstance value = VK_NULL_HANDLE;
    ~InstanceOwner() { if (value) vkDestroyInstance(value, nullptr); }
};

struct DeviceOwner
{
    VkDevice value = VK_NULL_HANDLE;
    ~DeviceOwner() { if (value) vkDestroyDevice(value, nullptr); }
};
bool checked_mul(uint64_t a, uint64_t b, uint64_t &value)
{
    if (a && b > UINT64_MAX / a)
        return false;
    value = a * b;
    return true;
}

bool checked_add(uint64_t a, uint64_t b, uint64_t &value)
{
    if (b > UINT64_MAX - a)
        return false;
    value = a + b;
    return true;
}
} // namespace

DeviceCapabilities probe_device_impl()
{
    DeviceCapabilities result;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Native3DGS";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    InstanceOwner instance_owner;
    auto &instance = instance_owner.value;
    VkResult status = vkCreateInstance(&create, nullptr, &instance);
    if (status != VK_SUCCESS)
    {
        result.result = status;
        result.diagnostic = "vkCreateInstance";
        return result;
    }
    uint32_t count = 0;
    status = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (status != VK_SUCCESS || !count)
    {
        result.result = status == VK_SUCCESS ? VK_ERROR_FEATURE_NOT_PRESENT : status;
        result.diagnostic = "vkEnumeratePhysicalDevices";
        return result;
    }
    std::vector<VkPhysicalDevice> devices(count);
    status = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (status != VK_SUCCESS)
    {
        result.result = status;
        result.diagnostic = "vkEnumeratePhysicalDevices data";
        return result;
    }
    for (auto physical : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_1 ||
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            continue;
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues.data());
        auto queue = std::find_if(queues.begin(), queues.end(), [](const auto &q) {
            return q.queueCount && (q.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                   (q.queueFlags & VK_QUEUE_COMPUTE_BIT);
        });
        if (queue == queues.end())
            continue;
        uint32_t extension_count = 0;
        if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr) !=
            VK_SUCCESS)
            continue;
        std::vector<VkExtensionProperties> extensions(extension_count);
        if (vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count,
                                                 extensions.data()) != VK_SUCCESS)
            continue;
        const auto has = [&extensions](const char *name) {
            return std::any_of(extensions.begin(), extensions.end(), [name](const auto &e) {
                return std::strcmp(e.extensionName, name) == 0;
            });
        };
        if (!has(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;
        const bool budget = has(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        const char *enabled[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                 VK_EXT_MEMORY_BUDGET_EXTENSION_NAME};
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = static_cast<uint32_t>(queue - queues.begin());
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = budget ? 2 : 1;
        device_info.ppEnabledExtensionNames = enabled;
        DeviceOwner device_owner;
        auto &device = device_owner.value;
        status = vkCreateDevice(physical, &device_info, nullptr, &device);
        if (status != VK_SUCCESS)
        {
            result.result = status;
            result.diagnostic = "vkCreateDevice";
            continue;
        }
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget_properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 memory{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        memory.pNext = budget ? &budget_properties : nullptr;
        vkGetPhysicalDeviceMemoryProperties2(physical, &memory);
        VkBufferCreateInfo probe_buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        probe_buffer.size = 256;
        probe_buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        probe_buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer = VK_NULL_HANDLE;
        status = vkCreateBuffer(device, &probe_buffer, nullptr, &buffer);
        if (status != VK_SUCCESS)
        {
            result.result = status;
            result.diagnostic = "vkCreateBuffer memory probe";
            continue;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        vkDestroyBuffer(device, buffer, nullptr);
        uint32_t selected_heap = UINT32_MAX;
        for (uint32_t i = 0; i < memory.memoryProperties.memoryTypeCount; ++i)
        {
            const auto &type = memory.memoryProperties.memoryTypes[i];
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (type.propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            {
                selected_heap = type.heapIndex;
                break;
            }
        }
        if (selected_heap == UINT32_MAX)
        {
            result.result = VK_ERROR_FEATURE_NOT_PRESENT;
            result.diagnostic = "No compatible host-coherent buffer memory";
            continue;
        }
        else
        {
            result.supported = true;
            result.result = VK_SUCCESS;
            result.api_version = properties.apiVersion;
            result.vendor_id = properties.vendorID;
            result.device_id = properties.deviceID;
            result.max_storage_buffer_range = properties.limits.maxStorageBufferRange;
            result.heap_size_bytes = memory.memoryProperties.memoryHeaps[selected_heap].size;
            result.heap_budget_bytes = budget ? budget_properties.heapBudget[selected_heap]
                                              : result.heap_size_bytes;
            result.heap_usage_bytes = budget ? budget_properties.heapUsage[selected_heap] : 0;
            result.memory_budget_extension = budget;
            result.device_name = properties.deviceName;
            result.diagnostic.clear();
        }
        if (result.supported)
            break;
    }
    if (!result.supported && result.diagnostic.empty())
    {
        result.result = VK_ERROR_FEATURE_NOT_PRESENT;
        result.diagnostic = "No Vulkan 1.1 graphics/compute hardware with compatible memory";
    }
    return result;
}

DeviceCapabilities probe_device()
{
    try
    {
        return probe_device_impl();
    }
    catch (const std::bad_alloc &)
    {
        DeviceCapabilities failure;
        failure.result = VK_ERROR_OUT_OF_HOST_MEMORY;
        failure.diagnostic = "Device probe allocation";
        return failure;
    }
}

SceneAdmission assess_scene(const gs::SceneHandle &scene, const DeviceCapabilities &device)
{
    SceneAdmission result;
    if (!scene || !scene->storage || !scene->count || scene->shDegree > 3 || !device.supported)
    {
        result.diagnostic = "Invalid scene or unsupported device";
        return result;
    }
    const uint64_t n = scene->count;
    const uint64_t sh_width = 3ull * ((scene->shDegree + 1) * (scene->shDegree + 1) - 1);
    if (scene->centerLocal.size() != n * 3 || scene->scale.size() != n * 3 ||
        scene->rotation.size() != n * 4 || scene->opacity.size() != n ||
        scene->rgb0.size() != n * 3 || scene->shRest.size() != n * sh_width ||
        !std::isfinite(scene->maxScale) || scene->maxScale <= 0)
    {
        result.diagnostic = "Scene array contract";
        return result;
    }
    uint64_t base = 0, sh = 0, sort = 0, total = 0;
    if (!checked_mul(n, 56, base) || !checked_mul(n, sh_width * sizeof(float), sh) ||
        !checked_mul(n, 64, sort) || !checked_add(base, sh, total) ||
        !checked_add(total, sort, total))
    {
        result.diagnostic = "Scene byte overflow";
        return result;
    }
    uint64_t level = 16 * (n / 128 + (n % 128 != 0));
    for (;;)
    {
        const uint64_t blocks = level / 128 + (level % 128 != 0);
        uint64_t words = 0, bytes = 0;
        if (!checked_add(level, level, words) || !checked_add(words, blocks, words) ||
            !checked_mul(words, sizeof(uint32_t), bytes) ||
            !checked_add(total, bytes, total))
        {
            result.diagnostic = "Scan byte overflow";
            return result;
        }
        if (blocks <= 1) break;
        level = blocks;
    }
    if (!checked_add(total, 8ull << 20, total))
    {
        result.diagnostic = "Scene byte overflow";
        return result;
    }
    result.required_bytes = total;
    if (base > device.max_storage_buffer_range || sh > device.max_storage_buffer_range)
    {
        result.diagnostic = "Storage buffer range";
        return result;
    }
    const uint64_t free = device.heap_budget_bytes > device.heap_usage_bytes
                              ? device.heap_budget_bytes - device.heap_usage_bytes : 0;
    result.available_bytes = free / 5 * 4 + (free % 5) * 4 / 5;
    result.accepted = total <= result.available_bytes;
    result.diagnostic = result.accepted ? "" : "Current device memory budget";
    return result;
}

} // namespace gs::android::render
