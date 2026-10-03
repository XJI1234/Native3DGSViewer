#include "gs_server/windows_client.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <psapi.h>
#include <stdexcept>

namespace
{
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
uint32_t number(const std::string &value, uint32_t limit)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Invalid integer");
    const auto number = std::stoull(value);
    if (number > limit)
        throw std::runtime_error("Integer out of range");
    return uint32_t(number);
}
} // namespace
int main(int argc, char **argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--help")
        {
            std::cout
                << "gs-client --url http://127.0.0.1:8080 --width 1920 --height 1080 --view 0 "
                   "--profile jpeg85 --repeat 1 --out image.ppm --packet frame.ngsf --show 0\n"
                   "gs-client --file frame.ngsf --out image.ppm --show 1\n"
                   "Profiles: rgba jpeg95 jpeg90 jpeg85; cleartext "
                   "restricted to loopback or explicit literal private-IP consent\n";
            return 0;
        }
        std::map<std::string, std::string> options;
        const std::vector<std::string> allowed{"--url",
                                               "--file",
                                               "--out",
                                               "--packet",
                                               "--width",
                                               "--height",
                                               "--yaw",
                                               "--profile",
                                               "--repeat",
                                               "--show",
                                               "--model",
                                               "--token-file",
                                               "--allow-private-http",
                                               "--pitch",
                                               "--zoom",
                                               "--flip-y"};
        for (int index = 1; index < argc; index += 2)
        {
            const std::string name = argv[index];
            if (index + 1 >= argc ||
                std::find(allowed.begin(), allowed.end(), name) == allowed.end() ||
                !options.emplace(name, argv[index + 1]).second)
                throw std::runtime_error("Unknown, duplicate or missing option: " + name);
        }
        const auto value = [&](const std::string &name, const std::string &fallback) {
            const auto found = options.find(name);
            return found == options.end() ? fallback : found->second;
        };
        const auto width = number(value("--width", "640"), 4096),
                   height = number(value("--height", "360"), 4096);
        const auto repeats = number(value("--repeat", "1"), 1000);
        const auto profile = gs::server::parse_profile(value("--profile", "rgba"));
        if (!repeats || !width || !height ||
            (options.contains("--file") && options.contains("--url")))
            throw std::runtime_error("Invalid client arguments");
        const auto init_start = Clock::now();
        gs::server::ThinRenderer renderer;
        const double init_ms = elapsed(init_start);
        std::cerr << "D3D12 adapter: " << renderer.adapter() << ", init " << init_ms << " ms\n";
        gs::server::Frame last;
        for (uint32_t repeat = 0; repeat < repeats; ++repeat)
        {
            const auto start = Clock::now();
            gs::server::Download download;
            if (options.contains("--file"))
                download.packet = gs::server::read_bytes(options.at("--file"));
            else
            {
                gs::server::Endpoint endpoint{value("--url", "http://127.0.0.1:8888")};
                if (options.contains("--token-file"))
                {
                    const auto token = gs::server::read_bytes(options.at("--token-file"));
                    endpoint.token.assign(token.begin(), token.end());
                    while (!endpoint.token.empty() &&
                           (endpoint.token.back() == '\n' || endpoint.token.back() == '\r'))
                        endpoint.token.pop_back();
                }
                endpoint.allow_private_http = number(value("--allow-private-http", "0"), 1) != 0;
                const auto request = gs::server::parse_request(
                    "NGSREQ3 " + std::to_string(repeat + 1) + " " + value("--model", "") + " " +
                    std::to_string(width) + " " + std::to_string(height) + " " +
                    value("--yaw", "0") + " " + value("--pitch", "14.0362434679") + " " +
                    value("--zoom", "1") + " " + gs::server::profile_name(profile) + " " +
                    value("--flip-y", "0"));
                download = gs::server::request_frame(endpoint, request);
            }
            const auto decode_start = Clock::now();
            auto frame = gs::server::decode_frame(download.packet);
            const double decode_ms = elapsed(decode_start);
            const auto render_start = Clock::now();
            auto capture = renderer.render(frame);
            const double render_ms = elapsed(render_start), total_ms = elapsed(start);
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            if (!GetProcessMemoryInfo(GetCurrentProcess(),
                                      reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                                      sizeof(memory)))
                throw std::runtime_error("Process memory query failed");
            std::cout << std::setprecision(10) << "{\"profile\":\""
                      << gs::server::profile_name(frame.profile) << "\",\"width\":" << frame.width
                      << ",\"height\":" << frame.height
                      << ",\"packet_bytes\":" << download.packet.size()
                      << ",\"network_ms\":" << download.milliseconds
                      << ",\"decode_ms\":" << decode_ms << ",\"render_wall_ms\":" << render_ms
                      << ",\"gpu_upload_ms\":" << capture.upload_ms
                      << ",\"gpu_draw_ms\":" << capture.draw_ms
                      << ",\"capture_ms\":" << capture.readback_ms
                      << ",\"client_committed_resource_bytes\":" << capture.allocated_bytes
                      << ",\"peak_working_set_bytes\":" << memory.PeakWorkingSetSize
                      << ",\"ready_with_capture_ms\":" << total_ms << ",\"init_ms\":" << init_ms
                      << ",\"server\":" << download.server_stats << "}\n";
            if (repeat + 1 == repeats)
            {
                if (options.contains("--out"))
                    gs::server::write_ppm(options.at("--out"), capture.rgba, frame.width,
                                          frame.height);
                if (options.contains("--packet"))
                    gs::server::write_bytes(options.at("--packet"), download.packet);
                last = std::move(frame);
            }
        }
        if (number(value("--show", "0"), 1))
            renderer.show(last);
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
