#pragma once

#include "../common/probe.h"

#include <cstdio>
#include <optional>

namespace gs::io::detail
{

std::optional<LoadError> decode_ply(FILE *stream, const ProbeResult &probe, Coordinates coordinates,
                                    SceneHeader *output);
std::optional<LoadError> decode_spz(std::span<const uint8_t> input, const ProbeResult &probe,
                                    SceneHeader *output);

} // namespace gs::io::detail
