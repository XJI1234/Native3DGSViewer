#include "cuda_sort.h"
#include "contracts.h"
#include <filesystem>
#include <d3d12sdklayers.h>

namespace gs::render::detail
{
std::shared_ptr<CudaSortDevice> CudaSortDevice::probe(ID3D12Device *device)
{
    if (!device) return {};
    // External-fence handle retention under the Debug Layer remains unresolved.
    // Keep diagnostic configurations on the DX12 path.
    ComPtr<ID3D12InfoQueue> debug_queue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&debug_queue)))) return {};
    auto result = std::make_shared<CudaSortDevice>();
    // An absolute application-local path prevents current-directory/PATH DLL injection.
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    if (!length || length >= executable.size()) return {};
    executable.resize(length);
    const auto path = std::filesystem::path(executable).parent_path() / L"gs-cuda-backend.dll";
    result->module_ = LoadLibraryExW(path.c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!result->module_) return {};
    const auto get = reinterpret_cast<GetCudaSortApi>(GetProcAddress(result->module_, "gs_cuda_sort_api"));
    if (!get) return {};
    result->api_ = get(1);
    if (!result->api_ || result->api_->size != sizeof(CudaSortApi) || result->api_->version != 1 ||
        !result->api_->create_device || !result->api_->destroy_device || !result->api_->create_scene ||
        !result->api_->destroy_scene || !result->api_->submit ||
        FAILED(result->api_->create_device(device, &result->device_))) return {};
    return result;
}
CudaSortDevice::~CudaSortDevice()
{
    if (device_) api_->destroy_device(device_);
    // A failed CUDA submission may leave quarantined imports/workspace alive.
    // Keep their runtime and kernel module loaded for the rest of the process.
    if (module_ && !failed_) FreeLibrary(module_);
}
std::shared_ptr<CudaSortScene> CudaSortDevice::prepare(GpuDevice &gpu, SortBuffers &sort)
{
    if (!use_cuda_sort(true, sort.count)) return {};
    DXGI_QUERY_VIDEO_MEMORY_INFO budget{};
    // Include temporary overlap with original DX buffers, CUB scratch and heap alignment.
    if (FAILED(gpu.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &budget)) ||
        !fits_budget(uint64_t(sort.count) * 48 + (64ull << 20), budget.Budget, budget.CurrentUsage))
        return {};
    auto scene = std::make_shared<CudaSortScene>();
    scene->device_ = shared_from_this();
    ID3D12Resource *resources[4]{}; // Borrowed until destroy_scene; ComPtr takes references.
    if (FAILED(api_->create_scene(device_, sort.count, &scene->scene_, resources, &scene->selector)))
        return {};
    for (uint32_t i = 0; i < 4; ++i) scene->buffers[i] = resources[i];
    for (uint32_t i = 0; i < 2; ++i) {
        sort.keys[i] = scene->buffers[i];
        sort.values[i] = scene->buffers[i + 2];
    }
    return scene;
}
CudaSortScene::~CudaSortScene()
{
    if (scene_) device_->api_->destroy_scene(scene_);
}
bool CudaSortScene::submit(ID3D12CommandQueue *queue)
{
    const bool submitted = SUCCEEDED(device_->api_->submit(scene_, queue));
    if (!submitted) device_->failed_ = true;
    return submitted;
}
void CudaSortScene::barriers(ID3D12GraphicsCommandList *list, bool release)
{
    std::array<D3D12_RESOURCE_BARRIER, 4> barriers{};
    for (uint32_t i = 0; i < 4; ++i) {
        auto &b = barriers[i]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = buffers[i].Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = release ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = release ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    for (const auto &barrier : barriers) list->ResourceBarrier(1, &barrier);
}
}
