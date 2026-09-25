#pragma once

#include "model-io/model_loader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace gs::io::detail
{

constexpr uint32_t kSceneMagic = 0x47535343;
constexpr uint32_t kStatusMagic = 0x47535354;
constexpr uint32_t kProtocolVersion = 1;
constexpr size_t kArrayCount = 6;

enum ArrayIndex : size_t
{
    Center,
    Scale,
    Rotation,
    Opacity,
    Rgb0,
    ShRest
};

struct alignas(16) SceneHeader
{
    uint32_t magic = 0;
    uint32_t version = 0;
    uint64_t totalBytes = 0;
    uint64_t count = 0;
    uint8_t shDegree = 0;
    uint8_t sourceFormat = 0;
    uint8_t reserved[6]{};
    double origin[3]{};
    double min[3]{};
    double max[3]{};
    double maxScale = 0;
    uint64_t offsets[kArrayCount]{};
    uint64_t lengths[kArrayCount]{};
};

struct StatusMessage
{
    uint32_t magic = kStatusMagic;
    uint32_t version = kProtocolVersion;
    uint32_t length = sizeof(StatusMessage);
    uint8_t code = 0;
    uint8_t stage = 0;
    uint8_t success = 0;
    uint8_t reserved = 0;
    uint64_t byteOffset = UINT64_MAX;
    char diagnostic[512]{};
};

struct Probe
{
    SourceFormat format;
    uint64_t count;
    uint8_t degree;
    uint64_t inputBytes;
};

inline bool checked_add(uint64_t a, uint64_t b, uint64_t &out)
{
    if (b > UINT64_MAX - a)
        return false;
    out = a + b;
    return true;
}

inline bool checked_mul(uint64_t a, uint64_t b, uint64_t &out)
{
    if (a && b > UINT64_MAX / a)
        return false;
    out = a * b;
    return true;
}

inline std::optional<SceneHeader> make_layout(const Probe &probe, uint64_t limit)
{
    if (!probe.count || probe.degree > 3)
        return std::nullopt;
    SceneHeader header;
    header.magic = kSceneMagic;
    header.version = kProtocolVersion;
    header.count = probe.count;
    header.shDegree = probe.degree;
    header.sourceFormat = static_cast<uint8_t>(probe.format);
    const uint64_t rest = 3ull * ((probe.degree + 1) * (probe.degree + 1) - 1);
    const std::array<uint64_t, kArrayCount> widths{3, 3, 4, 1, 3, rest};
    uint64_t offset = sizeof(SceneHeader);
    for (size_t i = 0; i < kArrayCount; ++i)
    {
        if (!checked_add(offset, 15, offset))
            return std::nullopt;
        offset &= ~15ull;
        uint64_t elements, bytes;
        if (!checked_mul(probe.count, widths[i], elements) ||
            !checked_mul(elements, sizeof(float), bytes))
            return std::nullopt;
        header.offsets[i] = offset;
        header.lengths[i] = elements;
        if (!checked_add(offset, bytes, offset) || offset > limit)
            return std::nullopt;
    }
    header.totalBytes = offset;
    return header;
}

inline LoadError error(LoadErrorCode code, LoadStage stage, std::string diagnostic = {})
{
    diagnostic.resize(std::min<size_t>(diagnostic.size(), 512));
    return {code, stage, std::nullopt, std::move(diagnostic)};
}

} // namespace gs::io::detail
