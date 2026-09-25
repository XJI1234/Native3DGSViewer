#pragma once

#include "protocol.h"

#include <span>
#include <string>
#include <vector>

namespace gs::io::detail
{

struct PlyProperty
{
    std::string name;
    uint32_t offset;
    uint32_t bytes;
    bool isFloat;
};

struct PlyLayout
{
    uint64_t vertexOffset = 0;
    uint64_t stride = 0;
    std::vector<PlyProperty> properties;
};

struct ProbeResult
{
    Probe probe{};
    PlyLayout ply;
    LoadError failure = error(LoadErrorCode::InvalidHeader, LoadStage::Inspecting);
    bool ok = false;
};

ProbeResult probe_file(std::span<const uint8_t> prefix, uint64_t fileBytes,
                       const LoadLimits &limits);

} // namespace gs::io::detail
