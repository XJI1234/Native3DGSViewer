#include "gs_cuda/interop.h"
#include "gpu.h"
#include <cuda_runtime_api.h>
#include <d3d12sdklayers.h>
#include <ProcessSnapshot.h>
#include <array>
#include <iostream>
#include <set>
#include <string>

using namespace gs::cuda_ops;
using namespace gs::render::detail;

namespace
{
// Diagnostic only: the native handle-tracing layout is not a public Win32
// contract. Never use it for ownership, cleanup or a production acceptance gate.
struct TraceEntry
{
    HANDLE handle;
    HANDLE process, thread;
    ULONG type;
    void *stack[16];
};
struct TraceQuery
{
    HANDLE handle = nullptr;
    ULONG total = 0;
    TraceEntry entries[128]{};
};
static_assert(offsetof(TraceQuery, entries) == 16);
static_assert(offsetof(TraceEntry, stack) == 32);
using ProcessInfo = LONG(NTAPI *)(HANDLE, ULONG, void *, ULONG);
using QueryInfo = LONG(NTAPI *)(HANDLE, ULONG, void *, ULONG, ULONG *);

std::set<HANDLE> event_handles()
{
    HPSS snapshot = nullptr;
    const auto result = PssCaptureSnapshot(GetCurrentProcess(),
        PSS_CAPTURE_FLAGS(PSS_CAPTURE_HANDLES | PSS_CAPTURE_HANDLE_BASIC_INFORMATION |
            PSS_CAPTURE_HANDLE_TYPE_SPECIFIC_INFORMATION), 0, &snapshot);
    if (result != ERROR_SUCCESS) throw std::runtime_error("PssCaptureSnapshot failed");
    HPSSWALK marker = nullptr;
    if (PssWalkMarkerCreate(nullptr, &marker) != ERROR_SUCCESS)
    {
        PssFreeSnapshot(GetCurrentProcess(), snapshot);
        throw std::runtime_error("PssWalkMarkerCreate failed");
    }
    std::set<HANDLE> handles;
    PSS_HANDLE_ENTRY entry{};
    while (PssWalkSnapshot(snapshot, PSS_WALK_HANDLES, marker, &entry, sizeof(entry)) == ERROR_SUCCESS)
        if (entry.ObjectType == PSS_OBJECT_TYPE_EVENT) handles.insert(entry.Handle);
    PssWalkMarkerFree(marker);
    PssFreeSnapshot(GetCurrentProcess(), snapshot);
    return handles;
}

void print_address(void *address)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module))
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        std::wcout << path << L"+0x" << std::hex << (uintptr_t(address) - uintptr_t(module)) << L'\n';
    }
    else std::cout << address << '\n';
}
}

