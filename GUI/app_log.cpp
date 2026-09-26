#include "pch.h"
#include "app_log.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <wrl/client.h>

namespace viewer::log
{
namespace
{
std::mutex log_mutex;
HANDLE log_file = INVALID_HANDLE_VALUE;
std::string run_id;
std::filesystem::path app_directory;

bool is_run_log(const std::wstring &name)
{
    if (name.size() < 26 || name[8] != L'-' || name[15] != L'-' ||
        name[19] != L'Z' || name[20] != L'-' ||
        name.compare(name.size() - 4, 4, L".log") != 0)
        return false;
    const auto digits = [&](size_t first, size_t last) {
        return std::all_of(name.begin() + first, name.begin() + last,
                           [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    };
    return digits(0, 8) && digits(9, 15) && digits(16, 19) &&
           digits(21, name.size() - 4);
}

void prune_logs(const std::filesystem::path &directory) noexcept
{
    try
    {
        const auto cutoff = std::filesystem::file_time_type::clock::now() -
                            std::chrono::days(30);
        std::error_code ec;
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end;
             it.increment(ec))
        {
            if (!is_run_log(it->path().filename().wstring()) || !it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }
            const auto modified = it->last_write_time(ec);
            if (!ec && modified < cutoff)
                std::filesystem::remove(it->path(), ec);
            ec.clear();
        }
    }
    catch (...) {}
}

bool open_log(const std::filesystem::path &directory, const wchar_t *filename)
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
    log_file = CreateFileW((directory / filename).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_file == INVALID_HANDLE_VALUE) return false;
    prune_logs(directory);
    return true;
}

std::string timestamp()
{
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char buffer[40]{};
    snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
             time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
             time.wSecond, time.wMilliseconds);
    return buffer;
}

void append_quoted(std::string &out, std::string_view value)
{
    static constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += char(c);
        }
        else if (c < 0x20)
        {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        }
        else out += char(c);
    }
    out += '"';
}

std::string number(uint64_t value) { return std::to_string(value); }

void record_adapter(IDXGIAdapter1 *adapter, UINT index)
{
    DXGI_ADAPTER_DESC1 desc{};
    const HRESULT described = adapter->GetDesc1(&desc);
    if (FAILED(described))
    {
        write("error", "adapter_description_failed",
              {{"index", number(index)}, {"hr", hresult(described)}});
        return;
    }
    write("info", "gpu_adapter",
          {{"index", number(index)}, {"name", utf8(desc.Description)},
           {"vendor_id", number(desc.VendorId)}, {"device_id", number(desc.DeviceId)},
           {"dedicated_video_bytes", number(desc.DedicatedVideoMemory)},
           {"shared_system_bytes", number(desc.SharedSystemMemory)},
           {"software", (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? "true" : "false"}});

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    const HRESULT created = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0,
                                               IID_PPV_ARGS(&device));
    if (FAILED(created))
    {
        write("warn", "gpu_device_probe_failed",
              {{"index", number(index)}, {"feature_level", "12_0"},
               {"hr", hresult(created)}});
        return;
    }
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model_6_6{D3D_SHADER_MODEL_6_6};
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options{};
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format{DXGI_FORMAT_B8G8R8A8_UNORM};
    const HRESULT sm_result = device->CheckFeatureSupport(
        D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    const HRESULT sm66_result = device->CheckFeatureSupport(
        D3D12_FEATURE_SHADER_MODEL, &shader_model_6_6, sizeof(shader_model_6_6));
    const HRESULT wave_result = device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS1, &options, sizeof(options));
    const HRESULT format_result = device->CheckFeatureSupport(
        D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format));
    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
    const HRESULT architecture_result = device->CheckFeatureSupport(
        D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture));
    D3D12_FEATURE_DATA_ARCHITECTURE legacy_architecture{};
    const HRESULT legacy_architecture_result = FAILED(architecture_result)
        ? device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &legacy_architecture,
                                      sizeof(legacy_architecture))
        : S_OK;
    const bool architecture_known = SUCCEEDED(architecture_result) ||
                                    SUCCEEDED(legacy_architecture_result);
    const bool uma = SUCCEEDED(architecture_result) ? architecture.UMA != FALSE :
                     legacy_architecture.UMA != FALSE;
    const auto shader_model_text = SUCCEEDED(sm_result)
        ? number(shader_model.HighestShaderModel >> 4) + "." +
              number(shader_model.HighestShaderModel & 0xf)
        : "unknown";
    write("info", "gpu_capabilities",
          {{"index", number(index)}, {"feature_level_12_0", "true"},
           {"shader_model_query_hr", hresult(sm_result)},
           {"shader_model", shader_model_text},
           {"shader_model_6_6", SUCCEEDED(sm66_result) &&
               shader_model_6_6.HighestShaderModel >= D3D_SHADER_MODEL_6_6 ? "true" : "false"},
           {"shader_model_6_6_query_hr", hresult(sm66_result)},
           {"wave_query_hr", hresult(wave_result)},
           {"wave_ops", SUCCEEDED(wave_result) ? (options.WaveOps ? "true" : "false") : "unknown"},
           {"wave_min", SUCCEEDED(wave_result) ? number(options.WaveLaneCountMin) : "unknown"},
           {"wave_max", SUCCEEDED(wave_result) ? number(options.WaveLaneCountMax) : "unknown"},
           {"format_query_hr", hresult(format_result)},
           {"architecture_query_hr", hresult(architecture_result)},
           {"legacy_architecture_query_hr", FAILED(architecture_result) ?
               hresult(legacy_architecture_result) : "not_queried"},
           {"uma", architecture_known ? (uma ? "true" : "false") : "unknown"},
           {"bgra8_render_target", SUCCEEDED(format_result) &&
               (format.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ? "true" : "false"},
           {"bgra8_blendable", SUCCEEDED(format_result) &&
               (format.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) ? "true" : "false"}});
}
} // namespace

