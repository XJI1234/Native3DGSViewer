#include "decode.h"

#include "../normalize/normalize.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif
#include "load-spz.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
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
                                    SceneHeader *output, const std::function<bool()> &cancelled,
                                    const std::function<void(uint64_t)> &progress)
{
    std::unique_ptr<FILE, decltype(&fclose)> file(stream, fclose);
#ifdef _WIN32
    const int seek_result = _fseeki64(stream, static_cast<__int64>(probe.ply.vertexOffset), SEEK_SET);
#else
    const int seek_result = fseeko(stream, static_cast<off_t>(probe.ply.vertexOffset), SEEK_SET);
#endif
    if (seek_result)
        return failed(LoadErrorCode::IoFailure, "PLY vertex seek");
    const uint32_t rest = 3 * ((probe.probe.degree + 1) * (probe.probe.degree + 1) - 1);
    std::array<const char *, 14> base{"x",       "y",      "z",      "scale_0", "scale_1",
                                      "scale_2", "rot_1",  "rot_2",  "rot_3",   "rot_0",
                                      "opacity", "f_dc_0", "f_dc_1", "f_dc_2"};
    std::vector<uint32_t> offsets;
    offsets.reserve(base.size() + rest);
    const auto property_offset = [&](const char *name) -> std::optional<uint32_t> {
        const auto it = std::find_if(probe.ply.properties.begin(), probe.ply.properties.end(),
                                     [name](const PlyProperty &property) {
                                         return property.name == name && property.isFloat;
                                     });
        if (it == probe.ply.properties.end())
            return std::nullopt;
        return it->offset;
    };
    for (const char *name : base)
    {
        auto offset = property_offset(name);
        if (!offset)
            return failed(LoadErrorCode::InvalidHeader, "Missing vertex property");
        offsets.push_back(*offset);
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
        auto offset = property_offset(name.c_str());
        if (!offset)
            return failed(LoadErrorCode::InvalidHeader, "Missing SH property");
        offsets.push_back(*offset);
    }
    constexpr size_t chunkBytes = 4ull << 20;
    if (!probe.ply.stride || probe.ply.stride > (64ull << 20))
        return failed(LoadErrorCode::ResourceLimit, "PLY vertex stride");
    const size_t chunkRows = (std::max)(size_t{1}, chunkBytes / probe.ply.stride);
    std::vector<uint8_t> bytes(chunkRows * probe.ply.stride);
    SceneWriter writer(output, coordinates == Coordinates::Rdf);
    std::vector<float> sh(rest);
    for (uint64_t start = 0; start < probe.probe.count; start += chunkRows)
    {
        if (cancelled && cancelled())
            return failed(LoadErrorCode::Cancelled, "PLY decode cancelled");
        const size_t count = static_cast<size_t>((std::min)(uint64_t(chunkRows),
                                                              probe.probe.count - start));
        const size_t size = count * probe.ply.stride;
        const size_t read = fread(bytes.data(), 1, size, stream);
        if (read != size)
        {
            auto reason = failed(ferror(stream) ? LoadErrorCode::IoFailure
                                                : LoadErrorCode::TruncatedData,
                                 "PLY vertex read");
            reason.byteOffset = probe.ply.vertexOffset + start * probe.ply.stride + read;
            return reason;
        }
        for (size_t item = 0; item < count; ++item)
        {
            const uint8_t *row = bytes.data() + item * probe.ply.stride;
            std::array<float, 14> values{};
            for (size_t j = 0; j < values.size(); ++j)
                std::memcpy(&values[j], row + offsets[j], sizeof(float));
            for (uint32_t coefficient = 0; coefficient < rest / 3; ++coefficient)
            {
                for (uint32_t channel = 0; channel < 3; ++channel)
                    std::memcpy(&sh[coefficient * 3 + channel],
                                row + offsets[14 + channel * (rest / 3) + coefficient],
                                sizeof(float));
            }
            RawSplat raw{values.data(), values.data() + 3, values.data() + 6,
                         values[10], values.data() + 11, sh.data()};
            SplatWriteError problem{};
            if (!writer.write(start + item, raw, &problem))
            {
                size_t property = 0;
                std::string attribute;
                switch (problem.attribute)
                {
                case SplatAttribute::Position: property = problem.component; break;
                case SplatAttribute::Scale: property = 3 + problem.component; break;
                case SplatAttribute::Rotation:
                    property = 6 + (problem.component == UINT32_MAX ? 0 : problem.component);
                    if (problem.component == UINT32_MAX)
                        attribute = "rotation";
                    break;
                case SplatAttribute::Opacity: property = 10; break;
                case SplatAttribute::Dc: property = 11 + problem.component; break;
                case SplatAttribute::Sh:
                    property = 14 + (problem.component % 3) * (rest / 3) +
                               problem.component / 3;
                    break;
                }
                if (attribute.empty())
                    attribute = property < base.size() ? base[property]
                                                       : restNames[property - base.size()];
                auto reason = failed(LoadErrorCode::InvalidAttribute,
                                     "Invalid PLY splat at index " + std::to_string(start + item) +
                                         " (" + attribute + ")");
                reason.byteOffset = probe.ply.vertexOffset +
                                    (start + item) * probe.ply.stride + offsets[property];
                return reason;
            }
        }
        if (progress)
            progress(probe.ply.vertexOffset + (start + count) * probe.ply.stride);
    }
    if (!writer.finish())
        return failed(LoadErrorCode::InvalidAttribute, "PLY bounds");
    return std::nullopt;
}

std::optional<LoadError> decode_spz(std::span<const uint8_t> input, const ProbeResult &probe,
                                    SceneHeader *output, const std::function<bool()> &cancelled)
{
    if (cancelled && cancelled())
        return failed(LoadErrorCode::Cancelled, "SPZ decode cancelled");
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
        if ((i & 1023) == 0 && cancelled && cancelled())
            return failed(LoadErrorCode::Cancelled, "SPZ decode cancelled");
        RawSplat raw{cloud.positions.data() + 3 * i,
                     cloud.scales.data() + 3 * i,
                     cloud.rotations.data() + 4 * i,
                     cloud.alphas[i],
                     cloud.colors.data() + 3 * i,
                     rest ? cloud.sh.data() + rest * i : nullptr};
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
