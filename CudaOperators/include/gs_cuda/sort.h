#pragma once
#include "interop.h"

namespace gs::cuda_ops
{
class GS_CUDA_API SortWorkspace
{
  public:
    SortWorkspace(Context &, uint32_t capacity);
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
