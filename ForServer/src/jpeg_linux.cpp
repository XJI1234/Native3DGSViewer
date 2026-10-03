#include "gs_server/frame.h"
#include <cstdio>
#include <cstdlib>
#include <jpeglib.h>
#include <memory>
#include <setjmp.h>
#include <stdexcept>

namespace gs::server
{
namespace
{
struct ErrorState
{
    jpeg_error_mgr base;
    jmp_buf jump;
};
struct CompressState
{
    jpeg_compress_struct context{};
    unsigned char *memory = nullptr;
    unsigned long bytes = 0;
    ~CompressState()
    {
        jpeg_destroy_compress(&context);
        std::free(memory);
    }
};
struct DecompressState
{
    jpeg_decompress_struct context{};
    ~DecompressState()
    {
        jpeg_destroy_decompress(&context);
    }
};
void jpeg_error(j_common_ptr context)
{
    auto *error = reinterpret_cast<ErrorState *>(context->err);
    longjmp(error->jump, 1);
}
void jpeg_warning(j_common_ptr context, int level)
{
    if (level < 0)
        jpeg_error(context);
}
} // namespace
std::vector<uint8_t> encode_jpeg(std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
                                 int quality)
{
    if (!width || !height || width > 4096 || height > 4096 ||
        rgba.size() != uint64_t(width) * height * 4 || quality < 1 || quality > 100)
        throw std::runtime_error("Invalid JPEG encode input");
    auto error = std::make_unique<ErrorState>();
    auto state = std::make_unique<CompressState>();
    auto &context = state->context;
    context.err = jpeg_std_error(&error->base);
    error->base.error_exit = jpeg_error;
    std::vector<uint8_t> row(size_t(width) * 3);
    if (setjmp(error->jump))
        throw std::runtime_error("JPEG encode failed");
    jpeg_create_compress(&context);
    jpeg_mem_dest(&context, &state->memory, &state->bytes);
    context.image_width = width;
    context.image_height = height;
    context.input_components = 3;
    context.in_color_space = JCS_RGB;
    jpeg_set_defaults(&context);
    for (int component = 0; component < 3; ++component)
        context.comp_info[component].h_samp_factor = context.comp_info[component].v_samp_factor = 1;
    jpeg_set_quality(&context, quality, TRUE);
    jpeg_start_compress(&context, TRUE);
    while (context.next_scanline < height)
    {
        for (uint32_t pixel = 0; pixel < width; ++pixel)
            for (size_t channel = 0; channel < 3; ++channel)
                row[pixel * 3 + channel] =
                    rgba[(size_t(context.next_scanline) * width + pixel) * 4 + channel];
        JSAMPROW rows[]{row.data()};
        jpeg_write_scanlines(&context, rows, 1);
    }
    jpeg_finish_compress(&context);
    std::vector<uint8_t> output(state->memory, state->memory + state->bytes);
    return output;
}
std::vector<uint8_t> decode_jpeg(std::span<const uint8_t> bytes, uint32_t width, uint32_t height)
{
    if (!width || !height || width > 4096 || height > 4096 || bytes.empty() ||
        bytes.size() > (512ull << 20))
        throw std::runtime_error("Invalid JPEG decode input");
    auto error = std::make_unique<ErrorState>();
    auto state = std::make_unique<DecompressState>();
    auto &context = state->context;
    context.err = jpeg_std_error(&error->base);
    error->base.error_exit = jpeg_error;
    error->base.emit_message = jpeg_warning;
    std::vector<uint8_t> output(size_t(width) * height * 4);
    std::vector<uint8_t> row(size_t(width) * 3);
    if (setjmp(error->jump))
        throw std::runtime_error("JPEG decode failed");
    jpeg_create_decompress(&context);
    jpeg_mem_src(&context, bytes.data(), bytes.size());
    jpeg_read_header(&context, TRUE);
    if (context.image_width != width || context.image_height != height)
        throw std::runtime_error("JPEG dimension mismatch");
    context.out_color_space = JCS_RGB;
    jpeg_start_decompress(&context);
    while (context.output_scanline < height)
    {
        const size_t offset = size_t(context.output_scanline) * width * 4;
        JSAMPROW rows[]{row.data()};
        jpeg_read_scanlines(&context, rows, 1);
        for (uint32_t pixel = 0; pixel < width; ++pixel)
        {
            for (size_t channel = 0; channel < 3; ++channel)
                output[offset + pixel * 4 + channel] = row[pixel * 3 + channel];
            output[offset + pixel * 4 + 3] = 255;
        }
    }
    jpeg_finish_decompress(&context);
    return output;
}
} // namespace gs::server
