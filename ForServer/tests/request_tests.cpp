#include "gs_server/request.h"
#include "gs_server/viewer_controls.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace gs::server;
TEST(FrameRequest, RoundTripAndStrictValidation)
{
    FrameRequest request;
    request.model_id = "m-0123456789abcdef";
    request.yaw = -67.5;
    request.pitch = 88;
    request.zoom = 0.15;
    const auto parsed = parse_request(format_request(request));
    EXPECT_EQ(parsed.model_id, request.model_id);
    EXPECT_EQ(parsed.yaw, request.yaw);
    EXPECT_EQ(parsed.zoom, request.zoom);
    for (const auto *body :
         {"NGSREQ1 640 360 0 rgba", "NGSREQ2 -1 m-1 640 360 0 0 1 rgba",
          "NGSREQ2 1 ../file 640 360 0 0 1 rgba", "NGSREQ2 1 m-1 4097 360 0 0 1 rgba",
          "NGSREQ2 1 m-1 640 360 nan 0 1 rgba", "NGSREQ2 1 m-1 640 360 0 90 1 rgba",
          "NGSREQ2 1 m-1 640 360 0 0 0 rgba", "NGSREQ2 1 m-1 640 360 0 0 1 f32",
          "NGSREQ2 1 m-1 640 360 0 0 1 rgba trailing"})
        EXPECT_THROW(parse_request(body), std::exception);
}
TEST(FrameRequest, SphericalGridFullCircleAndIndependentFlip)
{
    FrameRequest request{1, "m-grid", 640, 360, 359.9, 100, 1, Profile::Rgba};
    auto canonical = canonical_request(request);
    EXPECT_EQ(canonical.yaw, 180);
    EXPECT_EQ(canonical.pitch, 80);
    EXPECT_TRUE(canonical.flip_y);
    EXPECT_EQ(view_code(request), "a090-t05-d40");
    request.flip_y = true;
    EXPECT_EQ(view_code(request), view_code(canonical));
    EXPECT_NE(view_key(request), view_key(canonical));
    request.pitch = 90;
    EXPECT_EQ(canonical_request(request).pitch, 90);
    request.zoom = 0.1;
    EXPECT_EQ(canonical_request(request).zoom, 0.1);
    request.zoom = 10;
    EXPECT_EQ(canonical_request(request).zoom, 10);
    EXPECT_EQ(predicted_views(request).size(), 20);
    EXPECT_EQ(predicted_views(request, 10).size(), 40);
    EXPECT_EQ(predicted_views(request, 15).size(), 60);
    EXPECT_THROW(predicted_views(request, 2), std::exception);
    EXPECT_EQ(parse_request(format_request(request)).flip_y, true);
    EXPECT_THROW(parse_request("NGSREQ3 1 m-grid 640 360 0 90 1 rgba 2"), std::exception);
}
TEST(ViewerControls, ScreenDragDirectionSurvivesFlipAndPoleCrossing)
{
    FrameRequest view{1, "m-grid", 640, 360, 0, 14, 1, Profile::Jpeg85};
    auto moved = navigate_view(view, 2, 2);
    EXPECT_EQ(moved.yaw, -2);
    EXPECT_EQ(moved.pitch, 16);
    view.flip_y = true;
    moved = navigate_view(view, 2, 2);
    EXPECT_EQ(moved.yaw, 2);
    EXPECT_EQ(moved.pitch, 12);
    view.pitch = 100;
    moved = navigate_view(view, 2, 2);
    EXPECT_EQ(moved.yaw, -2);
    EXPECT_EQ(moved.pitch, 98);
    EXPECT_EQ(preview_depth(0), 5);
    EXPECT_EQ(preview_depth(1), 10);
    EXPECT_EQ(preview_depth(2), 15);
    EXPECT_THROW(navigate_view(view, NAN, 0), std::exception);
}
TEST(ViewerControls, LeaseHealthToleratesTransientFailureAndRecovers)
{
    LeaseHealth health;
    const auto start = LeaseHealth::Clock::now();
    health.success(start);
    EXPECT_FALSE(health.failure(start + std::chrono::seconds(10)));
    EXPECT_FALSE(health.failure(start + std::chrono::seconds(20)));
    EXPECT_TRUE(health.failure(start + std::chrono::seconds(30)));
    health.success(start + std::chrono::seconds(31));
    EXPECT_FALSE(health.failure(start + std::chrono::seconds(100)));
}
