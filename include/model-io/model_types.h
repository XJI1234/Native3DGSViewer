#pragma once

#include "splat-types/scene.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace gs::io
{

enum class Coordinates : uint8_t { Rdf, Rub };
enum class LoadStage : uint8_t { Opening, Inspecting, Decoding, Validating, Ready };

struct LoadLimits
{
    uint64_t maxInputBytes = UINT64_MAX;
    uint64_t maxSplats = UINT32_MAX;
    uint64_t maxSceneBytes = UINT64_MAX;
};

struct LoadProgress
{
    LoadStage stage;
    uint64_t bytesRead;
    std::optional<uint64_t> totalBytes;
};

enum class LoadErrorCode : uint8_t
{
    NotFound, AccessDenied, IoFailure, UnsupportedFormat, UnsupportedVersion,
    UnsupportedFeature, InvalidHeader, TruncatedData, InvalidAttribute, EmptyScene,
    ResourceLimit, OutOfMemory, Cancelled, Timeout, DecoderFailure, DecoderCrashed,
    ObserverFailure
};

struct LoadError
{
    LoadErrorCode code;
    LoadStage stage;
    std::optional<uint64_t> byteOffset;
    std::string diagnostic;
};

using LoadResult = std::variant<gs::SceneHandle, LoadError>;
using ProgressSink = std::function<void(const LoadProgress &)>;

} // namespace gs::io
