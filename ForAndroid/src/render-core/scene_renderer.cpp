#define VK_USE_PLATFORM_ANDROID_KHR
#include "render-core/scene_renderer.h"
#include "render-core/device_probe.h"
#include "render-core/gpu_timing.h"
#include "embedded_shaders.h"

#include <vulkan/vulkan.h>
#include <android/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gs::android::render
{
namespace
{
struct VulkanFailure : std::runtime_error
{
    VulkanFailure(VkResult value, const char *stage) : std::runtime_error(stage), result(value) {}
    VkResult result;
};

void check(VkResult result, const char *stage)
{
    if (result != VK_SUCCESS) throw VulkanFailure(result, stage);
}

uint32_t groups_for(uint32_t count)
{
    return count / 128 + (count % 128 != 0);
}

struct Buffer
{
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize bytes = 0;
};

struct ScanLevel
{
    Buffer input;
    Buffer prefix;
    Buffer sums;
    uint32_t count = 0;
};

struct SceneBuffers
{
    SceneHandle cpu;
    Buffer attributes;
    std::array<Buffer, 6> sh;
    Buffer projected;
    std::array<Buffer, 2> pairs;
    Buffer histogram;
    std::vector<ScanLevel> levels;
    Buffer constants;
    uint32_t count = 0;
    uint32_t group_count = 0;
    uint32_t sh_width = 0;
    uint32_t sh_chunk_points = 0;
};

struct alignas(16) FrameConstants
{
    std::array<float, 4> row[3]{};
    std::array<float, 4> camera{};
    std::array<float, 4> viewport{};
    std::array<float, 4> quality{3.0f, 0.0039f, 0.3f, 1024.0f};
    std::array<float, 4> clip_planes{};
    std::array<uint32_t, 4> meta{};
    std::array<uint32_t, 4> offsets0{};
    std::array<uint32_t, 4> offsets1{};
};
static_assert(sizeof(FrameConstants) == 160);

VkShaderModule shader_module(VkDevice device, const uint8_t *code, size_t bytes)
{
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = reinterpret_cast<const uint32_t *>(code);
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

} // namespace

struct SceneRenderer::Impl
{
    explicit Impl(ANativeWindow *input) : window(input)
    {
        if (!window || ANativeWindow_getWidth(window) <= 0 ||
            ANativeWindow_getHeight(window) <= 0)
            throw VulkanFailure(VK_ERROR_INITIALIZATION_FAILED, "ANativeWindow size");
        ANativeWindow_acquire(window);
        try { initialize(); }
        catch (...) { cleanup(); throw; }
    }

    ~Impl() { cleanup(); }

    void initialize()
    {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Native3DGS";
        app.apiVersion = VK_API_VERSION_1_1;
        const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app;
        instance_info.enabledExtensionCount = 2;
        instance_info.ppEnabledExtensionNames = extensions;
        check(vkCreateInstance(&instance_info, nullptr, &instance), "vkCreateInstance");
        VkAndroidSurfaceCreateInfoKHR surface_info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        surface_info.window = window;
        check(vkCreateAndroidSurfaceKHR(instance, &surface_info, nullptr, &surface),
              "vkCreateAndroidSurfaceKHR");
        uint32_t device_count = 0;
        check(vkEnumeratePhysicalDevices(instance, &device_count, nullptr),
              "vkEnumeratePhysicalDevices");
        if (!device_count)
            throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "No Vulkan device");
        std::vector<VkPhysicalDevice> devices(device_count);
        check(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()),
              "vkEnumeratePhysicalDevices data");
        for (auto candidate : devices)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1 ||
                properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
                continue;
            uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, queues.data());
            for (uint32_t i = 0; i < queue_count; ++i)
            {
                VkBool32 present = VK_FALSE;
                check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present),
                      "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (present && queues[i].queueCount &&
                    (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
                {
                    physical = candidate;
                    queue_family = i;
                    break;
                }
            }
            if (physical) break;
        }
        if (!physical)
            throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "No graphics/compute/present queue");
        vkGetPhysicalDeviceProperties(physical, &properties);
        vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
        VkPhysicalDeviceSubgroupProperties subgroup{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        VkPhysicalDeviceProperties2 extended{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        extended.pNext = &subgroup;
        vkGetPhysicalDeviceProperties2(physical, &extended);
        const auto required = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT;
        if (properties.vendorID == 0x5143 &&
            std::strstr(properties.deviceName, "Adreno (TM) 735") != nullptr &&
            (subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
            (subgroup.supportedOperations & required) == required &&
            subgroup.subgroupSize && subgroup.subgroupSize <= 128 &&
            128 % subgroup.subgroupSize == 0)
        {
            const auto diagnostic = sort_subgroup_self_test({});
            subgroup_scatter_enabled = diagnostic.empty();
            if (!subgroup_scatter_enabled)
                __android_log_print(ANDROID_LOG_WARN, "Native3DGS",
                                    "Subgroup sort self-test failed: %s", diagnostic.c_str());
        }
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
        std::vector<VkQueueFamilyProperties> family_properties(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count,
                                                 family_properties.data());
        timestamp_valid_bits = family_properties[queue_family].timestampValidBits;
        timestamps_supported = timestamp_valid_bits != 0;
        const float priority = 1;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        uint32_t extension_count = 0;
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr),
              "Device extension count");
        std::vector<VkExtensionProperties> device_extensions(extension_count);
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count,
                                                   device_extensions.data()),
              "Device extensions");
        memory_budget_enabled = std::any_of(device_extensions.begin(), device_extensions.end(),
            [](const auto &extension) {
                return std::strcmp(extension.extensionName,
                                   VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0;
            });
        const char *enabled_extensions[] = {swapchain_extension,
                                             VK_EXT_MEMORY_BUDGET_EXTENSION_NAME};
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = memory_budget_enabled ? 2 : 1;
        device_info.ppEnabledExtensionNames = enabled_extensions;
        check(vkCreateDevice(physical, &device_info, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, queue_family, 0, &queue);
        create_swapchain();
        create_descriptors();
        create_pipelines();
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = queue_family;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(vkCreateCommandPool(device, &pool, nullptr, &command_pool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command_info.commandPool = command_pool;
        command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_info.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &command_info, &command), "vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semaphore_info, nullptr, &acquired), "Acquire semaphore");
        finished.resize(views.size());
        for (auto &semaphore : finished)
            check(vkCreateSemaphore(device, &semaphore_info, nullptr, &semaphore),
                  "Present semaphore");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device, &fence_info, nullptr, &fence), "Frame fence");
        if (timestamps_supported)
        {
            VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queries.queryCount = 4;
            if (vkCreateQueryPool(device, &queries, nullptr, &timestamp_pool) != VK_SUCCESS)
                timestamps_supported = false;
        }
    }

    void create_swapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities),
              "Surface capabilities");
        __android_log_print(ANDROID_LOG_INFO, "Native3dgsSurface",
            "window=%dx%d extent=%ux%u current=%u supported=0x%x",
            ANativeWindow_getWidth(window), ANativeWindow_getHeight(window),
            capabilities.currentExtent.width, capabilities.currentExtent.height,
            capabilities.currentTransform, capabilities.supportedTransforms);
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            throw VulkanFailure(VK_ERROR_FORMAT_NOT_SUPPORTED, "Surface color attachment");
        if (!(capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
            throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "Identity Surface transform");
        uint32_t format_count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr),
              "Surface format count");
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count,
                                                  formats.data()), "Surface formats");
        auto chosen = std::find_if(formats.begin(), formats.end(), [](const auto &value) {
            return value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                (value.format == VK_FORMAT_R8G8B8A8_UNORM ||
                 value.format == VK_FORMAT_B8G8R8A8_UNORM);
        });
        if (chosen == formats.end())
            throw VulkanFailure(VK_ERROR_FORMAT_NOT_SUPPORTED, "No UNORM Surface format");
        format = chosen->format;
        extent = capabilities.currentExtent.width != UINT32_MAX
            ? capabilities.currentExtent
            : VkExtent2D{std::clamp<uint32_t>(ANativeWindow_getWidth(window),
                                               capabilities.minImageExtent.width,
                                               capabilities.maxImageExtent.width),
                         std::clamp<uint32_t>(ANativeWindow_getHeight(window),
                                               capabilities.minImageExtent.height,
                                               capabilities.maxImageExtent.height)};
        if (!extent.width || !extent.height)
            throw VulkanFailure(VK_ERROR_OUT_OF_DATE_KHR, "Zero Surface extent");
        window_extent = {static_cast<uint32_t>(ANativeWindow_getWidth(window)),
                         static_cast<uint32_t>(ANativeWindow_getHeight(window))};
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface;
        info.minImageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount)
            info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
        info.imageFormat = format;
        info.imageColorSpace = chosen->colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        // Projection and touch coordinates use the logical, unrotated Surface dimensions.
        info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        for (auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                          VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (capabilities.supportedCompositeAlpha & mode)
            {
                info.compositeAlpha = mode;
                break;
            }
        if (!info.compositeAlpha)
            throw VulkanFailure(VK_ERROR_FORMAT_NOT_SUPPORTED, "Composite alpha");
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");
        uint32_t image_count = 0;
        check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr),
              "Swapchain image count");
        std::vector<VkImage> images(image_count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data()),
              "Swapchain images");
        views.resize(image_count);
        framebuffers.resize(image_count);
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass.attachmentCount = 1;
        pass.pAttachments = &attachment;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = 1;
        pass.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &pass, nullptr, &render_pass), "vkCreateRenderPass");
        for (uint32_t i = 0; i < image_count; ++i)
        {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = images[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &view, nullptr, &views[i]), "vkCreateImageView");
            VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer.renderPass = render_pass;
            framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &views[i];
            framebuffer.width = extent.width;
            framebuffer.height = extent.height;
            framebuffer.layers = 1;
            check(vkCreateFramebuffer(device, &framebuffer, nullptr, &framebuffers[i]),
                  "vkCreateFramebuffer");
        }
    }

    uint32_t host_coherent_type(uint32_t bits) const
    {
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
        {
            const auto flags = memory_properties.memoryTypes[i].propertyFlags;
            if ((bits & (1u << i)) &&
                (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return i;
        }
        throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "Host-coherent memory");
    }

    Buffer allocate(VkDeviceSize bytes)
    {
        Buffer result;
        result.bytes = std::max<VkDeviceSize>(bytes, 4);
        if (result.bytes > properties.limits.maxStorageBufferRange)
            throw VulkanFailure(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Storage buffer range");
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = result.bytes;
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device, &info, nullptr, &result.handle), "vkCreateBuffer");
        try
        {
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, result.handle, &requirements);
            const uint32_t type = host_coherent_type(requirements.memoryTypeBits);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = type;
            check(vkAllocateMemory(device, &allocation, nullptr, &result.memory),
                  "vkAllocateMemory");
            check(vkBindBufferMemory(device, result.handle, result.memory, 0),
                  "vkBindBufferMemory");
            return result;
        }
        catch (...)
        {
            if (result.memory) vkFreeMemory(device, result.memory, nullptr);
            vkDestroyBuffer(device, result.handle, nullptr);
            throw;
        }
    }

    void release(Buffer &buffer) noexcept
    {
        if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
        buffer = {};
    }

    void release_scene(SceneBuffers &scene) noexcept
    {
        release(scene.attributes);
        for (auto &chunk : scene.sh) release(chunk);
        release(scene.projected);
        for (auto &pair : scene.pairs) release(pair);
        release(scene.histogram);
        for (auto &level : scene.levels)
        {
            release(level.prefix);
            release(level.sums);
        }
        release(scene.constants);
        scene = {};
    }

    void write(const Buffer &buffer, const void *data, size_t bytes)
    {
        if (bytes > buffer.bytes)
            throw VulkanFailure(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Buffer write exceeds allocation");
        void *mapped = nullptr;
        check(vkMapMemory(device, buffer.memory, 0, buffer.bytes, 0, &mapped), "vkMapMemory");
        std::memcpy(mapped, data, bytes);
        vkUnmapMemory(device, buffer.memory);
    }

    void upload(const SceneHandle &cpu)
    {
        DeviceCapabilities device_info;
        device_info.supported = true;
        device_info.max_storage_buffer_range = properties.limits.maxStorageBufferRange;
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 memory{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        memory.pNext = memory_budget_enabled ? &budget : nullptr;
        vkGetPhysicalDeviceMemoryProperties2(physical, &memory);
        VkBufferCreateInfo probe{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        probe.size = 256;
        probe.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        probe.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer buffer = VK_NULL_HANDLE;
        check(vkCreateBuffer(device, &probe, nullptr, &buffer), "Scene memory probe");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        vkDestroyBuffer(device, buffer, nullptr);
        const uint32_t heap_index = memory_properties.memoryTypes[
            host_coherent_type(requirements.memoryTypeBits)].heapIndex;
        const auto &heap = memory.memoryProperties.memoryHeaps[heap_index];
        device_info.heap_size_bytes = heap.size;
        device_info.heap_budget_bytes = memory_budget_enabled ? budget.heapBudget[heap_index]
                                                         : heap.size;
        device_info.heap_usage_bytes = memory_budget_enabled ? budget.heapUsage[heap_index] : 0;
        const auto admission = assess_scene(cpu, device_info);
        if (!admission.accepted)
            throw VulkanFailure(VK_ERROR_OUT_OF_DEVICE_MEMORY, "Scene admission");
        if (cpu->count > UINT32_MAX || groups_for(static_cast<uint32_t>(cpu->count)) >
            properties.limits.maxComputeWorkGroupCount[0])
            throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "Scene dispatch range");
        wait_frame();
        SceneBuffers candidate;
        candidate.cpu = cpu;
        candidate.count = static_cast<uint32_t>(cpu->count);
        candidate.group_count = groups_for(candidate.count);
        candidate.sh_width = 3u * ((cpu->shDegree + 1u) * (cpu->shDegree + 1u) - 1u);
        const uint64_t sh_stride = uint64_t(candidate.sh_width) * sizeof(float);
        candidate.sh_chunk_points = candidate.sh_width
            ? static_cast<uint32_t>(std::min<uint64_t>(candidate.count,
                properties.limits.maxStorageBufferRange / sh_stride))
            : candidate.count;
        if (!candidate.sh_chunk_points ||
            (uint64_t(candidate.count) + candidate.sh_chunk_points - 1) /
                candidate.sh_chunk_points > candidate.sh.size())
            throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "SH descriptor range");
        if (properties.limits.maxPerStageDescriptorStorageBuffers < 9 ||
            properties.limits.maxDescriptorSetStorageBuffers < 9)
            throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "SH descriptor count");
        try
        {
            const uint64_t n = candidate.count;
            const uint64_t histogram_count = 16ull * candidate.group_count;
            if (histogram_count > UINT32_MAX)
                throw VulkanFailure(VK_ERROR_FEATURE_NOT_PRESENT, "Histogram index range");
            candidate.attributes = allocate(n * 56);
            for (size_t chunk = 0; chunk < candidate.sh.size(); ++chunk)
            {
                const uint64_t first = uint64_t(chunk) * candidate.sh_chunk_points;
                if (first >= n) break;
                const uint64_t points = std::min<uint64_t>(candidate.sh_chunk_points, n - first);
                candidate.sh[chunk] = allocate(points * sh_stride);
                if (candidate.sh_width)
                    write(candidate.sh[chunk], cpu->shRest.data() + first * candidate.sh_width,
                          points * sh_stride);
            }
            candidate.projected = allocate(n * 48);
            for (auto &pair : candidate.pairs) pair = allocate(n * 8);
            candidate.histogram = allocate(histogram_count * sizeof(uint32_t));
            uint32_t length = static_cast<uint32_t>(histogram_count);
            do
            {
                const uint32_t blocks = groups_for(length);
                ScanLevel level;
                level.count = length;
                candidate.levels.push_back(level);
                candidate.levels.back().prefix = allocate(uint64_t(length) * sizeof(uint32_t));
                candidate.levels.back().sums = allocate(uint64_t(blocks) * sizeof(uint32_t));
                length = blocks;
            } while (length > 1);
            candidate.constants = allocate(sizeof(FrameConstants));
            void *mapped = nullptr;
            check(vkMapMemory(device, candidate.attributes.memory, 0,
                              candidate.attributes.bytes, 0, &mapped), "Scene map");
            auto *bytes = static_cast<uint8_t *>(mapped);
            std::memcpy(bytes, cpu->centerLocal.data(), cpu->centerLocal.size_bytes());
            std::memcpy(bytes + n * 12, cpu->scale.data(), cpu->scale.size_bytes());
            std::memcpy(bytes + n * 24, cpu->rotation.data(), cpu->rotation.size_bytes());
            std::memcpy(bytes + n * 40, cpu->opacity.data(), cpu->opacity.size_bytes());
            std::memcpy(bytes + n * 44, cpu->rgb0.data(), cpu->rgb0.size_bytes());
            vkUnmapMemory(device, candidate.attributes.memory);
        }
        catch (...)
        {
            release_scene(candidate);
            throw;
        }
        if (has_pending) rollback_pending();
        previous = std::exchange(scene, SceneBuffers{});
        scene = std::exchange(candidate, SceneBuffers{});
        has_pending = true;
    }

    void commit_pending()
    {
        if (!has_pending) return;
        wait_frame();
        release_scene(previous);
        has_pending = false;
    }

    void rollback_pending()
    {
        if (!has_pending) return;
        wait_frame();
        release_scene(scene);
        scene = std::exchange(previous, SceneBuffers{});
        has_pending = false;
    }

    void wait_frame()
    {
        if (!in_flight || device_lost) return;
        const auto started = std::chrono::steady_clock::now();
        const auto result = vkWaitForFences(device, 1, &fence, VK_TRUE, 5'000'000'000ull);
        check(result, "Frame fence wait");
        fence_wait_us += static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
        if (timestamps_supported)
        {
            std::array<uint64_t, 4> ticks{};
            if (vkGetQueryPoolResults(device, timestamp_pool, 0, 4, sizeof(ticks),
                                      ticks.data(), sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
            {
                ++measured_gpu_frames;
                for (size_t stage = 0; stage < gpu_stage_us.size(); ++stage)
                {
                    const auto duration = double(timestamp_delta(
                        ticks[stage], ticks[stage + 1], timestamp_valid_bits)) *
                        properties.limits.timestampPeriod / 1000.0;
                    gpu_stage_us[stage] += duration;
                    completed_gpu_stage_us[stage] = static_cast<uint64_t>(std::llround(duration));
                }
                completed_gpu_frame_id = in_flight_frame_id;
            }
        }
        in_flight = false;
    }

    void cleanup() noexcept
    {
        if (device)
        {
            if (in_flight && !device_lost) vkDeviceWaitIdle(device);
            release_scene(scene);
            release_scene(previous);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (timestamp_pool) vkDestroyQueryPool(device, timestamp_pool, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            for (auto semaphore : finished)
                if (semaphore) vkDestroySemaphore(device, semaphore, nullptr);
            if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
            if (descriptor_pool) vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            for (auto pipeline : sort_pipelines)
                if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (project_pipeline) vkDestroyPipeline(device, project_pipeline, nullptr);
            if (draw_pipeline) vkDestroyPipeline(device, draw_pipeline, nullptr);
            if (sort_layout) vkDestroyPipelineLayout(device, sort_layout, nullptr);
            if (project_layout) vkDestroyPipelineLayout(device, project_layout, nullptr);
            if (draw_layout) vkDestroyPipelineLayout(device, draw_layout, nullptr);
            if (sort_set_layout) vkDestroyDescriptorSetLayout(device, sort_set_layout, nullptr);
            if (project_set_layout) vkDestroyDescriptorSetLayout(device, project_set_layout, nullptr);
            if (draw_set_layout) vkDestroyDescriptorSetLayout(device, draw_set_layout, nullptr);
            for (auto framebuffer : framebuffers)
                if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
            for (auto view : views)
                if (view) vkDestroyImageView(device, view, nullptr);
            if (render_pass) vkDestroyRenderPass(device, render_pass, nullptr);
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        if (window) ANativeWindow_release(window);
    }

    void create_descriptors();
    void create_pipelines();
    FrameResult render(const engine::CameraPose &camera);
    VkDescriptorSet descriptor(VkDescriptorSetLayout layout, std::initializer_list<Buffer *> buffers,
                               uint32_t uniform_binding = UINT32_MAX);
    VkDescriptorSet project_descriptor();
    void dispatch(uint32_t pipeline, std::initializer_list<Buffer *> buffers,
                  uint32_t count, uint32_t shift, uint32_t groups);
    void barrier(VkPipelineStageFlags destination, VkAccessFlags access);

    ANativeWindow *window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkExtent2D window_extent{};
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkDescriptorSetLayout sort_set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout project_set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout draw_set_layout = VK_NULL_HANDLE;
    VkPipelineLayout sort_layout = VK_NULL_HANDLE;
    VkPipelineLayout project_layout = VK_NULL_HANDLE;
    VkPipelineLayout draw_layout = VK_NULL_HANDLE;
    std::array<VkPipeline, 4> sort_pipelines{};
    VkPipeline project_pipeline = VK_NULL_HANDLE;
    VkPipeline draw_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    std::vector<VkSemaphore> finished;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool timestamp_pool = VK_NULL_HANDLE;
    bool timestamps_supported = false;
    uint32_t timestamp_valid_bits = 0;
    uint64_t measured_frames = 0;
    uint64_t measured_gpu_frames = 0;
    std::array<double, 3> gpu_stage_us{};
    std::array<uint64_t, 3> completed_gpu_stage_us{};
    uint64_t completed_gpu_frame_id = 0;
    uint64_t submitted_frames = 0;
    uint64_t in_flight_frame_id = 0;
    uint64_t fence_wait_us = 0;
    bool in_flight = false;
    bool device_lost = false;
    bool memory_budget_enabled = false;
    bool subgroup_scatter_enabled = false;
    SceneBuffers scene;
    SceneBuffers previous;
    bool has_pending = false;
};

void SceneRenderer::Impl::create_descriptors()
{
    auto make_layout = [&](std::initializer_list<VkDescriptorType> types,
                           VkShaderStageFlags stages, VkDescriptorSetLayout &target) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        uint32_t index = 0;
        for (auto type : types)
            bindings.push_back({index++, type, 1, stages, nullptr});
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(device, &info, nullptr, &target),
              "vkCreateDescriptorSetLayout");
    };
    make_layout({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, VK_SHADER_STAGE_COMPUTE_BIT,
                sort_set_layout);
    std::array<VkDescriptorSetLayoutBinding, 5> project_bindings{};
    for (uint32_t i = 0; i < project_bindings.size(); ++i)
        project_bindings[i] = {i, i == 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
            : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, i == 1 ? 6u : 1u,
            VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo project_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    project_info.bindingCount = project_bindings.size();
    project_info.pBindings = project_bindings.data();
    check(vkCreateDescriptorSetLayout(device, &project_info, nullptr, &project_set_layout),
          "vkCreateDescriptorSetLayout project");
    make_layout({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}, VK_SHADER_STAGE_VERTEX_BIT,
                draw_set_layout);
    auto make_pipeline_layout = [&](VkDescriptorSetLayout set, bool push, VkPipelineLayout &target) {
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 1;
        info.pSetLayouts = &set;
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 12};
        if (push)
        {
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = &range;
        }
        check(vkCreatePipelineLayout(device, &info, nullptr, &target),
              "vkCreatePipelineLayout");
    };
    make_pipeline_layout(sort_set_layout, true, sort_layout);
    make_pipeline_layout(project_set_layout, false, project_layout);
    make_pipeline_layout(draw_set_layout, false, draw_layout);
    std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32}}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 512;
    pool.poolSizeCount = sizes.size();
    pool.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool),
          "vkCreateDescriptorPool");
}

