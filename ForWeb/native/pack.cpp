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
    uint8_t *gs_pack(uint32_t start, uint32_t count)
    {
        if (!gs_output || uint64_t(start) + count > gs_output->count)
            return nullptr;
        const uint32_t stride = gs_stride(),
                       rest = 3 * ((gs_output->shDegree + 1) * (gs_output->shDegree + 1) - 1);
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
            std::memcpy(dst + 4, array(Scale) + 3ull * i, 12);
            std::memcpy(dst + 8, array(Rotation) + 4ull * i, 16);
            std::memcpy(dst + 12, array(Rgb0) + 3ull * i, 12);
            dst[15] = array(Opacity)[i];
            if (rest)
                std::memcpy(dst + 16, array(ShRest) + uint64_t(rest) * i, rest * 4);
        }
        return result;
    }
}
