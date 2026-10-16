#include "native3dgs/engine.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace gs::engine
{
namespace
{
using namespace render;
using Clock = std::chrono::steady_clock;
RenderError error(RenderErrorCode code, const char *message)
{
    return {code, S_OK, message};
}
class Engine final : public IEngine
{
  public:
    explicit Engine(EngineConfig config) : config_(config), measurement_viewport_(config.viewport)
    {
        render_thread_ = std::thread([this] { render_loop(); });
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this] { return initialized_; });
        if (!init_error_)
        {
            try
            {
                load_thread_ = std::thread([this] { load_loop(); });
                load_started_ = true;
                metric_thread_ = std::thread([this] { metric_loop(); });
            }
            catch (...)
            {
                stopping_ = true;
                cv_.notify_all();
                lock.unlock();
                if (load_thread_.joinable())
                    load_thread_.join();
                render_thread_.join();
                throw;
            }
        }
    }
    ~Engine() override
    {
        request_shutdown();
        if (load_thread_.joinable())
            load_thread_.join();
        if (metric_thread_.joinable())
            metric_thread_.join();
        if (render_thread_.joinable())
            render_thread_.join();
    }
    std::optional<RenderError> init_error() const
    {
        return init_error_;
    }
    std::variant<RequestId, RenderError> open(io::LoadRequest request) override
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || fatal_ || awaiting_release_)
            return error(RenderErrorCode::DeviceRemoved, "Engine unavailable");
        supersede();
        invalidate_pick();
        invalidate_import();
        const auto id = ++next_request_;
        state_.current_request = id;
        queued_ = Job{id, std::move(request), std::stop_source{}};
        state_.phase = Phase::Loading;
        state_.load_progress.reset();
        state_.upload_done = state_.upload_total = 0;
        state_.error.reset();
        cv_.notify_all();
        return id;
    }
    void cancel(RequestId id) override
    {
        std::lock_guard lock(mutex_);
        if (id != state_.current_request || stopping_ ||
            (state_.phase != Phase::Loading && state_.phase != Phase::Uploading))
            return;
        supersede();
        state_.current_request = 0;
        state_.phase = state_.active_request ? Phase::Ready : Phase::Empty;
        state_.load_progress.reset();
        state_.upload_done = state_.upload_total = 0;
        state_.error.reset();
        push({EngineEvent::Kind::SceneFailed, id, state_.surface_generation,
              EngineError{error(RenderErrorCode::Cancelled, "Request cancelled")}});
    }
    void close() override
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || fatal_)
            return;
        supersede();
        state_.current_request = 0;
        state_.phase = Phase::Closing;
        reset_measurement();
        state_.load_progress.reset();
        state_.upload_done = state_.upload_total = 0;
        state_.error.reset();
        renderer_->clear_scene();
    }
    std::optional<RenderError> camera_command(CameraCommand command) override
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !active_scene_ || awaiting_release_ || fatal_ ||
            state_.phase == Phase::Closing)
            return error(RenderErrorCode::InvalidScene, "No controllable active scene");
        auto candidate = camera_;
        std::optional<RenderError> e;
        switch (command.action)
        {
        case CameraAction::Orbit:
            e = candidate.orbit(command.x, command.y);
            break;
        case CameraAction::Pan:
            e = candidate.pan(command.x, command.y);
            break;
        case CameraAction::Dolly:
            e = candidate.dolly(command.x);
            break;
        case CameraAction::Look:
            e = candidate.look(command.x, command.y);
            break;
        case CameraAction::Fly:
            e = candidate.fly({command.x, command.y, command.z, command.fast}, command.seconds);
            break;
        case CameraAction::Fit:
            e = candidate.fit_scene(*active_scene_, config_.quality, config_.viewport);
            break;
        case CameraAction::Reset:
            candidate.reset();
            break;
        case CameraAction::OrbitMode:
            e = candidate.set_mode(ViewMode::Orbit);
            break;
        case CameraAction::FlyMode:
            e = candidate.set_mode(ViewMode::Fly);
            break;
        case CameraAction::FlipX:
        case CameraAction::FlipY:
        case CameraAction::FlipZ:
        {
            const uint8_t bit = command.action == CameraAction::FlipX ?
                                    static_cast<uint8_t>(FlipAxis::X) :
                                command.action == CameraAction::FlipY ?
                                    static_cast<uint8_t>(FlipAxis::Y) :
                                    static_cast<uint8_t>(FlipAxis::Z);
            candidate.set_flip_axes(command.flip_enabled ? candidate.flip_axes() | bit :
                                                         candidate.flip_axes() & ~bit);
            break;
        }
        default:
            return error(RenderErrorCode::InvalidCamera, "Unknown camera command");
        }
        if (!e)
            e = renderer_->set_camera(state_.active_ticket, candidate.camera());
        if (!e)
        {
            invalidate_pick();
            camera_ = candidate;
            if (command.action == CameraAction::FlipX ||
                command.action == CameraAction::FlipY ||
                command.action == CameraAction::FlipZ)
            {
                flip_axes_ = candidate.flip_axes();
                state_.flip_axes = flip_axes_;
            }
            state_.camera = candidate.camera();
        }
        return e;
    }
    ID3D12CommandQueue *addref_surface_queue(SurfaceGeneration generation) override
    {
        std::lock_guard lock(mutex_);
        return renderer_ && !stopping_ && !awaiting_release_ && !fatal_
                   ? renderer_->addref_surface_queue(generation)
                   : nullptr;
    }
    std::optional<RenderError> attach_swapchain(SurfaceGeneration generation,
                                                IDXGISwapChain3 *surface) override
    {
        std::lock_guard lock(mutex_);
        if (!renderer_ || stopping_ || awaiting_release_ || fatal_)
            return error(RenderErrorCode::InvalidSurface, "Engine unavailable");
        return renderer_->attach_swapchain(generation, surface);
    }
    void detach_swapchain(SurfaceGeneration generation) override
    {
        std::lock_guard lock(mutex_);
        if (renderer_ && !stopping_ && !awaiting_release_ && !fatal_)
            renderer_->detach_swapchain(generation);
    }
    std::optional<RenderError> resize(SurfaceGeneration generation, Viewport viewport) override
    {
        std::lock_guard lock(mutex_);
        if (!renderer_ || stopping_ || awaiting_release_ || fatal_)
            return error(RenderErrorCode::InvalidSurface, "Engine unavailable");
        auto e = renderer_->resize(generation, ++revision_, viewport);
        if (!e)
        {
            measurement_viewport_ = viewport;
            invalidate_pick();
            // A hidden 0x0 viewport pauses presentation without corrupting camera state.
            if (viewport.physical_width && viewport.physical_height)
            {
                config_.viewport = viewport;
                camera_.resize(viewport);
                if (pending_)
                    pending_->camera.resize(viewport);
            }
        }
        return e;
    }
    bool acknowledge_device_release(SurfaceGeneration generation) override
    {
        std::lock_guard lock(mutex_);
        if (awaiting_release_ && generation == state_.surface_generation)
        {
            awaiting_release_ = false;
            cv_.notify_all();
            return true;
        }
        return false;
    }
    Snapshot snapshot() const override
    {
        std::lock_guard lock(mutex_);
        return state_;
    }
    std::optional<MeasurementError> set_measurement_enabled(bool enabled) override
    {
        std::lock_guard lock(mutex_);
        if (enabled && (!active_scene_ || stopping_ || fatal_ || state_.phase != Phase::Ready))
            return MeasurementError::NoScene;
        if (state_.measurement.enabled == enabled)
            return {};
        invalidate_pick();
        if (!enabled)
            invalidate_import();
        state_.measurement.enabled = enabled;
        clear_points();
        return {};
    }
    std::optional<MeasurementError> import_metric(std::filesystem::path path) override
    {
        std::lock_guard lock(mutex_);
        if (!measurement_ready())
            return MeasurementError::NoScene;
        invalidate_pick();
        invalidate_import();
        state_.measurement.importing = true;
        state_.measurement.error.reset();
        metric_stop_ = std::stop_source{};
        metric_job_ = MetricJob{++import_id_, state_.active_request, std::move(path), metric_stop_};
        cv_.notify_all();
        return {};
    }
    void clear_measurement() override
    {
        std::lock_guard lock(mutex_);
        invalidate_pick();
        invalidate_import();
        clear_points();
    }
    std::optional<MeasurementError> measure_at(double x, double y) override
    {
        std::lock_guard lock(mutex_);
        auto &measurement = state_.measurement;
        if (!measurement_ready())
            return MeasurementError::NoScene;
        if (!measurement.calibration)
            return MeasurementError::NoCalibration;
        if (measurement.importing || measurement.picking || measurement.b)
            return MeasurementError::Busy;
        const auto operation = ++next_measurement_operation_;
        if (auto e =
                renderer_->request_pick({operation, state_.active_ticket, state_.surface_generation,
                                         revision_, state_.camera, measurement_viewport_, x, y}))
        {
            const auto code = e->code == RenderErrorCode::ResourceLimit ? MeasurementError::Busy
                              : e->code == RenderErrorCode::DeviceRemoved
                                  ? MeasurementError::DeviceUnavailable
                                  : MeasurementError::StaleRequest;
            measurement.error = code;
            return code;
        }
        measurement.operation = operation;
        measurement.picking = true;
        measurement.missed = false;
        measurement.error.reset();
        pick_deadline_ = Clock::now() + std::chrono::seconds(5);
        return {};
    }
    std::vector<EngineEvent> poll_events() override
    {
        std::lock_guard lock(mutex_);
        std::vector<EngineEvent> events(events_.begin(), events_.end());
        events_.clear();
        return events;
    }
    void request_shutdown() override
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return;
        stopping_ = true;
        reset_measurement();
        supersede();
        state_.phase = Phase::Stopping;
        cv_.notify_all();
    }
    bool wait_until_stopped(std::chrono::milliseconds timeout) override
    {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return state_.phase == Phase::Stopped; });
    }

  private:
    struct Job
    {
        RequestId id;
        io::LoadRequest request;
        std::stop_source stop;
    };
    struct Pending
    {
        RequestId id;
        SceneHandle scene;
        CameraController camera;
        UploadTicket ticket;
        io::Coordinates coordinates = io::Coordinates::Rdf;
    };
    struct MetricJob
    {
        uint64_t id;
        RequestId scene;
        std::filesystem::path path;
        std::stop_source stop;
    };
    bool measurement_ready() const
    {
        return state_.measurement.enabled && active_scene_ && state_.phase == Phase::Ready &&
               !stopping_ && !fatal_ && !awaiting_release_;
    }
    void clear_points()
    {
        auto &m = state_.measurement;
        m.a.reset();
        m.b.reset();
        m.distance_m.reset();
        m.error.reset();
        m.missed = false;
    }
    void invalidate_pick()
    {
        auto &m = state_.measurement;
        if (m.picking)
            m.error = MeasurementError::StaleRequest;
        m.picking = false;
        m.operation = ++next_measurement_operation_;
    }
    void invalidate_import()
    {
        ++import_id_;
        metric_job_.reset();
        metric_stop_.request_stop();
        state_.measurement.importing = false;
    }
    void reset_measurement()
    {
        invalidate_import();
        invalidate_pick();
        state_.measurement = {};
    }
    void metric_loop() noexcept
    {
        for (;;)
        {
            std::optional<MetricJob> job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || metric_job_.has_value(); });
                if (stopping_)
                    return;
                job = std::move(metric_job_);
                metric_job_.reset();
            }
            auto result = load_metric(job->path, job->stop.get_token());
            std::lock_guard lock(mutex_);
            if (stopping_ || job->id != import_id_ || job->scene != state_.active_request ||
                job->stop.stop_requested())
                continue;
            auto &m = state_.measurement;
            m.importing = false;
            if (auto calibration = std::get_if<MetricCalibration>(&result))
            {
                m.calibration = *calibration;
                clear_points();
            }
            else
                m.error = std::get<MeasurementError>(result);
        }
    }
    void collect_measurement()
    {
        auto result = renderer_->take_pick_result();
        auto &m = state_.measurement;
        if (m.picking && Clock::now() >= pick_deadline_)
        {
            invalidate_pick();
            m.error = MeasurementError::Timeout;
        }
        if (!result || !m.picking || result->id != m.operation || !measurement_ready())
            return;
        m.picking = false;
        if (result->status == PickStatus::Miss)
        {
            m.missed = true;
            return;
        }
        if (result->status != PickStatus::Hit)
        {
            m.error = result->status == PickStatus::Overflow ? MeasurementError::ResourceLimit
                      : result->status == PickStatus::Stale  ? MeasurementError::StaleRequest
                                                             : MeasurementError::DeviceUnavailable;
            return;
        }
        MeasurementPoint point{result->position_rub,
                               source_from_rub(result->position_rub, active_coordinates_)};
        if (!m.a)
            m.a = point;
        else if (!m.b)
        {
            auto distance = distance_meters(m.a->position_source, point.position_source,
                                            m.calibration->meters_per_model_unit);
            if (distance)
            {
                m.b = point;
                m.distance_m = distance;
            }
            else
                m.error = MeasurementError::InvalidTransform;
        }
    }
    void supersede()
    {
        queued_.reset();
        decoded_.reset();
        loading_stop_.request_stop();
        if (pending_)
            renderer_->cancel_upload(pending_->ticket);
    }
    void push(EngineEvent e)
    {
        if (events_.size() == config_.event_capacity)
        {
            events_.pop_front();
            ++state_.dropped_events;
        }
        events_.push_back(std::move(e));
    }
    void fail(RequestId id, EngineError e)
    {
        if (id != state_.current_request)
            return;
        state_.error = e;
        state_.phase = state_.active_request ? Phase::Ready : Phase::Failed;
        push({EngineEvent::Kind::SceneFailed, id, state_.surface_generation, std::move(e)});
    }
    void load_loop() noexcept
    {
        try
        {
            auto loader = io::make_model_loader();
            for (;;)
            {
                Job job;
                {
                    std::unique_lock lock(mutex_);
                    cv_.wait(lock, [this] { return stopping_ || queued_.has_value(); });
                    if (stopping_)
                        break;
                    job = std::move(*queued_);
                    queued_.reset();
                    loading_stop_ = job.stop;
                }
                auto result = loader->load(job.request, job.stop.get_token(),
                                           [this, id = job.id](const io::LoadProgress &p) {
                                               std::lock_guard lock(mutex_);
                                               if (id == state_.current_request && !stopping_)
                                                   state_.load_progress = p;
                                           });
                std::lock_guard lock(mutex_);
                if (job.id != state_.current_request || stopping_ || job.stop.stop_requested())
                    continue;
                if (auto e = std::get_if<io::LoadError>(&result))
                    fail(job.id, *e);
                else
                    decoded_ = Pending{job.id,
                                       std::get<SceneHandle>(std::move(result)),
                                       {},
                                       0,
                                       job.request.plyCoordinates};
                cv_.notify_all();
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard lock(mutex_);
            fail(state_.current_request, error(RenderErrorCode::InternalFailure, e.what()));
            fatal_ = true;
        }
        std::lock_guard lock(mutex_);
        load_stopped_ = true;
        cv_.notify_all();
    }
    void on_event(const RendererEvent &e)
    {
        std::lock_guard lock(mutex_);
        const auto generation = e.surface_generation;
        switch (e.kind)
        {
        case RendererEvent::Kind::UploadProgress:
            if (pending_ && pending_->id == state_.current_request && e.ticket == pending_->ticket)
            {
                state_.upload_done = e.bytes_done;
                state_.upload_total = e.bytes_total;
            }
            break;
        case RendererEvent::Kind::SceneReady:
            if (pending_ && pending_->ticket == e.ticket)
            {
                state_.stats = renderer_->get_stats();
                // Cancellation after core activation is ordered after that successful commit.
                active_scene_ = pending_->scene;
                active_coordinates_ = active_scene_->sourceFormat == SourceFormat::Spz
                                          ? io::Coordinates::Rdf
                                          : pending_->coordinates;
                reset_measurement();
                camera_ = pending_->camera;
                camera_.set_flip_axes(flip_axes_);
                pending_camera_sync_ = true;
                camera_sync_fault_reported_ = false;
                state_.active_request = pending_->id;
                state_.active_ticket = e.ticket;
                state_.camera = camera_.camera();
                state_.active_scene =
                    Snapshot::SceneInfo{active_scene_->count,        active_scene_->shDegree,
                                        active_scene_->sourceFormat, active_scene_->worldOrigin,
                                        active_scene_->bounds,       active_scene_->maxScale};
                if (pending_->id == state_.current_request)
                {
                    state_.phase = Phase::Ready;
                    state_.error.reset();
                }
                push({EngineEvent::Kind::SceneReady, pending_->id, generation, {}});
                pending_.reset();
            }
            break;
        case RendererEvent::Kind::SceneFailed:
            if (pending_ && pending_->ticket == e.ticket)
            {
                if (e.error)
                    fail(pending_->id, *e.error);
                pending_.reset();
            }
            break;
        case RendererEvent::Kind::SceneCleared:
            if (e.ticket == state_.active_ticket ||
                (e.ticket == 0 && state_.phase == Phase::Closing))
            {
                state_.stats = renderer_->get_stats();
                active_scene_.reset();
                reset_measurement();
                camera_ = {};
                state_.active_request = 0;
                state_.active_ticket = 0;
                state_.camera = {};
                state_.active_scene.reset();
                if (state_.phase == Phase::Closing)
                    state_.phase = Phase::Empty;
                push({EngineEvent::Kind::SceneCleared, 0, generation, {}});
            }
            break;
        case RendererEvent::Kind::DeviceLost:
            invalidate_pick();
            invalidate_import();
            state_.stats = renderer_->get_stats();
            supersede();
            state_.current_request = 0;
            state_.phase = Phase::Recovering;
            awaiting_release_ = true;
            release_deadline_ = Clock::now() + std::chrono::seconds(10);
            state_.surface_generation = generation;
            if (e.error)
                state_.error = *e.error;
            push({EngineEvent::Kind::DeviceLost, state_.active_request, generation, state_.error});
            break;
        case RendererEvent::Kind::SurfaceRebindRequired:
            state_.surface_generation = generation;
            push({EngineEvent::Kind::SurfaceRebindRequired, state_.active_request, generation, {}});
            break;
        case RendererEvent::Kind::SurfaceDetached:
            push({EngineEvent::Kind::SurfaceDetached, 0, generation, {}});
            break;
        case RendererEvent::Kind::DeviceRestored:
            state_.phase = state_.active_request ? Phase::Ready : Phase::Empty;
            state_.error.reset();
            push({EngineEvent::Kind::DeviceRestored, state_.active_request, generation, {}});
            break;
        case RendererEvent::Kind::RenderFault:
        case RendererEvent::Kind::FatalDeviceError:
            if (e.kind == RendererEvent::Kind::FatalDeviceError)
            {
                fatal_ = true;
                state_.phase = Phase::Failed;
            }
            if (e.error)
                state_.error = *e.error;
            push({EngineEvent::Kind::Fault, state_.current_request, generation, state_.error});
            break;
        }
    }
    void render_loop() noexcept
    {
        try
        {
            auto result =
                create_renderer(config_.quality, [this](const RendererEvent &e) { on_event(e); });
            {
                std::lock_guard lock(mutex_);
                if (auto e = std::get_if<RenderError>(&result))
                    init_error_ = *e;
                else
                {
                    renderer_ = std::move(std::get<std::unique_ptr<IRenderer>>(result));
                    state_.surface_generation = renderer_->surface_generation();
                    if (auto e = renderer_->resize(state_.surface_generation, ++revision_,
                                                   config_.viewport))
                        init_error_ = *e;
                    state_.stats = renderer_->get_stats();
                }
                initialized_ = true;
                cv_.notify_all();
            }
            if (init_error_)
            {
                std::lock_guard lock(mutex_);
                state_.phase = Phase::Stopped;
                cv_.notify_all();
                return;
            }
            for (;;)
            {
                bool render_enabled = false;
                {
                    std::unique_lock lock(mutex_);
                    if (stopping_)
                        break;
                    if (awaiting_release_)
                    {
                        cv_.wait_until(lock, release_deadline_,
                                       [this] { return stopping_ || !awaiting_release_; });
                        if (stopping_)
                            break;
                        if (awaiting_release_)
                        {
                            fatal_ = true;
                            state_.phase = Phase::Failed;
                            state_.error = error(RenderErrorCode::DeviceRemoved,
                                                 "Host device release acknowledgement timed out");
                            push({EngineEvent::Kind::Fault, 0, state_.surface_generation,
                                  state_.error});
                            awaiting_release_ = false;
                        }
                    }
                    if (decoded_ && !pending_ && !fatal_)
                    {
                        auto next = std::move(*decoded_);
                        decoded_.reset();
                        next.camera.set_flip_axes(flip_axes_);
                        if (auto e = next.camera.fit_scene(*next.scene, config_.quality,
                                                           config_.viewport))
                            fail(next.id, *e);
                        else
                        {
                            auto upload = renderer_->upload_scene(next.scene, next.camera.camera());
                            if (auto e = std::get_if<RenderError>(&upload))
                                fail(next.id, *e);
                            else
                            {
                                next.ticket = std::get<UploadTicket>(upload);
                                pending_ = std::move(next);
                                state_.phase = Phase::Uploading;
                            }
                        }
                    }
                    render_enabled = !fatal_;
                }
                if (render_enabled)
                    renderer_->render_frame();
                std::unique_lock lock(mutex_);
                if (pending_camera_sync_ && state_.active_ticket && !fatal_ &&
                    !awaiting_release_ && state_.phase != Phase::Recovering)
                {
                    if (auto e = renderer_->set_camera(state_.active_ticket, camera_.camera()))
                    {
                        if (!camera_sync_fault_reported_)
                        {
                            state_.error = *e;
                            push({EngineEvent::Kind::Fault, state_.active_request,
                                  state_.surface_generation, state_.error});
                            camera_sync_fault_reported_ = true;
                        }
                    }
                    else
                    {
                        pending_camera_sync_ = false;
                        camera_sync_fault_reported_ = false;
                    }
                }
                state_.stats = renderer_->get_stats();
                collect_measurement();
                cv_.wait_for(lock, std::chrono::milliseconds(1),
                             [this] { return stopping_ || awaiting_release_; });
            }
        }
        catch (const std::exception &e)
        {
            std::lock_guard lock(mutex_);
            auto failure = error(RenderErrorCode::InternalFailure, e.what());
            if (!initialized_)
            {
                init_error_ = failure;
                initialized_ = true;
            }
            else
            {
                state_.error = failure;
                push({EngineEvent::Kind::Fault, 0, state_.surface_generation, state_.error});
            }
            stopping_ = true;
            supersede();
            cv_.notify_all();
        }
        // Move ownership under the lock, then perform fence waits on this render thread.
        std::unique_ptr<IRenderer> renderer;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return load_stopped_ || !load_started_; });
            renderer = std::move(renderer_);
        }
        renderer.reset();
        std::lock_guard lock(mutex_);
        active_scene_.reset();
        pending_.reset();
        decoded_.reset();
        state_.phase = Phase::Stopped;
        state_.active_scene.reset();
        state_.active_request = state_.current_request = state_.active_ticket = 0;
        push({EngineEvent::Kind::Stopped, 0, state_.surface_generation, {}});
        cv_.notify_all();
    }
    EngineConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread render_thread_, load_thread_, metric_thread_;
    std::unique_ptr<IRenderer> renderer_;
    std::optional<RenderError> init_error_;
    Snapshot state_;
    std::deque<EngineEvent> events_;
    std::optional<Job> queued_;
    std::stop_source loading_stop_;
    std::optional<Pending> decoded_, pending_;
    SceneHandle active_scene_;
    CameraController camera_;
    io::Coordinates active_coordinates_ = io::Coordinates::Rdf;
    Viewport measurement_viewport_{};
    std::optional<MetricJob> metric_job_;
    std::stop_source metric_stop_;
    uint64_t import_id_ = 0, next_measurement_operation_ = 0;
    Clock::time_point pick_deadline_{};
    uint8_t flip_axes_ = 0;
    bool pending_camera_sync_ = false;
    bool camera_sync_fault_reported_ = false;
    RequestId next_request_ = 0;
    ViewportRevision revision_ = 0;
    bool initialized_ = false, stopping_ = false, load_started_ = false, load_stopped_ = false,
         awaiting_release_ = false, fatal_ = false;
    Clock::time_point release_deadline_;
};
} // namespace
std::variant<std::unique_ptr<IEngine>, RenderError> create_engine(EngineConfig config)
{
    if (!config.event_capacity || config.event_capacity > 4096 || !config.viewport.physical_width ||
        !config.viewport.physical_height || config.viewport.physical_width > 16384 ||
        config.viewport.physical_height > 16384)
        return error(RenderErrorCode::InvalidSurface, "Invalid engine configuration");
    try
    {
        auto engine = std::make_unique<Engine>(config);
        if (auto e = engine->init_error())
            return *e;
        return std::unique_ptr<IEngine>(std::move(engine));
    }
    catch (const std::exception &e)
    {
        return error(RenderErrorCode::InternalFailure, e.what());
    }
}
} // namespace gs::engine
