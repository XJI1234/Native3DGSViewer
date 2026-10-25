#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include <cstdint>

namespace
{
// Private serial ABI: each decoder Worker owns its own WASM instance; pthread
// tasks only unpack batches after inflation has finished. No shared stream calls.
ZSTD_DCtx *context = nullptr;
uint32_t consumed = 0, produced = 0;
bool ended = false;
} // namespace

extern "C"
{
    // Header inspection allocates no window; reject dictionaries/skippable frames.
    double gs_zstd_window(const uint8_t *input, uint32_t length)
    {
        ZSTD_frameHeader header{};
        const auto result = ZSTD_getFrameHeader(&header, input, length);
        if (result != 0 || header.frameType != ZSTD_frame || header.dictID != 0)
            return -1;
        return static_cast<double>(header.windowSize);
    }
    void gs_zstd_end()
    {
        ZSTD_freeDCtx(context);
        context = nullptr;
        consumed = produced = 0;
        ended = false;
    }
    int gs_zstd_begin(uint32_t window_log)
    {
        gs_zstd_end();
        if (window_log < 10 || window_log > 27)
            return 0;
        context = ZSTD_createDCtx();
        if (!context)
            return 0;
        if (ZSTD_isError(ZSTD_DCtx_setParameter(context, ZSTD_d_windowLogMax, window_log)))
        {
            gs_zstd_end();
            return 0;
        }
        return 1;
    }
    int gs_zstd_step(const uint8_t *input, uint32_t length, uint8_t *output, uint32_t capacity)
    {
        consumed = produced = 0;
        if (!context || ended || !capacity)
            return 0;
        ZSTD_inBuffer in{input, length, 0};
        ZSTD_outBuffer out{output, capacity, 0};
        const auto result = ZSTD_decompressStream(context, &out, &in);
        consumed = static_cast<uint32_t>(in.pos);
        produced = static_cast<uint32_t>(out.pos);
        if (ZSTD_isError(result))
            return 0;
        if (!result)
        {
            ended = true;
            return 2;
        }
        return consumed || produced ? 1 : (!length ? 3 : 0);
    }
    uint32_t gs_zstd_consumed()
    {
        return consumed;
    }
    uint32_t gs_zstd_produced()
    {
        return produced;
    }
}
