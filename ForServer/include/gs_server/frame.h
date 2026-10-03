#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gs::server
{
enum class Profile : uint32_t
{
    Rgba = 0,
    Jpeg85 = 85,
    Jpeg90 = 90,
    Jpeg95 = 95
};
struct Frame
{
    uint32_t width = 0, height = 0;
    Profile profile = Profile::Rgba;
    std::vector<uint8_t> rgba;
};
struct FrameInfo
{
    uint32_t width = 0, height = 0;
    Profile profile{};
    uint64_t unpacked_bytes = 0, payload_bytes = 0;
    bool compressed = false;
};
Profile parse_profile(const std::string &name);
std::string profile_name(Profile profile);
std::vector<uint8_t> encode_frame(const Frame &frame, bool compress = true);
Frame decode_frame(std::span<const uint8_t> packet);
FrameInfo inspect_frame(std::span<const uint8_t> packet);
std::vector<uint8_t> encode_jpeg(std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
                                 int quality);
std::vector<uint8_t> decode_jpeg(std::span<const uint8_t> bytes, uint32_t width, uint32_t height);
std::vector<uint8_t> read_bytes(const std::string &path);
void write_bytes(const std::string &path, std::span<const uint8_t> data);
void write_ppm(const std::string &path, std::span<const uint8_t> rgba, uint32_t width,
               uint32_t height);
} // namespace gs::server
