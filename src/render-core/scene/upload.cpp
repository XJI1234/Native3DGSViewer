#include "upload.h"
#include <algorithm>

namespace gs::render::detail
{
namespace
{
constexpr uint64_t page_capacity = 4ull << 20;
}
UploadTransaction::UploadTransaction(GpuDevice &gpu, SplatPass &pass, SceneHandle cpu,
                                     UploadTicket ticket, CameraState camera)
{
    scene = pass.allocate(std::move(cpu), ticket, camera);
    page_ = gpu.buffer(page_capacity, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    page_->SetName(L"Copy upload page");
    check(
        gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&allocator_)),
        "Upload allocator");
    check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, allocator_.Get(), nullptr,
                                        IID_PPV_ARGS(&list_)),
          "Upload list");
    check(list_->Close(), "Upload initial close");
}
bool UploadTransaction::copy_complete(uint64_t completed_value) const
{
    if (completed_value == UINT64_MAX)
        throw GpuFailure{DXGI_ERROR_DEVICE_REMOVED, "Copy fence"};
    if (completed_value >= fence)
        return true;
    if (std::chrono::steady_clock::now() > deadline)
        throw GpuFailure{HRESULT_FROM_WIN32(ERROR_TIMEOUT), "Copy upload deadline",
                         RenderErrorCode::GpuTimeout};
    return false;
}
std::optional<UploadProgress> UploadTransaction::advance(GpuDevice &gpu)
{
    if (ready || !copy_complete(gpu.copy_fence->GetCompletedValue()))
        return {};
    const auto total = scene_bytes(*scene->cpu);
    std::optional<UploadProgress> progress;
    if (fence)
    {
        completed_ += page_bytes_;
        fence = 0;
        progress = UploadProgress{completed_, total};
    }
    if (completed_ == total)
    {
        check(gpu.direct->Wait(gpu.copy_fence.Get(), gpu.copy_value), "Direct copy dependency");
        ready = true;
        page_.Reset();
        return progress;
    }
    page_bytes_ = (std::min)(page_capacity, total - submitted_);
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    check(page_->Map(0, &empty, &mapped), "Upload map");
    const auto &s = *scene->cpu;
    const std::array spans{s.centerLocal, s.scale, s.rotation, s.opacity, s.rgb0, s.shRest};
    uint64_t base = 0;
    for (auto span : spans)
    {
        const uint64_t end = base + span.size_bytes();
        const auto first = (std::max)(base, submitted_),
                   last = (std::min)(end, submitted_ + page_bytes_);
        if (last > first)
            memcpy(static_cast<uint8_t *>(mapped) + first - submitted_,
                   reinterpret_cast<const uint8_t *>(span.data()) + first - base,
                   size_t(last - first));
        base = end;
    }
    page_->Unmap(0, nullptr);
    check(allocator_->Reset(), "Copy allocator reset");
    check(list_->Reset(allocator_.Get(), nullptr), "Copy list reset");
    // Copy queue promotion/decay keeps attributes COMMON between completed chunks.
    list_->CopyBufferRegion(scene->attributes.Get(), submitted_, page_.Get(), 0, page_bytes_);
    check(list_->Close(), "Copy close");
    fence = gpu.submit(gpu.copy.Get(), list_.Get(), gpu.copy_fence.Get(), gpu.copy_value);
    submitted_ += page_bytes_;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    return progress;
}
} // namespace gs::render::detail
