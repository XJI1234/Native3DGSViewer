#include "native3dgs/measurement.h"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

namespace
{
using namespace gs::engine;
const std::string metric = R"({"schema_version":1,
"frame":{"source":"ply_local","target":"local_enu","units":"meters",
"matrix_convention":"row_serialized_column_vector_left_multiply"},
"quality":{"status":"estimated"},"validation":{"status":"not_measured"},
"meters_per_model_unit":2,
"enu_from_ply":[[0,-2,0,17],[2,0,0,-3],[0,0,2,91],[0,0,0,1]]})";
TEST(EngineMetric, AcceptsArbitraryScaleWithoutModelIdentity)
{
    auto result = parse_metric(metric);
    ASSERT_TRUE(std::holds_alternative<MetricCalibration>(result));
    auto calibration = std::get<MetricCalibration>(result);
    EXPECT_EQ(calibration.meters_per_model_unit, 2);
    EXPECT_TRUE(calibration.estimated);
    EXPECT_FALSE(calibration.independently_validated);
    EXPECT_EQ(distance_meters({0, 0, 0}, {3, 4, 0}, calibration.meters_per_model_unit), 10);
    auto foreign = metric;
    foreign.insert(1, R"("model":{"file":"unrelated.spz","sha256":"wrong","bytes":1},)");
    EXPECT_TRUE(std::holds_alternative<MetricCalibration>(parse_metric(foreign)));
}
TEST(EngineMetric, RejectsMalformedUnavailableAndConflictingTransforms)
{
    EXPECT_EQ(std::get<MeasurementError>(parse_metric("{")), MeasurementError::InvalidJson);
    auto change = [](std::string from, std::string to) {
        auto text = metric;
        text.replace(text.find(from), from.size(), to);
        return parse_metric(text);
    };
    EXPECT_EQ(std::get<MeasurementError>(change("estimated", "unreliable")),
              MeasurementError::UnavailableCalibration);
    EXPECT_EQ(std::get<MeasurementError>(change("unit\":2", "unit\":0")),
              MeasurementError::InvalidTransform);
    EXPECT_EQ(std::get<MeasurementError>(change("[2,0,0,-3]", "[3,0,0,-3]")),
              MeasurementError::InvalidTransform);
    EXPECT_EQ(std::get<MeasurementError>(change("[0,0,2,91]", "[0,0,-2,91]")),
              MeasurementError::InvalidTransform);
    EXPECT_EQ(std::get<MeasurementError>(change("version\":1", "version\":9")),
              MeasurementError::UnsupportedSchema);
    EXPECT_EQ(std::get<MeasurementError>(parse_metric(std::string(1048577, ' '))),
              MeasurementError::ResourceLimit);
    EXPECT_EQ(std::get<MeasurementError>(
                  change("\"schema_version\":1", "\"schema_version\":1,\"schema_version\":1")),
              MeasurementError::InvalidJson);
}
TEST(EngineMetric, ChecksInverseAndFiniteDistanceAndRestoresSourceCoordinates)
{
    auto inverse = metric;
    inverse.insert(1, R"("ply_from_enu":[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]],)");
    EXPECT_EQ(std::get<MeasurementError>(parse_metric(inverse)),
              MeasurementError::InvalidTransform);
    EXPECT_EQ(distance_meters({1, 2, 3}, {1, 2, 3}, 2), 0);
    EXPECT_FALSE(distance_meters({0, 0, 0}, {1, 2, 3}, -1));
    EXPECT_FALSE(distance_meters({0, 0, 0}, {INFINITY, 2, 3}, 2));
    EXPECT_FALSE(distance_meters({0, 0, 0}, {1e308, 1e308, 0}, 1e308));
    auto p = source_from_rub({1, -2, -3}, gs::io::Coordinates::Rdf);
    EXPECT_EQ(p.x, 1);
    EXPECT_EQ(p.y, 2);
    EXPECT_EQ(p.z, 3);
    p = source_from_rub(p, gs::io::Coordinates::Rub);
    EXPECT_EQ(p.y, 2);
    EXPECT_EQ(p.z, 3);
}
TEST(EngineMetric, BoundsNestingEscapedDuplicatesAndCancelledReads)
{
    auto deep = metric;
    deep.insert(1, "\"extra\":" + std::string(64, '[') + "0" + std::string(64, ']') + ",");
    EXPECT_EQ(std::get<MeasurementError>(parse_metric(deep)), MeasurementError::ResourceLimit);
    auto duplicate = metric;
    duplicate.insert(1, R"("schema_\u0076ersion":1,)");
    EXPECT_EQ(std::get<MeasurementError>(parse_metric(duplicate)), MeasurementError::InvalidJson);
    auto inverse = metric;
    inverse.insert(1,
                   R"("ply_from_enu":[[0,0.5,0,1.5],[-0.5,0,0,8.5],[0,0,0.5,-45.5],[0,0,0,1]],)");
    EXPECT_TRUE(std::holds_alternative<MetricCalibration>(parse_metric(inverse)));
    std::stop_source stop;
    stop.request_stop();
    EXPECT_EQ(std::get<MeasurementError>(load_metric(L"unused.json", stop.get_token())),
              MeasurementError::Cancelled);
}
} // namespace