void SceneRenderer::Impl::create_pipelines()
{
    const uint8_t *scatter_code = subgroup_scatter_enabled
        ? embedded::scatter_subgroup : embedded::scatter;
    const size_t scatter_bytes = subgroup_scatter_enabled
        ? sizeof(embedded::scatter_subgroup) : sizeof(embedded::scatter);
    const struct Shader { const uint8_t *code; size_t bytes; } sort_shaders[] = {
        {embedded::histogram, sizeof(embedded::histogram)},
        {embedded::scan, sizeof(embedded::scan)},
        {embedded::add_prefix, sizeof(embedded::add_prefix)},
        {scatter_code, scatter_bytes}};
    for (size_t i = 0; i < sort_pipelines.size(); ++i)
    {
        VkShaderModule module = shader_module(device, sort_shaders[i].code,
                                               sort_shaders[i].bytes);
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = sort_layout;
        VkResult status = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info,
                                                    nullptr, &sort_pipelines[i]);
        vkDestroyShaderModule(device, module, nullptr);
        if (status != VK_SUCCESS && i == 3 && subgroup_scatter_enabled)
        {
            subgroup_scatter_enabled = false;
            __android_log_print(ANDROID_LOG_WARN, "Native3DGS",
                                "Subgroup sort pipeline failed (%d); using baseline", status);
            module = shader_module(device, embedded::scatter, sizeof(embedded::scatter));
            info.stage.module = module;
            status = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info,
                                              nullptr, &sort_pipelines[i]);
            vkDestroyShaderModule(device, module, nullptr);
        }
        check(status, "Sort pipeline");
    }
    __android_log_print(ANDROID_LOG_INFO, "Native3DGS", "Sort path=%s",
                        subgroup_scatter_enabled ? "subgroup" : "baseline");
    {
        VkShaderModule module = shader_module(device, embedded::project,
                                               sizeof(embedded::project));
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = project_layout;
        const auto status = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info,
                                                     nullptr, &project_pipeline);
        vkDestroyShaderModule(device, module, nullptr);
        check(status, "Projection pipeline");
    }
    std::array<VkShaderModule, 2> modules{};
    try
    {
        modules[0] = shader_module(device, embedded::splat_vert,
                                   sizeof(embedded::splat_vert));
        modules[1] = shader_module(device, embedded::splat_frag,
                                   sizeof(embedded::splat_frag));
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (size_t i = 0; i < stages.size(); ++i)
        {
            stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = modules[i];
            stages[i].pName = "main";
        }
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = stages.size();
        info.pStages = stages.data();
        info.pVertexInputState = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = draw_layout;
        info.renderPass = render_pass;
        check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr,
                                        &draw_pipeline), "Gaussian pipeline");
    }
    catch (...)
    {
        for (auto module : modules)
            if (module) vkDestroyShaderModule(device, module, nullptr);
        throw;
    }
    for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
}

