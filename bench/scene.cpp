#include "model-io/model_loader.h"
#include "native3dgs/camera.h"
#include "splat.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace gs::render;
using namespace gs::render::detail;
int wmain(int argc, wchar_t **argv)
{
    if (argc != 4)
    {
        std::cerr << "Usage: SceneBench file.ply|spz cached|force|orbit frames:1..10000\n";
        return 2;
    }
    std::wstring_view mode(argv[2]);
    if (mode != L"cached" && mode != L"force" && mode != L"orbit")
        return 2;
    uint32_t frames = 0;
    std::wstring_view count(argv[3]);
    for (wchar_t c : count)
    {
        if (c < L'0' || c > L'9' || frames > 10000)
            return 2;
        frames = frames * 10 + uint32_t(c - L'0');
    }
    if (!frames || frames > 10000)
        return 2;
    try
    {
        auto loaded = gs::io::make_model_loader()->load({argv[1]}, {}, {});
        if (auto e = std::get_if<gs::io::LoadError>(&loaded))
            throw std::runtime_error(e->diagnostic);
        auto scene = std::get<gs::SceneHandle>(loaded);
        const Viewport viewport{1920, 1080};
        const QualityConfig quality{};
        gs::engine::CameraController camera;
        if (auto e = camera.fit_scene(*scene, quality, viewport))
            throw std::runtime_error(e->diagnostic);
        GpuDevice gpu;
        SplatPass pass(gpu, quality);
        auto model = pass.allocate(scene, 1, camera.camera());
        std::wcout << L"Adapter: " << gpu.adapter_description.Description << L" mode=" << mode
                   << L" SH=" << unsigned(scene->shDegree) << L" viewport="
                   << viewport.physical_width << L"x" << viewport.physical_height
                   << L" radial stddev=" << quality.max_stddev << L" alpha=" << quality.min_alpha
                   << L" blur=" << quality.covariance_blur_px2 << L" radius="
                   << quality.max_pixel_radius_px << L"\n";
        auto upload = gpu.buffer(scene_bytes(*scene), D3D12_HEAP_TYPE_UPLOAD,
                                 D3D12_RESOURCE_STATE_GENERIC_READ);
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        check(upload->Map(0, &empty, &mapped), "Scene benchmark map");
        uint64_t offset = 0;
        for (auto span : {scene->centerLocal, scene->scale, scene->rotation, scene->opacity,
                          scene->rgb0, scene->shRest})
        {
            if (!span.empty())
                memcpy(static_cast<uint8_t *>(mapped) + offset, span.data(), span.size_bytes());
            offset += span.size_bytes();
        }
        upload->Unmap(0, nullptr);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&allocator)),
              "Scene allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                            nullptr, IID_PPV_ARGS(&list)),
              "Scene list");
        transition(list.Get(), model->attributes.Get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(model->attributes.Get(), 0, upload.Get(), 0, scene_bytes(*scene));
        transition(list.Get(), model->attributes.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        check(list->Close(), "Scene upload close");
        gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(),
                                                    gpu.direct_fence.Get(), gpu.direct_value));
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
        q.Count = 3;
        check(gpu.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)), "Scene queries");
        auto readback = gpu.buffer(24, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        std::wcout << L"frame,count,projection_sort_ms,draw_ms,gpu_total_ms\n";
        // A fixed number of warm-up frames makes this a stage diagnostic, not the acceptance run.
        for (uint32_t i = 0; i < frames + 60; ++i)
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
                pass.project_sort(list.Get(), *model, viewport);
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            const float clear[4]{};
            list->ClearRenderTargetView(handle, clear, 0, nullptr);
            pass.draw(list.Get(), *model, viewport, handle);
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
            list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 3, readback.Get(),
                                   0);
            check(list->Close(), "Scene frame close");
            gpu.wait(gpu.direct_fence.Get(), gpu.submit(gpu.direct.Get(), list.Get(),
                                                        gpu.direct_fence.Get(), gpu.direct_value));
            uint64_t ticks[3];
            D3D12_RANGE range{0, sizeof(ticks)};
            check(readback->Map(0, &range, &mapped), "Scene times");
            memcpy(ticks, mapped, sizeof(ticks));
            readback->Unmap(0, &empty);
            if (i >= 60)
                std::wcout << i - 60 << ',' << scene->count << ','
                           << 1000.0 * (ticks[1] - ticks[0]) / gpu.timestamp_frequency << ','
                           << 1000.0 * (ticks[2] - ticks[1]) / gpu.timestamp_frequency << ','
                           << 1000.0 * (ticks[2] - ticks[0]) / gpu.timestamp_frequency << '\n';
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
