#pragma once

#include "splat-types/scene.h"
#include <Windows.h>
#include <functional>
#include <optional>
#include <string>
#include <variant>

struct ID3D12CommandQueue;
struct IDXGISwapChain3;

namespace gs::render
{
struct Quaterniond
{
    double x = 0, y = 0, z = 0, w = 1;
};
struct CameraState
{
    Double3 position_rub{};
    Quaterniond orientation_xyzw{};
    double vertical_fov_radians = 1.0471975511965976;
    double near_plane = 0.01;
    double far_plane = 10000;
};
struct Viewport
{
    uint32_t physical_width = 0, physical_height = 0;
};
enum class SortMode : uint8_t
{
    Radial,
    ViewDepth
};
struct QualityConfig
{
    SortMode sort_mode = SortMode::Radial;
    uint8_t sh_degree_cap = 3;
    float max_stddev = 3;
    float min_alpha = 0;
    float covariance_blur_px2 = 0;
    float max_pixel_radius_px = 1024;
    bool premultiplied_alpha = true;
};
enum class RenderErrorCode : uint8_t
{
    UnsupportedDevice,
    InvalidSurface,
    InvalidCamera,
    InvalidScene,
    InvalidQualityConfig,
    ResourceLimit,
    OutOfVideoMemory,
    UploadFailed,
    ShaderFailure,
    DeviceRemoved,
    SurfaceLost,
    Cancelled,
    InternalFailure,
    GpuTimeout
};
struct RenderError
{
    RenderErrorCode code;
    HRESULT hresult = S_OK;
    std::string diagnostic;
};
using UploadTicket = uint64_t;
using SurfaceGeneration = uint64_t;
using ViewportRevision = uint64_t;
struct RendererEvent
{
    enum class Kind
    {
        UploadProgress,
        SceneReady,
        SceneFailed,
        DeviceLost,
        SurfaceRebindRequired,
        SurfaceDetached,
        DeviceRestored,
        SceneCleared,
        RenderFault,
        FatalDeviceError
    } kind;
    UploadTicket ticket = 0;
    SurfaceGeneration surface_generation = 0;
    uint64_t bytes_done = 0, bytes_total = 0;
    std::optional<RenderError> error;
};
enum class SortShaderMode : uint8_t
{
    Standard,
    FixedWave32,
    WaveAgnostic
};
struct RenderStats
{
    SortShaderMode sort_shader_mode = SortShaderMode::Standard;
    bool sort_self_test_passed = false;
    HRESULT wave32_fallback_hr = S_OK;
    uint64_t presented_frame_id = 0;
    std::optional<double> cpu_frame_ms, gpu_frame_ms, gpu_sort_ms, gpu_draw_ms, present_call_ms;
    uint64_t candidate_splats = 0, drawn_splats = 0, sort_reuse_count = 0,
             completed_upload_bytes = 0;
    uint64_t rejected_projection_splats = 0;
    uint64_t sort_pass_count = 0;
    uint64_t wrong_thread_frame_calls = 0;
    uint64_t local_budget_bytes = 0, local_usage_bytes = 0;
    uint64_t nonlocal_budget_bytes = 0, nonlocal_usage_bytes = 0;
    uint32_t device_recovery_count = 0;
};
class IRenderer
{
  public:
    // Destroy on the render thread after the host has stopped issuing commands.
    virtual ~IRenderer() = default;
    virtual SurfaceGeneration surface_generation() const = 0;
    // The host releases the returned COM reference after creating its swapchain.
    virtual ID3D12CommandQueue *addref_surface_queue(SurfaceGeneration) = 0;
    virtual std::optional<RenderError> attach_swapchain(SurfaceGeneration, IDXGISwapChain3 *) = 0;
    virtual void detach_swapchain(SurfaceGeneration) = 0;
    virtual std::variant<UploadTicket, RenderError> upload_scene(SceneHandle, CameraState) = 0;
    virtual void cancel_upload(UploadTicket) = 0;
    virtual void clear_scene() = 0;
    virtual std::optional<RenderError> set_camera(UploadTicket, CameraState) = 0;
    virtual std::optional<RenderError> resize(SurfaceGeneration, ViewportRevision, Viewport) = 0;
    // One render thread owns this call; event callbacks run here without internal locks.
    // After DeviceLost, release old surface/device/queue references before calling again.
    virtual void render_frame() = 0;
    virtual RenderStats get_stats() const = 0;
};
using EventSink = std::function<void(const RendererEvent &)>;
std::variant<std::unique_ptr<IRenderer>, RenderError> create_renderer(QualityConfig, EventSink);
} // namespace gs::render