VkDescriptorSet SceneRenderer::Impl::descriptor(VkDescriptorSetLayout layout,
    std::initializer_list<Buffer *> buffers, uint32_t uniform_binding)
{
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = descriptor_pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocation, &set), "vkAllocateDescriptorSets");
    std::array<VkDescriptorBufferInfo, 5> infos{};
    std::array<VkWriteDescriptorSet, 5> writes{};
    uint32_t index = 0;
    for (auto *buffer : buffers)
    {
        infos[index] = {buffer->handle, 0, buffer->bytes};
        writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[index].dstSet = set;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = index == uniform_binding
            ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[index].pBufferInfo = &infos[index];
        ++index;
    }
    vkUpdateDescriptorSets(device, index, writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet SceneRenderer::Impl::project_descriptor()
{
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = descriptor_pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &project_set_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocation, &set), "vkAllocateDescriptorSets project");
    std::array<VkDescriptorBufferInfo, 10> infos{};
    infos[0] = {scene.attributes.handle, 0, scene.attributes.bytes};
    for (size_t i = 0; i < scene.sh.size(); ++i)
    {
        const auto &buffer = scene.sh[i].handle ? scene.sh[i] : scene.sh[0];
        infos[i + 1] = {buffer.handle, 0, buffer.bytes};
    }
    infos[7] = {scene.projected.handle, 0, scene.projected.bytes};
    infos[8] = {scene.pairs[0].handle, 0, scene.pairs[0].bytes};
    infos[9] = {scene.constants.handle, 0, scene.constants.bytes};
    std::array<VkWriteDescriptorSet, 5> writes{};
    const uint32_t first[] = {0, 1, 7, 8, 9};
    for (uint32_t i = 0; i < writes.size(); ++i)
    {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = i == 1 ? 6u : 1u;
        writes[i].descriptorType = i == 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[first[i]];
    }
    vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
    return set;
}

