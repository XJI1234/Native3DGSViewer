#include "native3dgs/android_engine.h"

#include "engine/transactions.h"
#include "model-io/fd_loader.h"
#include "render-core/scene_renderer.h"

#include <android/native_window.h>
#include <android/log.h>
#include <vulkan/vulkan.h>
#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace
{
using gs::android::engine::Transactions;
using gs::android::engine::Error;
using gs::android::render::SceneRenderer;
using gs::android::render::FrameStatus;

struct Job
{
    uint64_t request_id = 0;
    int fd = -1;
    bool shared = false;
};

struct Decoded
{
    uint64_t request_id = 0;
    gs::SceneHandle scene;
    Error error = Error::None;
};

struct State : std::enable_shared_from_this<State>
{
    Transactions transactions;
    std::mutex mutex;
    std::condition_variable condition;
    std::optional<Job> queued;
    std::optional<Decoded> decoded;
    ANativeWindow *pending_window = nullptr;
    uint64_t pending_generation = 0;
    uint64_t detach_generation = 0;
    bool viewport_changed = false;
    bool clear_requested = false;
    uint64_t clear_request_id = 0;
    bool io_done = false;
    bool render_done = false;
    std::thread io_thread;
    std::thread render_thread;
    std::atomic<bool> closed{false};
    std::atomic<uint64_t> latest_request{0};
    std::atomic<uint64_t> frames_presented{0};
    std::atomic<uint64_t> last_frame_us{0};
    std::atomic<uint32_t> viewport_width{0};
    std::atomic<uint32_t> viewport_height{0};
    uint64_t max_input_bytes = UINT64_MAX;
    uint64_t max_scene_bytes = UINT64_MAX;
    std::string temporary_directory;

    ~State()
    {
        if (queued && queued->fd >= 0) close(queued->fd);
        if (pending_window) ANativeWindow_release(pending_window);
        if (io_thread.joinable()) io_thread.detach();
        if (render_thread.joinable()) render_thread.detach();
    }

    void start()
    {
        auto self = shared_from_this();
        io_thread = std::thread([self] { self->io_loop(); });
        try { render_thread = std::thread([self] { self->render_loop(); }); }
        catch (...)
        {
            { std::lock_guard lock(mutex); closed = true; }
            condition.notify_all();
            io_thread.join();
            throw;
        }
    }

    void io_loop()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [&] { return closed || queued.has_value(); });
                if (closed) break;
                job = *queued;
                queued.reset();
            }
            Decoded response;
            response.request_id = job.request_id;
            try
            {
                gs::io::LoadResult result;
                if (job.shared)
                    result = gs::android::io::import_shared_fd(job.fd, max_scene_bytes, [&] {
                        return closed || latest_request != job.request_id;
                    });
                else
                {
                    gs::android::io::FdLoadRequest request{job.fd};
                    request.limits.maxInputBytes = max_input_bytes;
                    request.limits.maxSceneBytes = max_scene_bytes;
                    request.temporary_directory = temporary_directory;
                    result = gs::android::io::load_fd(request, [&] {
                        return closed || latest_request != job.request_id;
                    });
                }
                if (auto *scene = std::get_if<gs::SceneHandle>(&result))
                    response.scene = std::move(*scene);
                else response.error = Error::Decode;
            }
            catch (...) { response.error = Error::Decode; }
            close(job.fd);
            {
                std::lock_guard lock(mutex);
                if (!closed && latest_request == job.request_id)
                    decoded = std::move(response);
            }
            condition.notify_all();
        }
        {
            std::lock_guard lock(mutex);
            io_done = true;
        }
        condition.notify_all();
    }

    void render_loop()
    {
        std::unique_ptr<SceneRenderer> renderer;
        ANativeWindow *window = nullptr;
        uint64_t generation = 0;
        uint32_t width = 0, height = 0;
        uint64_t next_ticket = 0;
        std::optional<Decoded> ready;
        unsigned int recovery_attempts = 0;
        try
        {
        for (;;)
        {
            ANativeWindow *replacement = nullptr;
            uint64_t replacement_generation = 0, detach = 0;
            bool clear = false;
            uint64_t cleared_id = 0;
            {
                std::unique_lock lock(mutex);
                auto awakened = [&] {
                    return closed || pending_window || detach_generation ||
                           viewport_changed || clear_requested || decoded.has_value();
                };
                const bool surface_ready = window && ANativeWindow_getWidth(window) > 0 &&
                                           ANativeWindow_getHeight(window) > 0;
                if (surface_ready && (ready || transactions.active_scene()))
                    condition.wait_for(lock, std::chrono::milliseconds(16), awakened);
                else
                    condition.wait(lock, awakened);
                if (closed) break;
                replacement = std::exchange(pending_window, nullptr);
                replacement_generation = std::exchange(pending_generation, 0);
                detach = std::exchange(detach_generation, 0);
                viewport_changed = false;
                clear = std::exchange(clear_requested, false);
                cleared_id = std::exchange(clear_request_id, 0);
                if (decoded) ready = std::exchange(decoded, std::nullopt);
            }
            if (clear && renderer)
            {
                try { renderer->clear_scene(); }
                catch (...) { renderer.reset(); }
            }
            if (clear && ready && ready->request_id <= cleared_id) ready.reset();
            if (detach && detach >= generation)
            {
                renderer.reset();
                if (window) ANativeWindow_release(window);
                window = nullptr;
                generation = 0;
            }
            if (replacement)
            {
                renderer.reset();
                if (window) ANativeWindow_release(window);
                window = replacement;
                generation = replacement_generation;
                recovery_attempts = 0;
            }
            if (ready && ready->request_id != transactions.snapshot().request_id)
                ready.reset();
            if (ready)
            {
                const auto phase = transactions.snapshot().phase;
                if (phase != gs::android::engine::Phase::Loading &&
                    phase != gs::android::engine::Phase::Uploading)
                    ready.reset();
            }
            if (ready && (ready->error != Error::None || !ready->scene))
            {
                transactions.fail(ready->request_id,
                                  ready->error == Error::None ? Error::Decode : ready->error);
                ready.reset();
            }
            if (!window) continue;
            width = static_cast<uint32_t>(std::max(0, ANativeWindow_getWidth(window)));
            height = static_cast<uint32_t>(std::max(0, ANativeWindow_getHeight(window)));
            if (!width || !height) continue;
            if (!renderer)
            {
                try
                {
                    renderer = std::make_unique<SceneRenderer>(window);
                    if (auto active = transactions.active_scene())
                    {
                        std::string diagnostic;
                        if (renderer->upload(active, diagnostic)) renderer->commit_pending();
                        else
                        {
                            transactions.render_failure(generation, Error::Upload);
                            renderer.reset();
                            ANativeWindow_release(window);
                            window = nullptr;
                            generation = 0;
                            continue;
                        }
                    }
                }
                catch (...)
                {
                    if (++recovery_attempts >= 2)
                    {
                        if (ready) transactions.fail(ready->request_id, Error::Device);
                        ready.reset();
                        transactions.render_failure(generation, Error::Device);
                        ANativeWindow_release(window);
                        window = nullptr;
                        generation = 0;
                    }
                    continue;
                }
            }
            if (ready)
            {
                {
                    auto snapshot = transactions.snapshot();
                    if (snapshot.phase == gs::android::engine::Phase::Loading &&
                        !transactions.decoded(ready->request_id, ready->scene, width, height))
                        ready.reset();
                    if (ready && transactions.pending_scene(ready->request_id))
                    {
                        std::string diagnostic;
                        snapshot = transactions.snapshot();
                        const uint64_t ticket = snapshot.upload_ticket ? snapshot.upload_ticket
                            : ++next_ticket;
                        const bool uploaded = renderer->upload(ready->scene, diagnostic);
                        const bool accepted = uploaded && (snapshot.upload_ticket ||
                            transactions.upload_started(ready->request_id, ticket));
                        if (accepted)
                        {
                            const auto pose = transactions.pending_pose(ready->request_id);
                            const auto result = pose ? renderer->render(*pose)
                                : gs::android::render::FrameResult{};
                            if (result.status == FrameStatus::Presented &&
                                transactions.presented(ready->request_id, ticket, generation))
                            {
                                renderer->commit_pending();
                                ++frames_presented;
                                last_frame_us = result.cpu_frame_us;
                                recovery_attempts = 0;
                                if (result.platform_result == VK_SUBOPTIMAL_KHR) renderer.reset();
                            }
                            else
                            {
                                renderer->rollback_pending();
                                if (transactions.snapshot().surface_generation != generation)
                                    renderer.reset();
                                else if (result.status == FrameStatus::SurfaceChanged ||
                                    result.status == FrameStatus::SurfaceLost ||
                                    result.status == FrameStatus::DeviceLost)
                                {
                                    renderer.reset();
                                    if (++recovery_attempts >= 2)
                                    {
                                        transactions.fail(ready->request_id, Error::Surface);
                                        transactions.render_failure(generation, Error::Surface);
                                        ANativeWindow_release(window);
                                        window = nullptr;
                                        generation = 0;
                                    }
                                }
                                else
                                {
                                    transactions.fail(ready->request_id, Error::Upload);
                                    renderer.reset();
                                }
                            }
                        }
                        else
                        {
                            if (uploaded) renderer->rollback_pending();
                            if (transactions.snapshot().request_id == ready->request_id)
                                transactions.fail(ready->request_id, Error::Upload);
                        }
                    }
                    if (transactions.snapshot().phase != gs::android::engine::Phase::Uploading)
                        ready.reset();
                }
            }
            if (renderer && renderer->scene_count() && transactions.active_scene() &&
                !ready)
            {
                const auto result = renderer->render(transactions.snapshot().camera);
                if (result.status == FrameStatus::Presented)
                {
                    transactions.surface_restored(generation);
                    ++frames_presented;
                    last_frame_us = result.cpu_frame_us;
                    recovery_attempts = 0;
                    if (result.platform_result == VK_SUBOPTIMAL_KHR) renderer.reset();
                }
                else if (result.status == FrameStatus::DeviceLost ||
                         result.status == FrameStatus::SurfaceChanged ||
                         result.status == FrameStatus::SurfaceLost ||
                         result.status == FrameStatus::Failed)
                {
                    renderer.reset();
                    if (++recovery_attempts >= 2)
                    {
                        transactions.render_failure(generation,
                            result.status == FrameStatus::DeviceLost ? Error::Device : Error::Surface);
                        ANativeWindow_release(window);
                        window = nullptr;
                        generation = 0;
                    }
                }
            }
        }
        }
        catch (const std::exception &error)
        {
            __android_log_print(ANDROID_LOG_ERROR, "Native3DGS", "Render thread: %s",
                                error.what());
            if (ready) transactions.fail(ready->request_id, Error::Device);
            transactions.render_failure(generation, Error::Device);
            { std::lock_guard lock(mutex); closed = true; }
        }
        catch (...)
        {
            __android_log_print(ANDROID_LOG_ERROR, "Native3DGS", "Render thread failure");
            if (ready) transactions.fail(ready->request_id, Error::Device);
            transactions.render_failure(generation, Error::Device);
            { std::lock_guard lock(mutex); closed = true; }
        }
        condition.notify_all();
        renderer.reset();
        if (window) ANativeWindow_release(window);
        {
            std::lock_guard lock(mutex);
            render_done = true;
        }
        condition.notify_all();
    }

    void stop()
    {
        {
            std::lock_guard lock(mutex);
            closed = true;
            latest_request = 0;
        }
        transactions.shutdown();
        condition.notify_all();
        if (io_thread.joinable()) io_thread.join();
        if (render_thread.joinable()) render_thread.join();
    }
};
} // namespace

