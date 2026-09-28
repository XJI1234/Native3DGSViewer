#pragma once

#include "engine/camera.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace gs::android::engine
{

enum class Phase : uint8_t { Empty, Loading, Uploading, Ready, Recovering, Failed, Stopping, Stopped };

enum class Error : uint8_t { None, Cancelled, Decode, Upload, Surface, Device, Closed };

struct Snapshot
{
    Phase phase = Phase::Empty;
    uint64_t request_id = 0;
    uint64_t active_request_id = 0;
    uint64_t upload_ticket = 0;
    uint64_t surface_generation = 0;
    uint64_t viewport_revision = 0;
    uint64_t camera_revision = 0;
    uint64_t scene_count = 0;
    Error error = Error::None;
    CameraPose camera{};
};

// Serializes results from decoding and rendering without owning their worker threads.
class Transactions
{
  public:
    uint64_t begin_open();
    bool decoded(uint64_t request_id, SceneHandle scene, uint32_t width, uint32_t height);
    bool upload_started(uint64_t request_id, uint64_t ticket);
    bool presented(uint64_t request_id, uint64_t ticket, uint64_t surface_generation);
    bool fail(uint64_t request_id, Error error);
    bool cancel(uint64_t request_id);
    void clear_scene();
    bool attach_surface(uint64_t generation);
    bool detach_surface(uint64_t generation);
    bool resize(uint64_t generation, uint64_t revision, uint32_t width, uint32_t height);
    bool render_failure(uint64_t generation, Error error);
    bool surface_restored(uint64_t generation);
    // Invokes command once outside the state lock. A concurrent resize/fit may reject the result.
    bool camera_command(const std::function<bool(CameraController &)> &command);
    bool fit_active(uint32_t width, uint32_t height);
    void shutdown();

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] SceneHandle active_scene() const;
    [[nodiscard]] SceneHandle pending_scene(uint64_t request_id) const;
    [[nodiscard]] std::optional<CameraPose> pending_pose(uint64_t request_id) const;

  private:
    mutable std::mutex mutex_;
    std::mutex camera_command_mutex_;
    Snapshot state_{};
    SceneHandle active_;
    SceneHandle pending_;
    CameraController camera_;
    CameraController pending_camera_;
    uint64_t next_request_ = 0;
    uint64_t camera_revision_ = 0;
    bool has_surface_ = false;
    bool active_presented_ = false;
};

} // namespace gs::android::engine
