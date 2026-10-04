#include "decoder.h"
#include <cstdlib>
#include <cstring>
using namespace gs::io::detail;
extern "C"
{
    uint32_t gs_stride()
    {
        if (!gs_output)
            return 0;
        const uint32_t rest = 3 * ((gs_output->shDegree + 1) * (gs_output->shDegree + 1) - 1);
        return (64 + rest * 4 + 15) & ~15u;
    }
    // Renderer-private raw scalar layout; never used as a file-decoder contract.
    uint8_t *pack(uint32_t start, uint32_t count, bool compact)
    {
        if (!gs_output || uint64_t(start) + count > gs_output->count)
            return nullptr;
        const uint32_t rest = 3 * ((gs_output->shDegree + 1) * (gs_output->shDegree + 1) - 1);
        const uint32_t stride = compact ? 56 + rest * 4 : gs_stride();
        auto *result = static_cast<uint8_t *>(std::calloc(count, stride));
        if (!result)
            return nullptr;
        const auto array = [](ArrayIndex a)
        {
            return reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(gs_output) +
                                                   gs_output->offsets[a]);
        };
        for (uint32_t j = 0; j < count; ++j)
        {
            const uint32_t i = start + j;
            auto *dst = reinterpret_cast<float *>(result + uint64_t(j) * stride);
            std::memcpy(dst, array(Center) + 3ull * i, 12);
            std::memcpy(dst + (compact ? 3 : 4), array(Scale) + 3ull * i, 12);
            std::memcpy(dst + (compact ? 6 : 8), array(Rotation) + 4ull * i, 16);
            std::memcpy(dst + (compact ? 10 : 12), array(Rgb0) + 3ull * i, 12);
            dst[compact ? 13 : 15] = array(Opacity)[i];
            if (rest)
                std::memcpy(dst + (compact ? 14 : 16), array(ShRest) + uint64_t(rest) * i, rest * 4);
        }
        return result;
    }
    uint8_t *gs_pack(uint32_t start, uint32_t count)
    {
        return pack(start, count, false);
    }
    uint8_t *gs_pack_compact(uint32_t start, uint32_t count)
    {
        return pack(start, count, true);
    }
    uint8_t *gs_pack_tiled(uint32_t start, uint32_t count)
    {
        if (!gs_output || uint64_t(start) + count > gs_output->count)
            return nullptr;
        const uint32_t rest = 3 * ((gs_output->shDegree + 1) * (gs_output->shDegree + 1) - 1);
        const uint32_t width = 14 + rest, padded = (count + 63u) & ~63u;
        auto *result = static_cast<float *>(std::calloc(uint64_t(padded) * width, 4));
        if (!result)
            return nullptr;
        const auto array = [](ArrayIndex a)
        {
            return reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(gs_output) +
                                                   gs_output->offsets[a]);
        };
        for (uint32_t tile = 0; tile < padded; tile += 64)
            for (uint32_t field = 0; field < width; ++field)
            {
                const float *source = nullptr;
                uint32_t components = 0, component = 0;
                if (field < 3)
                {
                    source = array(Center);
                    components = 3;
                    component = field;
                }
                else if (field < 6)
                {
                    source = array(Scale);
                    components = 3;
                    component = field - 3;
                }
                else if (field < 10)
                {
                    source = array(Rotation);
                    components = 4;
                    component = field - 6;
                }
                else if (field < 13)
                {
                    source = array(Rgb0);
                    components = 3;
                    component = field - 10;
                }
                else if (field == 13)
                {
                    source = array(Opacity);
                    components = 1;
                }
                else
                {
                    source = array(ShRest);
                    components = rest;
                    component = field - 14;
                }
                for (uint32_t lane = 0; lane < 64 && tile + lane < count; ++lane)
                    result[uint64_t(tile) * width + field * 64 + lane] =
                        source[uint64_t(start + tile + lane) * components + component];
            }
        return reinterpret_cast<uint8_t *>(result);
    }
    int gs_rebase_tiled(uint8_t *data, uint32_t count, uint32_t stride, double x, double y, double z)
    {
        if (count % 64 || stride < 56 || stride % 4)
            return 0;
        const double origin[3] = {x, y, z};
        auto *values = reinterpret_cast<float *>(data);
        for (uint32_t tile = 0; tile < count; tile += 64)
            for (uint32_t k = 0; k < 3; ++k)
                for (uint32_t lane = 0; lane < 64; ++lane)
                {
                    auto &value = values[uint64_t(tile) * (stride / 4) + k * 64 + lane];
                    value = static_cast<float>(double(value) - origin[k]);
                }
        return 1;
    }
    int gs_rebase(uint8_t *data, uint32_t count, uint32_t stride, double x, double y, double z)
    {
        const double origin[3] = {x, y, z};
        if (stride < 56 || stride % 4)
            return 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            auto *point = reinterpret_cast<float *>(data + uint64_t(i) * stride);
            for (uint32_t k = 0; k < 3; ++k)
                point[k] = static_cast<float>(double(point[k]) - origin[k]);
        }
        return 1;
    }
}
