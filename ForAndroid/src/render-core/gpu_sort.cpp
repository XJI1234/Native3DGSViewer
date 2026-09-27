#include "render-core/device_probe.h"
#include "embedded_shaders.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

namespace gs::android::render
{
namespace
{
constexpr uint32_t kGroupWidth = 128;

void require(VkResult status, const char *operation)
{
    if (status != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " returned " + std::to_string(status));
}

uint32_t groups_for(uint32_t count)
{
    return count / kGroupWidth + (count % kGroupWidth != 0);
}

std::vector<uint32_t> read_shader(const std::string &path)
{
    if (path.empty())
        throw std::runtime_error("Missing shader name");
    if (path[0] == ':')
    {
        struct Source { const char *name; const uint8_t *data; size_t size; };
        constexpr Source embedded_sources[] = {
            {":histogram", embedded::histogram, sizeof(embedded::histogram)},
            {":scan", embedded::scan, sizeof(embedded::scan)},
            {":add_prefix", embedded::add_prefix, sizeof(embedded::add_prefix)},
            {":scatter", embedded::scatter, sizeof(embedded::scatter)}};
        for (const auto &source : embedded_sources)
            if (path == source.name)
        {
            std::vector<uint32_t> code(source.size / sizeof(uint32_t));
            std::memcpy(code.data(), source.data, source.size);
            return code;
        }
        throw std::runtime_error("Unknown embedded shader: " + path);
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > (8 << 20) || (file.tellg() % 4) != 0)
        throw std::runtime_error("Shader read: " + path);
    const auto bytes = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> code(bytes / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(code.data()), bytes))
        throw std::runtime_error("Shader truncated: " + path);
    if (code.front() != 0x07230203)
        throw std::runtime_error("Invalid SPIR-V magic: " + path);
    return code;
}

struct Buffer
{
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t bytes = 0;
};

struct Params
{
    uint32_t count;
    uint32_t groups;
    uint32_t shift;
};

class SortContext
{
  public:
    SortContext(const SortContext &) = delete;
    SortContext &operator=(const SortContext &) = delete;
    explicit SortContext(const std::string &shader_directory)
    {
        try
        {
            initialize(shader_directory);
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }

    void initialize(const std::string &shader_directory)
    {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instance_info.pApplicationInfo = &app;
        require(vkCreateInstance(&instance_info, nullptr, &instance_), "vkCreateInstance");
        uint32_t count = 0;
        require(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                "vkEnumeratePhysicalDevices");
        if (!count)
            throw std::runtime_error("No Vulkan physical device");
        std::vector<VkPhysicalDevice> physical_devices(count);
        require(vkEnumeratePhysicalDevices(instance_, &count, physical_devices.data()),
                "vkEnumeratePhysicalDevices data");
        for (auto candidate : physical_devices)
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
                if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
                {
                    physical_ = candidate;
                    queue_family_ = i;
                    break;
                }
            }
            if (physical_)
                break;
        }
        if (!physical_)
            throw std::runtime_error("No Vulkan 1.1 compute device");
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        require(vkCreateDevice(physical_, &device_info, nullptr, &device_), "vkCreateDevice");
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
        vkGetPhysicalDeviceMemoryProperties(physical_, &memory_properties_);

        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        for (uint32_t i = 0; i < bindings.size(); ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_info.bindingCount = bindings.size();
        set_info.pBindings = bindings.data();
        require(vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &set_layout_),
                "vkCreateDescriptorSetLayout");
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &set_layout_;
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Params)};
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        require(vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_),
                "vkCreatePipelineLayout");
        constexpr std::array<const char *, 4> names{"histogram", "scan", "add_prefix",
                                                     "scatter"};
        for (size_t i = 0; i < names.size(); ++i)
        {
            const auto code = read_shader(shader_directory.empty()
                ? ":" + std::string(names[i])
                : shader_directory + "/" + names[i] + ".spv");
            VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            module_info.codeSize = code.size() * sizeof(uint32_t);
            module_info.pCode = code.data();
            VkShaderModule module = VK_NULL_HANDLE;
            require(vkCreateShaderModule(device_, &module_info, nullptr, &module),
                    "vkCreateShaderModule");
            VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            pipeline_info.stage.module = module;
            pipeline_info.stage.pName = "main";
            pipeline_info.layout = pipeline_layout_;
            const VkResult status = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1,
                                                               &pipeline_info, nullptr,
                                                               &pipelines_[i]);
            vkDestroyShaderModule(device_, module, nullptr);
            require(status, "vkCreateComputePipelines");
        }
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * 512};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 512;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        require(vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_),
                "vkCreateDescriptorPool");
        VkCommandPoolCreateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        command_info.queueFamilyIndex = queue_family_;
        command_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        require(vkCreateCommandPool(device_, &command_info, nullptr, &command_pool_),
                "vkCreateCommandPool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = command_pool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        require(vkAllocateCommandBuffers(device_, &allocate, &command_),
                "vkAllocateCommandBuffers");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        require(vkCreateFence(device_, &fence_info, nullptr, &fence_), "vkCreateFence");
    }

    ~SortContext()
    {
        cleanup();
    }

    void cleanup() noexcept
    {
        if (device_)
        {
            if (fence_) vkDestroyFence(device_, fence_, nullptr);
            if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
            if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            for (auto pipeline : pipelines_)
                if (pipeline) vkDestroyPipeline(device_, pipeline, nullptr);
            if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            if (set_layout_) vkDestroyDescriptorSetLayout(device_, set_layout_, nullptr);
            for (const auto &buffer : buffers_)
            {
                if (buffer->handle) vkDestroyBuffer(device_, buffer->handle, nullptr);
                if (buffer->memory) vkFreeMemory(device_, buffer->memory, nullptr);
            }
            vkDestroyDevice(device_, nullptr);
        }
        if (instance_) vkDestroyInstance(instance_, nullptr);
    }

    Buffer *buffer(uint64_t bytes)
    {
        buffers_.push_back(std::make_unique<Buffer>());
        auto *result = buffers_.back().get();
        result->bytes = std::max<uint64_t>(bytes, 4);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = result->bytes;
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        require(vkCreateBuffer(device_, &info, nullptr, &result->handle), "vkCreateBuffer");
        VkMemoryRequirements requirement{};
        vkGetBufferMemoryRequirements(device_, result->handle, &requirement);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < memory_properties_.memoryTypeCount; ++i)
        {
            const auto flags = memory_properties_.memoryTypes[i].propertyFlags;
            if ((requirement.memoryTypeBits & (1u << i)) &&
                (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            {
                type = i;
                break;
            }
        }
        if (type == UINT32_MAX)
            throw std::runtime_error("No coherent host-visible Vulkan memory");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirement.size;
        allocation.memoryTypeIndex = type;
        require(vkAllocateMemory(device_, &allocation, nullptr, &result->memory),
                "vkAllocateMemory");
        require(vkBindBufferMemory(device_, result->handle, result->memory, 0),
                "vkBindBufferMemory");
        return result;
    }

    void write(Buffer *target, const std::vector<uint32_t> &values)
    {
        void *mapped = nullptr;
        require(vkMapMemory(device_, target->memory, 0, target->bytes, 0, &mapped),
                "vkMapMemory input");
        std::memcpy(mapped, values.data(), values.size() * sizeof(uint32_t));
        vkUnmapMemory(device_, target->memory);
    }

    std::vector<uint32_t> read(Buffer *source, uint32_t count)
    {
        void *mapped = nullptr;
        require(vkMapMemory(device_, source->memory, 0, source->bytes, 0, &mapped),
                "vkMapMemory output");
        std::vector<uint32_t> values(count);
        std::memcpy(values.data(), mapped, values.size() * sizeof(uint32_t));
        vkUnmapMemory(device_, source->memory);
        return values;
    }

    void begin()
    {
        require(vkResetCommandBuffer(command_, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        require(vkBeginCommandBuffer(command_, &info), "vkBeginCommandBuffer");
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
                             0, nullptr, 0, nullptr);
    }

    void dispatch(size_t pipeline, std::initializer_list<Buffer *> bindings,
                  Params params, uint32_t groups)
    {
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = descriptor_pool_;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &set_layout_;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        require(vkAllocateDescriptorSets(device_, &allocation, &descriptor),
                "vkAllocateDescriptorSets");
        std::array<VkDescriptorBufferInfo, 3> infos{};
        std::array<VkWriteDescriptorSet, 3> writes{};
        size_t index = 0;
        for (Buffer *buffer : bindings)
        {
            infos[index] = {buffer->handle, 0, buffer->bytes};
            writes[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[index].dstSet = descriptor;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[index].pBufferInfo = &infos[index];
            ++index;
        }
        vkUpdateDescriptorSets(device_, index, writes.data(), 0, nullptr);
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines_[pipeline]);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_,
                                0, 1, &descriptor, 0, nullptr);
        vkCmdPushConstants(command_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(params), &params);
        vkCmdDispatch(command_, groups, 1, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
                             0, nullptr, 0, nullptr);
    }

    void submit()
    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier,
                             0, nullptr, 0, nullptr);
        require(vkEndCommandBuffer(command_), "vkEndCommandBuffer");
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &command_;
        require(vkQueueSubmit(queue_, 1, &info, fence_), "vkQueueSubmit");
        const auto status = vkWaitForFences(device_, 1, &fence_, VK_TRUE, 5'000'000'000ull);
        if (status == VK_TIMEOUT)
        {
            // Pending work cannot be freed while the device still reads its buffers.
            // This diagnostic helper is synchronous; production recovery owns device loss.
            vkDeviceWaitIdle(device_);
        }
        require(status, "vkWaitForFences");
        require(vkResetFences(device_, 1, &fence_), "vkResetFences");
    }

  private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 4> pipelines_{};
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Buffer>> buffers_;
};

