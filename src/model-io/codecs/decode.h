#pragma once

#include "../common/probe.h"

#include <cstdio>
#include <functional>
#include <optional>

namespace gs::io::detail
{

std::optional<LoadError> decode_ply(FILE *stream, const ProbeResult &probe, Coordinates coordinates,
                                    SceneHeader *output,
                                    const std::function<bool()> &cancelled = {},
                                    const std::function<void(uint64_t)> &progress = {});
std::optional<LoadError> decode_spz(std::span<const uint8_t> input, const ProbeResult &probe,
                                    SceneHeader *output,
                                    const std::function<bool()> &cancelled = {});

} // namespace gs::io::detail