void SceneRenderer::Impl::barrier(VkPipelineStageFlags destination, VkAccessFlags access)
{
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = access;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         destination, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void SceneRenderer::Impl::dispatch(uint32_t pipeline, std::initializer_list<Buffer *> buffers,
                                   uint32_t count, uint32_t shift, uint32_t groups)
{
    const auto set = descriptor(sort_set_layout, buffers);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, sort_pipelines[pipeline]);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, sort_layout,
                            0, 1, &set, 0, nullptr);
    const uint32_t params[] = {count, scene.group_count, shift};
    vkCmdPushConstants(command, sort_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(params), params);
    vkCmdDispatch(command, groups, 1, 1);
    barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

FrameResult SceneRenderer::Impl::render(const engine::CameraPose &camera)
{
    if (!scene.cpu) return {FrameStatus::NoScene, VK_SUCCESS, {}};
    if (ANativeWindow_getWidth(window) != static_cast<int>(window_extent.width) ||
        ANativeWindow_getHeight(window) != static_cast<int>(window_extent.height))
        return {FrameStatus::SurfaceChanged, VK_ERROR_OUT_OF_DATE_KHR, "Surface size changed"};
    try
    {
        wait_frame();
        check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
        check(vkResetDescriptorPool(device, descriptor_pool, 0), "vkResetDescriptorPool");
        uint32_t image = 0;
        const auto acquire = vkAcquireNextImageKHR(device, swapchain, 5'000'000'000ull,
                                                    acquired, VK_NULL_HANDLE, &image);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
            return {FrameStatus::SurfaceChanged, acquire, "Acquire Surface"};
        if (acquire != VK_SUBOPTIMAL_KHR) check(acquire, "vkAcquireNextImageKHR");

        FrameConstants constants;
        const auto &q = camera.orientation;
        const double x = q[0], y = q[1], z = q[2], w = q[3];
        constants.row[0] = {float(1 - 2 * (y * y + z * z)), float(2 * (x * y + z * w)),
                            float(2 * (x * z - y * w)), 0};
        constants.row[1] = {float(2 * (x * y - z * w)), float(1 - 2 * (x * x + z * z)),
                            float(2 * (y * z + x * w)), 0};
        constants.row[2] = {float(2 * (x * z + y * w)), float(2 * (y * z - x * w)),
                            float(1 - 2 * (x * x + y * y)), 0};
        const auto &origin = scene.cpu->worldOrigin;
        constants.camera = {float(camera.position.x - origin.x),
                            float(camera.position.y - origin.y),
                            float(camera.position.z - origin.z), 0};
        const float focal = float(extent.height / (2 * std::tan(camera.vertical_fov / 2)));
        constants.viewport = {focal, focal, float(extent.width), float(extent.height)};
        constants.clip_planes = {float(camera.near_plane), float(camera.far_plane), 0, 0};
        constants.meta = {scene.count, scene.cpu->shDegree, scene.sh_width, 1};
        constants.offsets0 = {0, scene.count * 3u, scene.count * 6u, scene.count * 10u};
        constants.offsets1 = {scene.count * 11u, camera.horizontal_mirror ? 1u : 0u,
                              scene.sh_chunk_points, 0};
        write(scene.constants, &constants, sizeof(constants));

        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
        if (timestamps_supported)
        {
            vkCmdResetQueryPool(command, timestamp_pool, 0, 4);
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                timestamp_pool, 0);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host,
                             0, nullptr, 0, nullptr);
        auto project_set = project_descriptor();
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, project_pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, project_layout,
                                0, 1, &project_set, 0, nullptr);
        vkCmdDispatch(command, scene.group_count, 1, 1);
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        if (timestamps_supported)
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                timestamp_pool, 1);

        Buffer *input = &scene.pairs[0], *output = &scene.pairs[1];
        for (uint32_t shift = 0; shift < 32; shift += 4)
        {
            dispatch(0, {input, &scene.histogram}, scene.count, shift, scene.group_count);
            for (size_t i = 0; i < scene.levels.size(); ++i)
            {
                auto &level = scene.levels[i];
                Buffer *source = i ? &scene.levels[i - 1].sums : &scene.histogram;
                dispatch(1, {source, &level.prefix, &level.sums}, level.count,
                         shift, groups_for(level.count));
            }
            for (size_t i = scene.levels.size(); i > 1; --i)
            {
                auto &lower = scene.levels[i - 2];
                dispatch(2, {&lower.prefix, &scene.levels[i - 1].prefix},
                         lower.count, shift, groups_for(lower.count));
            }
            dispatch(3, {input, &scene.levels.front().prefix, output}, scene.count,
                     shift, scene.group_count);
            std::swap(input, output);
        }
        barrier(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        if (timestamps_supported)
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                timestamp_pool, 2);

        VkClearValue clear{};
        clear.color = {{1, 1, 1, 1}};
        VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        pass.renderPass = render_pass;
        pass.framebuffer = framebuffers[image];
        pass.renderArea.extent = extent;
        pass.clearValueCount = 1;
        pass.pClearValues = &clear;
        vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(command, 0, 1, &viewport);
        vkCmdSetScissor(command, 0, 1, &scissor);
        auto draw_set = descriptor(draw_set_layout,
                                   {&scene.projected, input, &scene.constants}, 2);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_layout,
                                0, 1, &draw_set, 0, nullptr);
        vkCmdDraw(command, 6, scene.count, 0, 0);
        vkCmdEndRenderPass(command);
        if (timestamps_supported)
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                timestamp_pool, 3);
        check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &finished[image];
        check(vkResetFences(device, 1, &fence), "vkResetFences");
        check(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
        in_flight = true;
        in_flight_frame_id = ++submitted_frames;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &finished[image];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image;
        const auto result = vkQueuePresentKHR(queue, &present);
        const auto present_call_ns = static_cast<uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (++measured_frames == 120)
        {
            if (measured_gpu_frames)
                __android_log_print(ANDROID_LOG_DEBUG, "Native3DGSPerf",
                    "gpu_project_us=%.0f gpu_sort_us=%.0f gpu_draw_us=%.0f fence_wait_us=%llu",
                    gpu_stage_us[0] / measured_gpu_frames, gpu_stage_us[1] / measured_gpu_frames,
                    gpu_stage_us[2] / measured_gpu_frames,
                    static_cast<unsigned long long>(fence_wait_us / measured_frames));
            else
                __android_log_print(ANDROID_LOG_DEBUG, "Native3DGSPerf",
                    "gpu_timestamps=unavailable fence_wait_us=%llu",
                    static_cast<unsigned long long>(fence_wait_us / measured_frames));
            measured_frames = measured_gpu_frames = fence_wait_us = 0;
            gpu_stage_us = {};
        }
        // Android may report SUBOPTIMAL for a valid identity image on a rotated Surface.
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
            return {FrameStatus::SurfaceChanged, result, "Present Surface"};
        if (result != VK_SUBOPTIMAL_KHR) check(result, "vkQueuePresentKHR");
        FrameResult frame;
        frame.status = FrameStatus::Presented;
        frame.platform_result = result == VK_SUBOPTIMAL_KHR ? result
            : acquire == VK_SUBOPTIMAL_KHR ? acquire : VK_SUCCESS;
        frame.frame_id = in_flight_frame_id;
        frame.present_call_ns = present_call_ns;
        frame.gpu_frame_id = completed_gpu_frame_id;
        frame.gpu_project_us = completed_gpu_stage_us[0];
        frame.gpu_sort_us = completed_gpu_stage_us[1];
        frame.gpu_draw_us = completed_gpu_stage_us[2];
        frame.width = extent.width;
        frame.height = extent.height;
        frame.submitted_splats = scene.count;
        return frame;
    }
    catch (const VulkanFailure &failure)
    {
        if (failure.result == VK_ERROR_DEVICE_LOST) device_lost = true;
        const auto status = failure.result == VK_ERROR_DEVICE_LOST ? FrameStatus::DeviceLost
            : failure.result == VK_ERROR_SURFACE_LOST_KHR ? FrameStatus::SurfaceLost
            : FrameStatus::Failed;
        return {status, failure.result, failure.what()};
    }
    catch (const std::exception &failure)
    {
        return {FrameStatus::Failed, VK_ERROR_OUT_OF_HOST_MEMORY, failure.what()};
    }
}

SceneRenderer::SceneRenderer(ANativeWindow *window) : impl_(std::make_unique<Impl>(window)) {}
SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::upload(const SceneHandle &scene, std::string &diagnostic)
{
    try
    {
        impl_->upload(scene);
        diagnostic.clear();
        return true;
    }
    catch (const std::exception &failure)
    {
        diagnostic = failure.what();
        return false;
    }
}

void SceneRenderer::commit_pending() { impl_->commit_pending(); }
void SceneRenderer::rollback_pending() { impl_->rollback_pending(); }

void SceneRenderer::clear_scene()
{
    impl_->wait_frame();
    impl_->release_scene(impl_->scene);
    impl_->release_scene(impl_->previous);
    impl_->has_pending = false;
}

FrameResult SceneRenderer::render(const engine::CameraPose &camera)
{
    const auto start = std::chrono::steady_clock::now();
    auto result = impl_->render(camera);
    result.cpu_frame_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
    return result;
}

uint64_t SceneRenderer::scene_count() const
{
    return impl_->scene.count;
}

} // namespace gs::android::render
