#include "decoder.h"
#include "../../src/model-io/normalize/normalize.h"
#include "load-spz.h"
#include "zlib.h"
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace spz
{
GaussianCloud unpackGaussians(const PackedGaussians &, const UnpackOptions &);
}

using namespace gs::io;
using namespace gs::io::detail;
SceneHeader *gs_output = nullptr;
namespace
{
ProbeResult inspected;
std::unique_ptr<SceneWriter> writer;
std::string failure;
uint64_t written = 0;
std::array<int64_t, 59> field_offsets;
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
    int gs_raw_spz(const uint8_t *data, uint32_t bytes, uint32_t version, uint32_t fractional_bits)
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
            if (uint64_t(n) * (position_bytes + 7 + rotation_bytes + rest) != bytes)
                return reject("Invalid SPZ attribute lengths");
            uint32_t offset = 0;
            const auto copy = [&](std::vector<uint8_t> &out, uint32_t width)
            {
                out.assign(data + offset, data + offset + n * width);
                offset += n * width;
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
            for (uint32_t i = 0; i < n; ++i)
                if (!writer->write(i, {cloud.positions.data() + 3ull * i, cloud.scales.data() + 3ull * i,
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
            for (uint64_t offset = 0; offset < bytes; offset += stride)
            {
                float fields[59]{};
                for (uint32_t index = 0; index < 14 + rest; ++index)
                {
                    if (field_offsets[index] >= 0)
                        std::memcpy(fields + index, data + offset + field_offsets[index], 4);
                }
                if (!writer->write(written++, {fields, fields + 3, fields + 6, fields[13], fields + 10,
                                               rest ? fields + 14 : nullptr}))
                    return reject("InvalidAttribute");
            }
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
        if (!gs_output)
            return 0;
        if (field < 3)
            return gs_output->origin[field];
        if (field < 6)
            return gs_output->min[field - 3];
        if (field < 9)
            return gs_output->max[field - 6];
        if (field == 9)
            return gs_output->maxScale;
        if (field == 10)
            return inspected.ply.vertexOffset;
        if (field == 11)
            return inspected.ply.stride;
        return 0;
    }
}