std::string utf8(std::wstring_view value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (!size) return "<conversion failed>";
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

std::string hresult(long value)
{
    char buffer[16]{};
    snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(value));
    return buffer;
}

bool initialize() noexcept
{
    try
    {
        std::lock_guard lock(log_mutex);
        if (log_file != INVALID_HANDLE_VALUE) return true;
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
        if (!length || length >= executable.size()) return false;
        executable.resize(length);
        const auto executable_directory = std::filesystem::path(executable).parent_path();
        SYSTEMTIME time{};
        GetSystemTime(&time);
        wchar_t filename[80]{};
        swprintf_s(filename, L"%04u%02u%02u-%02u%02u%02u-%03uZ-%lu.log",
                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                   time.wSecond, time.wMilliseconds, GetCurrentProcessId());
        app_directory = executable_directory;
        run_id = utf8(filename);
        if (!open_log(executable_directory / L"logs", filename))
        {
            std::wstring local_appdata(32768, L'\0');
            const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata.data(),
                                                       DWORD(local_appdata.size()));
            if (!size || size >= local_appdata.size()) return false;
            local_appdata.resize(size);
            if (!open_log(std::filesystem::path(local_appdata) / L"Native3DGSViewer" / L"logs",
                          filename))
                return false;
        }
        return true;
    }
    catch (...) { return false; }
}

void close() noexcept
{
    std::lock_guard lock(log_mutex);
    if (log_file != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(log_file);
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
    }
}

void write(std::string_view level, std::string_view event,
           std::initializer_list<Field> fields) noexcept
{
    try
    {
        std::lock_guard lock(log_mutex);
        if (log_file == INVALID_HANDLE_VALUE) return;
        std::string line = "{\"time\":";
        append_quoted(line, timestamp());
        line += ",\"run\":";
        append_quoted(line, run_id);
        line += ",\"level\":";
        append_quoted(line, level);
        line += ",\"event\":";
        append_quoted(line, event);
        for (const auto &field : fields)
        {
            line += ',';
            append_quoted(line, field.name);
            line += ':';
            append_quoted(line, field.value);
        }
        line += "}\n";
        DWORD written = 0;
        if (!WriteFile(log_file, line.data(), DWORD(line.size()), &written, nullptr) ||
            written != DWORD(line.size()))
            return;
        if (event == "app_start" || event == "engine_created")
            FlushFileBuffers(log_file);
    }
    catch (...) {}
}

void record_host_diagnostics() noexcept
{
    try
    {
        using RtlGetVersionFn = LONG (WINAPI *)(OSVERSIONINFOW *);
        auto ntdll = GetModuleHandleW(L"ntdll.dll");
        auto version_fn = ntdll ? reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
        OSVERSIONINFOW version{sizeof(version)};
        if (version_fn && version_fn(&version) == 0)
            write("info", "os_version",
                  {{"major", number(version.dwMajorVersion)},
                   {"minor", number(version.dwMinorVersion)},
                   {"build", number(version.dwBuildNumber)}});
        else write("warn", "os_version_unavailable");

        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        MEMORYSTATUSEX memory{sizeof(memory)};
        const BOOL memory_ok = GlobalMemoryStatusEx(&memory);
        write("info", "host_hardware",
              {{"cpu_arch", number(system.wProcessorArchitecture)},
               {"logical_processors", number(system.dwNumberOfProcessors)},
               {"physical_memory_bytes", memory_ok ? number(memory.ullTotalPhys) : "unknown"}});

        for (const char *name : {"count", "reduce", "scan", "scan_add", "scatter",
                                 "reset_args", "project", "vertex", "pixel"})
        {
            const auto path = app_directory / "shaders" / (std::string(name) + ".cso");
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            write(ec || !size ? "error" : "info", "shader_file",
                  {{"name", name}, {"bytes", ec ? "unavailable" : number(size)},
                   {"present", ec || !size ? "false" : "true"}});
        }
        for (const char *name : {"count", "reduce", "scan", "scan_add", "scatter"})
            for (const char *variant : {"wave32", "wave_agnostic"})
            {
                const std::string shader_name = std::string(name) + "_" + variant;
                const auto path = app_directory / "shaders" / (shader_name + ".cso");
                std::error_code ec;
                const auto size = std::filesystem::file_size(path, ec);
                write(ec || !size ? "error" : "info", "shader_file",
                      {{"name", shader_name},
                       {"bytes", ec ? "unavailable" : number(size)},
                       {"present", ec || !size ? "false" : "true"}});
            }

        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        const HRESULT factory_result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (FAILED(factory_result))
        {
            write("error", "dxgi_factory_probe_failed", {{"hr", hresult(factory_result)}});
            return;
        }
        for (UINT index = 0; index < 32; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            const HRESULT enumerated = factory->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
            if (enumerated == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(enumerated))
            {
                write("error", "adapter_enumeration_failed",
                      {{"index", number(index)}, {"hr", hresult(enumerated)}});
                break;
            }
            record_adapter(adapter.Get(), index);
        }
    }
    catch (const std::exception &e)
    {
        write("error", "host_probe_exception", {{"diagnostic", e.what()}});
    }
    catch (...) { write("error", "host_probe_exception"); }
}
} // namespace viewer::log
