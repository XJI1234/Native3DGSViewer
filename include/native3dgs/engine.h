#pragma once
#include "model-io/model_loader.h"
#include "native3dgs/camera.h"
#include "render-core/renderer.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

struct ID3D12CommandQueue;
struct IDXGISwapChain3;

namespace gs::engine
{
using RequestId = uint64_t;
enum class Phase : uint8_t
{
    Empty,
    Loading,
    Uploading,
    Ready,
    Closing,
    Recovering,
    Failed,
    Stopping,
    Stopped
};
using EngineError = std::variant<io::LoadError, render::RenderError>;
struct EngineConfig
{
    render::QualityConfig quality{};
    render::Viewport viewport{128, 128};
    uint32_t event_capacity = 128;
};
struct Snapshot
{
    Phase phase = Phase::Empty;
    RequestId current_request = 0, active_request = 0;
    render::UploadTicket active_ticket = 0;
    render::SurfaceGeneration surface_generation = 0;
    std::optional<io::LoadProgress> load_progress;
    uint64_t upload_done = 0, upload_total = 0, dropped_events = 0;
    render::CameraState camera{};
    uint8_t flip_axes = 0;
    struct SceneInfo
    {
        uint64_t count = 0;
        uint8_t sh_degree = 0;
        SourceFormat source_format{};
        Double3 world_origin{};
        Bounds3 bounds{};
        double max_scale = 0;
    };
    std::optional<SceneInfo> active_scene;
    render::RenderStats stats{};
    std::optional<EngineError> error;
};
struct EngineEvent
{
    enum class Kind : uint8_t
    {
        SceneReady,
        SceneFailed,
        SceneCleared,
        DeviceLost,
        SurfaceRebindRequired,
        SurfaceDetached,
        DeviceRestored,
        Fault,
        Stopped
    } kind;
    RequestId request = 0;
    render::SurfaceGeneration generation = 0;
    std::optional<EngineError> error;
};
enum class CameraAction : uint8_t
{
    Orbit,
    Pan,
    Dolly,
    Look,
    Fly,
    Fit,
    Reset,
    OrbitMode,
    FlyMode,
    FlipX,
    FlipY,
    FlipZ
};
struct CameraCommand
{
    CameraAction action;
    double x = 0, y = 0, z = 0, seconds = 0;
    bool fast = false;
    bool flip_enabled = false;
};
class IEngine
{
  public:
    virtual ~IEngine() = default;
    virtual std::variant<RequestId, render::RenderError> open(io::LoadRequest) = 0;
    virtual void cancel(RequestId) = 0;
    virtual void close() = 0;
    virtual std::optional<render::RenderError> camera_command(CameraCommand) = 0;
    virtual ID3D12CommandQueue *addref_surface_queue(render::SurfaceGeneration) = 0;
    virtual std::optional<render::RenderError> attach_swapchain(render::SurfaceGeneration,
                                                                IDXGISwapChain3 *) = 0;
    virtual void detach_swapchain(render::SurfaceGeneration) = 0;
    virtual std::optional<render::RenderError> resize(render::SurfaceGeneration,
                                                      render::Viewport) = 0;
    virtual bool acknowledge_device_release(render::SurfaceGeneration) = 0;
    virtual Snapshot snapshot() const = 0;
    virtual std::vector<EngineEvent> poll_events() = 0;
    virtual void request_shutdown() = 0;
    virtual bool wait_until_stopped(std::chrono::milliseconds) = 0;
};
std::variant<std::unique_ptr<IEngine>, render::RenderError> create_engine(EngineConfig = {});
} // namespace gs::engine
