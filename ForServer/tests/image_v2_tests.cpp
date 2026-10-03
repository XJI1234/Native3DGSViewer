#include "gs_server/frame.h"
#include <cstring>
#include <gtest/gtest.h>

using namespace gs::server;

TEST(ImageV2, RejectsRetiredProfilesAndVersion)
{
    for (const auto *name : {"f32", "f16", "q20", "q16", "jpeg70", "jpeg50"})
        EXPECT_THROW(parse_profile(name), std::exception);
    Frame frame{17, 13, Profile::Rgba};
    frame.rgba.assign(17 * 13 * 4, 255);
    auto packet = encode_frame(frame);
    EXPECT_EQ(std::string(reinterpret_cast<char *>(packet.data()), 8), "NGSFRM02");
    std::memcpy(packet.data(), "NGSFRM01", 8);
    EXPECT_THROW(decode_frame(packet), std::exception);
}
TEST(ImageV2, AllQualityLevelsHaveValidIdentity)
{
    Frame frame{17, 13, Profile::Rgba};
    frame.rgba.assign(17 * 13 * 4, 255);
    for (int quality = 85; quality <= 95; ++quality)
    {
        const auto name = "jpeg" + std::to_string(quality);
        frame.profile = parse_profile(name);
        EXPECT_EQ(profile_name(frame.profile), name);
        auto packet = encode_frame(frame);
        EXPECT_FALSE(inspect_frame(packet).compressed);
        EXPECT_EQ(decode_frame(packet).rgba.size(), frame.rgba.size());
    }
    EXPECT_THROW(parse_profile("jpeg84"), std::exception);
    EXPECT_THROW(parse_profile("jpeg96"), std::exception);
}
