#include "../codecs/decode.h"
#include "../common/win_handle.h"
#include "../normalize/normalize.h"

#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace gs::io;
using namespace gs::io::detail;

namespace
{

void send_status(HANDLE pipe, std::optional<LoadError> failure)
{
    StatusMessage message;
    if (failure)
    {
        message.code = static_cast<uint8_t>(failure->code);
        message.stage = static_cast<uint8_t>(failure->stage);
        if (failure->byteOffset)
            message.byteOffset = *failure->byteOffset;
        std::memcpy(message.diagnostic, failure->diagnostic.data(),
                    (std::min)(failure->diagnostic.size(), sizeof(message.diagnostic) - 1));
    }
    else
    {
        message.success = 1;
        message.stage = static_cast<uint8_t>(LoadStage::Ready);
    }
    DWORD written = 0;
    WriteFile(pipe, &message, sizeof(message), &written, nullptr);
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc != 7)
        return 2;
    UniqueHandle file(reinterpret_cast<HANDLE>(_wcstoui64(argv[1], nullptr, 10)));
    UniqueHandle output(reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10)));
    UniqueHandle status(reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10)));
    const uint64_t outputBytes = _wcstoui64(argv[4], nullptr, 10);
    const auto coordinates = static_cast<Coordinates>(_wcstoui64(argv[5], nullptr, 10));
    const uint64_t inputBytes = _wcstoui64(argv[6], nullptr, 10);
    try
    {
        if (outputBytes < sizeof(SceneHeader) || inputBytes == 0 || coordinates > Coordinates::Rub)
        {
            send_status(status.get(), error(LoadErrorCode::InvalidHeader, LoadStage::Inspecting));
            return 1;
        }
        UniqueHandle inputMap(
            CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
        UniqueView input(inputMap.valid() ? MapViewOfFile(inputMap.get(), FILE_MAP_READ, 0, 0, 0)
                                          : nullptr);
        UniqueView outputView(MapViewOfFile(output.get(), FILE_MAP_WRITE, 0, 0, 0));
        if (!input.get() || !outputView.get())
        {
            send_status(status.get(), error(LoadErrorCode::OutOfMemory, LoadStage::Decoding));
            return 1;
        }
        const auto *bytes = static_cast<const uint8_t *>(input.get());
        LoadLimits limits;
        auto probe =
            probe_file({bytes, static_cast<size_t>(std::min<uint64_t>(inputBytes, 1 << 20))},
                       inputBytes, limits);
        if (!probe.ok)
        {
            send_status(status.get(), probe.failure);
            return 1;
        }
        auto layout = make_layout(probe.probe, outputBytes);
        if (!layout || layout->totalBytes != outputBytes)
        {
            send_status(status.get(), error(LoadErrorCode::ResourceLimit, LoadStage::Inspecting));
            return 1;
        }
        auto *header = static_cast<SceneHeader *>(outputView.get());
        std::memcpy(header, &*layout, sizeof(SceneHeader));
        std::optional<LoadError> failure;
        if (probe.probe.format == gs::SourceFormat::Ply)
        {
            LARGE_INTEGER zero{};
            if (!SetFilePointerEx(file.get(), zero, nullptr, FILE_BEGIN))
            {
                failure = error(LoadErrorCode::IoFailure, LoadStage::Decoding);
            }
            else
            {
                const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(file.release()),
                                               _O_RDONLY | _O_BINARY);
                FILE *stream = fd >= 0 ? _fdopen(fd, "rb") : nullptr;
                if (!stream)
                {
                    if (fd >= 0)
                        _close(fd);
                    failure = error(LoadErrorCode::IoFailure, LoadStage::Decoding);
                }
                else
                {
                    failure = decode_ply(stream, probe, coordinates, header);
                }
            }
        }
        else
        {
            failure = decode_spz({bytes, static_cast<size_t>(inputBytes)}, probe, header);
        }
        if (!failure && !validate_scene(*header, outputBytes))
            failure = error(LoadErrorCode::InvalidAttribute, LoadStage::Validating);
        send_status(status.get(), failure);
        return failure ? 1 : 0;
    }
    catch (const std::bad_alloc &)
    {
        send_status(status.get(), error(LoadErrorCode::OutOfMemory, LoadStage::Decoding));
    }
    catch (const std::exception &)
    {
        send_status(status.get(), error(LoadErrorCode::DecoderFailure, LoadStage::Decoding));
    }
    return 1;
}
