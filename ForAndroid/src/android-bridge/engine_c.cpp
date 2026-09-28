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
#include <deque>
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
    std::atomic<uint32_t> quality_mode{GS_ANDROID_QUALITY_FULL};
    std::atomic<uint32_t> requested_scale_milli{1000};
    std::atomic<uint32_t> actual_scale_milli{1000};
    std::atomic<uint32_t> display_width{0};
    std::atomic<uint32_t> display_height{0};
    std::atomic<uint64_t> quality_revision{0};
    std::atomic<uint64_t> active_splats{0};
    std::atomic<uint64_t> last_gpu_frame_id{0};
    std::atomic<uint64_t> last_gpu_project_us{0};
    std::atomic<uint64_t> last_gpu_sort_us{0};
    std::atomic<uint64_t> last_gpu_draw_us{0};
    std::deque<gs_android_frame_sample_v2_t> frame_samples;
    uint64_t dropped_frame_samples = 0;
    std::atomic<uint32_t> viewport_width{0};
    std::atomic<uint32_t> viewport_height{0};
    uint64_t max_input_bytes = UINT64_MAX;
    uint64_t max_scene_bytes = UINT64_MAX;
    std::string temporary_directory;

    void record_frame(const gs::android::render::FrameResult &frame)
    {
        if (frame.status != FrameStatus::Presented) return;
        gs_android_frame_sample_v2_t sample{};
        sample.struct_size = sizeof(sample);
        sample.quality_mode = quality_mode;
        sample.frame.frame_id = frame.frame_id;
        sample.frame.present_call_ns = frame.present_call_ns;
        sample.frame.cpu_frame_us = frame.cpu_frame_us;
        sample.frame.gpu_frame_id = frame.gpu_frame_id;
        sample.frame.gpu_project_us = frame.gpu_project_us;
        sample.frame.gpu_sort_us = frame.gpu_sort_us;
        sample.frame.gpu_draw_us = frame.gpu_draw_us;
        sample.frame.submitted_splats = frame.submitted_splats;
        sample.frame.width = frame.width;
        sample.frame.height = frame.height;
        sample.source_splats = frame.submitted_splats;
        sample.active_splats = frame.submitted_splats;
        sample.requested_scale_milli = requested_scale_milli;
        const uint32_t display_w = display_width, display_h = display_height;
        sample.actual_scale_milli = display_w && display_h
            ? static_cast<uint32_t>(std::min(uint64_t(frame.width) * 1000 / display_w,
                                              uint64_t(frame.height) * 1000 / display_h))
            : 1000;
        actual_scale_milli = sample.actual_scale_milli;
        active_splats = sample.active_splats;
        if (frame.gpu_frame_id)
        {
            last_gpu_frame_id = frame.gpu_frame_id;
            last_gpu_project_us = frame.gpu_project_us;
            last_gpu_sort_us = frame.gpu_sort_us;
            last_gpu_draw_us = frame.gpu_draw_us;
        }
        std::lock_guard lock(mutex);
        if (frame_samples.size() == 16384)
        {
            frame_samples.pop_front();
            ++dropped_frame_samples;
        }
        frame_samples.push_back(sample);
    }

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
            const auto import_started = std::chrono::steady_clock::now();
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
            __android_log_print(ANDROID_LOG_DEBUG, "Native3DGSPerf", "import us=%lld shared=%d error=%d",
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - import_started).count()),
                    job.shared, static_cast<int>(response.error));
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
        uint64_t sampled_frames = 0, sampled_frame_us = 0, max_frame_us = 0;
        auto sampled_since = std::chrono::steady_clock::now();
        uint64_t last_rendered_revision = 0;
        uint64_t last_quality_revision = 0;
        bool force_render = true;
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
                           viewport_changed || clear_requested || decoded.has_value() ||
                           quality_revision != last_quality_revision ||
                           (renderer && window && ANativeWindow_getWidth(window) > 0 &&
                            ANativeWindow_getHeight(window) > 0 && transactions.active_scene() &&
                            (force_render || transactions.snapshot().camera_revision !=
                             last_rendered_revision));
                };
                const bool surface_ready = window && ANativeWindow_getWidth(window) > 0 &&
                                           ANativeWindow_getHeight(window) > 0;
                if (surface_ready && (ready || (transactions.active_scene() && !renderer)))
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
            const uint64_t quality = quality_revision;
            if (quality != last_quality_revision)
            {
                last_quality_revision = quality;
                force_render = true;
            }
            if (clear && renderer)
            {
                try { renderer->clear_scene(); }
                catch (...) { renderer.reset(); }
            }
            if (clear) active_splats = 0;
            if (clear && ready && ready->request_id <= cleared_id) ready.reset();
            if (clear) force_render = true;
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
                force_render = true;
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
                    force_render = true;
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
                catch (const std::exception &error)
                {
                    __android_log_print(ANDROID_LOG_ERROR, "Native3DGS",
                                        "Renderer initialization: %s", error.what());
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
                        const auto upload_started = std::chrono::steady_clock::now();
                        const bool uploaded = renderer->upload(ready->scene, diagnostic);
                        __android_log_print(ANDROID_LOG_DEBUG, "Native3DGSPerf", "upload us=%lld count=%llu ok=%d",
                                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - upload_started).count()),
                                static_cast<unsigned long long>(ready->scene->count), uploaded);
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
                                force_render = true;
                                ++frames_presented;
                                last_frame_us = result.cpu_frame_us;
                                record_frame(result);
                                __android_log_print(ANDROID_LOG_DEBUG, "Native3DGSPerf", "first_present us=%llu",
                                        static_cast<unsigned long long>(result.cpu_frame_us));
                                recovery_attempts = 0;
                            }
                            else
                            {
                                __android_log_print(ANDROID_LOG_ERROR, "Native3DGS",
                                    "Pending frame status=%d result=%d stage=%s",
                                    static_cast<int>(result.status), result.platform_result,
                                    result.diagnostic.c_str());
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
                            if (!uploaded)
                                __android_log_print(ANDROID_LOG_ERROR, "Native3DGS",
                                    "Scene upload failed count=%llu reason=%s",
                                    static_cast<unsigned long long>(ready->scene->count),
                                    diagnostic.c_str());
                            if (uploaded) renderer->rollback_pending();
                            if (transactions.snapshot().request_id == ready->request_id)
                                transactions.fail(ready->request_id, Error::Upload);
                        }
                    }
                    if (transactions.snapshot().phase != gs::android::engine::Phase::Uploading)
                        ready.reset();
                }
            }
            const auto active_snapshot = transactions.snapshot();
            if (renderer && renderer->scene_count() && transactions.active_scene() &&
                !ready && (force_render || active_snapshot.camera_revision !=
                           last_rendered_revision))
            {
                const auto result = renderer->render(active_snapshot.camera);
                if (result.status == FrameStatus::Presented)
                {
                    transactions.surface_restored(generation);
                    last_rendered_revision = active_snapshot.camera_revision;
                    force_render = false;
                    ++frames_presented;
                    last_frame_us = result.cpu_frame_us;
                    record_frame(result);
                    if (sampled_frames == 0) sampled_since = std::chrono::steady_clock::now();
                    sampled_frame_us += result.cpu_frame_us;
                    max_frame_us = std::max(max_frame_us, result.cpu_frame_us);
                    if (++sampled_frames == 120)
                    {
                        __android_log_print(ANDROID_LOG_DEBUG, "Native3DGSPerf",
                                "frames=120 elapsed_ms=%lld avg_us=%llu max_us=%llu count=%llu",
                                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - sampled_since).count()),
                                static_cast<unsigned long long>(sampled_frame_us / sampled_frames),
                                static_cast<unsigned long long>(max_frame_us),
                                static_cast<unsigned long long>(renderer->scene_count()));
                        sampled_frames = sampled_frame_us = max_frame_us = 0;
                        sampled_since = std::chrono::steady_clock::now();
                    }
                    recovery_attempts = 0;
                }
                else if (result.status == FrameStatus::DeviceLost ||
                         result.status == FrameStatus::SurfaceChanged ||
                         result.status == FrameStatus::SurfaceLost ||
                         result.status == FrameStatus::Failed)
                {
                    __android_log_print(ANDROID_LOG_ERROR, "Native3DGS",
                        "Frame status=%d result=%d stage=%s",
                        static_cast<int>(result.status), result.platform_result,
                        result.diagnostic.c_str());
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
        + sizeof(config->max_scene_bytes) ||
        (config->api_version != 1 && config->api_version != GS_ANDROID_API_VERSION))
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
        state->active_splats = 0;
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
    auto state = engine->state;
    // Pair command publication with the condition variable's predicate lock.
    std::lock_guard lock(state->mutex);
    if (state->closed) return GS_ANDROID_CLOSED;
    const uint32_t width = state->viewport_width, height = state->viewport_height;
    if (action == GS_ANDROID_FIT)
    {
        const bool accepted = state->transactions.fit_active(width, height);
        if (accepted) state->condition.notify_all();
        return accepted ? GS_ANDROID_OK : GS_ANDROID_INVALID_ARGUMENT;
    }
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

