#include "decoder.h"
#include "../../src/model-io/normalize/normalize.h"
#include "load-spz.h"
#include "zlib.h"
#include <array>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <emscripten.h>

namespace spz
{
GaussianCloud unpackGaussians(const PackedGaussians &, const UnpackOptions &);
}

using namespace gs::io;
using namespace gs::io::detail;
GS_DECODER_LOCAL SceneHeader *gs_output = nullptr;
namespace
{
GS_DECODER_LOCAL ProbeResult inspected;
GS_DECODER_LOCAL std::unique_ptr<SceneWriter> writer;
GS_DECODER_LOCAL std::string failure;
GS_DECODER_LOCAL uint64_t written = 0;
GS_DECODER_LOCAL std::array<int64_t, 59> field_offsets;
GS_DECODER_LOCAL std::optional<SceneHeader> batch_metadata;
uint32_t thread_count = 1;
double timings[3]{};
int reject(const std::string &reason)
{
    failure = reason.substr(0, 512);
    return 0;
}
// Validate into constant-size scratch before the vendor's allocating gzip path.
// Exact inflated length makes its subsequent allocation bounded by the admitted layout.
bool validate_legacy_spz(const uint8_t *data, uint32_t bytes)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(data);
    stream.avail_in = bytes;
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        return false;
    std::array<uint8_t, 65536> scratch{};
    stream.next_out = scratch.data();
    stream.avail_out = 16;
    int status = inflate(&stream, Z_NO_FLUSH);
    uint32_t version = 0;
    std::memcpy(&version, scratch.data() + 4, 4);
    const uint64_t rest = 3ull * ((gs_output->shDegree + 1) * (gs_output->shDegree + 1) - 1);
    const uint64_t expected =
        16 + gs_output->count * ((version == 1 ? 6 : 9) + 1 + 3 + 3 + (version >= 3 ? 4 : 3) + rest);
    bool valid = stream.total_out == 16 && (status == Z_OK || status == Z_STREAM_END);
    while (valid && status == Z_OK)
    {
        stream.next_out = scratch.data();
        stream.avail_out = static_cast<uInt>(scratch.size());
        status = inflate(&stream, Z_NO_FLUSH);
        valid = stream.total_out <= expected;
    }
    valid = valid && status == Z_STREAM_END && stream.total_out == expected && stream.avail_in == 0;
    inflateEnd(&stream);
    return valid;
}
} // namespace
extern "C"
{
    const char *gs_error()
    {
        return failure.c_str();
    }
    void gs_release()
    {
        writer.reset();
        batch_metadata.reset();
        std::free(gs_output);
        gs_output = nullptr;
        written = 0;
    }
    int gs_probe(const uint8_t *prefix, uint32_t bytes, double input_bytes, double scene_limit)
    {
        gs_release();
        failure.clear();
        try
        {
            LoadLimits limits;
            limits.maxSceneBytes = static_cast<uint64_t>(scene_limit);
            inspected = probe_file({prefix, bytes}, static_cast<uint64_t>(input_bytes), limits);
            if (!inspected.ok)
            {
                std::string code = "DecoderFailure";
                switch (inspected.failure.code)
                {
                case LoadErrorCode::UnsupportedFormat:
                case LoadErrorCode::UnsupportedVersion:
                case LoadErrorCode::UnsupportedFeature:
                    code = "UnsupportedFormat";
                    break;
                case LoadErrorCode::ResourceLimit:
                    code = "ResourceLimit";
                    break;
                case LoadErrorCode::OutOfMemory:
                    code = "OutOfMemory";
                    break;
                default:
                    break;
                }
                return reject(code + ": " + inspected.failure.diagnostic);
            }
            if (inspected.probe.count > 0xffffff00ull)
            {
                inspected.ok = false;
                return reject("ResourceLimit: uint32 GPU point indexing");
            }
            if (!make_layout(inspected.probe, limits.maxSceneBytes))
            {
                inspected.ok = false;
                return reject("ResourceLimit: normalized scene bytes");
            }
            return 1;
        }
        catch (const std::exception &e)
        {
            return reject(e.what());
        }
    }
    uint32_t gs_count()
    {
        return inspected.ok ? static_cast<uint32_t>(inspected.probe.count) : 0;
    }
    uint32_t gs_degree()
    {
        return inspected.probe.degree;
    }
    int gs_begin_batch(int rdf, uint32_t count)
    {
        try
        {
            gs_release();
            auto probe = inspected.probe;
            if (!count || count > probe.count)
                return reject("Invalid batch count");
            probe.count = count;
            auto layout = make_layout(probe, 768ull << 20);
            if (!inspected.ok || !layout)
                return reject("ResourceLimit: normalized scene exceeds policy");
            gs_output = static_cast<SceneHeader *>(std::malloc(layout->totalBytes));
            if (!gs_output)
                return reject("OutOfMemory");
            *gs_output = *layout;
            field_offsets.fill(-1);
            const std::array<std::string_view, 14> names = {
                "x",     "y",     "z",     "scale_0", "scale_1", "scale_2", "rot_1",
                "rot_2", "rot_3", "rot_0", "f_dc_0",  "f_dc_1",  "f_dc_2",  "opacity"};
            const uint32_t rest = 3 * ((layout->shDegree + 1) * (layout->shDegree + 1) - 1);
            for (const auto &property : inspected.ply.properties)
            {
                if (!property.isFloat)
                    continue;
                for (size_t index = 0; index < names.size(); ++index)
                {
                    if (property.name == names[index])
                        field_offsets[index] = property.offset;
                }
                if (property.name.starts_with("f_rest_"))
                {
                    uint32_t index = 0;
                    const auto suffix = std::string_view(property.name).substr(7);
                    const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
                    if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() ||
                        index >= rest)
                        return reject("Invalid SH property");
                    field_offsets[14 + (index % (rest / 3)) * 3 + index / (rest / 3)] = property.offset;
                }
            }
            writer = std::make_unique<SceneWriter>(gs_output, rdf != 0);
            return 1;
        }
        catch (const std::exception &e)
        {
            return reject(e.what());
        }
    }
    int gs_begin(int rdf)
    {
        return gs_begin_batch(rdf, gs_count());
    }
    int raw_spz_range(const uint8_t *data, uint32_t bytes, uint32_t version, uint32_t fractional_bits,
                      uint32_t start, uint32_t input_count)
    {
        try
        {
            if (!writer || !gs_output || version < 1 || version > 3 || fractional_bits > 30)
                return reject("Invalid SPZ batch state");
            spz::PackedGaussians packed;
            packed.version = version;
            packed.numPoints = static_cast<int32_t>(gs_output->count);
            packed.shDegree = gs_output->shDegree;
            packed.fractionalBits = fractional_bits;
            packed.usesQuaternionSmallestThree = version >= 3;
            const uint32_t n = packed.numPoints;
            const uint32_t rest = 3 * ((packed.shDegree + 1) * (packed.shDegree + 1) - 1);
            const uint32_t position_bytes = version == 1 ? 6 : 9;
            const uint32_t rotation_bytes = version >= 3 ? 4 : 3;
            if (uint64_t(input_count) * (position_bytes + 7 + rotation_bytes + rest) != bytes ||
                uint64_t(start) + n > input_count)
                return reject("Invalid SPZ attribute lengths");
            uint32_t offset = 0;
            const auto copy = [&](std::vector<uint8_t> &out, uint32_t width)
            {
                out.assign(data + offset + uint64_t(start) * width, data + offset + uint64_t(start + n) * width);
                offset += input_count * width;
            };
            copy(packed.positions, position_bytes);
            copy(packed.alphas, 1);
            copy(packed.colors, 3);
            copy(packed.scales, 3);
            copy(packed.rotations, rotation_bytes);
            copy(packed.sh, rest);
            // Use the same vendor full-array unpack math as the original path.
            spz::UnpackOptions options;
            options.to = spz::CoordinateSystem::RUB;
            const auto cloud = spz::unpackGaussians(packed, options);
            if (cloud.numPoints != n)
                return reject("SPZ batch mismatch");
            auto *batch_writer = writer.get();
            for (uint32_t i = 0; i < n; ++i)
                if (!batch_writer->write(i, {cloud.positions.data() + 3ull * i, cloud.scales.data() + 3ull * i,
                                       cloud.rotations.data() + 4ull * i, cloud.alphas[i],
                                       cloud.colors.data() + 3ull * i,
                                       rest ? cloud.sh.data() + uint64_t(rest) * i : nullptr}))
                    return reject("Invalid SPZ attribute");
            written = n;
            return 1;
        }
        catch (const std::exception &e)
        {
            return reject(e.what());
        }
    }
    int gs_raw_spz(const uint8_t *data, uint32_t bytes, uint32_t version, uint32_t fractional_bits)
    {
        return raw_spz_range(data, bytes, version, fractional_bits, 0, gs_output ? gs_output->count : 0);
    }
    int gs_chunk(const uint8_t *data, uint32_t bytes)
    {
        try
        {
            if (!gs_output || !writer || inspected.probe.format != gs::SourceFormat::Ply)
                return reject("Invalid decoder state");
            const uint64_t stride = inspected.ply.stride;
            if (!stride || bytes % stride || written + bytes / stride > gs_output->count)
                return reject("Invalid chunk boundary");
            const uint32_t rest = 3 * ((gs_output->shDegree + 1) * (gs_output->shDegree + 1) - 1);
            auto *batch_writer = writer.get();
            const auto offsets = field_offsets;
            auto next = written;
            for (uint64_t offset = 0; offset < bytes; offset += stride)
            {
                float fields[59]{};
                for (uint32_t index = 0; index < 14 + rest; ++index)
                {
                    if (offsets[index] >= 0)
                        std::memcpy(fields + index, data + offset + offsets[index], 4);
                }
                if (!batch_writer->write(next++, {fields, fields + 3, fields + 6, fields[13], fields + 10,
                                               rest ? fields + 14 : nullptr}))
                    return reject("InvalidAttribute");
            }
            written = next;
            return 1;
        }
        catch (const std::exception &e)
        {
            return reject(e.what());
        }
    }
    int gs_spz(const uint8_t *data, uint32_t bytes)
    {
        try
        {
            if (!gs_output)
                return reject("Invalid decoder state");
            const auto complete = probe_file({data, bytes}, bytes, LoadLimits{});
            if (!complete.ok || complete.probe.format != gs::SourceFormat::Spz ||
                complete.probe.count != gs_output->count || complete.probe.degree != gs_output->shDegree)
                return reject("DecoderFailure: SPZ header changed after admission");
            if (bytes >= 2 && data[0] == 0x1f && data[1] == 0x8b && !validate_legacy_spz(data, bytes))
                return reject("DecoderFailure: SPZ gzip length, checksum or trailing data");
            spz::UnpackOptions options;
            options.to = spz::CoordinateSystem::RUB;
            const auto cloud = spz::loadSpz(data, bytes, options);
            const uint64_t n = gs_output->count;
            const uint64_t rest = 3ull * ((gs_output->shDegree + 1) * (gs_output->shDegree + 1) - 1);
            if (cloud.numPoints != n || cloud.shDegree != gs_output->shDegree || cloud.antialiased ||
                cloud.positions.size() != 3 * n || cloud.scales.size() != 3 * n ||
                cloud.rotations.size() != 4 * n || cloud.alphas.size() != n || cloud.colors.size() != 3 * n ||
                cloud.sh.size() != rest * n)
                return reject("SPZ cloud mismatch");
            SceneWriter normalized(gs_output, false);
            for (uint64_t i = 0; i < n; ++i)
            {
                if (!normalized.write(i, {cloud.positions.data() + 3 * i, cloud.scales.data() + 3 * i,
                                          cloud.rotations.data() + 4 * i, cloud.alphas[i],
                                          cloud.colors.data() + 3 * i,
                                          rest ? cloud.sh.data() + rest * i : nullptr}))
                    return reject("Invalid SPZ attribute");
            }
            if (!normalized.finish())
                return reject("Invalid SPZ bounds");
            written = gs_output->count;
            return 1;
        }
        catch (const std::exception &e)
        {
            return reject(e.what());
        }
    }
    int gs_finish(int spz)
    {
        if (!gs_output || written != gs_output->count)
            return reject("TruncatedData");
        if (!spz && !writer->finish())
            return reject("Invalid bounds");
        if (!validate_scene(*gs_output, gs_output->totalBytes))
            return reject("Invalid scene");
        return 1;
    }
    int gs_finish_batch()
    {
        try
        {
            if (!gs_output || !writer || written != gs_output->count)
                return reject("TruncatedData");
            auto *centers = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(gs_output) +
                                                      gs_output->offsets[Center]);
            // Streaming packs world centers and performs ONE global rebase later. Validate in
            // world space: a disposable local-origin float32 round trip can move near-zero bounds
            // beyond the native absolute tolerance despite valid source attributes.
            const std::vector<float> world(centers, centers + gs_output->count * 3);
            if (!writer->finish())
                return reject("Invalid bounds");
            std::memcpy(centers, world.data(), world.size() * sizeof(float));
            for (auto &origin : gs_output->origin)
                origin = 0;
            if (!validate_scene(*gs_output, gs_output->totalBytes))
                return reject("Invalid scene");
            return 1;
        }
        catch (const std::exception &e)
        {
            return reject(e.what());
        }
    }
    double gs_meta(uint32_t field)
    {
        const auto *header = gs_output ? gs_output : (batch_metadata ? &*batch_metadata : nullptr);
        if (!header)
            return 0;
        if (field < 3)
            return header->origin[field];
        if (field < 6)
            return header->min[field - 3];
        if (field < 9)
            return header->max[field - 6];
        if (field == 9)
            return header->maxScale;
        if (field == 10)
            return inspected.ply.vertexOffset;
        if (field == 11)
            return inspected.ply.stride;
        return 0;
    }

    int gs_pack_tiled_into(uint8_t *destination, uint32_t start, uint32_t count);
    int gs_rebase_tiled(uint8_t *data, uint32_t count, uint32_t stride, double x, double y, double z);

    int gs_set_threads(uint32_t count)
    {
#ifdef __EMSCRIPTEN_PTHREADS__
        if (!count || count > 8)
            return reject("InvalidInput: decoder threads");
        thread_count = count;
#else
        if (count != 1)
            return reject("UnsupportedCapability: pthreads");
#endif
        return 1;
    }
    double gs_timing(uint32_t stage)
    {
        return stage < 3 ? timings[stage] : 0;
    }

    // Each task owns its probe/writer/header; only its output tile range is shared.
    uint8_t *decode_batch_impl(const uint8_t *data, uint32_t bytes, int rdf, uint32_t count,
                             uint32_t version, uint32_t fractional_bits)
    {
        struct BatchResult
        {
            SceneHeader header{};
            std::string error;
            double decode_ms = 0, finish_pack_ms = 0;
        };
        timings[0] = timings[1] = 0;
        if (!inspected.ok || !count || count > 65536 || count > inspected.probe.count)
        {
            reject("InvalidInput: parallel batch count");
            return nullptr;
        }
        const bool ply = inspected.probe.format == gs::SourceFormat::Ply;
        const uint32_t rest = 3 * ((inspected.probe.degree + 1) * (inspected.probe.degree + 1) - 1);
        const uint32_t width = 14 + rest, stride = width * 4;
        const uint32_t widths[6] = {version == 1 ? 6u : 9u, 1, 3, 3, version >= 3 ? 4u : 3u, rest};
        const uint64_t encoded_stride = ply ? inspected.ply.stride : widths[0] + 7 + widths[4] + rest;
        if ((!ply && (version < 1 || version > 3 || fractional_bits > 30)) ||
            encoded_stride * count != bytes)
        {
            reject("InvalidInput: parallel batch bytes/version");
            return nullptr;
        }
        std::unique_ptr<uint8_t, decltype(&std::free)> output(
            static_cast<uint8_t *>(std::calloc((uint64_t(count) + 63) / 64 * 64, stride)), &std::free);
        if (!output)
        {
            reject("OutOfMemory: parallel output");
            return nullptr;
        }
        const ProbeResult probe = inspected;
        const uint32_t tiles = (count + 63) / 64;
        const uint32_t workers = std::min(thread_count, tiles);
        std::array<BatchResult, 8> results;
        const auto task = [&](uint32_t index)
        {
            const uint32_t start = (uint64_t(tiles) * index / workers) * 64;
            const uint32_t end = std::min(count, uint32_t((uint64_t(tiles) * (index + 1) / workers) * 64));
            const uint32_t n = end - start;
            auto &result = results[index];
            try
            {
                inspected = probe;
                const double begin = emscripten_get_now();
                if (!gs_begin_batch(rdf, n))
                    throw std::runtime_error(failure);
                if (ply)
                {
                    if (!gs_chunk(data + uint64_t(start) * encoded_stride, n * encoded_stride))
                        throw std::runtime_error(failure);
                }
                else
                {
                    if (!raw_spz_range(data, bytes, version, fractional_bits, start, count))
                        throw std::runtime_error(failure);
                }
                result.decode_ms = emscripten_get_now() - begin;
                const double finish_begin = emscripten_get_now();
                if (!gs_finish_batch())
                    throw std::runtime_error(failure);
                if (!gs_pack_tiled_into(output.get() + uint64_t(start) * stride, 0, n))
                    throw std::runtime_error("DecoderFailure: parallel pack");
                result.header = *gs_output;
                result.finish_pack_ms = emscripten_get_now() - finish_begin;
            }
            catch (const std::exception &e)
            {
                result.error = e.what();
            }
            gs_release();
        };
        std::vector<std::thread> threads;
        try
        {
#ifdef __EMSCRIPTEN_PTHREADS__
            for (uint32_t i = 1; i < workers; ++i)
                threads.emplace_back(task, i);
#endif
            task(0);
        }
        catch (const std::exception &e)
        {
            results[0].error = e.what();
        }
        for (auto &thread : threads)
            thread.join();
        inspected = probe;
        gs_release();
        for (uint32_t i = 0; i < workers; ++i)
        {
            if (!results[i].error.empty())
            {
                reject(results[i].error);
                return nullptr;
            }
            timings[0] = std::max(timings[0], results[i].decode_ms);
            timings[1] = std::max(timings[1], results[i].finish_pack_ms);
        }
        // Metadata is separate from normalized scene storage; no synthetic layout is exposed.
        batch_metadata = results[0].header;
        batch_metadata->count = count;
        for (uint32_t i = 1; i < workers; ++i)
        {
            for (uint32_t k = 0; k < 3; ++k)
            {
                batch_metadata->min[k] = std::min(batch_metadata->min[k], results[i].header.min[k]);
                batch_metadata->max[k] = std::max(batch_metadata->max[k], results[i].header.max[k]);
            }
            batch_metadata->maxScale = std::max(batch_metadata->maxScale, results[i].header.maxScale);
        }
        return output.release();
    }

    uint8_t *gs_decode_batch(const uint8_t *data, uint32_t bytes, int rdf, uint32_t count,
                             uint32_t version, uint32_t fractional_bits)
    {
        try
        {
            return decode_batch_impl(data, bytes, rdf, count, version, fractional_bits);
        }
        catch (const std::exception &e)
        {
            gs_release();
            reject(e.what());
            return nullptr;
        }
    }

    int gs_rebase_parallel(uint8_t *data, uint32_t count, uint32_t stride, double x, double y, double z)
    {
        if (!data || count % 64 || stride < 56 || stride % 4)
            return 0;
        const double begin = emscripten_get_now();
        const uint32_t tiles = count / 64, workers = std::min(thread_count, std::max(1u, tiles));
        const auto task = [&](uint32_t index)
        {
            const uint32_t first = uint64_t(tiles) * index / workers * 64;
            const uint32_t last = uint64_t(tiles) * (index + 1) / workers * 64;
            gs_rebase_tiled(data + uint64_t(first) * stride, last - first, stride, x, y, z);
        };
        std::vector<std::thread> threads;
        try
        {
#ifdef __EMSCRIPTEN_PTHREADS__
            for (uint32_t i = 1; i < workers; ++i)
                threads.emplace_back(task, i);
#endif
            task(0);
        }
        catch (const std::exception &)
        {
            for (auto &thread : threads)
                thread.join();
            return 0;
        }
        for (auto &thread : threads)
            thread.join();
        timings[2] = emscripten_get_now() - begin;
        return 1;
    }
}
