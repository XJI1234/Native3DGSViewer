#pragma once
#include "interop.h"
#include "layout.h"

namespace gs::cuda_ops
{
enum class ProjectMath { Reference, FactoredReciprocal, ReciprocalSqrt, FactoredAndRsqrt };
// Borrows shared buffers until the context fence completes. No host readback.
GS_CUDA_API void project(Context &, const render::detail::FrameConstants &,
    SharedBuffer &attributes, SharedBuffer &sh, SharedBuffer &ellipses,
    SharedBuffer &keys, SharedBuffer &indexes, SharedBuffer &arguments,
    uint32_t block_size = 256, ProjectMath math = ProjectMath::Reference);
}
