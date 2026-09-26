#include "test_support.h"
#include <filesystem>
#include <fstream>
#include <thread>

namespace render_test
{
Image render_image(gs::SceneHandle scene, CameraState camera, QualityConfig quality,
                   Viewport viewport)
{
    GpuDevice gpu(true);
    SplatPass pass(gpu, quality);
    auto model = pass.allocate(scene, 1, camera);
    auto upload =
        gpu.buffer(scene_bytes(*scene), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    check(upload->Map(0, &empty, &mapped), "Image upload");
    uint64_t offset = 0;
    for (auto span : {scene->centerLocal, scene->scale, scene->rotation, scene->opacity,
                      scene->rgb0, scene->shRest})
    {
        memcpy(static_cast<uint8_t *>(mapped) + offset, span.data(), span.size_bytes());
        offset += span.size_bytes();
    }
    upload->Unmap(0, nullptr);
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = viewport.physical_width;
    td.Height = viewport.physical_height;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> target;
    check(gpu.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
                                              D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                              IID_PPV_ARGS(&target)),
          "Image target");
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "Image RTV heap");
    auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
    gpu.device->CreateRenderTargetView(target.Get(), nullptr, rtv);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator)),
          "Image allocator");
    check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&list)),
          "Image list");
    transition(list.Get(), model->attributes.Get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyBufferRegion(model->attributes.Get(), 0, upload.Get(), 0, scene_bytes(*scene));
    transition(list.Get(), model->attributes.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    pass.project_sort(list.Get(), *model, viewport);
    const float clear[4]{};
    list->ClearRenderTargetView(rtv, clear, 0, nullptr);
    pass.draw(list.Get(), *model, viewport, rtv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows;
    UINT64 rowBytes, total;
    gpu.device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    auto readback = gpu.buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    transition(list.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = target.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    check(list->Close(), "Image close");
    gpu.wait(gpu.direct_fence.Get(),
             gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value));
    Image image;
    image.width = viewport.physical_width;
    image.height = viewport.physical_height;
    image.bgra.resize(size_t(image.width) * image.height * 4);
    D3D12_RANGE range{0, size_t(total)};
    check(readback->Map(0, &range, &mapped), "Image map");
    for (UINT y = 0; y < rows; ++y)
        memcpy(image.bgra.data() + size_t(y) * rowBytes,
               static_cast<const uint8_t *>(mapped) + size_t(y) * footprint.Footprint.RowPitch,
               size_t(rowBytes));
    readback->Unmap(0, &empty);
    auto projected = gpu.readback(model->projected.Get(), scene->count * 48,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    image.projected.resize(projected.size() / 4);
    memcpy(image.projected.data(), projected.data(), projected.size());
    auto args = gpu.readback(model->arguments.Get(), 20, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    memcpy(&image.candidates, args.data() + 4, 4);
    memcpy(&image.rejected, args.data() + 16, 4);
    for (const auto &error : gpu.debug_errors())
        ADD_FAILURE() << error;
    return image;
}
void save_bmp(const Image &image, const std::filesystem::path &path)
{
    std::filesystem::create_directories(path.parent_path());
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + DWORD(image.bgra.size());
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = LONG(image.width);
    info.biHeight = -LONG(image.height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&file), sizeof(file));
    out.write(reinterpret_cast<const char *>(&info), sizeof(info));
    out.write(reinterpret_cast<const char *>(image.bgra.data()), image.bgra.size());
}
Session::Session(std::shared_ptr<RendererTestControl> control)
{
    auto result = create_renderer_for_testing(
        {}, [this](const RendererEvent &e) { events.push_back(e); }, std::move(control));
    if (auto e = std::get_if<RenderError>(&result))
        throw GpuFailure{e->hresult, "Renderer creation"};
    renderer = std::move(std::get<std::unique_ptr<IRenderer>>(result));
    bind();
}
Session::~Session()
{
    renderer->detach_swapchain(renderer->surface_generation());
    renderer->render_frame();
    for (const auto &error : renderer_debug_errors(*renderer))
        ADD_FAILURE() << error;
    for (const auto &e : events)
        if (e.kind == RendererEvent::Kind::RenderFault && e.error &&
            e.error->code == RenderErrorCode::InternalFailure)
            ADD_FAILURE() << e.error->diagnostic;
}
void Session::bind(Viewport viewport)
{
    ComPtr<ID3D12CommandQueue> queue;
    queue.Attach(renderer->addref_surface_queue(renderer->surface_generation()));
    if (!queue)
        throw GpuFailure{E_FAIL, "Surface queue"};
    ComPtr<IDXGIFactory2> factory;
    check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Session factory");
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = viewport.physical_width;
    desc.Height = viewport.physical_height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    ComPtr<IDXGISwapChain1> swap;
    check(factory->CreateSwapChainForComposition(queue.Get(), &desc, nullptr, &swap),
          "Composition swapchain");
    check(swap.As(&surface), "Swapchain3");
    auto e = renderer->attach_swapchain(renderer->surface_generation(), surface.Get());
    if (e)
        throw GpuFailure{e->hresult, "Surface attach"};
}
bool Session::pump_until(std::function<bool()> predicate, int timeout_ms)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do
    {
        renderer->render_frame();
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
UploadTicket Session::upload(gs::SceneHandle scene, CameraState camera)
{
    auto result = renderer->upload_scene(std::move(scene), camera);
    if (auto e = std::get_if<RenderError>(&result))
        throw GpuFailure{e->hresult, "Scene upload rejected"};
    return std::get<UploadTicket>(result);
}
bool Session::ready(UploadTicket ticket) const
{
    return std::any_of(events.begin(), events.end(), [&](const auto &e) {
        return e.kind == RendererEvent::Kind::SceneReady && e.ticket == ticket;
    });
}
} // namespace render_test
