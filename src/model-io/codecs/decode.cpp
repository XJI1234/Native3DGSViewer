#include "decode.h"

#include "../normalize/normalize.h"

#include "load-spz.h"
#include "miniply.h"

#include <array>
#include <cmath>
#include <vector>

namespace gs::io::detail
{
namespace
{

LoadError failed(LoadErrorCode code, std::string reason)
{
    return error(code, LoadStage::Decoding, std::move(reason));
}

} // namespace

std::optional<LoadError> decode_ply(FILE *stream, const ProbeResult &probe, Coordinates coordinates,
                                    SceneHeader *output)
{
    miniply::PLYReader reader(stream); // Owns stream.
    if (!reader.valid() || reader.file_type() != miniply::PLYFileType::Binary)
        return failed(LoadErrorCode::InvalidHeader, "miniply rejected header");
    while (reader.has_element() && !reader.element_is("vertex"))
        reader.next_element();
    if (!reader.has_element() || reader.num_rows() != probe.probe.count || !reader.load_element())
        return failed(LoadErrorCode::TruncatedData, "miniply vertex read");
    const uint32_t rest = 3 * ((probe.probe.degree + 1) * (probe.probe.degree + 1) - 1);
    std::array<const char *, 14> base{"x",       "y",      "z",      "scale_0", "scale_1",
                                      "scale_2", "rot_1",  "rot_2",  "rot_3",   "rot_0",
                                      "opacity", "f_dc_0", "f_dc_1", "f_dc_2"};
    std::vector<uint32_t> indexes;
    indexes.reserve(base.size() + rest);
    for (const char *name : base)
    {
        const uint32_t index = reader.find_property(name);
        if (index == miniply::kInvalidIndex)
            return failed(LoadErrorCode::InvalidHeader, "Missing vertex property");
        indexes.push_back(index);
    }
    std::vector<std::string> restNames;
    restNames.reserve(rest);
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        for (uint32_t coefficient = 0; coefficient < rest / 3; ++coefficient)
        {
            restNames.push_back("f_rest_" + std::to_string(channel * (rest / 3) + coefficient));
        }
    }
    for (const auto &name : restNames)
    {
        const uint32_t index = reader.find_property(name.c_str());
        if (index == miniply::kInvalidIndex)
            return failed(LoadErrorCode::InvalidHeader, "Missing SH property");
        indexes.push_back(index);
    }
    const size_t width = indexes.size();
    std::vector<float> rows(probe.probe.count * width);
    if (!reader.extract_properties(indexes.data(), static_cast<uint32_t>(width),
                                   miniply::PLYPropertyType::Float, rows.data()))
        return failed(LoadErrorCode::DecoderFailure, "miniply extract");
    SceneWriter writer(output, coordinates == Coordinates::Rdf);
    std::vector<float> sh(rest);
    for (uint64_t i = 0; i < probe.probe.count; ++i)
    {
        const float *row = rows.data() + i * width;
        for (uint32_t coefficient = 0; coefficient < rest / 3; ++coefficient)
        {
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                sh[coefficient * 3 + channel] = row[14 + channel * (rest / 3) + coefficient];
            }
        }
        RawSplat raw{row, row + 3, row + 6, row[10], row + 11, sh.data()};
        if (!writer.write(i, raw))
            return failed(LoadErrorCode::InvalidAttribute, "Non-finite or invalid PLY splat");
    }
    if (!writer.finish())
        return failed(LoadErrorCode::InvalidAttribute, "PLY bounds");
    return std::nullopt;
}

std::optional<LoadError> decode_spz(std::span<const uint8_t> input, const ProbeResult &probe,
                                    SceneHeader *output)
{
    spz::UnpackOptions options;
    options.to = spz::CoordinateSystem::RUB;
    auto cloud = spz::loadSpz(input.data(), input.size(), options);
    const uint64_t n = probe.probe.count;
    const uint64_t rest = 3ull * ((probe.probe.degree + 1) * (probe.probe.degree + 1) - 1);
    if (cloud.numPoints != n || cloud.shDegree != probe.probe.degree || cloud.antialiased ||
        cloud.positions.size() != 3 * n || cloud.scales.size() != 3 * n ||
        cloud.rotations.size() != 4 * n || cloud.alphas.size() != n ||
        cloud.colors.size() != 3 * n || cloud.sh.size() != rest * n)
        return failed(LoadErrorCode::DecoderFailure, "SPZ cloud length mismatch");
    SceneWriter writer(output, false);
    for (uint64_t i = 0; i < n; ++i)
    {
        RawSplat raw{cloud.positions.data() + 3 * i,
                     cloud.scales.data() + 3 * i,
                     cloud.rotations.data() + 4 * i,
                     cloud.alphas[i],
                     cloud.colors.data() + 3 * i,
                     rest ? cloud.sh.data() + rest * i : nullptr,
                     true};
        if (!writer.write(i, raw))
        {
            auto reason = failed(LoadErrorCode::InvalidAttribute,
                                 "Non-finite or invalid SPZ splat at index " + std::to_string(i));
            if (!std::isfinite(raw.logitAlpha))
                reason.diagnostic += " (alpha logit)";
            else if (!std::isfinite(raw.position[0]) || !std::isfinite(raw.position[1]) ||
                     !std::isfinite(raw.position[2]))
                reason.diagnostic += " (position)";
            else if (!std::isfinite(raw.logScale[0]) || !std::isfinite(raw.logScale[1]) ||
                     !std::isfinite(raw.logScale[2]))
                reason.diagnostic += " (scale)";
            else if (!std::isfinite(raw.rotationXyzw[0]) || !std::isfinite(raw.rotationXyzw[1]) ||
                     !std::isfinite(raw.rotationXyzw[2]) || !std::isfinite(raw.rotationXyzw[3]))
                reason.diagnostic += " (rotation)";
            return reason;
        }
    }
    if (!writer.finish())
        return failed(LoadErrorCode::InvalidAttribute, "SPZ bounds");
    return std::nullopt;
}

} // namespace gs::io::detail
