#include "gs_cuda/sort.h"
#include "gs_cuda/project.h"
#include "model-io/model_loader.h"
#include "native3dgs/camera.h"
#include "splat.h"
#include "upload.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <thread>

using namespace gs::render;
using namespace gs::render::detail;
namespace cuda_ops = gs::cuda_ops;
using Clock = std::chrono::steady_clock;
static double seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

struct Commands
{
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    explicit Commands(GpuDevice &gpu)
    {
        check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Bench allocator");
        check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Bench commands");
        check(list->Close(), "Bench close");
    }
    void reset() { check(allocator->Reset(), "Bench reset allocator"); check(list->Reset(allocator.Get(), nullptr), "Bench reset commands"); }
    void submit(GpuDevice &gpu)
    {
        check(list->Close(), "Bench submit close");
        gpu.submit(gpu.direct.Get(), list.Get(), gpu.direct_fence.Get(), gpu.direct_value);
    }
};

static void capture(GpuDevice &gpu, ID3D12Resource *texture, const std::filesystem::path &path)
{
    auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t total = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    auto readback = gpu.buffer(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    Commands commands(gpu);
    commands.reset();
    transition(commands.list.Get(), texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{}, target{};
    source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    target.pResource = readback.Get();
    target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    target.PlacedFootprint = footprint;
    commands.list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
    transition(commands.list.Get(), texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands.submit(gpu);
    gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    void *mapped = nullptr;
    D3D12_RANGE range{0, size_t(total)};
    check(readback->Map(0, &range, &mapped), "Bench image map");
    std::ofstream output(path, std::ios::binary);
    for (uint32_t y = 0; y < desc.Height; ++y)
        output.write(static_cast<const char *>(mapped) + y * footprint.Footprint.RowPitch, std::streamsize(desc.Width * 4));
    D3D12_RANGE empty{};
    readback->Unmap(0, &empty);
    if (!output) throw std::runtime_error("Cannot save image");
}

static void upload_span(GpuDevice &gpu, ID3D12Resource *target, uint64_t destination, std::span<const float> values)
{
    constexpr uint64_t page_bytes = 64 * 1024 * 1024;
    if (values.empty()) return;
    auto staging = gpu.buffer((std::min)(page_bytes, uint64_t(values.size_bytes())),
        D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    Commands commands(gpu);
    for (uint64_t offset = 0; offset < values.size_bytes(); offset += page_bytes)
    {
        const auto size = (std::min)(page_bytes, uint64_t(values.size_bytes()) - offset);
        void *mapped = nullptr;
        D3D12_RANGE empty{};
        check(staging->Map(0, &empty, &mapped), "Shared input upload map");
        std::memcpy(mapped, reinterpret_cast<const uint8_t *>(values.data()) + offset, size_t(size));
        staging->Unmap(0, nullptr);
        commands.reset();
        transition(commands.list.Get(), target, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        commands.list->CopyBufferRegion(target, destination + offset, staging.Get(), 0, size);
        transition(commands.list.Get(), target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        commands.submit(gpu);
        gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
    }
}

int wmain(int argc, wchar_t **argv)
{
    try
    {
        std::map<std::wstring, std::wstring> args;
        const std::vector<std::wstring> accepted{L"--model", L"--backend", L"--frames", L"--warmup-frames",
            L"--warmup-seconds", L"--sample-seconds", L"--output", L"--camera", L"--diagnostics"};
        for (int i = 1; i < argc; i += 2)
        {
            if (i + 1 >= argc || std::find(accepted.begin(), accepted.end(), argv[i]) == accepted.end() || args.contains(argv[i]))
                throw std::invalid_argument("Unknown, duplicate or incomplete argument");
            args.emplace(argv[i], argv[i + 1]);
        }
        if (!args.contains(L"--model")) throw std::invalid_argument("--model is required");
        auto get = [&](const wchar_t *key, const wchar_t *fallback) { return args.contains(key) ? args.at(key) : std::wstring(fallback); };
        auto number = [&](const wchar_t *key, const wchar_t *fallback, double maximum) {
            const auto text = get(key, fallback);
            size_t used = 0;
            const double value = std::stod(text, &used);
            if (used != text.size() || !std::isfinite(value) || value < 0 || value > maximum) throw std::invalid_argument("Invalid numeric argument");
            return value;
        };
        const auto backend = get(L"--backend", L"d3d12");
        const std::vector<std::wstring> backends{L"d3d12", L"interop-noop", L"cuda-sort", L"cuda-project-128",
            L"cuda-project-256", L"cuda-project-sort-128", L"cuda-project-sort-256"};
        if (std::find(backends.begin(), backends.end(), backend) == backends.end()) throw std::invalid_argument("Backend not implemented");
        const bool interop = backend != L"d3d12", cuda_projection = backend.starts_with(L"cuda-project");
        const bool cuda_sort = backend == L"cuda-sort" || backend.starts_with(L"cuda-project-sort");
        const uint32_t block_size = backend.ends_with(L"128") ? 128 : 256;
        const auto camera_mode = get(L"--camera", L"fit");
        if (camera_mode != L"fit" && camera_mode != L"orbit") throw std::invalid_argument("Unknown camera path");
        const uint32_t frames = uint32_t(number(L"--frames", L"120", 10000000));
        const uint32_t warm_frames = uint32_t(number(L"--warmup-frames", L"60", 10000000));
        const auto warm_seconds = number(L"--warmup-seconds", L"0", 3600);
        const auto sample_seconds = number(L"--sample-seconds", L"0", 86400);
        if (!frames && !sample_seconds) throw std::invalid_argument("Empty sample");
        QualityConfig quality{};
        quality.allow_memory_mitigation = false;
        const Viewport viewport{1920, 1080};
        auto loaded = gs::io::make_model_loader()->load({args.at(L"--model")}, {}, {});
        if (auto error = std::get_if<gs::io::LoadError>(&loaded)) throw std::runtime_error(error->diagnostic);
        auto scene = std::get<gs::SceneHandle>(loaded);
        gs::engine::CameraController camera;
        if (auto error = camera.fit_scene(*scene, quality, viewport)) throw std::runtime_error(error->diagnostic);
        const auto initial_camera = camera;
        GpuDevice gpu(get(L"--diagnostics", L"off") == L"on");
        SplatPass pass(gpu, quality);
        UploadTransaction upload(gpu, pass, scene, 1, camera.camera(), scene->shDegree, false, 1);
        while (!upload.ready) { upload.advance(gpu); if (!upload.ready) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        auto model = upload.scene;
        std::unique_ptr<cuda_ops::Context> context;
        std::array<std::unique_ptr<cuda_ops::SharedBuffer>, 8> shared;
        const uint32_t mutable_buffers = cuda_projection ? 6 : 4;
        std::unique_ptr<cuda_ops::SortWorkspace> workspace;
        Commands producer(gpu), consumer(gpu);
        if (interop)
        {
            context = std::make_unique<cuda_ops::Context>(gpu.device.Get(), gpu.adapter_description.AdapterLuid);
            const uint64_t bytes = (uint64_t(model->count) + 511) / 512 * 512 * 4;
            for (int i = 0; i < 4; ++i) shared[i] = std::make_unique<cuda_ops::SharedBuffer>(*context, bytes);
            if (cuda_projection)
            {
                // Re-upload canonical CPU spans before timing. Release original input
                // allocations first to avoid doubling a 4GB SH buffer on the 10GB GPU.
                model->attributes.Reset(); model->sh_attributes.Reset();
                model->projected.Reset(); model->arguments.Reset();
                shared[4] = std::make_unique<cuda_ops::SharedBuffer>(*context, uint64_t(model->count) * sizeof(ProjectedEllipse));
                shared[5] = std::make_unique<cuda_ops::SharedBuffer>(*context, sizeof(DrawCounters));
                shared[6] = std::make_unique<cuda_ops::SharedBuffer>(*context, uint64_t(model->count) * 56);
                shared[7] = std::make_unique<cuda_ops::SharedBuffer>(*context,
                    (std::max)(uint64_t{4}, uint64_t(model->count) * model->sh_floats_per_splat * 4));
                const std::array spans{scene->centerLocal, scene->scale, scene->rotation, scene->opacity, scene->rgb0};
                for (int i = 0; i < 5; ++i) upload_span(gpu, shared[6]->resource(), model->offsets[i], spans[i]);
                upload_span(gpu, shared[7]->resource(), 0, scene->shRest);
                model->projected = shared[4]->resource(); model->arguments = shared[5]->resource();
                model->attributes = shared[6]->resource();
                if (model->sh_floats_per_splat) model->sh_attributes = shared[7]->resource();
            }
            producer.reset();
            for (uint32_t i = 0; i < mutable_buffers; ++i) transition(producer.list.Get(), shared[i]->resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            producer.submit(gpu);
            gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
            for (int i = 0; i < 2; ++i) { model->sorting.keys[i] = shared[i]->resource(); model->sorting.values[i] = shared[i + 2]->resource(); }
            if (cuda_sort) workspace = std::make_unique<cuda_ops::SortWorkspace>(*context, model->count);
        }
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = 1920; td.Height = 1080;
        td.DepthOrArraySize = td.MipLevels = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        ComPtr<ID3D12Resource> target;
        check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)), "Bench target");
        ComPtr<ID3D12DescriptorHeap> rtv;
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 1;
        check(gpu.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv)), "Bench RTV");
        const auto handle = rtv->GetCPUDescriptorHandleForHeapStart();
        gpu.device->CreateRenderTargetView(target.Get(), nullptr, handle);
        ComPtr<ID3D12QueryHeap> queries;
        D3D12_QUERY_HEAP_DESC q{}; q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; q.Count = 4;
        check(gpu.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)), "Bench queries");
        auto times = gpu.buffer(32, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        const std::filesystem::path output_path(get(L"--output", L"cuda-bench.csv"));
        if (output_path.has_parent_path()) std::filesystem::create_directories(output_path.parent_path());
        std::ofstream output(output_path);
        output << std::setprecision(12) << "frame,time_s,count,projection_ms,sort_and_interop_ms,draw_ms,gpu_total_ms,cpu_wall_ms\n";
        std::cout << "count=" << model->count << " SH=" << unsigned(model->sh_degree)
                  << " stride=1 viewport=1920x1080 radial stddev=3 alpha=0 blur=0 radius=1024 no_mitigation\n";
        for (int phase = 0; phase < 2; ++phase)
        {
            const auto phase_start = Clock::now();
            const double duration = phase ? sample_seconds : warm_seconds;
            const uint32_t limit = phase ? frames : warm_frames;
            for (uint32_t frame = 0; duration > 0 ? seconds(phase_start) < duration : frame < limit; ++frame)
            {
                const auto start = Clock::now();
                const double t = seconds(phase_start);
                if (camera_mode == L"orbit")
                {
                    camera = initial_camera;
                    camera.orbit(80 * std::sin(t * 0.4), 20 * std::sin(t * 0.25));
                    model->camera = camera.camera();
                }
                producer.reset();
                auto *list = producer.list.Get();
                list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
                if (!cuda_projection)
                {
                    if (cuda_sort) pass.project(list, *model, viewport, queries.Get());
                    else pass.project_sort(list, *model, viewport, queries.Get());
                }
                uint32_t selected = 0;
                if (interop)
                {
                    for (uint32_t i = 0; i < mutable_buffers; ++i) transition(list, shared[i]->resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
                    producer.submit(gpu);
                    context->begin(gpu.direct.Get());
                    if (cuda_projection) cuda_ops::project(*context, pass.constants(*model, viewport),
                        *shared[6], *shared[7], *shared[4], *shared[0], *shared[2], *shared[5], block_size);
                    if (cuda_sort) selected = workspace->submit(*context, *shared[0], *shared[1], *shared[2], *shared[3], model->count);
                    context->end(gpu.direct.Get());
                    consumer.reset();
                    list = consumer.list.Get();
                    for (uint32_t i = 0; i < mutable_buffers; ++i) transition(list, shared[i]->resource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    // For E04 this span contains projection + CUB + interop; CUDA
                    // events will provide separate kernel diagnostics, not D3D clocks.
                    if (cuda_projection) list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
                    if (cuda_projection && !cuda_sort) pass.sort().record(list, model->sorting);
                    if (selected) { std::swap(model->sorting.keys[0], model->sorting.keys[1]); std::swap(model->sorting.values[0], model->sorting.values[1]); }
                }
                list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2);
                const float clear[4]{};
                list->ClearRenderTargetView(handle, clear, 0, nullptr);
                pass.draw(list, *model, viewport, handle);
                if (selected) { std::swap(model->sorting.keys[0], model->sorting.keys[1]); std::swap(model->sorting.values[0], model->sorting.values[1]); }
                list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 3);
                list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, times.Get(), 0);
                (interop ? consumer : producer).submit(gpu);
                gpu.wait(gpu.direct_fence.Get(), gpu.direct_value);
                uint64_t ticks[4]; void *mapped = nullptr;
                D3D12_RANGE range{0, 32}, empty{};
                check(times->Map(0, &range, &mapped), "Bench timestamp map");
                std::memcpy(ticks, mapped, 32); times->Unmap(0, &empty);
                const double factor = 1000.0 / gpu.timestamp_frequency;
                if (phase) output << frame << ',' << t << ',' << model->count << ','
                    << (ticks[1] - ticks[0]) * factor << ',' << (ticks[2] - ticks[1]) * factor << ','
                    << (ticks[3] - ticks[2]) * factor << ',' << (ticks[3] - ticks[0]) * factor << ',' << seconds(start) * 1000 << '\n';
            }
        }
        if (context && !context->wait_idle()) throw std::runtime_error("CUDA shutdown timeout");
        capture(gpu, target.Get(), output_path.string() + ".bgra");
        if (!output) throw std::runtime_error("Cannot save samples");
        if (!gpu.debug_errors().empty()) throw std::runtime_error("D3D12 validation failed");
        return 0;
    }
    catch (const GpuFailure &error) { std::cerr << error.operation << " HRESULT=" << std::hex << error.hr << '\n'; }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; }
    return 1;
}
