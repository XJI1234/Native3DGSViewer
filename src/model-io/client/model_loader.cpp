#include "model-io/model_loader.h"

#include "../common/probe.h"
#include "../common/win_handle.h"
#include "../normalize/normalize.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace gs::io
{
namespace
{

using namespace detail;

LoadError file_error(DWORD code)
{
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        return error(LoadErrorCode::NotFound, LoadStage::Opening);
    if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION)
        return error(LoadErrorCode::AccessDenied, LoadStage::Opening);
    return error(LoadErrorCode::IoFailure, LoadStage::Opening);
}

bool local_regular_file(HANDLE file)
{
    if (GetFileType(file) != FILE_TYPE_DISK)
        return false;
    const DWORD length = GetFinalPathNameByHandleW(file, nullptr, 0, VOLUME_NAME_DOS);
    if (!length || length > 32768)
        return false;
    std::wstring path(length + 1, L'\0');
    if (!GetFinalPathNameByHandleW(file, path.data(), length + 1, VOLUME_NAME_DOS))
        return false;
    if (!path.starts_with(L"\\\\?\\") || path.size() < 7 || path[5] != L':')
        return false;
    wchar_t root[]{path[4], L':', L'\\', L'\0'};
    return GetDriveTypeW(root) != DRIVE_REMOTE;
}

std::wstring helper_path()
{
    std::wstring path(32768, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size())
        return {};
    path.resize(size);
    auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    path.resize(slash + 1);
    return path + L"model-io-helper.exe";
}

std::wstring command_line(const std::wstring &path, HANDLE file, HANDLE output, HANDLE status,
                          uint64_t outputBytes, Coordinates coordinates, uint64_t inputBytes)
{
    return L"\"" + path + L"\" " + std::to_wstring(reinterpret_cast<uintptr_t>(file)) + L" " +
           std::to_wstring(reinterpret_cast<uintptr_t>(output)) + L" " +
           std::to_wstring(reinterpret_cast<uintptr_t>(status)) + L" " +
           std::to_wstring(outputBytes) + L" " +
           std::to_wstring(static_cast<uint8_t>(coordinates)) + L" " + std::to_wstring(inputBytes);
}

struct AttributeList
{
    std::vector<uint8_t> buffer;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    ~AttributeList()
    {
        if (list)
            DeleteProcThreadAttributeList(list);
    }
};

LoadResult run_helper(HANDLE file, uint64_t inputBytes, const SceneHeader &layout,
                      Coordinates coordinates, std::stop_token stop)
{
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (!GlobalMemoryStatusEx(&memory))
        return error(LoadErrorCode::IoFailure, LoadStage::Inspecting);
    const uint64_t jobLimit = std::min<uint64_t>(6ull << 30, memory.ullAvailPhys / 2);
    if (jobLimit < (1ull << 30) || layout.totalBytes > memory.ullAvailPhys / 2 ||
        layout.totalBytes > memory.ullAvailPageFile / 2)
        return error(LoadErrorCode::ResourceLimit, LoadStage::Inspecting, "CPU memory budget");
    UniqueHandle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                            static_cast<DWORD>(layout.totalBytes >> 32),
                                            static_cast<DWORD>(layout.totalBytes), nullptr));
    if (!mapping.valid())
        return error(LoadErrorCode::OutOfMemory, LoadStage::Inspecting);
    UniqueHandle statusRead, statusWrite, logRead, logWrite;
    HANDLE a, b;
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    if (!SetHandleInformation(file, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
        !SetHandleInformation(mapping.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        return error(LoadErrorCode::DecoderFailure, LoadStage::Inspecting, "Inheritable handles");
    UniqueHandle nullInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!nullInput.valid())
        return error(LoadErrorCode::DecoderFailure, LoadStage::Inspecting, "NUL input");
    if (!CreatePipe(&a, &b, &inherit, 0))
        return error(LoadErrorCode::IoFailure, LoadStage::Inspecting);
    statusRead.reset(a);
    statusWrite.reset(b);
    if (!CreatePipe(&a, &b, &inherit, 0))
        return error(LoadErrorCode::IoFailure, LoadStage::Inspecting);
    logRead.reset(a);
    logWrite.reset(b);
    SetHandleInformation(statusRead.get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(logRead.get(), HANDLE_FLAG_INHERIT, 0);
    HANDLE handles[]{file, mapping.get(), statusWrite.get(), logWrite.get(), nullInput.get()};
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    AttributeList attrs;
    attrs.buffer.resize(bytes);
    attrs.list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.buffer.data());
    if (!InitializeProcThreadAttributeList(attrs.list, 1, 0, &bytes) ||
        !UpdateProcThreadAttribute(attrs.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                   sizeof(handles), nullptr, nullptr))
        return error(LoadErrorCode::DecoderFailure, LoadStage::Inspecting, "Handle inheritance");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = logWrite.get();
    startup.StartupInfo.hStdError = logWrite.get();
    startup.StartupInfo.hStdInput = nullInput.get();
    startup.lpAttributeList = attrs.list;
    const auto path = helper_path();
    if (path.empty())
        return error(LoadErrorCode::DecoderFailure, LoadStage::Inspecting, "Helper path");
    auto args = command_line(path, file, mapping.get(), statusWrite.get(), layout.totalBytes,
                             coordinates, inputBytes);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(path.c_str(), args.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                        nullptr, &startup.StartupInfo, &process))
        return error(LoadErrorCode::DecoderFailure, LoadStage::Inspecting,
                     "CreateProcess " + std::to_string(GetLastError()));
    UniqueHandle child(process.hProcess), thread(process.hThread);
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
    jobInfo.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_JOB_MEMORY;
    jobInfo.JobMemoryLimit = static_cast<SIZE_T>(jobLimit);
    if (!job.valid() ||
        !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &jobInfo,
                                 sizeof(jobInfo)) ||
        !AssignProcessToJobObject(job.get(), child.get()))
    {
        TerminateProcess(child.get(), 1);
        WaitForSingleObject(child.get(), 3000);
        return error(LoadErrorCode::DecoderFailure, LoadStage::Inspecting, "Job setup");
    }
    statusWrite.reset();
    logWrite.reset();
    std::array<char, 4096> logPrefix{};
    size_t logSize = 0;
    std::jthread drain([&] {
        std::array<char, 4096> block;
        DWORD count;
        while (ReadFile(logRead.get(), block.data(), static_cast<DWORD>(block.size()), &count,
                        nullptr) &&
               count)
        {
            const size_t keep = (std::min)(size_t(count), logPrefix.size() - logSize);
            std::memcpy(logPrefix.data() + logSize, block.data(), keep);
            logSize += keep;
        }
    });
    if (ResumeThread(thread.get()) == DWORD(-1))
    {
        TerminateJobObject(job.get(), 1);
        WaitForSingleObject(child.get(), 3000);
        return error(LoadErrorCode::DecoderFailure, LoadStage::Decoding, "ResumeThread");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    bool cancelled = false, timedOut = false;
    while (WaitForSingleObject(child.get(), 20) == WAIT_TIMEOUT)
    {
        if (stop.stop_requested())
        {
            cancelled = true;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            timedOut = true;
            break;
        }
    }
    if (cancelled || timedOut)
    {
        TerminateJobObject(job.get(), 1);
        WaitForSingleObject(child.get(), 3000);
        return error(cancelled ? LoadErrorCode::Cancelled : LoadErrorCode::Timeout,
                     LoadStage::Decoding);
    }
    DWORD exitCode = 0, read = 0;
    GetExitCodeProcess(child.get(), &exitCode);
    StatusMessage message{};
    const bool received = ReadFile(statusRead.get(), &message, sizeof(message), &read, nullptr) &&
                          read == sizeof(message) && message.magic == kStatusMagic &&
                          message.version == kProtocolVersion && message.length == sizeof(message);
    if (!received)
        return error(exitCode == 0 ? LoadErrorCode::DecoderFailure : LoadErrorCode::DecoderCrashed,
                     LoadStage::Decoding, "Helper status missing");
    if (!message.success)
    {
        const auto code = message.code <= static_cast<uint8_t>(LoadErrorCode::ObserverFailure)
                              ? static_cast<LoadErrorCode>(message.code)
                              : LoadErrorCode::DecoderFailure;
        LoadError result =
            error(code, static_cast<LoadStage>(message.stage),
                  std::string(message.diagnostic, strnlen_s(message.diagnostic, 512)));
        if (message.byteOffset != UINT64_MAX)
            result.byteOffset = message.byteOffset;
        return result;
    }
    if (exitCode != 0)
        return error(LoadErrorCode::DecoderCrashed, LoadStage::Decoding);
    if (stop.stop_requested())
        return error(LoadErrorCode::Cancelled, LoadStage::Validating);
    void *view = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0);
    if (!view)
        return error(LoadErrorCode::OutOfMemory, LoadStage::Validating);
    UniqueView guard(view);
    auto *header = static_cast<const SceneHeader *>(view);
    if (!validate_scene(*header, layout.totalBytes) || header->count != layout.count ||
        header->shDegree != layout.shDegree || header->sourceFormat != layout.sourceFormat)
        return error(LoadErrorCode::InvalidAttribute, LoadStage::Validating,
                     "Shared scene validation");
    if (stop.stop_requested())
        return error(LoadErrorCode::Cancelled, LoadStage::Validating);
    auto scene = std::make_shared<gs::SplatScene>();
    scene->storage =
        std::shared_ptr<const void>(guard.release(), [](const void *p) { UnmapViewOfFile(p); });
    scene->count = header->count;
    scene->shDegree = header->shDegree;
    scene->sourceFormat = static_cast<gs::SourceFormat>(header->sourceFormat);
    scene->worldOrigin = {header->origin[0], header->origin[1], header->origin[2]};
    scene->bounds = {{header->min[0], header->min[1], header->min[2]},
                     {header->max[0], header->max[1], header->max[2]}};
    scene->maxScale = header->maxScale;
    auto span = [header](ArrayIndex index) {
        const auto *p = reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(header) +
                                                        header->offsets[index]);
        return std::span<const float>(p, static_cast<size_t>(header->lengths[index]));
    };
    scene->centerLocal = span(Center);
    scene->scale = span(Scale);
    scene->rotation = span(Rotation);
    scene->opacity = span(Opacity);
    scene->rgb0 = span(Rgb0);
    scene->shRest = span(ShRest);
    return gs::SceneHandle(scene);
}

