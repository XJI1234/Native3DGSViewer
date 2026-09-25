#pragma once

#include "model-io/model_loader.h"

#include <chrono>

namespace gs::io::detail
{

struct LoaderTestOptions
{
    std::filesystem::path helperPath;
    std::chrono::milliseconds timeout{180000};
    bool failJobSetup = false;
    uint64_t jobMemoryLimitBytes = 0;
};

std::unique_ptr<IModelLoader> make_model_loader_for_testing(LoaderTestOptions options);

} // namespace gs::io::detail
