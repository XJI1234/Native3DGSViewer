#include "gpu.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace gs::render::detail
{
void check(HRESULT hr, const char *operation, RenderErrorCode code)
{
    if (FAILED(hr))
        throw GpuFailure{hr, operation, code};
}
void transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    if (from == to)
        return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    list->ResourceBarrier(1, &b);
}
void uav_barrier(ID3D12GraphicsCommandList *list)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    list->ResourceBarrier(1, &b);
}
GpuDevice::GpuDevice(bool diagnostics, std::optional<LUID> required_adapter)
{
    UINT flags = 0;
    ComPtr<ID3D12Debug> debug;
    if (diagnostics && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
        flags = DXGI_CREATE_FACTORY_DEBUG;
    }
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
    if (diagnostics && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
    {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
    check(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory)), "DXGI factory");
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumerated = factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (enumerated == DXGI_ERROR_NOT_FOUND)
            break;
        check(enumerated, "Adapter enumeration");
        DXGI_ADAPTER_DESC1 desc{};
        check(candidate->GetDesc1(&desc), "Adapter description");
        if (required_adapter && (desc.AdapterLuid.HighPart != required_adapter->HighPart ||
                                 desc.AdapterLuid.LowPart != required_adapter->LowPart))
            continue;
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        ComPtr<ID3D12Device> found;
        if (FAILED(
                D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&found))))
            continue;
        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_0};
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 options{};
        D3D12_FEATURE_DATA_FORMAT_SUPPORT format{DXGI_FORMAT_B8G8R8A8_UNORM};
        if (FAILED(found->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) ||
            FAILED(found->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options,
                                              sizeof(options))) ||
            !options.WaveOps || options.WaveLaneCountMin < 16 || options.WaveLaneCountMax > 128 ||
            FAILED(found->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &format,
                                              sizeof(format))) ||
            !(format.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ||
            !(format.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE))
            continue;
        device = found;
        check(candidate.As(&adapter), "Adapter3");
        adapter_description = desc;
        wave_min = options.WaveLaneCountMin;
        wave_max = options.WaveLaneCountMax;
        break;
    }
    if (!device)
        throw GpuFailure{DXGI_ERROR_UNSUPPORTED, "D3D12 hardware SM6 wave16-128"};
    const auto diagnostic = std::wstring(L"render-core: ") + adapter_description.Description +
                            L" LUID=" + std::to_wstring(adapter_description.AdapterLuid.HighPart) +
                            L":" + std::to_wstring(adapter_description.AdapterLuid.LowPart) +
                            L" FL12.0 SM6.0 wave=" + std::to_wstring(wave_min) + L"-" +
                            std::to_wstring(wave_max) + L"\n";
    OutputDebugStringW(diagnostic.c_str());
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&direct)), "Direct queue");
    q.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&copy)), "Copy queue");
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&direct_fence)),
          "Direct fence");
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_fence)), "Copy fence");
    check(direct->GetTimestampFrequency(&timestamp_frequency), "Timestamp frequency");
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_)
        throw GpuFailure{HRESULT_FROM_WIN32(GetLastError()), "Fence event"};
}
GpuDevice::~GpuDevice()
{
    if (event_)
        CloseHandle(event_);
}
ComPtr<ID3D12Resource> GpuDevice::buffer(uint64_t bytes, D3D12_HEAP_TYPE heap,
                                         D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heap;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (std::max)(bytes, 4ull);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
                                          IID_PPV_ARGS(&result)),
          "Buffer allocation");
    result->SetName((L"Buffer " + std::to_wstring(bytes)).c_str());
    return result;
}
uint64_t GpuDevice::submit(ID3D12CommandQueue *queue, ID3D12CommandList *list, ID3D12Fence *fence,
                           uint64_t &value)
{
    queue->ExecuteCommandLists(1, &list);
    check(queue->Signal(fence, ++value), "Queue signal");
    return value;
}
void GpuDevice::wait(ID3D12Fence *fence, uint64_t value)
{
    auto done = fence->GetCompletedValue();
    if (done == UINT64_MAX)
        throw GpuFailure{DXGI_ERROR_DEVICE_REMOVED, "Fence removed"};
    if (done >= value)
        return;
    check(fence->SetEventOnCompletion(value, event_), "Fence event registration");
    const auto waited = WaitForSingleObject(event_, 5000);
    const auto wait_error = waited == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
    check(device->GetDeviceRemovedReason(), "Device removed");
    done = fence->GetCompletedValue();
    if (done == UINT64_MAX)
        throw GpuFailure{DXGI_ERROR_DEVICE_REMOVED, "Fence removed"};
    if (done >= value)
        return;
    if (waited == WAIT_FAILED)
        throw GpuFailure{HRESULT_FROM_WIN32(wait_error), "Fence event wait"};
    throw GpuFailure{HRESULT_FROM_WIN32(ERROR_TIMEOUT), "GPU fence deadline",
                     RenderErrorCode::GpuTimeout};
}
std::vector<uint8_t> GpuDevice::readback(ID3D12Resource *resource, uint64_t bytes,
                                         D3D12_RESOURCE_STATES state)
{
    auto out = buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12CommandAllocator> allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
          "Readback allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&list)),
          "Readback list");
    transition(list.Get(), resource, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(out.Get(), 0, resource, 0, bytes);
    transition(list.Get(), resource, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
    check(list->Close(), "Readback close");
    wait(direct_fence.Get(), submit(direct.Get(), list.Get(), direct_fence.Get(), direct_value));
    std::vector<uint8_t> result(bytes);
    void *mapped = nullptr;
    D3D12_RANGE range{0, size_t(bytes)};
    check(out->Map(0, &range, &mapped), "Readback map");
    memcpy(result.data(), mapped, bytes);
    D3D12_RANGE empty{};
    out->Unmap(0, &empty);
    return result;
}
std::vector<std::string> GpuDevice::debug_errors() const
{
    ComPtr<ID3D12InfoQueue> info;
    std::vector<std::string> errors;
    if (FAILED(device.As(&info)))
        return errors;
    for (uint64_t i = 0; i < info->GetNumStoredMessages(); ++i)
    {
        SIZE_T n = 0;
        info->GetMessage(i, nullptr, &n);
        std::vector<uint8_t> bytes(n);
        auto *m = reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
        if (SUCCEEDED(info->GetMessage(i, m, &n)) && m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            errors.emplace_back(m->pDescription);
    }
    return errors;
}
std::vector<uint8_t> shader(const char *name)
{
    std::array<wchar_t, 32768> executable{};
    DWORD length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
    if (!length || length >= executable.size())
        throw GpuFailure{E_FAIL, "Executable path"};
    const auto path = std::filesystem::path(executable.data()).parent_path() / "shaders" /
                      (std::string(name) + ".cso");
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw GpuFailure{E_FAIL, "Shader file", RenderErrorCode::ShaderFailure};
    auto size = stream.tellg();
    if (size <= 0 || size > (16 << 20))
        throw GpuFailure{E_FAIL, "Shader size", RenderErrorCode::ShaderFailure};
    std::vector<uint8_t> bytes(size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()), size);
    if (!stream)
        throw GpuFailure{E_FAIL, "Shader read", RenderErrorCode::ShaderFailure};
    return bytes;
}
ComPtr<ID3D12RootSignature> root_signature(ID3D12Device *device,
                                           std::span<const D3D12_ROOT_PARAMETER> parameters)
{
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = UINT(parameters.size());
    desc.pParameters = parameters.data();
    ComPtr<ID3DBlob> blob, error;
    check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
          "Root signature serialization", RenderErrorCode::ShaderFailure);
    ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                      IID_PPV_ARGS(&root)),
          "Root signature");
    return root;
}
} // namespace gs::render::detail
