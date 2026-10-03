#include "gs_server/windows_client.h"
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <chrono>
#include <cstring>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <stdexcept>
#include <wrl/client.h>

namespace gs::server
{
namespace
{
using Microsoft::WRL::ComPtr;
bool test_failure(const wchar_t *name)
{
    wchar_t value[32]{};
    return GetEnvironmentVariableW(L"GS_THIN_TEST_FAILURE", value, 32) &&
           std::wstring(value) == name;
}
class WindowOwner
{
  public:
    explicit WindowOwner(HWND handle) : handle_(handle)
    {
    }
    ~WindowOwner()
    {
        if (IsWindow(handle_))
            DestroyWindow(handle_);
    }

  private:
    HWND handle_;
};
void checked(HRESULT status, const char *stage)
{
    if (FAILED(status))
        throw std::runtime_error(std::string(stage) + ": " + std::to_string(uint32_t(status)));
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM first, LPARAM second)
{
    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, first, second);
}
} // namespace
struct ThinRenderer::Impl
{
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12InfoQueue> validation;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12Resource> last_output;
    HANDLE event = nullptr;
    uint64_t fence_value = 0;
    std::string adapter_name;
    uint64_t allocated = 0;
    bool submitted = false;
    std::vector<ComPtr<IUnknown>> pending_resources;
    Impl()
    {
        wchar_t debug_flag[2]{};
        const bool debug_enabled = GetEnvironmentVariableW(L"GS_D3D12_DEBUG", debug_flag, 2) != 0;
        if (debug_enabled)
        {
            ComPtr<ID3D12Debug> debug;
            checked(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 debug layer unavailable");
            debug->EnableDebugLayer();
        }
        checked(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
        for (UINT index = 0;
             factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++index)
        {
            DXGI_ADAPTER_DESC1 info{};
            checked(adapter->GetDesc1(&info), "Adapter info");
            if (!(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                            IID_PPV_ARGS(&device))))
            {
                for (wchar_t character : info.Description)
                {
                    if (!character)
                        break;
                    adapter_name += character < 128 ? char(character) : '?';
                }
                break;
            }
            adapter.Reset();
        }
        if (!device)
            throw std::runtime_error("No hardware D3D12 adapter");
        if (debug_enabled)
        {
            checked(device.As(&validation), "D3D12 validation queue");
            checked(validation->SetMessageCountLimit(1024), "D3D12 validation limit");
        }
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)), "Command queue");
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&allocator)),
                "Allocator");
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                          nullptr, IID_PPV_ARGS(&commands)),
                "Command list");
        checked(commands->Close(), "Initial close");
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Fence");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event)
            throw std::runtime_error("Fence event creation failed");
    }
    ~Impl()
    {
        if (submitted)
        {
            const auto completion = ++fence_value;
            while (SUCCEEDED(device->GetDeviceRemovedReason()) &&
                   FAILED(queue->Signal(fence.Get(), completion)))
                Sleep(1);
            while (SUCCEEDED(device->GetDeviceRemovedReason()) &&
                   fence->GetCompletedValue() < completion)
                Sleep(1);
        }
        if (event)
            CloseHandle(event);
    }
    ComPtr<ID3D12Resource> resource(D3D12_RESOURCE_DESC desc, D3D12_HEAP_TYPE heap_type,
                                    D3D12_RESOURCE_STATES state,
                                    const D3D12_CLEAR_VALUE *clear = nullptr)
    {
        if (test_failure(L"allocation"))
            throw std::runtime_error("Injected resource allocation failure");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = heap_type;
        const auto info = device->GetResourceAllocationInfo(0, 1, &desc);
        if (info.SizeInBytes == UINT64_MAX)
            throw std::runtime_error("Invalid resource allocation");
        ComPtr<IDXGIAdapter3> budget_adapter;
        if (SUCCEEDED(adapter.As(&budget_adapter)))
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO budget{};
            if (SUCCEEDED(budget_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                                                               &budget)) &&
                (budget.CurrentUsage >= budget.Budget ||
                 info.SizeInBytes > (budget.Budget - budget.CurrentUsage) * 0.9))
                throw std::runtime_error("Client GPU ResourceLimit");
        }
        ComPtr<ID3D12Resource> value;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear,
                                                IID_PPV_ARGS(&value)),
                "Client resource");
        allocated += info.SizeInBytes;
        return value;
    }
    ComPtr<ID3D12Resource> buffer(uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return resource(desc, heap, state);
    }
    ComPtr<ID3D12Resource> texture(uint32_t width, uint32_t height, DXGI_FORMAT format,
                                   D3D12_RESOURCE_STATES state, bool target)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = target ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = format;
        clear.Color[3] = 1;
        return resource(desc, D3D12_HEAP_TYPE_DEFAULT, state, target ? &clear : nullptr);
    }
    void transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        commands->ResourceBarrier(1, &barrier);
    }
    void submit()
    {
        checked(commands->Close(), "Close client commands");
        ID3D12CommandList *lists[]{commands.Get()};
        submitted = true;
        queue->ExecuteCommandLists(1, lists);
        if (test_failure(L"submitted"))
            throw std::runtime_error("Injected post-submission failure");
        checked(queue->Signal(fence.Get(), ++fence_value), "Client signal");
        checked(fence->SetEventOnCompletion(fence_value, event), "Client fence event");
        if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
            throw std::runtime_error("Client GPU timeout");
        submitted = false;
        pending_resources.clear();
        if (validation)
        {
            for (uint64_t index = 0; index < validation->GetNumStoredMessages(); ++index)
            {
                SIZE_T bytes = 0;
                checked(validation->GetMessage(index, nullptr, &bytes), "D3D12 validation size");
                std::vector<uint8_t> storage(bytes);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                checked(validation->GetMessage(index, message, &bytes), "D3D12 validation message");
                if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                    message->Severity == D3D12_MESSAGE_SEVERITY_ERROR)
                    throw std::runtime_error(std::string("D3D12 validation: ") +
                                             message->pDescription);
            }
            validation->ClearStoredMessages();
        }
    }
};
ThinRenderer::ThinRenderer() : impl_(std::make_unique<Impl>())
{
}
ThinRenderer::~ThinRenderer() = default;
std::string ThinRenderer::adapter() const
{
    return impl_->adapter_name;
}
ClientRender ThinRenderer::render(const Frame &frame)
{
    auto &context = *impl_;
    if (!frame.width || !frame.height || frame.width > 4096 || frame.height > 4096 ||
        frame.rgba.size() != uint64_t(frame.width) * frame.height * 4)
        throw std::runtime_error("Invalid image layout");
    profile_name(frame.profile);
    if (context.submitted)
        throw std::runtime_error("Client GPU completion unknown; recreate renderer");
    context.allocated = 0;
    context.last_output.Reset();
    auto output = context.texture(frame.width, frame.height, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  D3D12_RESOURCE_STATE_COPY_DEST, true);
    const auto output_desc = output->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t readback_bytes = 0;
    context.device->GetCopyableFootprints(&output_desc, 0, 1, 0, &footprint, nullptr, nullptr,
                                          &readback_bytes);
    auto readback =
        context.buffer(readback_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    auto query_readback =
        context.buffer(32, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_QUERY_HEAP_DESC query_desc{};
    query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query_desc.Count = 4;
    ComPtr<ID3D12QueryHeap> queries;
    checked(context.device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&queries)), "Timestamp heap");
    ComPtr<ID3D12Resource> upload;
    {
        upload = context.buffer(readback_bytes, D3D12_HEAP_TYPE_UPLOAD,
                                D3D12_RESOURCE_STATE_GENERIC_READ);
        uint8_t *mapped = nullptr;
        D3D12_RANGE empty{};
        checked(upload->Map(0, &empty, reinterpret_cast<void **>(&mapped)), "Image upload map");
        for (uint32_t row = 0; row < frame.height; ++row)
            std::memcpy(mapped + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch,
                        frame.rgba.data() + size_t(row) * frame.width * 4, size_t(frame.width) * 4);
        upload->Unmap(0, nullptr);
        context.pending_resources = {output, readback, query_readback, upload, queries};
        checked(context.allocator->Reset(), "Reset client allocator");
        checked(context.commands->Reset(context.allocator.Get(), nullptr), "Reset client commands");
        context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = output.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        context.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
        context.transition(output.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = output.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    context.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
    context.commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 4,
                                       query_readback.Get(), 0);
    context.submit();
    uint64_t frequency = 0;
    checked(context.queue->GetTimestampFrequency(&frequency), "Timestamp frequency");
    uint64_t *timestamps = nullptr;
    D3D12_RANGE query_range{0, 32};
    checked(query_readback->Map(0, &query_range, reinterpret_cast<void **>(&timestamps)),
            "Timestamp readback");
    ClientRender result;
    result.upload_ms = double(timestamps[1] - timestamps[0]) * 1000 / frequency;
    result.draw_ms = double(timestamps[2] - timestamps[1]) * 1000 / frequency;
    result.readback_ms = double(timestamps[3] - timestamps[2]) * 1000 / frequency;
    D3D12_RANGE no_write{};
    query_readback->Unmap(0, &no_write);
    const auto copy_start = std::chrono::steady_clock::now();
    uint8_t *mapped = nullptr;
    D3D12_RANGE read_range{0, SIZE_T(readback_bytes)};
    checked(readback->Map(0, &read_range, reinterpret_cast<void **>(&mapped)), "Capture readback");
    result.rgba.resize(size_t(frame.width) * frame.height * 4);
    for (uint32_t row = 0; row < frame.height; ++row)
        std::memcpy(result.rgba.data() + size_t(row) * frame.width * 4,
                    mapped + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch,
                    size_t(frame.width) * 4);
    D3D12_RANGE written{};
    readback->Unmap(0, &written);
    result.readback_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - copy_start)
            .count();
    result.allocated_bytes = context.allocated;
    context.last_output = std::move(output);
    return result;
}
void ThinRenderer::show(const Frame &frame)
{
    auto &context = *impl_;
    if (context.submitted || !context.last_output)
        throw std::runtime_error("Client frame is not ready for presentation");
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"Native3DGSThinFrame";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    RECT rectangle{0, 0, LONG(frame.width), LONG(frame.height)};
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window =
        CreateWindowW(type.lpszClassName, L"Native3DGS remote single frame", WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
                      rectangle.bottom - rectangle.top, nullptr, nullptr, type.hInstance, nullptr);
    if (!window)
        throw std::runtime_error("Client window creation failed");
    WindowOwner window_owner(window);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = frame.width;
    desc.Height = frame.height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapchain;
    checked(context.factory->CreateSwapChainForHwnd(context.queue.Get(), window, &desc, nullptr,
                                                    nullptr, &swapchain),
            "Client swapchain");
    ComPtr<ID3D12Resource> backbuffer;
    checked(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "Client backbuffer");
    context.pending_resources = {backbuffer, context.last_output, swapchain};
    checked(context.allocator->Reset(), "Present allocator");
    checked(context.commands->Reset(context.allocator.Get(), nullptr), "Present commands");
    context.transition(backbuffer.Get(), D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_COPY_DEST);
    context.commands->CopyResource(backbuffer.Get(), context.last_output.Get());
    context.transition(backbuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_PRESENT);
    context.submit();
    ShowWindow(window, SW_SHOW);
    checked(swapchain->Present(1, 0), "Client Present");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
} // namespace gs::server
