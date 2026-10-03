#pragma once
#include "gs_server/frame.h"

namespace gs::server
{
struct FrameRequest
{
    uint64_t request_id = 1;
    std::string model_id;
    uint32_t width = 640, height = 360;
    double yaw = 0, pitch = 14.0362434679, zoom = 1;
    Profile profile = Profile::Rgba;
    bool flip_y = false;
};
void validate_request(const FrameRequest &request);
FrameRequest parse_request(const std::string &body);
std::string format_request(const FrameRequest &request);
FrameRequest canonical_request(const FrameRequest &request);
std::string view_code(const FrameRequest &request);
std::string view_key(const FrameRequest &request);
std::vector<FrameRequest> predicted_views(const FrameRequest &request, uint32_t depth = 5);
} // namespace gs::server
