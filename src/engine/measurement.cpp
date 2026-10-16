#include "native3dgs/measurement.h"
#include <Windows.h>
#include <roapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace gs::engine
{
namespace
{
using namespace winrt::Windows::Data::Json;
// JSON is parsed by Windows; this preflight bounds nesting and rejects duplicate keys.
void preflight(std::string_view text)
{
    struct Level
    {
        bool object;
        std::unordered_set<std::string> keys;
    };
    std::vector<Level> levels;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '"')
        {
            const size_t start = i++;
            for (; i < text.size() && text[i] != '"'; ++i)
                if (text[i] == '\\')
                    ++i;
            if (i >= text.size())
                throw MeasurementError::InvalidJson;
            size_t next = text.find_first_not_of(" \r\n\t", i + 1);
            if (next != std::string_view::npos && text[next] == ':' && !levels.empty() &&
                levels.back().object)
            {
                auto key = winrt::to_string(
                    JsonValue::Parse(winrt::to_hstring(text.substr(start, i - start + 1)))
                        .GetString());
                if (!levels.back().keys.insert(std::move(key)).second)
                    throw MeasurementError::InvalidJson;
            }
        }
        else if (c == '{' || c == '[')
        {
            if (levels.size() == 64)
                throw MeasurementError::ResourceLimit;
            levels.push_back({c == '{', {}});
        }
        else if (c == '}' || c == ']')
        {
            if (levels.empty() || levels.back().object != (c == '}'))
                throw MeasurementError::InvalidJson;
            levels.pop_back();
        }
    }
    if (!levels.empty())
        throw MeasurementError::InvalidJson;
}
std::array<double, 16> matrix(const JsonArray &rows)
{
    if (rows.Size() != 4)
        throw MeasurementError::InvalidTransform;
    std::array<double, 16> m{};
    for (uint32_t r = 0; r < 4; ++r)
    {
        auto row = rows.GetArrayAt(r);
        if (row.Size() != 4)
            throw MeasurementError::InvalidTransform;
        for (uint32_t c = 0; c < 4; ++c)
        {
            m[r * 4 + c] = row.GetNumberAt(c);
            if (!std::isfinite(m[r * 4 + c]))
                throw MeasurementError::InvalidTransform;
        }
    }
    for (int c = 0; c < 4; ++c)
        if (std::abs(m[12 + c] - (c == 3 ? 1 : 0)) > 1e-8)
            throw MeasurementError::InvalidTransform;
    return m;
}
void validate_transform(const MetricCalibration &metric)
{
    const auto &m = metric.enu_from_source;
    const double s = metric.meters_per_model_unit;
    if (!(s > 0) || !std::isfinite(s))
        throw MeasurementError::InvalidTransform;
    double n[9]{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            n[r * 3 + c] = m[r * 4 + c] / s;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
        {
            double dot = 0;
            for (int k = 0; k < 3; ++k)
                dot += n[k * 3 + r] * n[k * 3 + c];
            if (!std::isfinite(dot) || std::abs(dot - (r == c ? 1 : 0)) > 1e-5)
                throw MeasurementError::InvalidTransform;
        }
    double det = n[0] * (n[4] * n[8] - n[5] * n[7]) - n[1] * (n[3] * n[8] - n[5] * n[6]) +
                 n[2] * (n[3] * n[7] - n[4] * n[6]);
    if (!(det > 0))
        throw MeasurementError::InvalidTransform;
}
} // namespace
MetricResult parse_metric(std::string_view json)
{
    if (json.size() > 1024 * 1024)
        return MeasurementError::ResourceLimit;
    // RoInitialize may report an existing STA; Windows JSON supports either apartment.
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    struct Apartment
    {
        bool owned;
        ~Apartment()
        {
            if (owned)
            {
                winrt::clear_factory_cache();
                RoUninitialize();
            }
        }
    } apartment{SUCCEEDED(initialized)};
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return MeasurementError::InvalidJson;
    try
    {
        preflight(json);
        auto object = JsonObject::Parse(winrt::to_hstring(json));
        if (object.GetNamedNumber(L"schema_version") != 1)
            return MeasurementError::UnsupportedSchema;
        auto frame = object.GetNamedObject(L"frame");
        if (frame.GetNamedString(L"source") != L"ply_local" ||
            frame.GetNamedString(L"target") != L"local_enu" ||
            frame.GetNamedString(L"units") != L"meters" ||
            frame.GetNamedString(L"matrix_convention") !=
                L"row_serialized_column_vector_left_multiply")
            return MeasurementError::UnsupportedSchema;
        auto status = object.GetNamedObject(L"quality").GetNamedString(L"status");
        if (status != L"estimated" && status != L"validated")
            return MeasurementError::UnavailableCalibration;
        MetricCalibration metric;
        metric.estimated = status == L"estimated";
        metric.meters_per_model_unit = object.GetNamedNumber(L"meters_per_model_unit");
        metric.enu_from_source = matrix(object.GetNamedArray(L"enu_from_ply"));
        validate_transform(metric);
        if (object.HasKey(L"ply_from_enu"))
        {
            auto inverse = matrix(object.GetNamedArray(L"ply_from_enu"));
            for (int direction = 0; direction < 2; ++direction)
            {
                const auto &a = direction ? inverse : metric.enu_from_source;
                const auto &b = direction ? metric.enu_from_source : inverse;
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                    {
                        double product = 0;
                        for (int k = 0; k < 4; ++k)
                            product += a[r * 4 + k] * b[k * 4 + c];
                        if (!std::isfinite(product) || std::abs(product - (r == c ? 1 : 0)) > 1e-5)
                            return MeasurementError::InvalidTransform;
                    }
            }
        }
        if (object.HasKey(L"validation"))
        {
            auto validation = object.GetNamedObject(L"validation");
            metric.independently_validated =
                !metric.estimated &&
                validation.GetNamedString(L"status", L"not_measured") == L"validated" &&
                validation.HasKey(L"independent_lengths") &&
                validation.GetNamedArray(L"independent_lengths").Size() > 0;
        }
        return metric;
    }
    catch (MeasurementError error)
    {
        return error;
    }
    catch (const std::bad_alloc &)
    {
        return MeasurementError::ResourceLimit;
    }
    catch (...)
    {
        return MeasurementError::InvalidJson;
    }
}
std::optional<double> distance_meters(Double3 a, Double3 b, double scale)
{
    if (!(scale > 0) || !std::isfinite(scale))
        return {};
    const double length = std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) * scale;
    if (!std::isfinite(length))
        return {};
    return length;
}
MetricResult load_metric(const std::filesystem::path &path, std::stop_token stop)
{
    try
    {
        if (stop.stop_requested())
            return MeasurementError::Cancelled;
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(path, ec);
        if (ec)
            return MeasurementError::IoFailure;
        if (bytes > 1024 * 1024)
            return MeasurementError::ResourceLimit;
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return MeasurementError::IoFailure;
        std::string json;
        json.reserve(static_cast<size_t>(bytes));
        std::array<char, 65536> buffer;
        while (input)
        {
            if (stop.stop_requested())
                return MeasurementError::Cancelled;
            input.read(buffer.data(), buffer.size());
            json.append(buffer.data(), static_cast<size_t>(input.gcount()));
            if (json.size() > 1024 * 1024)
                return MeasurementError::ResourceLimit;
        }
        if (!input.eof())
            return MeasurementError::IoFailure;
        if (stop.stop_requested())
            return MeasurementError::Cancelled;
        return parse_metric(json);
    }
    catch (const std::bad_alloc &)
    {
        return MeasurementError::ResourceLimit;
    }
    catch (...)
    {
        return MeasurementError::IoFailure;
    }
}
Double3 source_from_rub(Double3 point, io::Coordinates coordinates)
{
    return coordinates == io::Coordinates::Rdf ? Double3{point.x, -point.y, -point.z} : point;
}
} // namespace gs::engine
