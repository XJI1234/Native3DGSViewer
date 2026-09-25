#include "normalize.h"

#include "splat-types.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace gs::io::detail
{
namespace
{

float *array(SceneHeader *header, ArrayIndex index)
{
    return reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(header) + header->offsets[index]);
}

const float *array(const SceneHeader &header, ArrayIndex index)
{
    return reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(&header) +
                                           header.offsets[index]);
}

uint64_t rest_width(uint8_t degree)
{
    return 3ull * ((degree + 1) * (degree + 1) - 1);
}

bool near(double a, double b)
{
    return std::abs(a - b) <= 1e-6 * std::max({1.0, std::abs(a), std::abs(b)});
}

} // namespace

SceneWriter::SceneWriter(SceneHeader *header, bool rdf) : header_(header), rdf_(rdf)
{
}

bool SceneWriter::write(uint64_t i, const RawSplat &raw)
{
    constexpr double kC0 = 0.28209479177387814;
    const auto flips =
        spz::coordinateConverter(spz::CoordinateSystem::RDF, spz::CoordinateSystem::RUB);
    for (int k = 0; k < 3; ++k)
    {
        if (!std::isfinite(raw.position[k]) || !std::isfinite(raw.logScale[k]) ||
            !std::isfinite(raw.dc[k]))
            return false;
        const double world = double(raw.position[k]) * (rdf_ ? flips.flipP[k] : 1.0);
        array(header_, Center)[3 * i + k] = static_cast<float>(world);
        min_[k] = std::min(min_[k], world);
        max_[k] = std::max(max_[k], world);
        const double scale = std::exp(double(raw.logScale[k]));
        if (!std::isfinite(scale) || scale <= 0 ||
            scale * scale > std::numeric_limits<float>::max() || static_cast<float>(scale) == 0)
            return false;
        array(header_, Scale)[3 * i + k] = static_cast<float>(scale);
        maxScale_ = std::max(maxScale_, scale);
        const double color = 0.5 + kC0 * double(raw.dc[k]);
        if (!std::isfinite(color) || std::abs(color) > std::numeric_limits<float>::max())
            return false;
        array(header_, Rgb0)[3 * i + k] = static_cast<float>(color);
    }
    double norm = 0;
    for (int k = 0; k < 4; ++k)
    {
        if (!std::isfinite(raw.rotationXyzw[k]))
            return false;
        norm += double(raw.rotationXyzw[k]) * raw.rotationXyzw[k];
    }
    if (!std::isfinite(norm) || norm < 1e-20)
        return false;
    norm = std::sqrt(norm);
    for (int k = 0; k < 4; ++k)
    {
        const float flip = rdf_ && k < 3 ? flips.flipQ[k] : 1.0f;
        array(header_, Rotation)[4 * i + k] =
            static_cast<float>(double(raw.rotationXyzw[k]) * flip / norm);
    }
    if (!std::isfinite(raw.logitAlpha) && !(raw.quantizedAlpha && std::isinf(raw.logitAlpha)))
        return false;
    const double alpha = std::isinf(raw.logitAlpha) ? (raw.logitAlpha > 0 ? 1.0 : 0.0)
                         : raw.logitAlpha >= 0 ? 1.0 / (1.0 + std::exp(-double(raw.logitAlpha)))
                                               : std::exp(double(raw.logitAlpha)) /
                                                     (1.0 + std::exp(double(raw.logitAlpha)));
    array(header_, Opacity)[i] = static_cast<float>(alpha);
    const uint64_t n = rest_width(header_->shDegree);
    for (uint64_t j = 0; j < n; ++j)
    {
        if (!std::isfinite(raw.sh[j]))
            return false;
        const float flip = rdf_ ? flips.flipSh[j / 3] : 1.0f;
        array(header_, ShRest)[i * n + j] = raw.sh[j] * flip;
    }
    return true;
}

bool SceneWriter::finish()
{
    for (int k = 0; k < 3; ++k)
    {
        header_->min[k] = min_[k];
        header_->max[k] = max_[k];
        header_->origin[k] = min_[k] + (max_[k] - min_[k]) / 2.0;
        if (!std::isfinite(header_->origin[k]))
            return false;
    }
    header_->maxScale = maxScale_;
    for (uint64_t i = 0; i < header_->count; ++i)
    {
        for (int k = 0; k < 3; ++k)
        {
            const double local = double(array(header_, Center)[3 * i + k]) - header_->origin[k];
            if (!std::isfinite(local) || std::abs(local) > std::numeric_limits<float>::max())
                return false;
            array(header_, Center)[3 * i + k] = static_cast<float>(local);
        }
    }
    return std::isfinite(maxScale_) && maxScale_ > 0;
}

bool validate_scene(const SceneHeader &h, uint64_t mappingBytes)
{
    if (h.magic != kSceneMagic || h.version != kProtocolVersion || h.count == 0 || h.shDegree > 3 ||
        h.sourceFormat > 1 || h.totalBytes != mappingBytes)
        return false;
    Probe probe{static_cast<SourceFormat>(h.sourceFormat), h.count, h.shDegree, 0};
    auto expected = make_layout(probe, mappingBytes);
    if (!expected || expected->totalBytes != h.totalBytes)
        return false;
    for (size_t i = 0; i < kArrayCount; ++i)
    {
        if (h.offsets[i] != expected->offsets[i] || h.lengths[i] != expected->lengths[i])
            return false;
    }
    double min[3]{INFINITY, INFINITY, INFINITY};
    double max[3]{-INFINITY, -INFINITY, -INFINITY};
    double maxScale = 0;
    for (uint64_t i = 0; i < h.count; ++i)
    {
        double norm = 0;
        for (int k = 0; k < 3; ++k)
        {
            const float local = array(h, Center)[3 * i + k];
            const float scale = array(h, Scale)[3 * i + k];
            const float color = array(h, Rgb0)[3 * i + k];
            if (!std::isfinite(local) || !std::isfinite(scale) || scale <= 0 ||
                double(scale) * scale > std::numeric_limits<float>::max() || !std::isfinite(color))
                return false;
            const double world = double(local) + h.origin[k];
            min[k] = std::min(min[k], world);
            max[k] = std::max(max[k], world);
            maxScale = std::max(maxScale, double(scale));
        }
        for (int k = 0; k < 4; ++k)
        {
            const float q = array(h, Rotation)[4 * i + k];
            if (!std::isfinite(q))
                return false;
            norm += double(q) * q;
        }
        if (!near(norm, 1.0))
            return false;
        const float alpha = array(h, Opacity)[i];
        if (!std::isfinite(alpha) || alpha < 0 || alpha > 1)
            return false;
    }
    for (uint64_t i = 0; i < h.lengths[ShRest]; ++i)
    {
        if (!std::isfinite(array(h, ShRest)[i]))
            return false;
    }
    for (int k = 0; k < 3; ++k)
    {
        if (!std::isfinite(h.origin[k]) || !near(min[k], h.min[k]) || !near(max[k], h.max[k]))
            return false;
    }
    return near(maxScale, h.maxScale);
}

} // namespace gs::io::detail
