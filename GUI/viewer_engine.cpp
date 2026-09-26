#include "viewer_engine.h"

#include <chrono>

namespace gs::desktop
{
ViewerEngine::ViewerEngine(Observer observer)
    : observer_(std::move(observer)), loader_(io::make_model_loader()) {}

ViewerEngine::~ViewerEngine()
{
    stop();
    if (load_thread_.joinable()) load_thread_.join();
    if (render_thread_.joinable()) render_thread_.join();
}

void ViewerEngine::start()
{
    render_thread_ = std::jthread([this](std::stop_token token) { render_loop(token); });
    load_thread_ = std::jthread([this](std::stop_token token) { load_loop(token); });
}

void ViewerEngine::notify(EngineMessage message)
{
    try { observer_(std::move(message)); } catch (...) { OutputDebugStringW(L"Viewer observer failed\n"); }
}

uint64_t ViewerEngine::open(std::filesystem::path path, render::Viewport viewport, bool flip_z)
{
    const uint64_t id = ++request_id_;
    std::lock_guard lock(mutex_);
    load_stop_.request_stop();
    if (auto *r = renderer_)
        if (auto old = pending_ticket_.exchange(0)) r->cancel_upload(old);
    queued_ = Request{id, std::move(path), viewport, flip_z};
    cv_.notify_all();
    return id;
}

void ViewerEngine::cancel()
{
    ++request_id_;
    std::lock_guard lock(mutex_);
    queued_.reset();
    load_stop_.request_stop();
    if (auto *r = renderer_)
        if (auto old = pending_ticket_.exchange(0)) r->cancel_upload(old);
}

void ViewerEngine::clear()
{
    cancel();
    with_renderer([](auto *r) { if (r) r->clear_scene(); });
}

void ViewerEngine::stop()
{
    if (!running_.exchange(false)) return;
    cancel();
    render_thread_.request_stop();
    load_thread_.request_stop();
    acknowledge_device_lost();
    cv_.notify_all();
}

void ViewerEngine::acknowledge_device_lost()
{
    std::lock_guard lock(mutex_);
    device_lost_ = false;
    cv_.notify_all();
}

bool ViewerEngine::take_upload(render::UploadTicket ticket, CameraController &camera,
                               std::filesystem::path &path, SceneHandle &scene)
{
    std::lock_guard lock(mutex_);
    auto found = uploads_.find(ticket);
    if (found == uploads_.end()) return false;
    camera = found->second.camera;
    path = std::move(found->second.path);
    scene = std::move(found->second.scene);
    uploads_.erase(found);
    if (pending_ticket_ == ticket) pending_ticket_ = 0;
    return true;
}

void ViewerEngine::render_loop(std::stop_token stop)
{
    auto created = render::create_renderer(quality_, [this](const render::RendererEvent &event) {
        if (event.kind == render::RendererEvent::Kind::DeviceLost)
        {
            std::lock_guard lock(mutex_);
            device_lost_ = true;
        }
        if (event.kind == render::RendererEvent::Kind::SceneFailed)
        {
            std::lock_guard lock(mutex_);
            uploads_.erase(event.ticket);
            if (pending_ticket_ == event.ticket) pending_ticket_ = 0;
        }
        EngineMessage message{EngineMessage::Kind::RendererEvent};
        message.render_event = event;
        notify(std::move(message));
    });
    if (std::holds_alternative<render::RenderError>(created))
    {
        notify({EngineMessage::Kind::InitFailed});
        return;
    }
    auto renderer = std::move(std::get<std::unique_ptr<render::IRenderer>>(created));
    {
        std::lock_guard lock(mutex_);
        renderer_ = renderer.get();
    }
    notify({EngineMessage::Kind::Ready});
    while (!stop.stop_requested() && running_)
    {
        renderer->render_frame();
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return !device_lost_ || !running_ || stop.stop_requested(); });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return load_finished_.load(); });
        renderer_ = nullptr;
    }
    renderer.reset();
}

void ViewerEngine::load_loop(std::stop_token stop)
{
    while (!stop.stop_requested() && running_)
    {
        Request request;
        std::stop_token load_token;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return queued_.has_value() || !running_ || stop.stop_requested(); });
            if (!running_ || stop.stop_requested()) break;
            request = std::move(*queued_);
            queued_.reset();
            load_stop_ = std::stop_source{};
            load_token = load_stop_.get_token();
        }
        auto result = loader_->load({request.path}, load_token, [this, id = request.id](const io::LoadProgress &progress) {
            if (id != request_id_) return;
            EngineMessage message{EngineMessage::Kind::LoadProgress};
            message.request_id = id;
            message.progress = progress;
            notify(std::move(message));
        });
        if (request.id != request_id_ || load_token.stop_requested()) continue;
        if (auto *error = std::get_if<io::LoadError>(&result))
        {
            EngineMessage message{EngineMessage::Kind::LoadFailed};
            message.request_id = request.id;
            message.load_error = error->code;
            notify(std::move(message));
            continue;
        }
        auto scene = std::get<SceneHandle>(std::move(result));
        CameraController camera;
        if (!camera.fit(*scene, request.viewport, quality_.max_stddev))
        {
            EngineMessage message{EngineMessage::Kind::LoadFailed};
            message.request_id = request.id;
            message.load_error = io::LoadErrorCode::InvalidAttribute;
            notify(std::move(message));
            continue;
        }
        camera.set_flip_z(request.flip_z);
        std::unique_lock lock(mutex_);
        auto *renderer = renderer_;
        if (request.id != request_id_) continue;
        if (!renderer)
        {
            lock.unlock();
            EngineMessage message{EngineMessage::Kind::LoadFailed};
            message.request_id = request.id;
            message.load_error = io::LoadErrorCode::IoFailure;
            notify(std::move(message));
            continue;
        }
        auto uploaded = renderer->upload_scene(scene, camera.camera());
        if (auto *error = std::get_if<render::RenderError>(&uploaded))
        {
            EngineMessage message{EngineMessage::Kind::LoadFailed};
            message.request_id = request.id;
            message.load_error = error->code == render::RenderErrorCode::OutOfVideoMemory
                                     ? io::LoadErrorCode::OutOfMemory : io::LoadErrorCode::IoFailure;
            lock.unlock();
            notify(std::move(message));
            continue;
        }
        const auto ticket = std::get<render::UploadTicket>(uploaded);
        if (request.id != request_id_) { renderer->cancel_upload(ticket); continue; }
        pending_ticket_ = ticket;
        uploads_.emplace(ticket, UploadInfo{ticket, camera, request.path, std::move(scene)});
        lock.unlock();
        EngineMessage message{EngineMessage::Kind::UploadStarted};
        message.request_id = request.id;
        message.ticket = ticket;
        notify(std::move(message));
    }
    {
        std::lock_guard lock(mutex_);
        load_finished_ = true;
    }
    cv_.notify_all();
}
} // namespace gs::desktop
