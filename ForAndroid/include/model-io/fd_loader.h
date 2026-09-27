#pragma once

#include "model-io/model_types.h"
#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace gs::android::io
{

struct FdLoadRequest
{
    int fd = -1;
    gs::io::Coordinates ply_coordinates = gs::io::Coordinates::Rdf;
    gs::io::LoadLimits limits{};
    uint64_t available_memory_bytes = 0;
    uint64_t memory_reserve_bytes = 64ull << 20;
    // Required for non-seekable providers; must name an app-private directory.
    std::string temporary_directory;
};

class SharedScene
{
  public:
    SharedScene(gs::SceneHandle scene, int fd) : scene_(std::move(scene)), fd_(fd) {}
    ~SharedScene();
    SharedScene(const SharedScene &) = delete;
    SharedScene &operator=(const SharedScene &) = delete;
    SharedScene(SharedScene &&other) noexcept;
    SharedScene &operator=(SharedScene &&other) noexcept;

    const gs::SceneHandle &scene() const { return scene_; }
    // The caller owns the returned descriptor and must close it.
    int duplicate_fd() const;

  private:
    gs::SceneHandle scene_;
    int fd_ = -1;
};

using SharedLoadResult = std::variant<SharedScene, gs::io::LoadError>;

SharedLoadResult load_shared_fd(const FdLoadRequest &request,
                                const std::function<bool()> &cancelled = {},
                                gs::io::ProgressSink progress = {});

// Duplicates and validates a completed shared scene from another process.
gs::io::LoadResult import_shared_fd(int fd, uint64_t max_scene_bytes,
                                    const std::function<bool()> &cancelled = {});

// Duplicates fd before reading; the caller retains ownership of its descriptor.
gs::io::LoadResult load_fd(const FdLoadRequest &request,
                           const std::function<bool()> &cancelled = {},
                           gs::io::ProgressSink progress = {});

} // namespace gs::android::io
