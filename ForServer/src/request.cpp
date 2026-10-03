#include "gs_server/request.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace gs::server
{
void validate_request(const FrameRequest &request)
{
    if (!request.request_id || request.model_id.empty() || request.model_id.size() > 64 ||
        request.model_id.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") !=
            std::string::npos ||
        !request.width || !request.height || request.width > 4096 || request.height > 4096 ||
        !std::isfinite(request.yaw) || std::abs(request.yaw) > 36000 ||
        !std::isfinite(request.pitch) || std::abs(request.pitch) > 36000 ||
        !std::isfinite(request.zoom) || request.zoom < 0.1 || request.zoom > 10)
        throw std::runtime_error("Invalid v2 render request");
    profile_name(request.profile);
}
FrameRequest parse_request(const std::string &body)
{
    if (body.size() > 1024)
        throw std::runtime_error("Request exceeds budget");
    std::istringstream input(body);
    input.imbue(std::locale::classic());
    std::string magic, profile, extra, identity, width, height;
    FrameRequest request;
    std::string flip;
    if (!(input >> magic >> identity >> request.model_id >> width >> height >> request.yaw >>
          request.pitch >> request.zoom >> profile) ||
        (magic != "NGSREQ2" && magic != "NGSREQ3"))
        throw std::runtime_error("Malformed v2 render request");
    if (magic == "NGSREQ3")
    {
        if (!(input >> flip) || (flip != "0" && flip != "1"))
            throw std::runtime_error("Invalid flip flag");
        request.flip_y = flip == "1";
    }
    else if (std::abs(request.pitch) > 89)
        throw std::runtime_error("Legacy pitch outside range");
    if (input >> extra)
        throw std::runtime_error("Trailing request data");
    const auto integer = [](const std::string &value, uint64_t maximum) {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid unsigned integer");
        const auto result = std::stoull(value);
        if (result > maximum)
            throw std::runtime_error("Integer exceeds budget");
        return result;
    };
    request.request_id = integer(identity, UINT64_MAX);
    request.width = uint32_t(integer(width, 4096));
    request.height = uint32_t(integer(height, 4096));
    request.profile = parse_profile(profile);
    validate_request(request);
    return request;
}
std::string format_request(const FrameRequest &request)
{
    validate_request(request);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << "NGSREQ3 " << request.request_id << ' ' << request.model_id
           << ' ' << request.width << ' ' << request.height << ' ' << request.yaw << ' '
           << request.pitch << ' ' << request.zoom << ' ' << profile_name(request.profile) << ' '
           << int(request.flip_y);
    return output.str();
}
FrameRequest canonical_request(const FrameRequest &request)
{
    validate_request(request);
    auto result = request;
    const auto wrap = [](double angle) { return angle - 360 * std::floor(angle / 360); };
    auto pitch = wrap(request.pitch + 180) - 180;
    auto yaw = request.yaw;
    if (pitch > 90 || pitch < -90)
    {
        pitch = pitch > 90 ? 180 - pitch : -180 - pitch;
        yaw += 180;
        result.flip_y = !result.flip_y;
    }
    result.yaw = std::fmod(std::floor(wrap(yaw) / 2 + 0.5), 180) * 2;
    result.pitch = 90 - std::clamp(int(std::floor((90 - pitch) / 2 + 0.5)), 0, 90) * 2;
    const auto distance =
        std::clamp(int(std::floor(40 + 40 * std::log10(request.zoom) + 0.5)), 0, 80);
    result.zoom = std::pow(10.0, (distance - 40) / 40.0);
    return result;
}
std::string view_code(const FrameRequest &request)
{
    const auto view = canonical_request(request);
    std::ostringstream output;
    output << std::setfill('0') << 'a' << std::setw(3) << int(view.yaw / 2) << "-t" << std::setw(2)
           << int((90 - view.pitch) / 2) << "-d" << std::setw(2)
           << int(std::round(40 + 40 * std::log10(view.zoom)));
    return output.str();
}
std::string view_key(const FrameRequest &request)
{
    const auto view = canonical_request(request);
    return view.model_id + "/" + view_code(view) + "-f" + std::to_string(int(view.flip_y)) + "-" +
           std::to_string(view.width) + "x" + std::to_string(view.height) + "-" +
           profile_name(view.profile);
}
std::vector<FrameRequest> predicted_views(const FrameRequest &request, uint32_t depth)
{
    if (depth != 5 && depth != 10 && depth != 15)
        throw std::runtime_error("Pre-render depth must be 5, 10 or 15");
    const auto current = canonical_request(request);
    std::vector<FrameRequest> result;
    for (uint32_t step = 1; step <= depth; ++step)
        for (const auto &direction :
             std::vector<std::pair<int, int>>{{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
        {
            auto next = current;
            next.yaw += direction.first * double(step) * 2;
            next.pitch += direction.second * double(step) * 2;
            next = canonical_request(next);
            if (view_key(next) != view_key(current) &&
                std::none_of(result.begin(), result.end(), [&](const auto &existing) {
                    return view_key(existing) == view_key(next);
                }))
                result.push_back(next);
        }
    return result;
}
} // namespace gs::server
