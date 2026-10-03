#include "gs_server/frame.h"
#include "gs_server/primitives.h"
#include <cmath>
#include <gtest/gtest.h>
#include <zstd.h>

using namespace gs::server;

namespace
{
uint32_t test_crc(std::span<const uint8_t> bytes)
{
    uint32_t checksum = ~0u;
    for (auto byte : bytes)
    {
        checksum ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            checksum = (checksum >> 1) ^ (0xedb88320u & (0u - (checksum & 1)));
    }
    return ~checksum;
}
void update_header_crc(std::vector<uint8_t> &packet)
{
    std::fill(packet.begin() + 52, packet.begin() + 56, 0);
    const auto checksum = test_crc(std::span(packet).first(64));
    for (size_t index = 0; index < 4; ++index)
        packet[52 + index] = uint8_t(checksum >> (index * 8));
}
} // namespace

TEST(FrameContracts, ChecksumValidSemanticMutationsAreRejected)
{
    Frame frame{2, 1, Profile::Rgba};
    frame.rgba.assign(8, 255);
    const auto packet = encode_frame(frame, false);
    for (auto offset : {8, 12, 24, 28, 56})
    {
        auto corrupt = packet;
        corrupt[offset] = 127;
        update_header_crc(corrupt);
        EXPECT_THROW(inspect_frame(corrupt), std::exception);
    }
    auto corrupt = packet;
    corrupt[16] = 3;
    corrupt[32] = 12;
    update_header_crc(corrupt);
    EXPECT_THROW(inspect_frame(corrupt), std::exception);
}

TEST(FrameContracts, RgbaRoundTripAndTrailingBytes)
{
    Frame frame{67, 43, Profile::Rgba};
    frame.rgba.resize(67 * 43 * 4);
    for (size_t index = 0; index < frame.rgba.size(); ++index)
        frame.rgba[index] = index % 4 == 3 ? 255 : uint8_t(index * 37);
    for (bool compress : {false, true})
    {
        auto packet = encode_frame(frame, compress);
        EXPECT_EQ(decode_frame(packet).rgba, frame.rgba);
        packet.push_back(0);
        EXPECT_THROW(decode_frame(packet), std::exception);
        packet.resize(63);
        EXPECT_THROW(decode_frame(packet), std::exception);
    }
}
TEST(FrameContracts, EveryHeaderBitMutationRejected)
{
    Frame frame{17, 13, Profile::Rgba};
    frame.rgba.assign(17 * 13 * 4, 255);
    const auto packet = encode_frame(frame);
    EXPECT_TRUE(inspect_frame(packet).compressed);
    for (size_t offset = 0; offset < 64; ++offset)
        for (int bit = 0; bit < 8; ++bit)
        {
            auto corrupt = packet;
            corrupt[offset] ^= uint8_t(1 << bit);
            EXPECT_THROW(decode_frame(corrupt), std::exception);
        }
    auto corrupt = packet;
    corrupt.back() ^= 1;
    EXPECT_THROW(decode_frame(corrupt), std::exception);
    EXPECT_EQ(decode_frame(packet).rgba, frame.rgba);
}
TEST(FrameContracts, InvalidImageLayoutAndProfiles)
{
    Frame frame{0, 13, Profile::Rgba};
    EXPECT_THROW(encode_frame(frame), std::exception);
    frame.width = 4097;
    EXPECT_THROW(encode_frame(frame), std::exception);
    frame.width = 17;
    EXPECT_THROW(encode_frame(frame), std::exception);
    frame.rgba.assign(17 * 13 * 4, 255);
    frame.profile = Profile(84);
    EXPECT_THROW(encode_frame(frame), std::exception);
    frame.profile = Profile::Rgba;
    EXPECT_NO_THROW(encode_frame(frame));
}
TEST(FrameContracts, JpegCorruptionAndWrongDimensions)
{
    std::vector<uint8_t> rgba(64 * 64 * 4, 255);
    const auto jpeg = encode_jpeg(rgba, 64, 64, 90);
    EXPECT_THROW(decode_jpeg(jpeg, 65, 64), std::exception);
    EXPECT_THROW(decode_jpeg(std::span(jpeg).first(8), 64, 64), std::exception);
    EXPECT_THROW(decode_jpeg({}, 64, 64), std::exception);
    EXPECT_EQ(decode_jpeg(jpeg, 64, 64), rgba);
}
TEST(FrameContracts, WireCrcMatchesIeeeKnownVector)
{
    Frame frame{2, 1, Profile::Rgba};
    frame.rgba = {1, 2, 3, 4, 5, 6, 7, 8};
    const auto packet = encode_frame(frame, false);
    EXPECT_EQ(packet[48], 0xc5);
    EXPECT_EQ(packet[49], 0x88);
    EXPECT_EQ(packet[50], 0xca);
    EXPECT_EQ(packet[51], 0x3f);
}
TEST(FrameContracts, RejectsNonJpegContainer)
{
    const std::vector<uint8_t> png{
        0x89, 0x50, 0x4e, 0x47, 0xd,  0xa,  0x1a, 0xa,  0x0,  0x0,  0x0,  0xd,  0x49, 0x48,
        0x44, 0x52, 0x0,  0x0,  0x0,  0x1,  0x0,  0x0,  0x0,  0x1,  0x8,  0x2,  0x0,  0x0,
        0x0,  0x90, 0x77, 0x53, 0xde, 0x0,  0x0,  0x0,  0xc,  0x49, 0x44, 0x41, 0x54, 0x78,
        0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x0,  0x0,  0x3,  0x1,  0x1,  0x0,  0xc9, 0xfe, 0x92,
        0xef, 0x0,  0x0,  0x0,  0x0,  0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    EXPECT_THROW(decode_jpeg(png, 1, 1), std::exception);
}
#ifndef _WIN32
TEST(FrameContracts, ReportsBufferedWriteFailure)
{
    const std::vector<uint8_t> pixels{255, 0, 0, 255};
    EXPECT_THROW(write_bytes("/dev/full", pixels), std::exception);
    EXPECT_THROW(write_ppm("/dev/full", pixels, 1, 1), std::exception);
}
#endif
TEST(FrameContracts, CpuReferenceClipsExtremeFiniteCentersSafely)
{
    std::vector<Ellipse> far{{1e30f, 1e30f, 5, 0, 0, 2, 1, 1, 1, 1}};
    EXPECT_EQ(raster_reference(far, 17, 13), raster_reference({}, 17, 13));
    far[0].center_x = NAN;
    EXPECT_THROW(raster_reference(far, 17, 13), std::exception);
}
TEST(FrameContracts, TransparentOrderIsNotCommutative)
{
    std::vector<Ellipse> layers{{8, 8, 6, 0, 0, 6, 1, 0, 0, 0.8f},
                                {8, 8, 6, 0, 0, 6, 0, 0, 1, 0.8f}};
    const auto first = raster_reference(layers, 16, 16);
    std::swap(layers[0], layers[1]);
    EXPECT_NE(first, raster_reference(layers, 16, 16));
}
