#include "engine/transactions.h"

#include <utility>

namespace gs::android::engine
{

uint64_t Transactions::begin_open()
{
    std::lock_guard lock(mutex_);
    if (state_.phase == Phase::Stopping || state_.phase == Phase::Stopped ||
        next_request_ == UINT64_MAX)
        return 0;
    pending_.reset();
    state_.request_id = ++next_request_;
    state_.upload_ticket = 0;
    state_.phase = Phase::Loading;
    state_.error = Error::None;
    return state_.request_id;
}

bool Transactions::decoded(uint64_t request_id, SceneHandle scene, uint32_t width,
                           uint32_t height)
{
    std::lock_guard lock(mutex_);
    if (!request_id || request_id != state_.request_id || state_.phase != Phase::Loading ||
        !scene || !scene->count)
        return false;
    CameraController candidate = camera_;
    if (!candidate.fit(*scene, width, height))
    {
        state_.phase = active_ ? (active_presented_ ? Phase::Ready : Phase::Recovering)
                               : Phase::Failed;
        state_.error = Error::Decode;
        return false;
    }
    pending_ = std::move(scene);
    pending_camera_ = std::move(candidate);
    state_.phase = Phase::Uploading;
    return true;
}

bool Transactions::upload_started(uint64_t request_id, uint64_t ticket)
{
    std::lock_guard lock(mutex_);
    if (!ticket || request_id != state_.request_id || state_.phase != Phase::Uploading ||
        !pending_ || state_.upload_ticket)
        return false;
    state_.upload_ticket = ticket;
    return true;
}

bool Transactions::presented(uint64_t request_id, uint64_t ticket,
                             uint64_t surface_generation)
{
    std::lock_guard lock(mutex_);
    if (!has_surface_ || surface_generation != state_.surface_generation ||
        !request_id || request_id != state_.request_id || !ticket ||
        ticket != state_.upload_ticket || !pending_ || state_.phase != Phase::Uploading)
        return false;
    active_ = std::move(pending_);
    camera_ = std::move(pending_camera_);
    ++camera_revision_;
    state_.active_request_id = request_id;
    state_.scene_count = active_->count;
    state_.camera = camera_.pose();
    state_.phase = Phase::Ready;
    active_presented_ = true;
    state_.error = Error::None;
    return true;
}

bool Transactions::fail(uint64_t request_id, Error error)
{
    std::lock_guard lock(mutex_);
    if (!request_id || request_id != state_.request_id ||
        (state_.phase != Phase::Loading && state_.phase != Phase::Uploading) ||
        error == Error::None)
        return false;
    pending_.reset();
    state_.upload_ticket = 0;
    state_.phase = active_ ? (active_presented_ ? Phase::Ready : Phase::Recovering)
                           : Phase::Failed;
    state_.error = error;
    return true;
}

bool Transactions::cancel(uint64_t request_id)
{
    return fail(request_id, Error::Cancelled);
}

void Transactions::clear_scene()
{
    std::lock_guard lock(mutex_);
    if (state_.phase == Phase::Stopping || state_.phase == Phase::Stopped) return;
    pending_.reset();
    active_.reset();
    active_presented_ = false;
    ++camera_revision_;
    state_.request_id = 0;
    state_.active_request_id = 0;
    state_.upload_ticket = 0;
    state_.scene_count = 0;
    state_.phase = Phase::Empty;
    state_.error = Error::None;
    camera_ = {};
    state_.camera = camera_.pose();
}

bool Transactions::attach_surface(uint64_t generation)
{
    std::lock_guard lock(mutex_);
    if (!generation || generation <= state_.surface_generation ||
        state_.phase == Phase::Stopping || state_.phase == Phase::Stopped)
        return false;
    has_surface_ = true;
    if (active_)
    {
        active_presented_ = false;
        if (state_.phase == Phase::Ready) state_.phase = Phase::Recovering;
    }
    state_.surface_generation = generation;
    state_.viewport_revision = 0;
    return true;
}

bool Transactions::detach_surface(uint64_t generation)
{
    std::lock_guard lock(mutex_);
    if (!has_surface_ || generation != state_.surface_generation) return false;
    has_surface_ = false;
    active_presented_ = false;
    if (active_ && state_.phase == Phase::Ready)
        state_.phase = Phase::Recovering;
    return true;
}

bool Transactions::resize(uint64_t generation, uint64_t revision, uint32_t width,
                          uint32_t height)
{
    std::lock_guard lock(mutex_);
    if (!has_surface_ || generation != state_.surface_generation ||
        revision <= state_.viewport_revision || !width || !height ||
        width > 16384 || height > 16384)
        return false;
    state_.viewport_revision = revision;
    if (camera_.has_scene())
    {
        camera_.resize(width, height);
        ++camera_revision_;
    }
    if (pending_) pending_camera_.resize(width, height);
    return true;
}

bool Transactions::render_failure(uint64_t generation, Error error)
{
    std::lock_guard lock(mutex_);
    if (generation != state_.surface_generation || error == Error::None ||
        state_.phase == Phase::Stopping || state_.phase == Phase::Stopped)
        return false;
    state_.error = error;
    active_presented_ = false;
    if (state_.phase != Phase::Loading && state_.phase != Phase::Uploading)
        state_.phase = active_ ? Phase::Recovering : Phase::Failed;
    return true;
}

bool Transactions::surface_restored(uint64_t generation)
{
    std::lock_guard lock(mutex_);
    if (!has_surface_ || generation != state_.surface_generation || !active_)
        return false;
    active_presented_ = true;
    if (state_.phase != Phase::Recovering) return true;
    state_.phase = Phase::Ready;
    if (state_.error == Error::Surface || state_.error == Error::Device)
        state_.error = Error::None;
    return true;
}

bool Transactions::camera_command(const std::function<bool(CameraController &)> &command)
{
    if (!command) return false;
    std::lock_guard command_lock(camera_command_mutex_);
    CameraController candidate;
    SceneHandle scene;
    uint64_t revision = 0;
    {
        std::lock_guard lock(mutex_);
        if (!active_ || state_.phase == Phase::Stopping ||
            state_.phase == Phase::Stopped)
            return false;
        candidate = camera_;
        scene = active_;
        revision = camera_revision_;
    }
    try { if (!command(candidate)) return false; }
    catch (...) { return false; }
    std::lock_guard lock(mutex_);
    if (active_ != scene || camera_revision_ != revision ||
        state_.phase == Phase::Stopping || state_.phase == Phase::Stopped)
        return false;
    camera_ = std::move(candidate);
    ++camera_revision_;
    state_.camera = camera_.pose();
    return true;
}

bool Transactions::fit_active(uint32_t width, uint32_t height)
{
    std::lock_guard lock(mutex_);
    if (!active_ || state_.phase == Phase::Stopping || state_.phase == Phase::Stopped)
        return false;
    auto candidate = camera_;
    if (!candidate.fit(*active_, width, height)) return false;
    camera_ = std::move(candidate);
    ++camera_revision_;
    state_.camera = camera_.pose();
    return true;
}

void Transactions::shutdown()
{
    std::lock_guard lock(mutex_);
    state_.phase = Phase::Stopping;
    pending_.reset();
    active_.reset();
    active_presented_ = false;
    ++camera_revision_;
    has_surface_ = false;
    state_.scene_count = 0;
    state_.phase = Phase::Stopped;
}

Snapshot Transactions::snapshot() const
{
    std::lock_guard lock(mutex_);
    auto result = state_;
    result.camera_revision = camera_revision_;
    return result;
}

SceneHandle Transactions::active_scene() const
{
    std::lock_guard lock(mutex_);
    return active_;
}

SceneHandle Transactions::pending_scene(uint64_t request_id) const
{
    std::lock_guard lock(mutex_);
    return request_id == state_.request_id ? pending_ : nullptr;
}

std::optional<CameraPose> Transactions::pending_pose(uint64_t request_id) const
{
    std::lock_guard lock(mutex_);
    if (request_id != state_.request_id || !pending_) return std::nullopt;
    return pending_camera_.pose();
}

} // namespace gs::android::engine
