#include "zlib.h"
#include <cstdint>
namespace
{
z_stream stream{};
bool active = false;
uint32_t consumed = 0, produced = 0;
} // namespace
extern "C"
{
    void gs_inflate_end()
    {
        if (active)
            inflateEnd(&stream);
        active = false;
    }
    int gs_inflate_begin()
    {
        gs_inflate_end();
        stream = {};
        active = inflateInit2(&stream, 16 + MAX_WBITS) == Z_OK;
        return active;
    }
    int gs_inflate_step(uint8_t *input, uint32_t length, uint8_t *output, uint32_t capacity)
    {
        if (!active)
            return 0;
        stream.next_in = input;
        stream.avail_in = length;
        stream.next_out = output;
        stream.avail_out = capacity;
        const int status = inflate(&stream, Z_NO_FLUSH);
        consumed = length - stream.avail_in;
        produced = capacity - stream.avail_out;
        if (status == Z_STREAM_END)
            return 2;
        if (status == Z_BUF_ERROR && !length && !consumed && !produced)
            return 3; // Need more input after draining an exactly full output buffer.
        return (status == Z_OK || status == Z_BUF_ERROR) && (consumed || produced) ? 1 : 0;
    }
    uint32_t gs_inflate_consumed()
    {
        return consumed;
    }
    uint32_t gs_inflate_produced()
    {
        return produced;
    }
}
