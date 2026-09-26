#include "splat.h"
#ifdef GS_RENDER_TEST_HOOKS
#include "test_renderer.h"
#endif
#include "upload.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <d3d12sdklayers.h>
#include <deque>
#include <cstdio>
#include <mutex>
#include <thread>

namespace gs::render
{
namespace
{
#ifdef GS_RENDER_TEST_HOOKS
namespace testing_build
#else
namespace production_build
#endif
{
using namespace detail;
using Clock = std::chrono::steady_clock;
RenderError failure(RenderErrorCode code, HRESULT hr = S_OK, const char *text = "")
{
    return {code, hr, std::string(text).substr(0, 512)};
}
std::string budget_diagnostic(const char *stage, uint64_t required,
                              const DXGI_QUERY_VIDEO_MEMORY_INFO &local,
                              const DXGI_QUERY_VIDEO_MEMORY_INFO &nonlocal, bool uma,
                              HRESULT nonlocal_hr)
{
    char query_hr[11]{};
    snprintf(query_hr, sizeof(query_hr), "0x%08X", static_cast<uint32_t>(nonlocal_hr));
    return std::string(stage) + " UMA=" + (uma ? "1" : "0") +
           " required_local=" + std::to_string(required) +
           " local_budget=" + std::to_string(local.Budget) +
           " local_usage=" + std::to_string(local.CurrentUsage) +
           " required_nonlocal=" + std::to_string(uma ? 0 : upload_reserve_bytes) +
           " nonlocal_query_hr=" + query_hr +
           " nonlocal_budget=" + (SUCCEEDED(nonlocal_hr) ?
               std::to_string(nonlocal.Budget) : "unavailable") +
           " nonlocal_usage=" + (SUCCEEDED(nonlocal_hr) ?
               std::to_string(nonlocal.CurrentUsage) : "unavailable");
}
bool same_camera(const CameraState &a, const CameraState &b)
{
    return a.position_rub.x == b.position_rub.x && a.position_rub.y == b.position_rub.y &&
           a.position_rub.z == b.position_rub.z && a.orientation_xyzw.x == b.orientation_xyzw.x &&
           a.orientation_xyzw.y == b.orientation_xyzw.y &&
           a.orientation_xyzw.z == b.orientation_xyzw.z &&
           a.orientation_xyzw.w == b.orientation_xyzw.w &&
           a.vertical_fov_radians == b.vertical_fov_radians && a.near_plane == b.near_plane &&
           a.far_plane == b.far_plane;
}
struct Command
{
    enum class Kind
    {
        Upload,
        Cancel,
        Clear,
        Camera,
        Attach,
        Detach,
        Resize
    } kind;
    UploadTicket ticket = 0;
    SurfaceGeneration generation = 0;
    ViewportRevision revision = 0;
    SceneHandle scene;
    CameraState camera;
    Viewport viewport;
    ComPtr<IDXGISwapChain3> swapchain;
};
struct FrameSlot
{
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Resource> readback;
    std::shared_ptr<SceneGpu> scene;
    uint64_t fence = 0, frame = 0;
    double cpu_ms = 0, present_ms = 0;
};
class Renderer final : public IRenderer
{
  public:
    Renderer(QualityConfig quality, EventSink sink
#ifdef GS_RENDER_TEST_HOOKS
             ,
             std::shared_ptr<RendererTestControl> control = {}
#endif
             )
        : quality_(quality), sink_(std::move(sink))
#ifdef GS_RENDER_TEST_HOOKS
          ,
          control_(std::move(control))
#endif
    {
        initialize();
        publish_device();
    }
    std::vector<std::string> debug_errors() const
    {
        return gpu_ ? gpu_->debug_errors() : std::vector<std::string>{};
    }
    ~Renderer() override
    {
        try
        {
            if (gpu_)
            {
                gpu_->wait(gpu_->copy_fence.Get(), gpu_->copy_value);
                gpu_->wait(gpu_->direct_fence.Get(), gpu_->direct_value);
            }
        }
        catch (...)
        {
            if (gpu_)
            {
                ComPtr<ID3D12Device5> device;
                if (SUCCEEDED(gpu_->device.As(&device)))
                    device->RemoveDevice();
            }
        }
    }
    SurfaceGeneration surface_generation() const override
    {
        std::lock_guard lock(mutex_);
        return generation_;
    }
    ID3D12CommandQueue *addref_surface_queue(SurfaceGeneration generation) override
    {
        std::lock_guard lock(mutex_);
        if (!surface_queue_ || fatal_ || generation != generation_)
            return nullptr;
        surface_queue_->AddRef();
        return surface_queue_.Get();
    }
    std::optional<RenderError> attach_swapchain(SurfaceGeneration generation,
                                                IDXGISwapChain3 *swapchain) override
    {
        std::lock_guard lock(mutex_);
        if (!swapchain || !surface_device_ || fatal_ || generation != generation_)
            return failure(RenderErrorCode::InvalidSurface);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        ComPtr<ID3D12Device> device;
        if (FAILED(swapchain->GetDesc1(&desc)) ||
            FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device))) ||
            device.Get() != surface_device_.Get() || desc.Width > 16384 || desc.Height > 16384 ||
            desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || desc.BufferCount < 2 ||
            desc.BufferCount > 3 || desc.SampleDesc.Count != 1 ||
            desc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ||
            desc.AlphaMode != DXGI_ALPHA_MODE_PREMULTIPLIED || desc.Scaling != DXGI_SCALING_STRETCH)
            return failure(RenderErrorCode::InvalidSurface);
        Command cmd{Command::Kind::Attach};
        cmd.generation = generation;
        cmd.swapchain = swapchain;
        enqueue(std::move(cmd));
        return {};
    }
    void detach_swapchain(SurfaceGeneration generation) override
    {
        std::lock_guard lock(mutex_);
        if (generation != generation_)
            return;
        Command c{Command::Kind::Detach};
        c.generation = generation;
        enqueue(std::move(c));
    }
    std::variant<UploadTicket, RenderError> upload_scene(SceneHandle scene,
                                                         CameraState camera) override
    {
        if (auto error = validate_scene(scene))
            return *error;
        if (auto error = validate_camera(camera))
            return *error;
        auto relative = relative_camera(*scene, camera);
        for (float v : relative)
            if (!std::isfinite(v) || std::abs(v) > 1e19f)
                return failure(RenderErrorCode::InvalidCamera);
        std::lock_guard lock(mutex_);
        if (!surface_adapter_ || fatal_ || recovering_)
            return failure(RenderErrorCode::DeviceRemoved);
        if (commands_.size() >= 64)
            return failure(RenderErrorCode::ResourceLimit);
#ifdef GS_RENDER_TEST_HOOKS
        if (control_ && control_->reject_budget)
            return failure(RenderErrorCode::OutOfVideoMemory);
#endif
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
        const auto local_hr = surface_adapter_->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        if (FAILED(local_hr))
            return failure(RenderErrorCode::InternalFailure, local_hr, "Local budget");
        const auto nonlocal_hr = surface_adapter_->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
        if (!surface_uma_ && FAILED(nonlocal_hr))
            return failure(RenderErrorCode::InternalFailure, nonlocal_hr, "Nonlocal budget");
        const auto required = incremental_bytes(*scene);
        if (!fits_scene_budgets(required, upload_reserve_bytes, local.Budget, local.CurrentUsage,
                                nonlocal.Budget, nonlocal.CurrentUsage, surface_uma_))
        {
            const auto diagnostic = budget_diagnostic("upload_admission", required, local,
                                                       nonlocal, surface_uma_, nonlocal_hr);
            return failure(RenderErrorCode::OutOfVideoMemory, S_OK, diagnostic.c_str());
        }
        Command c{Command::Kind::Upload};
        c.ticket = ++next_ticket_;
        c.scene = std::move(scene);
        c.camera = camera;
        auto ticket = c.ticket;
        commands_.push_back(std::move(c));
        return ticket;
    }
    void cancel_upload(UploadTicket ticket) override
    {
        std::lock_guard lock(mutex_);
        if (ticket == 0 || ticket > next_ticket_ || ticket == active_ticket_)
            return;
        Command c{Command::Kind::Cancel};
        c.ticket = ticket;
        enqueue(std::move(c));
    }
    void clear_scene() override
    {
        std::lock_guard lock(mutex_);
        enqueue(Command{Command::Kind::Clear});
    }
    std::optional<RenderError> set_camera(UploadTicket ticket, CameraState camera) override
    {
        if (auto e = validate_camera(camera))
            return e;
        std::lock_guard lock(mutex_);
        if (recovering_ || fatal_)
            return failure(RenderErrorCode::DeviceRemoved);
        if (ticket == 0 || ticket != active_ticket_)
            return failure(RenderErrorCode::InvalidScene);
        if (ticket == last_camera_ticket_ && same_camera(camera, last_camera_))
            return {};
        if (active_cpu_)
        {
            for (float v : relative_camera(*active_cpu_, camera))
                if (!std::isfinite(v) || std::abs(v) > 1e19f)
                    return failure(RenderErrorCode::InvalidCamera);
        }
        Command c{Command::Kind::Camera};
        c.ticket = ticket;
        c.camera = camera;
        enqueue(std::move(c));
        last_camera_ = camera;
        last_camera_ticket_ = ticket;
        return {};
    }
    std::optional<RenderError> resize(SurfaceGeneration generation, ViewportRevision revision,
                                      Viewport viewport) override
    {
        std::lock_guard lock(mutex_);
        if (generation != generation_ || viewport.physical_width > 16384 ||
            viewport.physical_height > 16384)
            return failure(RenderErrorCode::InvalidSurface);
        if (revision <= accepted_revision_)
            return {};
        if (viewport.physical_width && viewport.physical_height && surface_adapter_)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO budget{};
            if (FAILED(surface_adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                                                              &budget)))
                return failure(RenderErrorCode::InternalFailure);
            if (!fits_budget(uint64_t(viewport.physical_width) * viewport.physical_height * 12,
                             budget.Budget, budget.CurrentUsage))
                return failure(RenderErrorCode::OutOfVideoMemory);
        }
        accepted_revision_ = revision;
        Command c{Command::Kind::Resize};
        c.generation = generation;
        c.revision = revision;
        c.viewport = viewport;
        enqueue(std::move(c));
        return {};
    }
    RenderStats get_stats() const override
    {
        std::lock_guard lock(mutex_);
        return stats_;
    }
    void render_frame() override
    {
        const auto caller = std::this_thread::get_id();
        {
            std::lock_guard lock(mutex_);
            if (render_thread_ == std::thread::id{})
                render_thread_ = caller;
            if (render_thread_ != caller)
            {
                ++stats_.wrong_thread_frame_calls;
                return;
            }
        }
        try
        {
            if (fatal_)
                return;
            if (recovering_ && !gpu_)
                rebuild_device();
            if (!fatal_)
            {
#ifdef GS_RENDER_TEST_HOOKS
                if (control_ && control_->fence_timeout.exchange(false))
                {
                    gpu_->wait(gpu_->direct_fence.Get(), gpu_->direct_value + 1);
                }
#endif
                process_commands();
                pump_upload();
                collect_stats();
                if (recovering_ && !swapchain_ && Clock::now() > rebind_deadline_)
                    throw GpuFailure{DXGI_ERROR_DEVICE_REMOVED, "Surface rebind timeout"};
                if (swapchain_ && !surface_lost_ && viewport_.physical_width &&
                    viewport_.physical_height)
                    draw_frame();
            }
        }
        catch (const GpuFailure &e)
        {
            if (recovering_)
            {
                fatal_ = true;
                if (pending_)
                    emit(RendererEvent::Kind::SceneFailed, pending_->scene->ticket,
                         failure(RenderErrorCode::UploadFailed, e.hr, e.operation));
                emit(RendererEvent::Kind::FatalDeviceError, 0,
                     failure(RenderErrorCode::DeviceRemoved, e.hr, e.operation));
            }
            else if (e.hr == DXGI_ERROR_DEVICE_REMOVED || e.hr == DXGI_ERROR_DEVICE_RESET ||
                     e.hr == DXGI_ERROR_DEVICE_HUNG || e.code == RenderErrorCode::GpuTimeout ||
                     (gpu_ && FAILED(gpu_->device->GetDeviceRemovedReason())))
            {
                try
                {
                    recover(e);
                }
                catch (...)
                {
                    fatal_ = true;
                    emit(RendererEvent::Kind::FatalDeviceError, 0,
                         failure(RenderErrorCode::DeviceRemoved, e.hr));
                }
            }
            else
            {
                if (pending_)
                {
                    emit(RendererEvent::Kind::SceneFailed, pending_->scene->ticket,
                         failure(e.hr == E_OUTOFMEMORY ? RenderErrorCode::OutOfVideoMemory
                                                       : RenderErrorCode::UploadFailed,
                                 e.hr, e.operation));
                    abandon_pending();
                }
                emit(RendererEvent::Kind::RenderFault, 0,
                     failure(RenderErrorCode::SurfaceLost, e.hr, e.operation));
                surface_lost_ = true;
                emit(RendererEvent::Kind::SurfaceRebindRequired);
            }
        }
        catch (const std::bad_alloc &)
        {
            if (pending_)
            {
                emit(RendererEvent::Kind::SceneFailed, pending_->scene->ticket,
                     failure(RenderErrorCode::ResourceLimit, E_OUTOFMEMORY,
                             "Upload host allocation"));
                abandon_pending();
            }
            emit(RendererEvent::Kind::RenderFault, 0,
                 failure(RenderErrorCode::ResourceLimit, E_OUTOFMEMORY));
        }
        if (gpu_)
            for (const auto &text : gpu_->debug_errors())
                if (std::find(reported_debug_.begin(), reported_debug_.end(), text) ==
                    reported_debug_.end())
                {
                    reported_debug_.push_back(text);
                    emit(RendererEvent::Kind::RenderFault, 0,
                         {RenderError{RenderErrorCode::InternalFailure, E_FAIL,
                                      text.substr(0, 512)}});
                }
        auto events = std::move(events_);
        events_.clear();
        for (const auto &event : events)
        {
            try
            {
                if (sink_)
                    sink_(event);
            }
            catch (...)
            {
                OutputDebugStringA("render-core: event observer threw\n");
            }
        }
    }

  private:
    void enqueue(Command command)
    {
        if (command.kind != Command::Kind::Upload)
            std::erase_if(commands_, [&](const Command &c) {
                return c.kind == command.kind && c.ticket == command.ticket;
            });
        commands_.push_back(std::move(command));
    }
    void emit(RendererEvent::Kind kind, UploadTicket ticket = 0,
              std::optional<RenderError> error = {})
    {
        events_.push_back({kind, ticket, generation_, 0, 0, std::move(error)});
    }
    void initialize()
    {
        bool diagnostics = false;
#ifdef GS_RENDER_TEST_HOOKS
        diagnostics = control_ != nullptr;
#endif
        gpu_ = std::make_unique<GpuDevice>(diagnostics, adapter_luid_);
        adapter_luid_ = gpu_->adapter_description.AdapterLuid;
        pass_ = std::make_unique<SplatPass>(*gpu_, quality_);
        pass_->sort().validate();
        {
            std::lock_guard lock(mutex_);
            stats_.sort_shader_mode = pass_->sort().mode();
            stats_.sort_self_test_passed = true;
            stats_.wave32_fallback_hr = pass_->sort().wave32_fallback_hr();
        }
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = 3;
        check(gpu_->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv_)), "RTV heap");
        rtv_stride_ =
            gpu_->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_QUERY_HEAP_DESC query{};
        query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query.Count = 9;
        check(gpu_->device->CreateQueryHeap(&query, IID_PPV_ARGS(&queries_)), "Timestamp heap");
        for (auto &slot : frames_)
        {
            check(gpu_->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&slot.allocator)),
                  "Frame allocator");
            check(gpu_->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  slot.allocator.Get(), nullptr,
                                                  IID_PPV_ARGS(&slot.list)),
                  "Frame list");
            check(slot.list->Close(), "Frame close");
            slot.readback = gpu_->buffer(sizeof(FrameReadback), D3D12_HEAP_TYPE_READBACK,
                                         D3D12_RESOURCE_STATE_COPY_DEST);
            void *mapped = nullptr;
            D3D12_RANGE empty{};
            check(slot.readback->Map(0, &empty, &mapped), "Stats initialization");
            memset(mapped, 0, sizeof(FrameReadback));
            slot.readback->Unmap(0, &empty);
        }
    }
    void publish_device()
    {
        std::lock_guard lock(mutex_);
        surface_queue_ = gpu_->direct;
        surface_device_ = gpu_->device;
        surface_adapter_ = gpu_->adapter;
        surface_uma_ = gpu_->uma;
    }
    void rebuild_targets()
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        check(swapchain_->GetDesc1(&desc), "Swapchain description");
        for (UINT i = 0; i < desc.BufferCount; ++i)
        {
            check(swapchain_->GetBuffer(i, IID_PPV_ARGS(&targets_[i])), "Back buffer");
            targets_[i]->SetName(L"Swapchain target");
            auto handle = rtv_->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += i * rtv_stride_;
            gpu_->device->CreateRenderTargetView(targets_[i].Get(), nullptr, handle);
        }
    }
    void drain_frames()
    {
        gpu_->wait(gpu_->direct_fence.Get(), gpu_->direct_value);
        collect_stats();
        for (auto &slot : frames_)
            slot.scene.reset();
    }
    void abandon_pending()
    {
        if (!pending_)
            return;
        if (pending_->fence && gpu_->copy_fence->GetCompletedValue() < pending_->fence)
            abandoned_.push_back(std::move(pending_));
        else
            pending_.reset();
    }
    void process_commands()
    {
        std::deque<Command> commands;
        {
            std::lock_guard lock(mutex_);
            commands.swap(commands_);
        }
        for (auto &c : commands)
        {
            switch (c.kind)
            {
            case Command::Kind::Upload:
                if (pending_)
                {
                    emit(RendererEvent::Kind::SceneFailed, pending_->scene->ticket,
                         failure(RenderErrorCode::Cancelled));
                    abandon_pending();
                }
                begin_upload(c.scene, c.ticket, c.camera);
                break;
            case Command::Kind::Cancel:
                if (pending_ && pending_->scene->ticket == c.ticket)
                {
                    emit(RendererEvent::Kind::SceneFailed, c.ticket,
                         failure(RenderErrorCode::Cancelled));
                    abandon_pending();
                }
                break;
            case Command::Kind::Clear: {
                if (pending_)
                {
                    emit(RendererEvent::Kind::SceneFailed, pending_->scene->ticket,
                         failure(RenderErrorCode::Cancelled));
                    abandon_pending();
                }
                const auto ticket = active_ ? active_->ticket : 0;
                drain_frames();
                active_.reset();
                {
                    std::lock_guard lock(mutex_);
                    active_ticket_ = 0;
                    active_cpu_.reset();
                }
                emit(RendererEvent::Kind::SceneCleared, ticket);
                break;
            }
            case Command::Kind::Camera:
                if (active_ && active_->ticket == c.ticket)
                {
                    active_->camera = c.camera;
                    active_->sorted = false;
                }
                break;
            case Command::Kind::Attach:
                if (c.generation == generation_)
                {
                    drain_frames();
                    targets_ = {};
                    swapchain_ = std::move(c.swapchain);
                    DXGI_SWAP_CHAIN_DESC1 d{};
                    check(swapchain_->GetDesc1(&d), "Attach description");
                    if (applied_revision_ == 0)
                        viewport_ = {d.Width, d.Height};
                    else if (viewport_.physical_width && viewport_.physical_height &&
                             (d.Width != viewport_.physical_width ||
                              d.Height != viewport_.physical_height))
                        check(swapchain_->ResizeBuffers(d.BufferCount, viewport_.physical_width,
                                                        viewport_.physical_height, d.Format,
                                                        d.Flags),
                              "Attach resize");
                    rebuild_targets();
                    surface_lost_ = false;
                    if (active_)
                        active_->sorted = false;
                    if (pending_)
                        pending_->scene->sorted = false;
                }
                break;
            case Command::Kind::Detach:
                if (c.generation == generation_)
                {
                    drain_frames();
                    targets_ = {};
                    swapchain_.Reset();
                    emit(RendererEvent::Kind::SurfaceDetached);
                }
                break;
            case Command::Kind::Resize:
                if (c.generation != generation_ || c.revision <= applied_revision_)
                    break;
                applied_revision_ = c.revision;
                if (viewport_.physical_width == c.viewport.physical_width &&
                    viewport_.physical_height == c.viewport.physical_height)
                    break;
                viewport_ = c.viewport;
                if (active_)
                    active_->sorted = false;
                if (pending_)
                    pending_->scene->sorted = false;
                if (swapchain_ && viewport_.physical_width && viewport_.physical_height)
                {
                    drain_frames();
                    targets_ = {};
                    DXGI_SWAP_CHAIN_DESC1 d{};
                    swapchain_->GetDesc1(&d);
                    check(swapchain_->ResizeBuffers(d.BufferCount, viewport_.physical_width,
                                                    viewport_.physical_height, d.Format, d.Flags),
                          "Resize buffers");
                    rebuild_targets();
                }
                break;
            }
        }
    }
    void begin_upload(SceneHandle scene, UploadTicket ticket, CameraState camera)
    {
        try
        {
#ifdef GS_RENDER_TEST_HOOKS
            if (control_ && control_->fail_allocation.exchange(false))
                throw GpuFailure{E_OUTOFMEMORY, "Injected resource allocation"};
#endif
            DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
            check(gpu_->adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local),
                  "Local budget");
            const auto nonlocal_hr = gpu_->adapter->QueryVideoMemoryInfo(
                0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
            if (!gpu_->uma)
                check(nonlocal_hr, "Nonlocal budget");
            const auto required = incremental_bytes(*scene);
            if (!fits_scene_budgets(required, upload_reserve_bytes, local.Budget, local.CurrentUsage,
                                    nonlocal.Budget, nonlocal.CurrentUsage, gpu_->uma))
            {
                const auto diagnostic = budget_diagnostic("upload_begin", required, local,
                                                           nonlocal, gpu_->uma, nonlocal_hr);
                emit(RendererEvent::Kind::SceneFailed, ticket,
                     failure(RenderErrorCode::OutOfVideoMemory, S_OK, diagnostic.c_str()));
                return;
            }
            pending_ = std::make_unique<UploadTransaction>(*gpu_, *pass_, std::move(scene), ticket,
                                                           camera);
        }
        catch (const GpuFailure &e)
        {
            if (e.hr == DXGI_ERROR_DEVICE_REMOVED || e.hr == DXGI_ERROR_DEVICE_RESET)
                throw;
            emit(RendererEvent::Kind::SceneFailed, ticket,
                 failure(e.hr == E_OUTOFMEMORY ? RenderErrorCode::OutOfVideoMemory
                                               : RenderErrorCode::UploadFailed,
                         e.hr, e.operation));
        }
        catch (const std::bad_alloc &)
        {
            emit(RendererEvent::Kind::SceneFailed, ticket,
                 failure(RenderErrorCode::ResourceLimit, E_OUTOFMEMORY, "Upload host allocation"));
        }
    }
    void pump_upload()
    {
        const auto copy_completed = gpu_->copy_fence->GetCompletedValue();
        if (copy_completed == UINT64_MAX)
            throw GpuFailure{DXGI_ERROR_DEVICE_REMOVED, "Copy fence"};
        std::erase_if(abandoned_, [&](const auto &p) { return p->copy_complete(copy_completed); });
        if (!pending_ || pending_->ready)
            return;
        auto &p = *pending_;
        if (auto progress = p.advance(*gpu_))
        {
            events_.push_back({RendererEvent::Kind::UploadProgress,
                               p.scene->ticket,
                               generation_,
                               progress->completed,
                               progress->total,
                               {}});
            {
                std::lock_guard lock(mutex_);
                stats_.completed_upload_bytes = progress->completed;
            }
        }
#ifdef GS_RENDER_TEST_HOOKS
        if (p.fence && control_ && control_->copy_fence_timeout.exchange(false))
        {
            ++p.fence;
            p.deadline = Clock::now();
        }
#endif
    }
    void collect_stats()
    {
        auto completed = gpu_->direct_fence->GetCompletedValue();
        if (completed == UINT64_MAX)
            throw GpuFailure{DXGI_ERROR_DEVICE_REMOVED, "Direct fence"};
        for (auto &slot : frames_)
            if (slot.fence && completed >= slot.fence && slot.frame)
            {
                void *mapped = nullptr;
                D3D12_RANGE range{0, sizeof(FrameReadback)};
                check(slot.readback->Map(0, &range, &mapped), "Stats readback");
                FrameReadback result{};
                memcpy(&result, mapped, sizeof(result));
                const auto &ticks = result.ticks;
                uint32_t count = result.draw.instance_count;
                uint32_t rejected = result.draw.rejected;
                if (!slot.scene)
                {
                    count = 0;
                    rejected = 0;
                }
                D3D12_RANGE empty{};
                slot.readback->Unmap(0, &empty);
                std::lock_guard lock(mutex_);
                if (slot.frame >= stats_.presented_frame_id)
                {
                    stats_.presented_frame_id = slot.frame;
                    stats_.cpu_frame_ms = slot.cpu_ms;
                    stats_.present_call_ms = slot.present_ms;
                    stats_.gpu_frame_ms =
                        1000.0 * (ticks[2] - ticks[0]) / gpu_->timestamp_frequency;
                    stats_.gpu_sort_ms = 1000.0 * (ticks[1] - ticks[0]) / gpu_->timestamp_frequency;
                    stats_.gpu_draw_ms = 1000.0 * (ticks[2] - ticks[1]) / gpu_->timestamp_frequency;
                    stats_.candidate_splats = stats_.drawn_splats = count;
                    stats_.rejected_projection_splats = rejected;
                }
                slot.frame = 0;
            }
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
        gpu_->adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        gpu_->adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
        std::lock_guard lock(mutex_);
        stats_.local_budget_bytes = local.Budget;
        stats_.local_usage_bytes = local.CurrentUsage;
        stats_.nonlocal_budget_bytes = nonlocal.Budget;
        stats_.nonlocal_usage_bytes = nonlocal.CurrentUsage;
    }
    void draw_frame()
    {
        const auto begin = Clock::now();
        const UINT slotIndex = UINT(frame_number_ % 3);
        auto &slot = frames_[slotIndex];
        gpu_->wait(gpu_->direct_fence.Get(), slot.fence);
        collect_stats();
        slot.scene.reset();
        check(slot.allocator->Reset(), "Frame allocator reset");
        check(slot.list->Reset(slot.allocator.Get(), nullptr), "Frame list reset");
        auto *list = slot.list.Get();
        auto candidate = (pending_ && pending_->ready) ? pending_->scene : active_;
        list->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 3);
        if (candidate && !candidate->sorted)
        {
            transition(list, candidate->attributes.Get(), D3D12_RESOURCE_STATE_COMMON,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            pass_->project_sort(list, *candidate, viewport_);
            {
                std::lock_guard lock(mutex_);
                ++stats_.sort_pass_count;
            }
            transition(list, candidate->attributes.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        }
        else if (candidate)
        {
            std::lock_guard lock(mutex_);
            ++stats_.sort_reuse_count;
        }
        list->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 3 + 1);
        UINT buffer = swapchain_->GetCurrentBackBufferIndex();
        auto *target = targets_[buffer].Get();
        auto handle = rtv_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += buffer * rtv_stride_;
        transition(list, target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clear[4]{};
        list->ClearRenderTargetView(handle, clear, 0, nullptr);
        if (candidate)
            pass_->draw(list, *candidate, viewport_, handle);
        list->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 3 + 2);
        list->ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slotIndex * 3, 3,
                               slot.readback.Get(), 0);
        if (candidate)
        {
            transition(list, candidate->arguments.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyBufferRegion(slot.readback.Get(), offsetof(FrameReadback, draw),
                                   candidate->arguments.Get(), 0, sizeof(DrawCounters));
            transition(list, candidate->arguments.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        transition(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        check(list->Close(), "Frame close");
        slot.fence =
            gpu_->submit(gpu_->direct.Get(), list, gpu_->direct_fence.Get(), gpu_->direct_value);
        slot.scene = candidate;
        const auto beforePresent = Clock::now();
        const HRESULT present = swapchain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        // DXGI may enqueue work on the direct queue during Present. Cover it before releasing
        // targets.
        check(gpu_->direct->Signal(gpu_->direct_fence.Get(), ++gpu_->direct_value),
              "Post-present fence");
        slot.fence = gpu_->direct_value;
        slot.present_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - beforePresent).count();
        slot.cpu_ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        ++frame_number_;
        if (present == DXGI_ERROR_WAS_STILL_DRAWING || present == DXGI_STATUS_OCCLUDED)
            return;
        check(present, "Present");
        slot.frame = frame_number_;
        if (pending_ && pending_->ready)
        {
            bool cancelled = false;
            {
                std::lock_guard lock(mutex_);
                cancelled = std::any_of(commands_.begin(), commands_.end(), [&](const Command &c) {
                    return c.kind == Command::Kind::Clear ||
                           (c.kind == Command::Kind::Cancel && c.ticket == pending_->scene->ticket);
                });
                if (!cancelled)
                {
                    active_ticket_ = pending_->scene->ticket;
                    active_cpu_ = pending_->scene->cpu;
                    last_camera_ticket_ = active_ticket_;
                    last_camera_ = pending_->scene->camera;
                }
            }
            if (cancelled)
            {
                emit(RendererEvent::Kind::SceneFailed, pending_->scene->ticket,
                     failure(RenderErrorCode::Cancelled));
                abandon_pending();
                return;
            }
            active_ = pending_->scene;
            pending_.reset();
            emit(RendererEvent::Kind::SceneReady, active_->ticket);
            if (recovering_)
            {
                recovering_ = false;
                emit(RendererEvent::Kind::DeviceRestored, active_->ticket);
            }
        }
        else if (recovering_ && !active_)
        {
            recovering_ = false;
            emit(RendererEvent::Kind::DeviceRestored);
        }
    }
    void recover(const GpuFailure &e)
    {
        emit(
            RendererEvent::Kind::DeviceLost, 0,
            failure(e.code == RenderErrorCode::GpuTimeout ? e.code : RenderErrorCode::DeviceRemoved,
                    e.hr, e.operation));
        recovery_scene_ = active_ ? active_->cpu : SceneHandle{};
        recovery_ticket_ = active_ ? active_->ticket : 0;
        recovery_camera_ = active_ ? active_->camera : CameraState{};
        if (gpu_)
        {
            ComPtr<ID3D12DeviceRemovedExtendedData> dred;
            if (SUCCEEDED(gpu_->device.As(&dred)))
            {
                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
                D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
                dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
                dred->GetPageFaultAllocationOutput(&pageFault);
                auto &diagnostic = events_.back().error;
                if (diagnostic)
                    diagnostic->diagnostic +=
                        " DRED breadcrumbs=" +
                        std::to_string(breadcrumbs.pHeadAutoBreadcrumbNode != nullptr) +
                        " fault=" + std::to_string(pageFault.PageFaultVA);
            }
        }
        if (pending_)
            emit(RendererEvent::Kind::SceneFailed, pending_->scene->ticket,
                 failure(RenderErrorCode::DeviceRemoved, e.hr));
        // Removed devices cannot complete old fences. Drop every old GPU reference without waiting.
        {
            std::lock_guard lock(mutex_);
            recovering_ = true;
            surface_queue_.Reset();
            surface_device_.Reset();
            surface_adapter_.Reset();
            for (const auto &command : commands_)
            {
                if (command.kind == Command::Kind::Upload)
                    emit(RendererEvent::Kind::SceneFailed, command.ticket,
                         failure(RenderErrorCode::DeviceRemoved, e.hr));
                if (command.kind == Command::Kind::Clear)
                {
                    emit(RendererEvent::Kind::SceneCleared, recovery_ticket_);
                    recovery_scene_.reset();
                    active_cpu_.reset();
                    active_ticket_ = recovery_ticket_ = 0;
                }
            }
            // Queued Attach commands also own the removed device through their swapchains.
            commands_.clear();
        }
        if (gpu_ && SUCCEEDED(gpu_->device->GetDeviceRemovedReason()))
        {
            ComPtr<ID3D12Device5> device;
            if (SUCCEEDED(gpu_->device.As(&device)))
                device->RemoveDevice();
        }
        pending_.reset();
        abandoned_.clear();
        active_.reset();
        targets_ = {};
        swapchain_.Reset();
        frames_ = {};
        queries_.Reset();
        rtv_.Reset();
        pass_.reset();
        gpu_.reset();
        // Deliver DeviceLost before recreating the D3D12 singleton on this adapter.
        // The host must release its old queue/device/swapchain references at that event.
    }
    void rebuild_device()
    {
        bool restored = false;
        std::optional<RenderError> rebuild_error;
        for (int attempt = 0; attempt < 2 && recovery_attempts_ < 2;
             ++attempt, ++recovery_attempts_)
        {
            try
            {
                initialize();
                restored = true;
                break;
            }
            catch (const GpuFailure &e)
            {
                rebuild_error = failure(e.code, e.hr, e.operation);
                gpu_.reset();
                pass_.reset();
            }
            catch (const std::bad_alloc &)
            {
                rebuild_error = failure(RenderErrorCode::ResourceLimit, E_OUTOFMEMORY,
                                        "Device rebuild allocation");
                gpu_.reset();
                pass_.reset();
            }
        }
        if (!restored || ++recovery_cycles_ > 2)
        {
            fatal_ = true;
            emit(
                RendererEvent::Kind::FatalDeviceError, 0,
                rebuild_error.value_or(failure(RenderErrorCode::DeviceRemoved,
                                               DXGI_ERROR_DEVICE_REMOVED, "Recovery cycle limit")));
            return;
        }
        {
            std::lock_guard lock(mutex_);
            ++generation_;
            accepted_revision_ = applied_revision_ = 0;
            stats_.device_recovery_count++;
        }
        publish_device();
        surface_lost_ = false;
        rebind_deadline_ = Clock::now() + std::chrono::seconds(10);
        emit(RendererEvent::Kind::SurfaceRebindRequired);
        if (recovery_scene_)
        {
            begin_upload(recovery_scene_, recovery_ticket_, recovery_camera_);
            recovery_scene_.reset();
            if (!pending_)
            {
                fatal_ = true;
                emit(RendererEvent::Kind::FatalDeviceError, recovery_ticket_,
                     failure(RenderErrorCode::UploadFailed, E_FAIL, "Recovery upload failed"));
            }
        }
    }
    QualityConfig quality_;
    EventSink sink_;
    mutable std::mutex mutex_;
    std::deque<Command> commands_;
    std::unique_ptr<GpuDevice> gpu_;
    std::optional<LUID> adapter_luid_;
    std::unique_ptr<SplatPass> pass_;
    ComPtr<ID3D12CommandQueue> surface_queue_;
    ComPtr<ID3D12Device> surface_device_;
    ComPtr<IDXGIAdapter3> surface_adapter_;
    bool surface_uma_ = false;
    ComPtr<IDXGISwapChain3> swapchain_;
    std::array<ComPtr<ID3D12Resource>, 3> targets_;
    ComPtr<ID3D12DescriptorHeap> rtv_;
    UINT rtv_stride_ = 0;
    ComPtr<ID3D12QueryHeap> queries_;
    std::array<FrameSlot, 3> frames_;
    std::unique_ptr<UploadTransaction> pending_;
    std::vector<std::unique_ptr<UploadTransaction>> abandoned_;
    std::shared_ptr<SceneGpu> active_;
    SceneHandle active_cpu_;
    SceneHandle recovery_scene_;
    UploadTicket recovery_ticket_ = 0;
    CameraState recovery_camera_;
    UploadTicket next_ticket_ = 0, active_ticket_ = 0;
    UploadTicket last_camera_ticket_ = 0;
    CameraState last_camera_;
    SurfaceGeneration generation_ = 1;
    ViewportRevision accepted_revision_ = 0, applied_revision_ = 0;
    Viewport viewport_{};
    RenderStats stats_;
    std::vector<RendererEvent> events_;
    std::thread::id render_thread_;
    uint64_t frame_number_ = 0;
    std::atomic<bool> fatal_{false}, recovering_{false};
    uint32_t recovery_attempts_ = 0, recovery_cycles_ = 0;
    bool surface_lost_ = false;
    Clock::time_point rebind_deadline_{};
#ifdef GS_RENDER_TEST_HOOKS
    std::shared_ptr<RendererTestControl> control_;
#endif
    std::vector<std::string> reported_debug_;
};
}
#ifdef GS_RENDER_TEST_HOOKS
using namespace testing_build;
#else
using namespace production_build;
#endif
} // namespace
#ifndef GS_RENDER_TEST_HOOKS
std::variant<std::unique_ptr<IRenderer>, RenderError> create_renderer(QualityConfig quality,
                                                                      EventSink sink)
{
    if (auto e = detail::validate_quality(quality))
        return *e;
    try
    {
        return std::unique_ptr<IRenderer>(std::make_unique<Renderer>(quality, std::move(sink)));
    }
    catch (const detail::GpuFailure &e)
    {
        return failure(e.hr == DXGI_ERROR_UNSUPPORTED ? RenderErrorCode::UnsupportedDevice
                       : e.hr == E_OUTOFMEMORY        ? RenderErrorCode::OutOfVideoMemory
                                                      : e.code,
                       e.hr, e.operation);
    }
    catch (const std::bad_alloc &)
    {
        return failure(RenderErrorCode::ResourceLimit, E_OUTOFMEMORY);
    }
}
#endif
} // namespace gs::render

#ifdef GS_RENDER_TEST_HOOKS
namespace gs::render::detail
{
std::variant<std::unique_ptr<IRenderer>, RenderError> create_renderer_for_testing(
    QualityConfig q, EventSink sink, std::shared_ptr<RendererTestControl> control)
{
    if (auto e = validate_quality(q))
        return *e;
    if (!control)
        control = std::make_shared<RendererTestControl>();
    try
    {
        return std::unique_ptr<IRenderer>(
            std::make_unique<Renderer>(q, std::move(sink), std::move(control)));
    }
    catch (const GpuFailure &e)
    {
        return RenderError{RenderErrorCode::UnsupportedDevice, e.hr, e.operation};
    }
}
std::vector<std::string> renderer_debug_errors(IRenderer &renderer)
{
    auto *native = dynamic_cast<Renderer *>(&renderer);
    return native ? native->debug_errors() : std::vector<std::string>{"Wrong renderer"};
}
} // namespace gs::render::detail
#endif