extern "C" gs_android_result_t gs_android_set_quality(gs_android_engine_t *engine,
    gs_android_quality_mode_t mode, uint32_t requested_scale_milli,
    uint32_t display_width, uint32_t display_height)
{
    if (!engine || !display_width || !display_height ||
        (mode != GS_ANDROID_QUALITY_FULL && mode != GS_ANDROID_QUALITY_MOBILE) ||
        (mode == GS_ANDROID_QUALITY_FULL && requested_scale_milli != 1000) ||
        (mode == GS_ANDROID_QUALITY_MOBILE &&
         (requested_scale_milli < 750 || requested_scale_milli > 1000)))
        return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    {
        std::lock_guard lock(state->mutex);
        if (state->closed) return GS_ANDROID_CLOSED;
        if (state->quality_mode != mode ||
            state->requested_scale_milli != requested_scale_milli ||
            state->display_width != display_width || state->display_height != display_height)
        {
            state->quality_mode = mode;
            state->requested_scale_milli = requested_scale_milli;
            state->display_width = display_width;
            state->display_height = display_height;
            ++state->quality_revision;
        }
    }
    state->condition.notify_all();
    return GS_ANDROID_OK;
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
    result.quality_mode = engine->state->quality_mode;
    result.requested_scale_milli = engine->state->requested_scale_milli;
    result.actual_scale_milli = engine->state->actual_scale_milli;
    result.active_splats = engine->state->active_splats;
    result.last_gpu_frame_id = engine->state->last_gpu_frame_id;
    result.last_gpu_project_us = engine->state->last_gpu_project_us;
    result.last_gpu_sort_us = engine->state->last_gpu_sort_us;
    result.last_gpu_draw_us = engine->state->last_gpu_draw_us;
    {
        std::lock_guard lock(engine->state->mutex);
        result.dropped_frame_samples = engine->state->dropped_frame_samples;
    }
    std::memcpy(snapshot, &result, std::min<size_t>(snapshot->struct_size, sizeof(result)));
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_drain_frame_samples(gs_android_engine_t *engine,
    gs_android_frame_sample_t *samples, uint32_t capacity, uint32_t *count)
{
    if (!engine || !count || (capacity && !samples)) return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    std::lock_guard lock(state->mutex);
    *count = std::min<size_t>(capacity, state->frame_samples.size());
    for (uint32_t i = 0; i < *count; ++i)
    {
        samples[i] = state->frame_samples.front().frame;
        state->frame_samples.pop_front();
    }
    return GS_ANDROID_OK;
}

extern "C" gs_android_result_t gs_android_drain_frame_samples_v2(gs_android_engine_t *engine,
    void *samples, uint32_t sample_stride, uint32_t capacity, uint32_t *count)
{
    if (!engine || !count ||
        (capacity && (!samples || sample_stride < offsetof(gs_android_frame_sample_v2_t,
                                                           source_splats))))
        return GS_ANDROID_INVALID_ARGUMENT;
    auto state = engine->state;
    std::lock_guard lock(state->mutex);
    *count = std::min<size_t>(capacity, state->frame_samples.size());
    auto *bytes = static_cast<uint8_t *>(samples);
    for (uint32_t i = 0; i < *count; ++i)
    {
        const auto &sample = state->frame_samples.front();
        std::memcpy(bytes + size_t(i) * sample_stride, &sample,
                    std::min<size_t>(sample_stride, sizeof(sample)));
        state->frame_samples.pop_front();
    }
    return GS_ANDROID_OK;
}