class ModelLoader final : public IModelLoader
{
  public:
    LoadResult load(const LoadRequest &request, std::stop_token stop,
                    ProgressSink progress) override
    {
        LoadStage stage = LoadStage::Opening;
        struct ObserverException
        {
        };
        try
        {
            auto report = [&](LoadStage next, uint64_t bytes = 0,
                              std::optional<uint64_t> total = {}) {
                stage = next;
                if (progress)
                {
                    try
                    {
                        progress({next, bytes, total});
                    }
                    catch (...)
                    {
                        throw ObserverException{};
                    }
                }
            };
            if (stop.stop_requested())
                return error(LoadErrorCode::Cancelled, stage);
            report(LoadStage::Opening);
            if (request.limits.maxInputBytes > (1ull << 30) ||
                request.limits.maxSplats > 8'000'000 ||
                request.limits.maxSceneBytes > (2ull << 30) ||
                request.plyCoordinates > Coordinates::Rub)
                return error(LoadErrorCode::ResourceLimit, stage,
                             "Limits may only tighten defaults");
            UniqueHandle file(CreateFileW(request.path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (!file.valid())
                return file_error(GetLastError());
            if (!local_regular_file(file.get()))
                return error(LoadErrorCode::UnsupportedFormat, stage, "Remote or non-regular file");
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file.get(), &size))
                return error(LoadErrorCode::IoFailure, stage);
            if (size.QuadPart < 0 ||
                static_cast<uint64_t>(size.QuadPart) > request.limits.maxInputBytes)
                return error(LoadErrorCode::ResourceLimit, stage);
            const uint64_t inputBytes = static_cast<uint64_t>(size.QuadPart);
            std::vector<uint8_t> prefix(
                static_cast<size_t>(std::min<uint64_t>(inputBytes, 1 << 20)));
            DWORD read = 0;
            if (!prefix.empty() && (!ReadFile(file.get(), prefix.data(),
                                              static_cast<DWORD>(prefix.size()), &read, nullptr) ||
                                    read != prefix.size()))
                return error(LoadErrorCode::IoFailure, stage);
            report(LoadStage::Inspecting, read, inputBytes);
            auto probe = probe_file(prefix, inputBytes, request.limits);
            if (!probe.ok)
                return probe.failure;
            auto layout = make_layout(probe.probe, request.limits.maxSceneBytes);
            if (!layout)
                return error(LoadErrorCode::ResourceLimit, stage, "Scene byte limit");
            if (stop.stop_requested())
                return error(LoadErrorCode::Cancelled, stage);
            report(LoadStage::Decoding, 0, inputBytes);
            auto result = run_helper(file.get(), inputBytes, *layout, request.plyCoordinates, stop);
            if (std::holds_alternative<LoadError>(result))
                return result;
            report(LoadStage::Validating, 0, inputBytes);
            if (stop.stop_requested())
                return error(LoadErrorCode::Cancelled, stage);
            report(LoadStage::Ready, inputBytes, inputBytes);
            if (stop.stop_requested())
                return error(LoadErrorCode::Cancelled, stage);
            return result;
        }
        catch (const ObserverException &)
        {
            return error(LoadErrorCode::ObserverFailure, stage);
        }
        catch (const std::bad_alloc &)
        {
            return error(LoadErrorCode::OutOfMemory, stage);
        }
        catch (const std::exception &)
        {
            return error(LoadErrorCode::DecoderFailure, stage);
        }
    }
};

} // namespace

std::unique_ptr<IModelLoader> make_model_loader()
{
    return std::make_unique<ModelLoader>();
}

} // namespace gs::io
