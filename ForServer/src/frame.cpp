#include "gs_server/frame.h"
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <zstd.h>

namespace gs::server
{
namespace
{
constexpr uint64_t max_bytes = 64ull << 20;
constexpr size_t header_size = 64;
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
uint32_t crc(std::span<const uint8_t> bytes)
{
    static constexpr auto table = [] {
        std::array<std::array<uint32_t, 256>, 8> values{};
        for (uint32_t index = 0; index < 256; ++index)
        {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1)));
            values[0][index] = value;
        }
        for (size_t slice = 1; slice < values.size(); ++slice)
            for (size_t index = 0; index < 256; ++index)
                values[slice][index] =
                    (values[slice - 1][index] >> 8) ^ values[0][values[slice - 1][index] & 255];
        return values;
    }();
    uint32_t value = ~0u;
    size_t offset = 0;
    while (bytes.size() - offset >= 8)
    {
        const uint32_t first =
            value ^ uint32_t(bytes[offset]) ^ (uint32_t(bytes[offset + 1]) << 8) ^
            (uint32_t(bytes[offset + 2]) << 16) ^ (uint32_t(bytes[offset + 3]) << 24);
        value = table[7][first & 255] ^ table[6][(first >> 8) & 255] ^
                table[5][(first >> 16) & 255] ^ table[4][first >> 24] ^
                table[3][bytes[offset + 4]] ^ table[2][bytes[offset + 5]] ^
                table[1][bytes[offset + 6]] ^ table[0][bytes[offset + 7]];
        offset += 8;
    }
    for (; offset < bytes.size(); ++offset)
        value = (value >> 8) ^ table[0][(value ^ bytes[offset]) & 255];
    return ~value;
}

void set(std::span<uint8_t> bytes, size_t offset, uint64_t value, size_t length)
{
    for (size_t index = 0; index < length; ++index)
        bytes[offset + index] = uint8_t(value >> (index * 8));
}
uint64_t get(std::span<const uint8_t> bytes, size_t offset, size_t length)
{
    uint64_t value = 0;
    for (size_t index = 0; index < length; ++index)
        value |= uint64_t(bytes[offset + index]) << (index * 8);
    return value;
}
} // namespace
std::string profile_name(Profile profile)
{
    const auto value = uint32_t(profile);
    if (value == 0)
        return "rgba";
    if (value >= 85 && value <= 95)
        return "jpeg" + std::to_string(value);
    throw std::runtime_error("Unsupported image profile");
}
Profile parse_profile(const std::string &name)
{
    if (name == "rgba")
        return Profile::Rgba;
    for (uint32_t quality = 85; quality <= 95; ++quality)
        if (name == "jpeg" + std::to_string(quality))
            return Profile(quality);
    throw std::runtime_error("Unknown image profile: " + name);
}
std::vector<uint8_t> encode_frame(const Frame &frame, bool compress)
{
    profile_name(frame.profile);
    require(frame.width && frame.height && frame.width <= 4096 && frame.height <= 4096,
            "Invalid image dimensions");
    const uint64_t expected = uint64_t(frame.width) * frame.height * 4;
    require(frame.rgba.size() == expected, "Invalid RGBA layout");
    std::vector<uint8_t> payload;
    bool compressed = false;
    if (frame.profile != Profile::Rgba)
        payload = encode_jpeg(frame.rgba, frame.width, frame.height, int(frame.profile));
    else if (compress)
    {
        payload.resize(ZSTD_compressBound(frame.rgba.size()));
        const auto bytes =
            ZSTD_compress(payload.data(), payload.size(), frame.rgba.data(), frame.rgba.size(), 1);
        require(!ZSTD_isError(bytes), "Zstd compression failed");
        if (bytes < expected)
        {
            payload.resize(bytes);
            compressed = true;
        }
        else
            payload = frame.rgba;
    }
    else
        payload = frame.rgba;
    require(!payload.empty() && payload.size() <= max_bytes, "Payload exceeds budget");
    std::vector<uint8_t> packet(header_size, 0);
    std::memcpy(packet.data(), "NGSFRM02", 8);
    set(packet, 8, 2, 4);
    set(packet, 12, uint32_t(frame.profile), 4);
    set(packet, 16, frame.width, 4);
    set(packet, 20, frame.height, 4);
    set(packet, 28, compressed, 4);
    set(packet, 32, expected, 8);
    set(packet, 40, payload.size(), 8);
    set(packet, 48, crc(payload), 4);
    set(packet, 52, crc(packet), 4);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}