struct gs_android_engine { std::shared_ptr<State> state; };

extern "C" uint32_t gs_android_api_version(void) { return GS_ANDROID_API_VERSION; }

extern "C" gs_android_result_t gs_android_create(const gs_android_config_t *config,
                                                    gs_android_engine_t **out_engine)
{
    if (!config || !out_engine || config->struct_size < offsetof(gs_android_config_t, max_scene_bytes)
        + sizeof(config->max_scene_bytes) || config->api_version != GS_ANDROID_API_VERSION)
        return GS_ANDROID_INVALID_ARGUMENT;
    *out_engine = nullptr;
    try
    {
        auto handle = std::make_unique<gs_android_engine>();
        handle->state = std::make_shared<State>();
        handle->state->max_input_bytes = config->max_input_bytes ? config->max_input_bytes : UINT64_MAX;
        handle->state->max_scene_bytes = config->max_scene_bytes ? config->max_scene_bytes : UINT64_MAX;
        if (config->struct_size >= sizeof(gs_android_config_t) && config->temporary_directory)
            handle->state->temporary_directory = config->temporary_directory;
        handle->state->start();
        *out_engine = handle.release();
        return GS_ANDROID_OK;
    }
    catch (...) { return GS_ANDROID_OUT_OF_MEMORY; }
}

