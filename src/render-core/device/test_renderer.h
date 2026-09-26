#pragma once
#include "render-core/renderer.h"
#include <atomic>
#include <vector>
namespace gs::render::detail
{
struct RendererTestControl
{
    std::atomic<bool> fail_allocation{false};
    std::atomic<bool> reject_budget{false};
    std::atomic<bool> fence_timeout{false};
    std::atomic<bool> copy_fence_timeout{false};
};
std::variant<std::unique_ptr<IRenderer>, RenderError> create_renderer_for_testing(
    QualityConfig, EventSink, std::shared_ptr<RendererTestControl>);
std::vector<std::string> renderer_debug_errors(IRenderer &);
} // namespace gs::render::detail
