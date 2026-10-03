#include "gs_server/frame.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>

namespace gs::server
{
namespace
{
using Microsoft::WRL::ComPtr;
void checked(HRESULT status)
{
    if (FAILED(status))
        throw std::runtime_error("WIC failure: " + std::to_string(uint32_t(status)));
}
class ComScope
{
  public:
    ComScope() : status_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
        if (status_ != RPC_E_CHANGED_MODE)
            checked(status_);
    }
    ~ComScope()
    {
        if (SUCCEEDED(status_))
            CoUninitialize();
    }

  private:
    HRESULT status_;
};
ComPtr<IWICImagingFactory> factory()
{
    ComPtr<IWICImagingFactory> value;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&value)));
    return value;
}
} // namespace
std::vector<uint8_t> decode_jpeg(std::span<const uint8_t> bytes, uint32_t width, uint32_t height)
{
    if (!width || !height || width > 4096 || height > 4096 || bytes.empty() ||
        bytes.size() > (512ull << 20))
        throw std::runtime_error("Invalid JPEG decode input");
    ComScope scope;
    auto imaging = factory();
    ComPtr<IWICStream> stream;
    checked(imaging->CreateStream(&stream));
    checked(stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), DWORD(bytes.size())));
    ComPtr<IWICBitmapDecoder> decoder;
    checked(imaging->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                             &decoder));
    GUID container{};
    checked(decoder->GetContainerFormat(&container));
    if (container != GUID_ContainerFormatJpeg)
        throw std::runtime_error("Expected JPEG container");
    ComPtr<IWICBitmapFrameDecode> frame;
    checked(decoder->GetFrame(0, &frame));
    UINT actual_width = 0, actual_height = 0;
    checked(frame->GetSize(&actual_width, &actual_height));
    if (actual_width != width || actual_height != height)
        throw std::runtime_error("JPEG dimension mismatch");
    ComPtr<IWICBitmapSource> converted;
    checked(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame.Get(), &converted));
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    checked(converted->CopyPixels(nullptr, width * 4, UINT(pixels.size()), pixels.data()));
    return pixels;
}
std::vector<uint8_t> encode_jpeg(std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
                                 int quality)
{
    if (!width || !height || width > 4096 || height > 4096 ||
        rgba.size() != uint64_t(width) * height * 4 || quality < 1 || quality > 100)
        throw std::runtime_error("Invalid JPEG encode input");
    ComScope scope;
    auto imaging = factory();
    ComPtr<IStream> stream;
    checked(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    ComPtr<IWICBitmapEncoder> encoder;
    checked(imaging->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder));
    checked(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    checked(encoder->CreateNewFrame(&frame, &options));
    PROPBAG2 property{};
    property.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value{};
    value.vt = VT_R4;
    value.fltVal = quality / 100.0f;
    checked(options->Write(1, &property, &value));
    checked(frame->Initialize(options.Get()));
    checked(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    checked(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat24bppBGR)
        throw std::runtime_error("Unexpected WIC JPEG pixel format");
    std::vector<uint8_t> pixels(size_t(width) * height * 3);
    for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
        for (size_t channel = 0; channel < 3; ++channel)
            pixels[pixel * 3 + channel] = rgba[pixel * 4 + 2 - channel];
    checked(frame->WritePixels(height, width * 3, UINT(pixels.size()), pixels.data()));
    checked(frame->Commit());
    checked(encoder->Commit());
    STATSTG info{};
    checked(stream->Stat(&info, STATFLAG_NONAME));
    if (info.cbSize.QuadPart > (512ull << 20))
        throw std::runtime_error("JPEG output exceeds limit");
    std::vector<uint8_t> encoded(size_t(info.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    checked(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
    ULONG bytes = 0;
    checked(stream->Read(encoded.data(), ULONG(encoded.size()), &bytes));
    if (bytes != encoded.size())
        throw std::runtime_error("JPEG stream truncated");
    return encoded;
}
} // namespace gs::server
