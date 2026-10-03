#include "gs_server/windows_client.h"
#include <Windows.h>
#include <gtest/gtest.h>

using namespace gs::server;
TEST(WindowsThinRenderer, ImageUploadExactAndRecoverAfterInvalidLayout)
{
    ThinRenderer renderer;
    Frame image{17, 13, Profile::Rgba};
    image.rgba.resize(17 * 13 * 4);
    for (size_t index = 0; index < image.rgba.size(); ++index)
        image.rgba[index] = index % 4 == 3 ? 255 : uint8_t(index * 37);
    for (int repeat = 0; repeat < 5; ++repeat)
        EXPECT_EQ(renderer.render(image).rgba, image.rgba);
    auto invalid = image;
    invalid.rgba.clear();
    EXPECT_THROW(renderer.render(invalid), std::exception);
    EXPECT_EQ(renderer.render(image).rgba, image.rgba);
}
TEST(WindowsThinRenderer, AllocationFailureRecoversAndSubmittedFailureRetainsResources)
{
    Frame frame{17, 13, Profile::Rgba};
    frame.rgba.assign(17 * 13 * 4, 255);
    {
        ThinRenderer renderer;
        SetEnvironmentVariableW(L"GS_THIN_TEST_FAILURE", L"allocation");
        EXPECT_THROW(renderer.render(frame), std::exception);
        SetEnvironmentVariableW(L"GS_THIN_TEST_FAILURE", nullptr);
        EXPECT_EQ(renderer.render(frame).rgba, frame.rgba);
        SetEnvironmentVariableW(L"GS_THIN_TEST_FAILURE", L"submitted");
        EXPECT_THROW(renderer.render(frame), std::exception);
        SetEnvironmentVariableW(L"GS_THIN_TEST_FAILURE", nullptr);
        EXPECT_THROW(renderer.render(frame), std::exception);
    }
    ThinRenderer recovered;
    EXPECT_EQ(recovered.render(frame).rgba, frame.rgba);
}
