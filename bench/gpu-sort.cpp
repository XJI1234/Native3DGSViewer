#include "gpu.h"
#include <iostream>
#include <numeric>
#include <random>

using namespace gs::render::detail;
int main(int argc, char **argv)
{
    const uint32_t n = argc > 1 ? uint32_t(std::stoul(argv[1])) : 8'000'000;
    const uint32_t iterations = argc > 2 ? uint32_t(std::stoul(argv[2])) : 3;
    if (n == 0 || n > 8'000'000 || iterations == 0 || iterations > 1000)
        return 2;
    try
    {
        GpuDevice gpu;
        SortPass sort(gpu);
        auto buffers = sort.allocate(n);
        std::wcout << L"Adapter: " << gpu.adapter_description.Description << L" wave="
                   << gpu.wave_min << L"-" << gpu.wave_max << L"\n";
        auto upload =
            gpu.buffer(uint64_t(n) * 8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        check(upload->Map(0, &empty, &mapped), "Benchmark upload");
        auto *words = static_cast<uint32_t *>(mapped);
        std::mt19937 random(42);
        for (uint32_t i = 0; i < n; ++i)
        {
            words[i] = random();
            words[n + i] = i;
        }
        upload->Unmap(0, nullptr);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&allocator)),
              "Benchmark allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                            nullptr, IID_PPV_ARGS(&list)),
              "Benchmark list");
        for (int i = 0; i < 2; ++i)
        {
            auto *r = i ? buffers.values[0].Get() : buffers.keys[0].Get();
            transition(list.Get(), r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyBufferRegion(r, 0, upload.Get(), uint64_t(i) * n * 4, uint64_t(n) * 4);
            transition(list.Get(), r, D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        check(list->Close(), "Benchmark upload close");
        gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(),
                                                    gpu.direct_fence.Get(), gpu.direct_value));
        ComPtr<ID3D12QueryHeap> queries;
        D3D12_QUERY_HEAP_DESC q{};
        q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        q.Count = 2;
        check(gpu.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)), "Benchmark timestamps");
        auto readback = gpu.buffer(16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        std::cout << "iteration,count,gpu_sort_ms\n";
        for (uint32_t i = 0; i < iterations; ++i)
        {
            check(allocator->Reset(), "Benchmark allocator reset");
            check(list->Reset(allocator.Get(), nullptr), "Benchmark reset");
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
            sort.record(list.Get(), buffers);
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(),
                                   0);
            check(list->Close(), "Benchmark close");
            gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(),
                                                        gpu.direct_fence.Get(), gpu.direct_value));
            D3D12_RANGE range{0, 16};
            check(readback->Map(0, &range, &mapped), "Benchmark result");
            uint64_t ticks[2];
            memcpy(ticks, mapped, 16);
            readback->Unmap(0, &empty);
            std::cout << i << "," << n << ","
                      << 1000.0 * (ticks[1] - ticks[0]) / gpu.timestamp_frequency << "\n";
        }
    }
    catch (const GpuFailure &e)
    {
        std::cerr << e.operation << " " << std::hex << e.hr << "\n";
        return 1;
    }
    return 0;
}
