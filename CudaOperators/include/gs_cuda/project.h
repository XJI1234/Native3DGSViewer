#pragma once
#include "interop.h"
#include "layout.h"

namespace gs::cuda_ops
{
enum class ProjectMath { Reference, FactoredReciprocal, ReciprocalSqrt, FactoredAndRsqrt };
enum class ProjectCounters { Scalar, WarpAggregated };
// PlaneMajor stores coefficient channel j of point i at j * point_count + i.
enum class ShLayout { Interleaved, PlaneMajor };
// Borrows shared buffers until the context fence completes. No host readback.
GS_CUDA_API void project(Context &, const render::detail::FrameConstants &,
    SharedBuffer &attributes, SharedBuffer &sh, SharedBuffer &ellipses,
    SharedBuffer &keys, SharedBuffer &indexes, SharedBuffer &arguments,
    uint32_t block_size = 256, ProjectMath math = ProjectMath::Reference,
    ProjectCounters counters = ProjectCounters::Scalar, ShLayout layout = ShLayout::Interleaved);
GS_CUDA_API void project(Context &, const render::detail::FrameConstants &,
    OwnedBuffer &attributes, OwnedBuffer &sh, SharedBuffer &ellipses,
    SharedBuffer &keys, SharedBuffer &indexes, SharedBuffer &arguments,
    uint32_t block_size = 256, ProjectMath math = ProjectMath::Reference,
    ProjectCounters counters = ProjectCounters::Scalar, ShLayout layout = ShLayout::Interleaved);
}