struct ScanLevel
{
    Buffer *input;
    Buffer *prefix;
    Buffer *sums;
    uint32_t count;
};

void verify_case(SortContext &context, const std::vector<uint32_t> &keys)
{
    if (keys.empty())
        return;
    if (keys.size() > UINT32_MAX)
        throw std::runtime_error("Sort fixture exceeds uint32 range");
    const uint32_t count = static_cast<uint32_t>(keys.size());
    const uint32_t group_count = groups_for(count);
    Buffer *input = context.buffer(uint64_t(count) * 2 * sizeof(uint32_t));
    Buffer *output = context.buffer(uint64_t(count) * 2 * sizeof(uint32_t));
    Buffer *histogram = context.buffer(16ull * group_count * sizeof(uint32_t));
    std::vector<uint32_t> values(count);
    std::iota(values.begin(), values.end(), 0);
    std::vector<uint32_t> pairs(size_t(count) * 2);
    for (uint32_t i = 0; i < count; ++i)
    {
        pairs[size_t(i) * 2] = keys[i];
        pairs[size_t(i) * 2 + 1] = i;
    }
    context.write(input, pairs);

    std::vector<ScanLevel> levels;
    Buffer *source = histogram;
    uint32_t length = 16 * group_count;
    do
    {
        const uint32_t blocks = groups_for(length);
        levels.push_back({source, context.buffer(uint64_t(length) * sizeof(uint32_t)),
                          context.buffer(uint64_t(blocks) * sizeof(uint32_t)), length});
        source = levels.back().sums;
        length = blocks;
    } while (length > 1);

    context.begin();
    for (uint32_t shift = 0; shift < 32; shift += 4)
    {
        context.dispatch(0, {input, histogram}, {count, group_count, shift}, group_count);
        for (const auto &level : levels)
            context.dispatch(1, {level.input, level.prefix, level.sums},
                             {level.count, group_count, shift}, groups_for(level.count));
        for (size_t i = levels.size(); i > 1; --i)
        {
            const auto &lower = levels[i - 2];
            context.dispatch(2, {lower.prefix, levels[i - 1].prefix},
                             {lower.count, group_count, shift}, groups_for(lower.count));
        }
        context.dispatch(3, {input, levels.front().prefix, output},
                         {count, group_count, shift}, group_count);
        std::swap(input, output);
    }
    context.submit();
    const auto gpu_pairs = context.read(input, count * 2);
    std::stable_sort(values.begin(), values.end(), [&](uint32_t a, uint32_t b) {
        return keys[a] < keys[b];
    });
    for (uint32_t i = 0; i < count; ++i)
    {
        if (gpu_pairs[size_t(i) * 2 + 1] != values[i] ||
            gpu_pairs[size_t(i) * 2] != keys[values[i]])
            throw std::runtime_error("Stable GPU radix mismatch at index " +
                                     std::to_string(i) + " of " + std::to_string(count));
    }
}
} // namespace