FrameInfo inspect_frame(std::span<const uint8_t> packet)
{
    require(packet.size() >= header_size && !std::memcmp(packet.data(), "NGSFRM02", 8),
            "Invalid magic/header; v2 image frames required");
    require(get(packet, 8, 4) == 2 && get(packet, 24, 4) == 0 && get(packet, 56, 8) == 0,
            "Unsupported frame version/flags");
    std::array<uint8_t, header_size> header;
    std::memcpy(header.data(), packet.data(), header_size);
    const auto checksum = get(packet, 52, 4);
    set(header, 52, 0, 4);
    require(crc(header) == checksum, "Header checksum mismatch");
    FrameInfo info;
    info.profile = Profile(get(packet, 12, 4));
    profile_name(info.profile);
    info.width = uint32_t(get(packet, 16, 4));
    info.height = uint32_t(get(packet, 20, 4));
    require(info.width && info.height && info.width <= 4096 && info.height <= 4096,
            "Invalid dimensions");
    require(get(packet, 28, 4) <= 1, "Unsupported compression");
    info.compressed = get(packet, 28, 4) != 0;
    require(info.profile == Profile::Rgba || !info.compressed, "JPEG cannot be Zstd wrapped");
    info.unpacked_bytes = get(packet, 32, 8);
    info.payload_bytes = get(packet, 40, 8);
    require(info.unpacked_bytes == uint64_t(info.width) * info.height * 4,
            "Invalid decompressed length");
    require(info.payload_bytes && info.payload_bytes <= max_bytes &&
                packet.size() == header_size + info.payload_bytes,
            "Invalid payload length");
    require(info.profile != Profile::Rgba || info.compressed ||
                info.payload_bytes == info.unpacked_bytes,
            "Raw length mismatch");
    require(crc(packet.subspan(header_size)) == get(packet, 48, 4), "Payload checksum mismatch");
    return info;
}
Frame decode_frame(std::span<const uint8_t> packet)
{
    const auto info = inspect_frame(packet);
    Frame frame{info.width, info.height, info.profile, {}};
    const auto payload = packet.subspan(header_size);
    if (info.profile != Profile::Rgba)
        frame.rgba = decode_jpeg(payload, info.width, info.height);
    else if (info.compressed)
    {
        require(ZSTD_getFrameContentSize(payload.data(), payload.size()) == info.unpacked_bytes,
                "Zstd content size mismatch");
        require(ZSTD_findFrameCompressedSize(payload.data(), payload.size()) == payload.size(),
                "Trailing/concatenated Zstd data");
        frame.rgba.resize(size_t(info.unpacked_bytes));
        const auto bytes =
            ZSTD_decompress(frame.rgba.data(), frame.rgba.size(), payload.data(), payload.size());
        require(!ZSTD_isError(bytes) && bytes == frame.rgba.size(), "Zstd decompression failed");
    }
    else
    {
        require(payload.size() == info.unpacked_bytes, "Raw length mismatch");
        frame.rgba.assign(payload.begin(), payload.end());
    }
    return frame;
}
std::vector<uint8_t> read_bytes(const std::string &path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(bool(input), "Cannot open file");
    const auto length = input.tellg();
    require(length >= 0 && uint64_t(length) <= max_bytes + header_size, "File too large");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    input.seekg(0);
    require(bool(input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()))),
            "File read failed");
    return bytes;
}
void write_bytes(const std::string &path, std::span<const uint8_t> data)
{
    std::ofstream output(path, std::ios::binary);
    require(bool(output.write(reinterpret_cast<const char *>(data.data()),
                              std::streamsize(data.size()))),
            "File write failed");
    output.close();
    require(bool(output), "File close failed");
}
void write_ppm(const std::string &path, std::span<const uint8_t> rgba, uint32_t width,
               uint32_t height)
{
    require(rgba.size() == uint64_t(width) * height * 4, "Invalid PPM input");
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (size_t index = 0; index < rgba.size(); index += 4)
        output.write(reinterpret_cast<const char *>(rgba.data() + index), 3);
    output.close();
    require(bool(output), "PPM write failed");
}
} // namespace gs::server
