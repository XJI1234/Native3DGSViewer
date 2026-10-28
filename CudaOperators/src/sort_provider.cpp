#include "gs_cuda/sort.h"
#include "cuda_api.h"
#include <array>

using namespace gs::cuda_ops;
using gs::render::detail::CudaSortApi;
namespace
{
struct Device { std::shared_ptr<Context> context; };
struct Scene
{
    std::shared_ptr<Context> context;
    std::array<std::unique_ptr<SharedBuffer>, 4> buffers;
    std::unique_ptr<SortWorkspace> workspace;
    uint32_t count = 0, selector = 0;
    ~Scene()
    {
        // The renderer retains Scene until its final D3D consumer fence completes.
        // On a lost device, quarantine CUDA workspace as well as imported buffers.
        try { if (!context->wait_idle()) workspace.release(); }
        catch (...) { workspace.release(); }
    }
};
HRESULT create_device(ID3D12Device *gpu, void **output) noexcept
{
    if (!gpu || !output) return E_INVALIDARG;
    *output = nullptr;
    try {
        auto device = std::make_unique<Device>();
        device->context = std::make_shared<Context>(gpu, gpu->GetAdapterLuid());
        // Execute a real kernel: an NVIDIA vendor ID alone does not prove support.
        SharedBuffer probe(*device->context, 4);
        fill_u32(*device->context, probe, 0, 1);
        if (!device->context->wait_idle()) return E_FAIL;
        *output = device.release(); return S_OK;
    } catch (...) { return E_FAIL; }
}
void destroy_device(void *device) noexcept { delete static_cast<Device *>(device); }
HRESULT create_scene(void *owner, uint32_t count, void **output, ID3D12Resource **resources,
                     uint32_t *selector) noexcept
{
    if (!owner || !count || !output || !resources || !selector) return E_INVALIDARG;
    *output = nullptr;
    try {
        auto scene = std::make_unique<Scene>();
        scene->context = static_cast<Device *>(owner)->context;
        scene->count = count;
        const uint64_t bytes = ((uint64_t(count) + 511) / 512) * 512 * 4;
        for (auto &buffer : scene->buffers) buffer = std::make_unique<SharedBuffer>(*scene->context, bytes);
        scene->workspace = std::make_unique<SortWorkspace>(*scene->context, count, true);
        fill_u32(*scene->context, *scene->buffers[0], 0, count);
        fill_u32(*scene->context, *scene->buffers[2], 0, count);
        scene->selector = scene->workspace->submit(*scene->context, *scene->buffers[0], *scene->buffers[1],
                                                  *scene->buffers[2], *scene->buffers[3], count);
        if (!scene->context->wait_idle()) return E_FAIL;
        for (uint32_t i = 0; i < 4; ++i) resources[i] = scene->buffers[i]->resource();
        *selector = scene->selector; *output = scene.release(); return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
void destroy_scene(void *scene) noexcept { delete static_cast<Scene *>(scene); }
HRESULT submit(void *opaque, ID3D12CommandQueue *queue) noexcept
{
    if (!opaque || !queue) return E_INVALIDARG;
    auto &scene = *static_cast<Scene *>(opaque);
    try {
        scene.context->begin(queue);
        const auto selected = scene.workspace->submit(*scene.context, *scene.buffers[0], *scene.buffers[1],
                                                      *scene.buffers[2], *scene.buffers[3], scene.count);
        scene.context->end(queue);
        return selected == scene.selector ? S_OK : E_FAIL;
    } catch (...) { return E_FAIL; }
}
const CudaSortApi api{sizeof(CudaSortApi), 1, create_device, destroy_device, create_scene, destroy_scene, submit};
}
extern "C" __declspec(dllexport) const CudaSortApi *__cdecl gs_cuda_sort_api(uint32_t version) noexcept
{
    return version == api.version ? &api : nullptr;
}
