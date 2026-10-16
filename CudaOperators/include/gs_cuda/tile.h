#pragma once
#include "interop.h"
#include "layout.h"

namespace gs::cuda_ops
{
struct TileConfig
{
    uint32_t source_count, width, height, tile_size, reference_capacity;
    uint64_t budget_bytes;
};
struct TileRange { uint32_t begin, end; };
struct TileStatus
{
    uint64_t references;
    uint32_t flags, max_tile_length;
};
static_assert(sizeof(TileStatus) == 16 && sizeof(TileRange) == 8);
// Experimental private interface. Inputs and outputs remain borrowed until the
// context fence completes. No per-frame CPU count readback or truncation.
class GS_CUDA_API TileWorkspace
{
  public:
    static uint64_t required_bytes(Context &, const TileConfig &);
    TileWorkspace(Context &, const TileConfig &);
    ~TileWorkspace();
    TileWorkspace(const TileWorkspace &) = delete;
    TileWorkspace &operator=(const TileWorkspace &) = delete;
    void submit(Context &, SharedBuffer &ellipses, SharedBuffer &sorted_indexes, SharedBuffer &arguments);
    SharedBuffer &keys() const;
    SharedBuffer &ranges() const;
    SharedBuffer &status() const;
    uint64_t allocated_bytes() const;
    const TileConfig &config() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct TileCompositeOptions
{
    float stddev = 3, alpha_cutoff = 0;
    bool quantize_each_splat = false;
    bool conditional_while = false; // Diagnostic only: unresolved sanitizer/driver failures.
};
class GS_CUDA_API TileCompositeWorkspace
{
  public:
    static uint64_t required_bytes(uint32_t width, uint32_t height);
    TileCompositeWorkspace(Context &, TileWorkspace &, uint64_t budget_bytes);
    ~TileCompositeWorkspace();
    TileCompositeWorkspace(const TileCompositeWorkspace &) = delete;
    TileCompositeWorkspace &operator=(const TileCompositeWorkspace &) = delete;
    void submit(Context &, SharedBuffer &ellipses, SharedBuffer &sorted_indexes, SharedTexture &, TileCompositeOptions = {});
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