extern "C" void gs_android_destroy(gs_android_engine_t *engine)
{
    if (!engine) return;
    engine->state->stop();
    delete engine;
}

static gs_android_result_t open_fd(gs_android_engine_t *engine, int fd,
                                    uint64_t *request_id, bool shared)
{
    if (!engine || !request_id || fd < 0) return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    if (state->closed) return GS_ANDROID_CLOSED;
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || (flags & O_ACCMODE) == O_WRONLY) return GS_ANDROID_IO_ERROR;
    const int owned = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (owned < 0) return GS_ANDROID_IO_ERROR;
    uint64_t id = 0;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed) { close(owned); return GS_ANDROID_CLOSED; }
        id = state->transactions.begin_open();
        if (!id) { close(owned); return GS_ANDROID_CLOSED; }
        state->latest_request = id;
        if (state->queued) close(state->queued->fd);
        state->queued = Job{id, owned, shared};
        state->decoded.reset();
    }
    state->condition.notify_all();
    *request_id = id;
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_open_fd(gs_android_engine_t *engine, int fd,
                                                     uint64_t *request_id)
{ return open_fd(engine, fd, request_id, false); }

extern "C" gs_android_result_t gs_android_open_shared_fd(gs_android_engine_t *engine, int fd,
                                                            uint64_t *request_id)
{ return open_fd(engine, fd, request_id, true); }

