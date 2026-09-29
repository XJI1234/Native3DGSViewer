#include "model-io/model_loader.h"
#include "native3dgs/camera.h"
#include "splat.h"
#include "upload.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace gs::render;
using namespace gs::render::detail;
int wmain(int argc, wchar_t **argv)
{
    if (argc < 4 || argc > 6)
    {
        std::cerr << "Usage: SceneBench file.ply|spz load|smoke|cached|force|orbit frames:1..10000 [sh_cap:0..3] [point_stride:1|2|4|8|16]\n";
        return 2;
    }
    std::wstring_view mode(argv[2]);
    if (mode != L"load" && mode != L"smoke" && mode != L"cached" &&
        mode != L"force" && mode != L"orbit")
        return 2;
    uint32_t frames = 0;
    std::wstring_view count(argv[3]);
    for (wchar_t c : count)
    {
        if (c < L'0' || c > L'9' || frames > 10000)
            return 2;
        frames = frames * 10 + uint32_t(c - L'0');
    }
    if (!frames || frames > 10000 || (argc >= 5 &&
        (argv[4][0] < L'0' || argv[4][0] > L'3' || argv[4][1] != L'\0')))
        return 2;
    uint32_t point_stride = 1;
    if (argc == 6)
    {
        const std::wstring_view stride(argv[5]);
        if (stride == L"2") point_stride = 2;
        else if (stride == L"4") point_stride = 4;
        else if (stride == L"8") point_stride = 8;
        else if (stride == L"16") point_stride = 16;
        else if (stride != L"1") return 2;
    }
    try
    {
        const auto load_start = std::chrono::steady_clock::now();
        auto loaded = gs::io::make_model_loader()->load({argv[1]}, {}, {});
        if (auto e = std::get_if<gs::io::LoadError>(&loaded))
            throw std::runtime_error("Load failed: code=" +
                                     std::to_string(static_cast<int>(e->code)) +
                                     " stage=" + std::to_string(static_cast<int>(e->stage)) +
                                     " " + e->diagnostic);
        auto scene = std::get<gs::SceneHandle>(loaded);
        const auto load_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - load_start)
                                 .count();
        if (mode == L"load")
        {
            std::cout << "splats=" << scene->count << " sh_degree=" << unsigned(scene->shDegree)
                      << " scene_bytes=" << scene_bytes(*scene) << " load_ms=" << load_ms
                      << '\n';
            return 0;
        }
        const Viewport viewport{1920, 1080};
        QualityConfig quality{};
        if (argc >= 5)
            quality.sh_degree_cap = uint8_t(argv[4][0] - L'0');
        gs::engine::CameraController camera;
        if (auto e = camera.fit_scene(*scene, quality, viewport))
            throw std::runtime_error(e->diagnostic);
        GpuDevice gpu;
        SplatPass pass(gpu, quality);
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
        check(gpu.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local),
              "Scene benchmark local budget");
        const auto nonlocal_hr = gpu.adapter->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
        if (!gpu.uma)
            check(nonlocal_hr, "Scene benchmark nonlocal budget");
        const auto required = incremental_bytes(*scene, quality.sh_degree_cap, point_stride);
        std::cout << "gpu_required_bytes=" << required << " local_budget_bytes=" << local.Budget
                  << " local_usage_bytes=" << local.CurrentUsage << " admission="
                  << fits_scene_budgets(required, upload_reserve_bytes, local.Budget,
                                        local.CurrentUsage, nonlocal.Budget,
                                        nonlocal.CurrentUsage, gpu.uma)
                  << '\n';
        const auto upload_start = std::chrono::steady_clock::now();
        UploadTransaction upload(gpu, pass, scene, 1, camera.camera(),
                                 quality.sh_degree_cap, false, point_stride);
        while (!upload.ready)
        {
            upload.advance(gpu);
            if (!upload.ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto upload_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - upload_start)
                                   .count();
        std::cout << "load_ms=" << load_ms << " upload_ms=" << upload_ms << '\n';
        auto model = upload.scene;
        std::wcout << L"Adapter: " << gpu.adapter_description.Description << L" mode=" << mode
                   << L" SH=" << unsigned(model->sh_degree)
                   << L" point_stride=" << point_stride << L" viewport="
                   << viewport.physical_width << L"x" << viewport.physical_height
                   << L" radial stddev=" << quality.max_stddev << L" alpha=" << quality.min_alpha
                   << L" blur=" << quality.covariance_blur_px2 << L" radius="
                   << quality.max_pixel_radius_px << L"\n";
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&allocator)),
              "Scene allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                            nullptr, IID_PPV_ARGS(&list)),
              "Scene list");
        check(list->Close(), "Scene upload close");
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = viewport.physical_width;
        td.Height = viewport.physical_height;
        td.DepthOrArraySize = td.MipLevels = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> target;
        check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                  IID_PPV_ARGS(&target)),
              "Scene target");
        ComPtr<ID3D12DescriptorHeap> rtv;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 1;
        check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv)), "Scene RTV");
        const auto handle = rtv->GetCPUDescriptorHandleForHeapStart();
        gpu.device->CreateRenderTargetView(target.Get(), nullptr, handle);
        ComPtr<ID3D12QueryHeap> queries;
        D3D12_QUERY_HEAP_DESC q{};
        q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        q.Count = 4;
        check(gpu.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)), "Scene queries");
        auto readback = gpu.buffer(32, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        std::wcout << L"frame,count,projection_ms,sort_ms,draw_ms,gpu_total_ms\n";
        // A fixed number of warm-up frames makes this a stage diagnostic, not the acceptance run.
        const uint32_t warmup = mode == L"smoke" ? 0 : 60;
        for (uint32_t i = 0; i < frames + warmup; ++i)
        {
            check(allocator->Reset(), "Scene allocator reset");
            check(list->Reset(allocator.Get(), nullptr), "Scene list reset");
            if (mode == L"orbit")
            {
                camera.orbit(1, 0);
                model->camera = camera.camera();
                model->sorted = false;
            }
            if (mode == L"force")
                model->sorted = false;
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
            if (!model->sorted)
                pass.project_sort(list.Get(), *model, viewport, queries.Get());
            else
                list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
            const float clear[4]{};
            list->ClearRenderTargetView(handle, clear, 0, nullptr);
            pass.draw(list.Get(), *model, viewport, handle);
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
            list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, readback.Get(),
                                   0);
            check(list->Close(), "Scene frame close");
            gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(),
                                                        gpu.direct_fence.Get(), gpu.direct_value));
            uint64_t ticks[4];
            D3D12_RANGE range{0, sizeof(ticks)};
            check(readback->Map(0, &range, &mapped), "Scene times");
            memcpy(ticks, mapped, sizeof(ticks));
            readback->Unmap(0, &empty);
            if (i >= warmup)
                std::wcout << i - warmup << ',' << model->count << ','
                           << 1000.0 * (ticks[1] - ticks[0]) / gpu.timestamp_frequency << ','
                           << 1000.0 * (ticks[2] - ticks[1]) / gpu.timestamp_frequency << ','
                           << 1000.0 * (ticks[3] - ticks[2]) / gpu.timestamp_frequency << ','
                           << 1000.0 * (ticks[3] - ticks[0]) / gpu.timestamp_frequency << '\n';
        }
    }
    catch (const GpuFailure &e)
    {
        std::cerr << e.operation << " HRESULT=" << std::hex << e.hr << '\n';
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