int main(int argc, char **argv)
{
    try
    {
        GpuDevice gpu(true);
        const std::string mode = argc == 2 ? argv[1] : "buffer";
        if (mode != "buffer" && mode != "context" && mode != "native" &&
            mode != "raw-import" && mode != "raw-wait" && mode != "raw-roundtrip" &&
            mode != "raw-notify" && mode != "fault-stall")
            throw std::invalid_argument("Unknown retirement probe mode");
        auto cuda_checked = [](cudaError_t error) {
            if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
        };
        if (mode == "fault-stall")
        {
            Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
            SharedBuffer buffer(context, 4);
            context.begin(gpu.direct.Get());
            fill_u32(context, buffer, 123, 1);
            context.end(gpu.direct.Get());
            if (!context.wait_idle()) throw std::runtime_error("Fault probe warmup timeout");
            ComPtr<ID3D12Fence> gate;
            check(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)), "Fault gate");
            check(gpu.direct->Wait(gate.Get(), 1), "Fault queue wait");
            context.begin(gpu.direct.Get());
            fill_u32(context, buffer, 456, 1);
            context.end(gpu.direct.Get());
            if (context.wait_idle(10)) throw std::runtime_error("Fault did not stall");
            // Intentionally never resolve the gate. Run only under the external
            // supervisor; this demonstrates why in-process deadlines cannot
            // interrupt every CUDA API. No TDR or driver configuration changes.
            std::cout << "BLOCKING_CUDA_API_READY\n" << std::flush;
            cuda_checked(cudaStreamSynchronize(static_cast<cudaStream_t>(context.native_stream())));
            throw std::runtime_error("Unexpected completion of blocked API");
        }
        auto cycle = [&] {
            if (mode == "native" || mode.starts_with("raw-"))
            {
                ComPtr<ID3D12Fence> fence;
                check(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence)), "Raw fence");
                if (mode == "native")
                {
                    check(gpu.direct->Signal(fence.Get(), 1), "Native signal");
                    gpu.wait(fence.Get(), 1);
                    return;
                }
                cudaStream_t stream = nullptr;
                cuda_checked(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
                HANDLE handle = nullptr;
                check(gpu.device->CreateSharedHandle(fence.Get(), nullptr, GENERIC_ALL, nullptr, &handle), "Raw handle");
                cudaExternalSemaphoreHandleDesc desc{};
                desc.type = cudaExternalSemaphoreHandleTypeD3D12Fence;
                desc.handle.win32.handle = handle;
                cudaExternalSemaphore_t semaphore = nullptr;
                const auto imported = cudaImportExternalSemaphore(&semaphore, &desc);
                CloseHandle(handle);
                cuda_checked(imported);
                if (mode != "raw-import")
                {
                    check(gpu.direct->Signal(fence.Get(), 1), "Raw producer");
                    cudaExternalSemaphoreWaitParams wait{};
                    wait.params.fence.value = 1;
                    cuda_checked(cudaWaitExternalSemaphoresAsync(&semaphore, &wait, 1, stream));
                    if (mode == "raw-roundtrip" || mode == "raw-notify")
                    {
                        cudaExternalSemaphoreSignalParams signal{};
                        signal.params.fence.value = 2;
                        cuda_checked(cudaSignalExternalSemaphoresAsync(&semaphore, &signal, 1, stream));
                        check(gpu.direct->Wait(fence.Get(), 2), "Raw consumer");
                    }
                }
                cuda_checked(cudaStreamSynchronize(stream));
                check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Raw tail");
                gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
                if (mode == "raw-notify")
                {
                    ComPtr<ID3D12SharingContract> sharing;
                    const auto result = gpu.direct.As(&sharing);
                    std::cout << "sharing_contract_hr=0x" << std::hex << result << '\n';
                    // Diagnostic notification only after actual GPU completion.
                    // Never use it to complete an unresolved CUDA dependency.
                    if (sharing) sharing->SharedFenceSignal(fence.Get(), 2);
                }
                cuda_checked(cudaDestroyExternalSemaphore(semaphore));
                cuda_checked(cudaStreamDestroy(stream));
                return;
            }
            Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
            if (mode == "context") return;
            SharedBuffer buffer(context, 4);
            context.begin(gpu.direct.Get());
            fill_u32(context, buffer, 123, 1);
            context.end(gpu.direct.Get());
            check(gpu.direct->Signal(gpu.direct_fence.Get(), ++gpu.direct_value), "Trace cycle");
            gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
            if (!context.wait_idle()) throw std::runtime_error("Trace timeout");
        };
        std::cout << "mode=" << mode << '\n';
        for (int i = 0; i < 5; ++i) cycle();
        auto set = reinterpret_cast<ProcessInfo>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess"));
        auto query = reinterpret_cast<QueryInfo>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
        if (!set || !query) throw std::runtime_error("Native tracing unavailable");
        ULONG enabled = 0;
        const auto status = set(GetCurrentProcess(), 32, &enabled, sizeof(enabled));
        std::cout << "enable_status=0x" << std::hex << status << '\n';
        if (status < 0) throw std::runtime_error("Handle tracing unavailable; no acceptance conclusion");
        const auto before = event_handles();
        for (int i = 0; i < 10; ++i) cycle();
        const auto after = event_handles();
        std::cout << "Event before=" << std::dec << before.size() << " after=" << after.size() << '\n';
        size_t printed = 0;
        for (const auto handle : after)
        {
            if (before.contains(handle)) continue;
            std::cout << "new Event=" << handle << '\n';
            TraceQuery traces;
            traces.handle = handle;
            const auto queried = query(GetCurrentProcess(), 32, &traces, sizeof(traces), nullptr);
            std::cout << "query_status=0x" << std::hex << queried << " traces=" << std::dec << traces.total << '\n';
            if (queried < 0 || traces.total > std::size(traces.entries)) continue;
            for (ULONG i = 0; i < traces.total; ++i)
            {
                const auto &trace = traces.entries[i];
                std::cout << "trace type=" << trace.type << " handle=" << trace.handle << '\n';
                for (auto *address : trace.stack) if (address) print_address(address);
            }
            if (++printed == 3) break;
        }
        enabled = 0;
        set(GetCurrentProcess(), 32, nullptr, 0);
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
