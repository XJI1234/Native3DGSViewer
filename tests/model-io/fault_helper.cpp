#include "protocol.h"

#include <Windows.h>

#include <array>
#include <cstdlib>
#include <string>

using namespace gs::io::detail;

int wmain(int argc, wchar_t **argv)
{
    if (argc != 7)
        return 2;
    HANDLE statusPipe = reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10));
    std::array<wchar_t, 64> mode{};
    GetEnvironmentVariableW(L"MODEL_IO_FAULT_MODE", mode.data(), static_cast<DWORD>(mode.size()));
    const std::wstring selected(mode.data());
    if (selected == L"crash")
        TerminateProcess(GetCurrentProcess(), 42);
    if (selected == L"hang")
    {
        Sleep(5000);
        return 1;
    }
    if (selected == L"memory")
    {
        std::array<void *, 512> blocks{};
        for (auto &block : blocks)
        {
            block = VirtualAlloc(nullptr, 1 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!block)
                return 43;
        }
        return 44;
    }
    if (selected == L"flood")
    {
        std::array<char, 4096> block{};
        DWORD written;
        for (int i = 0; i < 256; ++i)
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), block.data(),
                      static_cast<DWORD>(block.size()), &written, nullptr);
    }
    StatusMessage status;
    if (selected == L"malformed")
        status.magic = 0;
    if (selected == L"corrupt" || selected == L"malformed")
        status.success = 1;
    else
    {
        status.code = static_cast<uint8_t>(gs::io::LoadErrorCode::DecoderFailure);
        status.stage = static_cast<uint8_t>(gs::io::LoadStage::Decoding);
    }
    DWORD written = 0;
    WriteFile(statusPipe, &status, sizeof(status), &written, nullptr);
    return status.success ? 0 : 1;
}
