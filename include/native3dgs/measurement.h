#pragma once
#include "model-io/model_types.h"
#include <array>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string_view>

namespace gs::engine
{
enum class MeasurementError : uint8_t
{
    InvalidJson,
    ResourceLimit,
    UnsupportedSchema,
    UnavailableCalibration,
    InvalidTransform,
    IoFailure,
    Cancelled,
    NoScene,
    Busy,
    StaleRequest,
    Timeout,
    NoCalibration,
    DeviceUnavailable
};
struct MetricCalibration
{
    double meters_per_model_unit = 0;
    std::array<double, 16> enu_from_source{};
    bool estimated = true;
    bool independently_validated = false;
};
using MetricResult = std::variant<MetricCalibration, MeasurementError>;
// UTF-8 v1 metric JSON. Model names, sizes and hashes are intentionally not checked.
MetricResult parse_metric(std::string_view json);
// Blocking file I/O; use on a worker thread. Reading and parsing are bounded to 1 MiB.
MetricResult load_metric(const std::filesystem::path &, std::stop_token = {});
std::optional<double> distance_meters(Double3 a_source, Double3 b_source, double scale);
Double3 source_from_rub(Double3, io::Coordinates);
struct MeasurementPoint
{
    Double3 position_rub{}, position_source{};
};
struct MeasurementSnapshot
{
    bool enabled = false, importing = false, picking = false, missed = false;
    uint64_t operation = 0;
    std::optional<MetricCalibration> calibration;
    std::optional<MeasurementPoint> a, b;
    std::optional<double> distance_m;
    std::optional<MeasurementError> error;
};
} // namespace gs::engine
