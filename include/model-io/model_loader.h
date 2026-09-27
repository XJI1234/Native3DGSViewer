#pragma once

#include "model-io/model_types.h"

#include <filesystem>
#include <stop_token>

namespace gs::io
{

struct LoadRequest
{
    std::filesystem::path path;
    Coordinates plyCoordinates = Coordinates::Rdf;
    LoadLimits limits{};
};

class IModelLoader
{
  public:
    virtual ~IModelLoader() = default;
    virtual LoadResult load(const LoadRequest &, std::stop_token, ProgressSink) = 0;
};

std::unique_ptr<IModelLoader> make_model_loader();

} // namespace gs::io
