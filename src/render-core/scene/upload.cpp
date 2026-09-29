#include "upload.h"
#include <algorithm>

namespace gs::render::detail
{
UploadTransaction::UploadTransaction(GpuDevice &gpu, SplatPass &pass, SceneHandle cpu,
                                     UploadTicket ticket, CameraState camera, uint8_t sh_degree,
                                     bool memory_mitigation, uint32_t point_stride)
{
    scene = pass.allocate(std::move(cpu), ticket, camera, sh_degree,
                          memory_mitigation, point_stride);
    total_bytes_ = scene->count * 56ull +
                   scene->count * uint64_t(scene->sh_floats_per_splat) * sizeof(float);
    page_ = gpu.buffer((std::min)(total_bytes_, upload_page_capacity), D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ);
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
    const auto total = total_bytes_;
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
    page_bytes_ = (std::min)(upload_page_capacity, total - submitted_);
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    check(page_->Map(0, &empty, &mapped), "Upload map");
    const auto &s = *scene->cpu;
    const uint64_t base_bytes = scene->count * 56ull;
    const uint64_t source_sh_stride = s.shDegree == 0 ? 0 :
        3ull * ((s.shDegree + 1) * (s.shDegree + 1) - 1);
    uint64_t base = 0;
    const std::array spans{s.centerLocal, s.scale, s.rotation, s.opacity, s.rgb0};
    for (auto span : spans)
    {
        const uint64_t element_bytes = span.size_bytes() / s.count;
        const uint64_t end = base + scene->count * element_bytes;
        const auto first = (std::max)(base, submitted_),
                   last = (std::min)(end, submitted_ + page_bytes_);
        if (last > first)
        {
            if (scene->point_stride == 1)
                memcpy(static_cast<uint8_t *>(mapped) + first - submitted_,
                       reinterpret_cast<const uint8_t *>(span.data()) + first - base,
                       size_t(last - first));
            else for (uint64_t destination = first; destination < last; )
            {
                const uint64_t offset = destination - base;
                const uint64_t point = offset / element_bytes;
                const uint64_t in_point = offset % element_bytes;
                const uint64_t amount = (std::min)(last - destination, element_bytes - in_point);
                memcpy(static_cast<uint8_t *>(mapped) + destination - submitted_,
                       reinterpret_cast<const uint8_t *>(span.data()) +
                           point * scene->point_stride * element_bytes + in_point,
                       static_cast<size_t>(amount));
                destination += amount;
            }
        }
        base = end;
    }
    if (submitted_ + page_bytes_ > base_bytes && scene->sh_floats_per_splat)
    {
        const uint64_t page_begin = (std::max)(submitted_, base_bytes);
        const uint64_t page_end = (std::min)(total, submitted_ + page_bytes_);
        const auto *source = s.shRest.data();
        const uint64_t src_stride_bytes = source_sh_stride * sizeof(float);
        const uint64_t dst_stride_bytes = uint64_t(scene->sh_floats_per_splat) * sizeof(float);
        if (src_stride_bytes == dst_stride_bytes && scene->point_stride == 1)
        {
            std::memcpy(static_cast<uint8_t *>(mapped) + page_begin - submitted_,
                        reinterpret_cast<const uint8_t *>(source) + page_begin - base_bytes,
                        static_cast<size_t>(page_end - page_begin));
        }
        else for (uint64_t destination = page_begin; destination < page_end; )
        {
            const uint64_t offset = destination - base_bytes;
            const uint64_t point = offset / dst_stride_bytes;
            const uint64_t in_point = offset % dst_stride_bytes;
            const uint64_t amount = (std::min)(page_end - destination, dst_stride_bytes - in_point);
            std::memcpy(static_cast<uint8_t *>(mapped) + destination - submitted_,
                        reinterpret_cast<const uint8_t *>(source) +
                            point * scene->point_stride * src_stride_bytes + in_point,
                        static_cast<size_t>(amount));
            destination += amount;
        }
    }
    page_->Unmap(0, nullptr);
    check(allocator_->Reset(), "Copy allocator reset");
    check(list_->Reset(allocator_.Get(), nullptr), "Copy list reset");
    // Copy queue promotion/decay keeps attributes COMMON between completed chunks.
    const uint64_t base_copy = submitted_ < base_bytes
                                   ? (std::min)(page_bytes_, base_bytes - submitted_)
                                   : 0;
    if (base_copy)
        list_->CopyBufferRegion(scene->attributes.Get(), submitted_, page_.Get(), 0, base_copy);
    if (page_bytes_ > base_copy && scene->sh_attributes)
        list_->CopyBufferRegion(scene->sh_attributes.Get(), submitted_ + base_copy - base_bytes,
                                page_.Get(), base_copy, page_bytes_ - base_copy);
    check(list_->Close(), "Copy close");
    fence = gpu.submit(gpu.copy.Get(), list_.Get(), gpu.copy_fence.Get(), gpu.copy_value);
    submitted_ += page_bytes_;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    return progress;
}
} // namespace gs::render::detail