std::string sort_self_test(const std::string &shader_directory)
{
    try
    {
        SortContext context(shader_directory);
        verify_case(context, {});
        verify_case(context, {7});
        verify_case(context, {4, 4, 4, 4, 4});
        std::vector<uint32_t> keys(257);
        for (uint32_t i = 0; i < keys.size(); ++i)
            keys[i] = (i * 19) % 17;
        verify_case(context, keys);
        std::mt19937 random(513);
        keys.resize(4097);
        for (auto &key : keys)
            key = random();
        verify_case(context, keys);
        keys.resize(65537);
        for (auto &key : keys)
            key = random() % 1000;
        verify_case(context, keys);
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}

std::string sort_scene_self_test(const gs::SceneHandle &scene,
                                  const std::string &shader_directory)
{
    try
    {
        if (!scene || !scene->count || scene->count > UINT32_MAX / 2 ||
            scene->centerLocal.size() != scene->count * 3)
            return "Scene sort fixture layout";
        std::vector<uint32_t> keys(static_cast<size_t>(scene->count));
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const float x = scene->centerLocal[i * 3];
            const float y = scene->centerLocal[i * 3 + 1];
            const float z = scene->centerLocal[i * 3 + 2];
            const float distance = x * x + y * y + z * z;
            if (!std::isfinite(distance))
                return "Scene diagnostic distance overflow";
            keys[i] = ~std::bit_cast<uint32_t>(distance);
        }
        SortContext context(shader_directory);
        verify_case(context, keys);
        return {};
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
}

} // namespace gs::android::render
