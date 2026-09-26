#pragma once

#include "camera.h"
#include "model-io/model_loader.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace gs::desktop
{
struct EngineMessage
{
    enum class Kind { Ready, LoadProgress, UploadStarted, LoadFailed, RendererEvent, InitFailed } kind;
    uint64_t request_id = 0;
    render::UploadTicket ticket = 0;
    io::LoadProgress progress{};
    io::LoadErrorCode load_error = io::LoadErrorCode::IoFailure;
    render::RendererEvent render_event{};
};

class ViewerEngine
{
  public:
    using Observer = std::function<void(EngineMessage)>;
    explicit ViewerEngine(Observer observer);
    ~ViewerEngine();
    void start();
    uint64_t open(std::filesystem::path path, render::Viewport viewport, bool flip_z);
    void cancel();
    void clear();
    void stop();
    template <typename F> auto with_renderer(F &&call) const
    {
        std::lock_guard lock(mutex_);
        return call(renderer_);
    }
    bool has_renderer() const { return with_renderer([](auto *r) { return r != nullptr; }); }
    void acknowledge_device_lost();
    bool take_upload(render::UploadTicket ticket, CameraController &camera,
                     std::filesystem::path &path, SceneHandle &scene);

  private:
    struct Request { uint64_t id; std::filesystem::path path; render::Viewport viewport; bool flip_z; };
    void render_loop(std::stop_token stop);
    void load_loop(std::stop_token stop);
    void notify(EngineMessage message);
    Observer observer_;
    std::unique_ptr<io::IModelLoader> loader_;
    render::QualityConfig quality_{};
    render::IRenderer *renderer_ = nullptr;
    std::jthread render_thread_, load_thread_;
    std::atomic<bool> running_{true};
    std::atomic<bool> load_finished_{false};
    std::atomic<uint64_t> request_id_{0};
    std::atomic<render::UploadTicket> pending_ticket_{0};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Request> queued_;
    std::stop_source load_stop_;
    struct UploadInfo { render::UploadTicket ticket; CameraController camera;
                        std::filesystem::path path; SceneHandle scene; };
    std::unordered_map<render::UploadTicket, UploadInfo> uploads_;
    bool device_lost_ = false;
};
} // namespace gs::desktop
