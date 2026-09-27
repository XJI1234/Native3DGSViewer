#define VK_USE_PLATFORM_ANDROID_KHR
#include "render-core/surface_probe.h"
#include "embedded_shaders.h"
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace gs::android::render
{
namespace
{
struct ProbeFailure : std::runtime_error
{
    ProbeFailure(VkResult value, const char *stage) : std::runtime_error(stage), result(value) {}
    VkResult result;
};

void check(VkResult result, const char *stage)
{
    if (result != VK_SUCCESS)
        throw ProbeFailure(result, stage);
}

class SurfaceProbe
{
  public:
    SurfaceProbe() = default;
    SurfaceProbe(const SurfaceProbe &) = delete;
    SurfaceProbe &operator=(const SurfaceProbe &) = delete;
    ~SurfaceProbe()
    {
        if (device)
        {
            if (submitted) vkDeviceWaitIdle(device);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (finished) vkDestroySemaphore(device, finished, nullptr);
            if (command_pool) vkDestroyCommandPool(device, command_pool, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (pipeline_layout) vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
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
    }

    void run(ANativeWindow *window)
    {
        if (!window || ANativeWindow_getWidth(window) <= 0 || ANativeWindow_getHeight(window) <= 0)
            throw ProbeFailure(VK_ERROR_INITIALIZATION_FAILED, "Invalid or zero-sized window");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_1;
        const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app;
        instance_info.enabledExtensionCount = 2;
        instance_info.ppEnabledExtensionNames = extensions;
        check(vkCreateInstance(&instance_info, nullptr, &instance), "Surface vkCreateInstance");
        VkAndroidSurfaceCreateInfoKHR surface_info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        surface_info.window = window;
        check(vkCreateAndroidSurfaceKHR(instance, &surface_info, nullptr, &surface),
              "vkCreateAndroidSurfaceKHR");
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Surface device enumeration");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Surface device list");
        for (auto candidate : devices)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1)
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
            throw ProbeFailure(VK_ERROR_FEATURE_NOT_PRESENT, "No graphics/compute/present queue");
        const float priority = 1;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        device_info.enabledExtensionCount = 1;
        device_info.ppEnabledExtensionNames = &swapchain_extension;
        check(vkCreateDevice(physical, &device_info, nullptr, &device), "Surface vkCreateDevice");
        vkGetDeviceQueue(device, queue_family, 0, &queue);

        VkSurfaceCapabilitiesKHR capabilities{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            throw ProbeFailure(VK_ERROR_FORMAT_NOT_SUPPORTED, "Surface color attachment usage");
        uint32_t format_count = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr),
              "Surface format count");
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats.data()),
              "Surface formats");
        const auto format = std::find_if(formats.begin(), formats.end(), [](const auto &value) {
            return value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                   (value.format == VK_FORMAT_R8G8B8A8_UNORM ||
                    value.format == VK_FORMAT_B8G8R8A8_UNORM);
        });
        if (format == formats.end())
            throw ProbeFailure(VK_ERROR_FORMAT_NOT_SUPPORTED, "No RGBA/BGRA UNORM Surface format");
        const VkExtent2D extent = capabilities.currentExtent.width != UINT32_MAX
            ? capabilities.currentExtent
            : VkExtent2D{std::clamp<uint32_t>(ANativeWindow_getWidth(window),
                                              capabilities.minImageExtent.width,
                                              capabilities.maxImageExtent.width),
                         std::clamp<uint32_t>(ANativeWindow_getHeight(window),
                                              capabilities.minImageExtent.height,
                                              capabilities.maxImageExtent.height)};
        VkSwapchainCreateInfoKHR swapchain_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swapchain_info.surface = surface;
        swapchain_info.minImageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount)
            swapchain_info.minImageCount = std::min(swapchain_info.minImageCount,
                                                    capabilities.maxImageCount);
        swapchain_info.imageFormat = format->format;
        swapchain_info.imageColorSpace = format->colorSpace;
        swapchain_info.imageExtent = extent;
        swapchain_info.imageArrayLayers = 1;
        swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_info.preTransform = capabilities.currentTransform;
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> alpha_modes{
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
        for (auto mode : alpha_modes)
            if (capabilities.supportedCompositeAlpha & mode)
            {
                swapchain_info.compositeAlpha = mode;
                break;
            }
        swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchain_info.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain),
              "vkCreateSwapchainKHR");
        uint32_t image_count = 0;
        check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr), "Swapchain image count");
        std::vector<VkImage> images(image_count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data()), "Swapchain images");
        views.resize(image_count);
        framebuffers.resize(image_count);
        VkAttachmentDescription attachment{};
        attachment.format = format->format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
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
        VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass_info.attachmentCount = 1;
        pass_info.pAttachments = &attachment;
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = 1;
        pass_info.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &pass_info, nullptr, &render_pass), "vkCreateRenderPass");
        for (uint32_t i = 0; i < image_count; ++i)
        {
            VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view_info.image = images[i];
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = format->format;
            view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &view_info, nullptr, &views[i]), "vkCreateImageView");
            VkFramebufferCreateInfo framebuffer_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            framebuffer_info.renderPass = render_pass;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = &views[i];
            framebuffer_info.width = extent.width;
            framebuffer_info.height = extent.height;
            framebuffer_info.layers = 1;
            check(vkCreateFramebuffer(device, &framebuffer_info, nullptr, &framebuffers[i]),
                  "vkCreateFramebuffer");
        }
        create_pipeline(extent);
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.queueFamilyIndex = queue_family;
        check(vkCreateCommandPool(device, &pool_info, nullptr, &command_pool), "Surface command pool");
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = command_pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &allocation, &command), "Surface command buffer");
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semaphore_info, nullptr, &acquired), "Acquire semaphore");
        check(vkCreateSemaphore(device, &semaphore_info, nullptr, &finished), "Present semaphore");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device, &fence_info, nullptr, &fence), "Surface fence");
        uint32_t image = 0;
        const auto acquire = vkAcquireNextImageKHR(device, swapchain, 5'000'000'000ull,
                                                   acquired, VK_NULL_HANDLE, &image);
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
            check(acquire, "vkAcquireNextImageKHR");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(command, &begin), "Surface begin command");
        VkClearValue clear{};
        clear.color = {{1, 1, 1, 1}};
        VkRenderPassBeginInfo draw{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        draw.renderPass = render_pass;
        draw.framebuffer = framebuffers[image];
        draw.renderArea.extent = extent;
        draw.clearValueCount = 1;
        draw.pClearValues = &clear;
        vkCmdBeginRenderPass(command, &draw, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdDraw(command, 3, 1, 0, 0);
        vkCmdEndRenderPass(command);
        check(vkEndCommandBuffer(command), "Surface end command");
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &finished;
        check(vkQueueSubmit(queue, 1, &submit, fence), "Surface vkQueueSubmit");
        submitted = true;
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &finished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image;
        const auto status = vkQueuePresentKHR(queue, &present);
        if (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR)
            check(status, "vkQueuePresentKHR");
        check(vkWaitForFences(device, 1, &fence, VK_TRUE, 5'000'000'000ull), "Surface submit fence");
    }

  private:
    void create_pipeline(VkExtent2D extent)
    {
        std::array<VkShaderModule, 2> modules{};
        const uint8_t *code[] = {embedded::triangle_vert, embedded::triangle_frag};
        const size_t sizes[] = {sizeof(embedded::triangle_vert), sizeof(embedded::triangle_frag)};
        try
        {
            std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
            for (size_t i = 0; i < 2; ++i)
            {
                VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
                module.codeSize = sizes[i];
                module.pCode = reinterpret_cast<const uint32_t *>(code[i]);
                check(vkCreateShaderModule(device, &module, nullptr, &modules[i]), "Surface shader");
                stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
                stages[i].module = modules[i];
                stages[i].pName = "main";
            }
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            check(vkCreatePipelineLayout(device, &layout, nullptr, &pipeline_layout), "Surface layout");
            VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport viewport{0, 0, static_cast<float>(extent.width),
                                 static_cast<float>(extent.height), 0, 1};
            VkRect2D scissor{{0, 0}, extent};
            VkPipelineViewportStateCreateInfo viewport_info{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            viewport_info.viewportCount = 1;
            viewport_info.pViewports = &viewport;
            viewport_info.scissorCount = 1;
            viewport_info.pScissors = &scissor;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo sample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            sample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blend{};
            blend.colorWriteMask = 15;
            VkPipelineColorBlendStateCreateInfo blend_info{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            blend_info.attachmentCount = 1;
            blend_info.pAttachments = &blend;
            VkGraphicsPipelineCreateInfo graphics{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            graphics.stageCount = stages.size();
            graphics.pStages = stages.data();
            graphics.pVertexInputState = &vertex;
            graphics.pInputAssemblyState = &assembly;
            graphics.pViewportState = &viewport_info;
            graphics.pRasterizationState = &raster;
            graphics.pMultisampleState = &sample;
            graphics.pColorBlendState = &blend_info;
            graphics.layout = pipeline_layout;
            graphics.renderPass = render_pass;
            check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics, nullptr,
                                             &pipeline), "Surface graphics pipeline");
        }
        catch (...)
        {
            for (auto module : modules)
                if (module) vkDestroyShaderModule(device, module, nullptr);
            throw;
        }
        for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore finished = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool submitted = false;
};
}

SurfaceProbeResult probe_surface(ANativeWindow *window,
                                  const std::function<void()> &on_presented)
{
    try
    {
        SurfaceProbe probe;
        probe.run(window);
        if (on_presented) on_presented();
        return {true, VK_SUCCESS, {}};
    }
    catch (const ProbeFailure &error)
    {
        return {false, error.result, error.what()};
    }
    catch (const std::exception &error)
    {
        return {false, VK_ERROR_INITIALIZATION_FAILED, error.what()};
    }
}
}
