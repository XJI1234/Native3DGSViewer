#pragma once
#include "interop.h"

namespace gs::cuda_ops
{
enum class CompactMode { FullCapacity, DeviceBuckets };
class GS_CUDA_API CompactWorkspace
{
  public:
    CompactWorkspace(Context &, uint32_t capacity, CompactMode);
    ~CompactWorkspace();
    CompactWorkspace(const CompactWorkspace &) = delete;
    CompactWorkspace &operator=(const CompactWorkspace &) = delete;
    // Stable selection of key != UINT_MAX, then stable sort; only the visible
    // prefix is defined. Count remains on device, no per-frame host wait.
    uint32_t submit(Context &, SharedBuffer &keys0, SharedBuffer &keys1,
        SharedBuffer &values0, SharedBuffer &values1, uint32_t count);
    uint64_t scratch_bytes() const;
    SharedBuffer &visible_count_buffer() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
