#include "../../src/model-io/codecs/decode.h"
#include "../../src/model-io/normalize/normalize.h"
#include "gs_server/renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/mman.h>

namespace gs::server
{
SceneHandle load_scene(const std::string &path)
{
    using namespace gs::io;
    using namespace gs::io::detail;
    const auto file_bytes = std::filesystem::file_size(path);
    if (file_bytes > (8ull << 30))
        throw std::runtime_error("Model input exceeds 8 GiB limit");
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> prefix(size_t(std::min<uint64_t>(file_bytes, 65536)));
    if (!input.read(reinterpret_cast<char *>(prefix.data()), std::streamsize(prefix.size())))
        throw std::runtime_error("Model prefix read failed");
    LoadLimits limits{8ull << 30, 32'000'000, 8ull << 30};
    const auto probe = probe_file(prefix, file_bytes, limits);
    if (!probe.ok)
        throw std::runtime_error(probe.failure.diagnostic);
    const auto layout = make_layout(probe.probe, limits.maxSceneBytes);
    if (!layout)
        throw std::runtime_error("Model layout exceeds limit");
    void *memory = mmap(nullptr, size_t(layout->totalBytes), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED)
        throw std::runtime_error("Model allocation failed");
    std::shared_ptr<void> owner(
        memory, [size = size_t(layout->totalBytes)](void *address) { munmap(address, size); });
    auto *header = static_cast<SceneHeader *>(memory);
    std::memcpy(header, &*layout, sizeof(*header));
    std::optional<LoadError> failure;
    if (probe.probe.format == SourceFormat::Ply)
    {
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
            throw std::runtime_error("PLY open failed");
        failure = decode_ply(file, probe, Coordinates::Rdf, header);
    }
    else
    {
        input.clear();
        input.seekg(0);
        std::vector<uint8_t> compressed(static_cast<size_t>(file_bytes));
        if (!input.read(reinterpret_cast<char *>(compressed.data()),
                        std::streamsize(compressed.size())))
            throw std::runtime_error("SPZ read failed");
        failure = decode_spz(compressed, probe, header);
    }
    if (failure)
        throw std::runtime_error(failure->diagnostic);
    if (!validate_scene(*header, layout->totalBytes))
        throw std::runtime_error("Normalized scene validation failed");
    if (mprotect(memory, size_t(layout->totalBytes), PROT_READ))
        throw std::runtime_error("Model protection failed");
    auto scene = std::make_shared<SplatScene>();
    scene->count = header->count;
    scene->shDegree = header->shDegree;
    scene->sourceFormat = SourceFormat(header->sourceFormat);
    scene->worldOrigin = {header->origin[0], header->origin[1], header->origin[2]};
    scene->bounds = {{header->min[0], header->min[1], header->min[2]},
                     {header->max[0], header->max[1], header->max[2]}};
    scene->maxScale = header->maxScale;
    std::span<const float> *arrays[]{&scene->centerLocal, &scene->scale, &scene->rotation,
                                     &scene->opacity,     &scene->rgb0,  &scene->shRest};
    for (size_t index = 0; index < 6; ++index)
        *arrays[index] = {reinterpret_cast<const float *>(static_cast<const uint8_t *>(memory) +
                                                          header->offsets[index]),
                          size_t(header->lengths[index])};
    scene->storage = std::move(owner);
    return scene;
}
} // namespace gs::server
