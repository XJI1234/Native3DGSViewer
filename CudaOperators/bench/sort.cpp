#include "gs_cuda/sort.h"
#include "gpu.h"
#include <cuda_runtime_api.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>

using namespace gs::cuda_ops;
using namespace gs::render::detail;
namespace
{
void cuda_check(cudaError_t error) { if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error)); }
struct Events
{
    cudaEvent_t first = nullptr, last = nullptr;
    Events() { cuda_check(cudaEventCreate(&first)); try { cuda_check(cudaEventCreate(&last)); } catch (...) { cudaEventDestroy(first); throw; } }
    ~Events() { cudaEventDestroy(first); cudaEventDestroy(last); }
};
}
int main(int argc, char **argv)
{
    try
    {
        uint32_t count = 1000000;
        if (argc != 3 && argc != 4) throw std::invalid_argument("Usage: CudaSortBench count output.csv [i32|i64]");
        const std::string offset_mode = argc == 4 ? argv[3] : "i64";
        if (offset_mode != "i32" && offset_mode != "i64") throw std::invalid_argument("Invalid offset mode");
        auto end = argv[1] + std::strlen(argv[1]);
        auto parsed = std::from_chars(argv[1], end, count);
        if (parsed.ec != std::errc{} || parsed.ptr != end || !count || count > 22480361) throw std::invalid_argument("Invalid sort count");
        std::ofstream output(argv[2]);
        if (!output) throw std::runtime_error("Cannot open output");
        output << "distribution,round,backend,iteration,count,sort_and_interop_ms,cuda_kernel_ms\n";
        GpuDevice gpu;
        Context context(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
        SortWorkspace cub(context, count, false, offset_mode == "i32" ? SortOffsets::Int32 : SortOffsets::Int64);
        SortPass native(gpu);
        auto buffers = native.allocate(count);
        const auto bytes = uint64_t(count) * 4;
        SharedBuffer keys0(context, bytes), keys1(context, bytes), values0(context, bytes), values1(context, bytes);
        auto upload = gpu.buffer(bytes * 2, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto readback = gpu.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        ComPtr<ID3D12QueryHeap> queries;
        D3D12_QUERY_HEAP_DESC query_desc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
        check(gpu.device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&queries)), "Sort bench query heap");
        ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Sort bench allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Sort bench list");
        check(list->Close(), "Sort bench initial close");
        ComPtr<ID3D12CommandAllocator> consumer_allocator; ComPtr<ID3D12GraphicsCommandList> consumer_list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&consumer_allocator)), "Sort bench consumer allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, consumer_allocator.Get(), nullptr, IID_PPV_ARGS(&consumer_list)), "Sort bench consumer list");
        check(consumer_list->Close(), "Sort bench consumer initial close");
        auto reset = [&] { check(allocator->Reset(), "Sort bench reset allocator"); check(list->Reset(allocator.Get(), nullptr), "Sort bench reset list"); };
        auto submit = [&] {
            check(list->Close(), "Sort bench close"); gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value);
        };
        Events events;
        std::vector<uint32_t> source(count), expected(count);
        for (const auto *distribution : {"random", "sorted", "reverse", "equal"})
        {
            std::mt19937 random(42);
            for (auto &key : source) key = std::strcmp(distribution, "equal") ? random() : 17;
            if (!std::strcmp(distribution, "sorted") || !std::strcmp(distribution, "reverse")) std::sort(source.begin(), source.end());
            if (!std::strcmp(distribution, "reverse")) std::reverse(source.begin(), source.end());
            std::iota(expected.begin(), expected.end(), 0u);
            std::stable_sort(expected.begin(), expected.end(), [&](auto a, auto b) { return source[a] < source[b]; });
            void *mapped = nullptr; D3D12_RANGE empty{};
            check(upload->Map(0, &empty, &mapped), "Sort bench input map");
            std::memcpy(mapped, source.data(), size_t(bytes));
            std::iota(static_cast<uint32_t *>(mapped) + count, static_cast<uint32_t *>(mapped) + count * 2ull, 0u);
            upload->Unmap(0, nullptr);
            for (uint32_t round = 1; round <= 3; ++round) for (uint32_t pass = 0; pass < 2; ++pass)
            {
                const bool cuda = (pass == 1) == (round % 2 == 1);
                uint32_t selected = 0;
                // Every warmup and sample restores the same original keys AND
                // values. Reset copy is before the timestamp, explicitly excluded.
                for (uint32_t iteration = 0; iteration < 24; ++iteration)
                {
                    reset();
                    for (uint32_t field = 0; field < 2; ++field)
                    {
                        auto *target = cuda ? (field ? values0.resource() : keys0.resource()) :
                            (field ? buffers.values[0].Get() : buffers.keys[0].Get());
                        const auto state = cuda ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                        transition(list.Get(), target, state, D3D12_RESOURCE_STATE_COPY_DEST);
                        list->CopyBufferRegion(target, 0, upload.Get(), field * bytes, bytes);
                        transition(list.Get(), target, D3D12_RESOURCE_STATE_COPY_DEST, state);
                    }
                    list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
                    if (cuda)
                    {
                        submit();
                        context.begin(gpu.direct.Get());
                        auto stream = static_cast<cudaStream_t>(context.native_stream());
                        cuda_check(cudaEventRecord(events.first, stream));
                        selected = cub.submit(context, keys0, keys1, values0, values1, count);
                        cuda_check(cudaEventRecord(events.last, stream));
                        context.end(gpu.direct.Get());
                        // A separate consumer allocator keeps the dependency
                        // span free of a CPU wait between CUDA handoffs.
                        allocator.Swap(consumer_allocator); list.Swap(consumer_list);
                        reset();
                    }
                    else native.record(list.Get(), buffers);
                    list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
                    list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
                    submit(); gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
                    if (cuda) { allocator.Swap(consumer_allocator); list.Swap(consumer_list); }
                    uint64_t ticks[2]{}; D3D12_RANGE range{0, 16};
                    check(readback->Map(0, &range, &mapped), "Sort bench ticks");
                    std::memcpy(ticks, mapped, 16); readback->Unmap(0, &empty);
                    float kernel_ms = -1;
                    if (cuda) cuda_check(cudaEventElapsedTime(&kernel_ms, events.first, events.last));
                    if (iteration >= 4) output << distribution << ',' << round << ',' << (cuda ? "cuda" : "d3d12") << ',' <<
                        iteration - 4 << ',' << count << ',' << 1000.0 * (ticks[1] - ticks[0]) / gpu.timestamp_frequency << ',' << kernel_ms << '\n';
                }
                const auto state = cuda ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                const auto actual_keys = gpu.readback(cuda ? (selected ? keys1.resource() : keys0.resource()) : buffers.keys[0].Get(), bytes, state);
                const auto actual_values = gpu.readback(cuda ? (selected ? values1.resource() : values0.resource()) : buffers.values[0].Get(), bytes, state);
                for (uint32_t i = 0; i < count; ++i)
                {
                    uint32_t key = 0, value = 0;
                    std::memcpy(&key, actual_keys.data() + i * 4ull, 4); std::memcpy(&value, actual_values.data() + i * 4ull, 4);
                    if (value != expected[i] || key != source[expected[i]]) throw std::runtime_error("Sort bench full-array validation failed");
                }
                std::cout << distribution << " r" << round << ' ' << (cuda ? "cuda" : "d3d12") << " full-array correct\n" << std::flush;
            }
        }
        if (!context.wait_idle() || !output) throw std::runtime_error("Sort bench completion/output failed");
    }
    catch (const GpuFailure &error) { std::cerr << error.operation << " hr=" << std::hex << error.hr << '\n'; return 1; }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