extern "C" gs_android_result_t gs_android_cancel(gs_android_engine_t *engine,
                                                    uint64_t request_id)
{
    if (!engine || !request_id) return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed) return GS_ANDROID_CLOSED;
        if (!state->transactions.cancel(request_id)) return GS_ANDROID_INVALID_ARGUMENT;
        state->latest_request = 0;
        if (state->queued && state->queued->request_id == request_id)
        {
            close(state->queued->fd);
            state->queued.reset();
        }
        state->decoded.reset();
    }
    state->condition.notify_all();
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_close_scene(gs_android_engine_t *engine)
{
    if (!engine) return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed) return GS_ANDROID_CLOSED;
        state->latest_request = 0;
        state->clear_request_id = state->transactions.snapshot().request_id;
        state->transactions.clear_scene();
        if (state->queued)
        {
            close(state->queued->fd);
            state->queued.reset();
        }
        state->clear_requested = true;
        state->decoded.reset();
    }
    state->condition.notify_all();
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_attach_surface(gs_android_engine_t *engine,
    void *window, uint64_t generation)
{
    if (!engine || !window || !generation) return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    auto *native = static_cast<ANativeWindow *>(window);
    {
        std::lock_guard lock(state->mutex);
        if (state->closed) return GS_ANDROID_CLOSED;
        if (!state->transactions.attach_surface(generation)) return GS_ANDROID_INVALID_ARGUMENT;
        ANativeWindow_acquire(native);
        state->viewport_width = static_cast<uint32_t>(std::max(0, ANativeWindow_getWidth(native)));
        state->viewport_height = static_cast<uint32_t>(std::max(0, ANativeWindow_getHeight(native)));
        if (state->pending_window) ANativeWindow_release(state->pending_window);
        state->pending_window = native;
        state->pending_generation = generation;
    }
    state->condition.notify_all();
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_detach_surface(gs_android_engine_t *engine,
    uint64_t generation)
{
    if (!engine || !generation) return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed) return GS_ANDROID_CLOSED;
        if (!state->transactions.detach_surface(generation)) return GS_ANDROID_INVALID_ARGUMENT;
        state->viewport_width = 0;
        state->viewport_height = 0;
        if (state->pending_generation == generation && state->pending_window)
        {
            ANativeWindow_release(state->pending_window);
            state->pending_window = nullptr;
            state->pending_generation = 0;
        }
        state->detach_generation = std::max(state->detach_generation, generation);
    }
    state->condition.notify_all();
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_resize(gs_android_engine_t *engine,
    uint64_t generation, uint64_t revision, uint32_t width, uint32_t height)
{
    if (!engine) return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    std::lock_guard lock(state->mutex);
    if (state->closed) return GS_ANDROID_CLOSED;
    if (!state->transactions.resize(generation, revision, width, height))
        return GS_ANDROID_INVALID_ARGUMENT;
    state->viewport_width = width;
    state->viewport_height = height;
    state->viewport_changed = true;
    state->condition.notify_all();
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_camera(gs_android_engine_t *engine,
    gs_android_camera_action_t action, double x, double y, double z, double seconds)
{
    if (!engine) return GS_ANDROID_INVALID_ARGUMENT;
    if (engine->state->closed) return GS_ANDROID_CLOSED;
    auto state = engine->state;
    const uint32_t width = state->viewport_width, height = state->viewport_height;
    if (action == GS_ANDROID_FIT)
        return state->transactions.fit_active(width, height)
            ? GS_ANDROID_OK : GS_ANDROID_INVALID_ARGUMENT;
    const bool accepted = state->transactions.camera_command([&](auto &camera) {
        switch (action)
        {
        case GS_ANDROID_ORBIT: return camera.orbit(x, y);
        case GS_ANDROID_PAN: return camera.pan(x, y);
        case GS_ANDROID_DOLLY: return camera.dolly(x);
        case GS_ANDROID_LOOK: return camera.look(x, y);
        case GS_ANDROID_FLY: return camera.fly({x, y, z, false}, seconds);
        case GS_ANDROID_RESET: camera.reset(); return true;
        case GS_ANDROID_ORBIT_MODE:
            return camera.set_mode(gs::android::engine::ViewMode::Orbit);
        case GS_ANDROID_FLY_MODE:
            return camera.set_mode(gs::android::engine::ViewMode::Fly);
        case GS_ANDROID_FLIP_AXES:
            if (!std::isfinite(x) || x < 0 || x > 7 || x != std::floor(x)) return false;
            camera.set_flip_axes(static_cast<uint8_t>(x)); return true;
        default: return false;
        }
    });
    if (accepted) state->condition.notify_all();
    return accepted ? GS_ANDROID_OK : GS_ANDROID_INVALID_ARGUMENT;
}

extern "C" gs_android_result_t gs_android_get_snapshot(gs_android_engine_t *engine,
    gs_android_snapshot_t *snapshot)
{
    if (!engine || !snapshot || snapshot->struct_size < offsetof(gs_android_snapshot_t, phase)
        + sizeof(snapshot->phase)) return GS_ANDROID_INVALID_ARGUMENT;
    const auto source = engine->state->transactions.snapshot();
    gs_android_snapshot_t result{};
    result.struct_size = sizeof(result);
    result.phase = static_cast<uint32_t>(source.phase);
    result.request_id = source.request_id;
    result.active_request_id = source.active_request_id;
    result.upload_ticket = source.upload_ticket;
    result.surface_generation = source.surface_generation;
    result.scene_count = source.scene_count;
    result.error = static_cast<uint32_t>(source.error);
    result.frames_presented = engine->state->frames_presented;
    result.last_frame_us = engine->state->last_frame_us;
    std::memcpy(snapshot, &result, std::min<size_t>(snapshot->struct_size, sizeof(result)));
    return GS_ANDROID_OK;
}
