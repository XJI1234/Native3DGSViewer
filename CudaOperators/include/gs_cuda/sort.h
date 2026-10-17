#pragma once
#include "interop.h"

namespace gs::cuda_ops
{
enum class SortOffsets { Int64, Int32 };
class GS_CUDA_API SortWorkspace
{
  public:
    // Borrow context and submitted buffers until GPU completion. Wait before
    // destruction. Capture binds stream/addresses/count; rebuild on changes.
    SortWorkspace(Context &, uint32_t capacity, bool capture = false,
        SortOffsets offsets = SortOffsets::Int64);
    ~SortWorkspace();
    SortWorkspace(const SortWorkspace &) = delete;
    SortWorkspace &operator=(const SortWorkspace &) = delete;
    // Return the CUB output buffer selector without reading GPU data back.
    uint32_t submit(Context &, SharedBuffer &keys0, SharedBuffer &keys1,
                    SharedBuffer &values0, SharedBuffer &values1, uint32_t count);
    uint64_t scratch_bytes() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
