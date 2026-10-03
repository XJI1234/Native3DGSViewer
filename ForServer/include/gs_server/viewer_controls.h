#pragma once
#include "gs_server/request.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace gs::server
{
inline FrameRequest navigate_view(FrameRequest view, double horizontal, double vertical)
{
    if (!std::isfinite(horizontal) || !std::isfinite(vertical))
        throw std::runtime_error("Invalid orbit input");
    const auto canonical = canonical_request(view);
    view.yaw = std::remainder(view.yaw + horizontal * (canonical.flip_y ? 1 : -1), 360.0);
    view.pitch = std::remainder(view.pitch + vertical * (view.flip_y ? -1 : 1), 360.0);
    return view;
}
inline uint32_t preview_depth(int selected_index)
{
    return std::array<uint32_t, 3>{5, 10, 15}[std::clamp(selected_index, 0, 2)];
}
class LeaseHealth
{
  public:
    using Clock = std::chrono::steady_clock;
    void success(Clock::time_point now = Clock::now())
    {
        last_success_ = now;
        failures_ = 0;
    }
    bool failure(Clock::time_point now = Clock::now())
    {
        failures_ = std::min(failures_ + 1, 3u);
        return failures_ >= 3 && now - last_success_ >= std::chrono::seconds(30);
    }

  private:
    Clock::time_point last_success_ = Clock::now();
    unsigned failures_ = 0;
};
} // namespace gs::server
