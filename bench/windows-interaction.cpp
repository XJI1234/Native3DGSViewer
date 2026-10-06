#include "model-io/model_loader.h"
#include "native3dgs/camera.h"
#include "render-core/renderer.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <mmsystem.h>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;
using Microsoft::WRL::ComPtr;
using namespace gs::render;
using namespace std::chrono_literals;
void checked(HRESULT hr, const char *message)
{
    if (FAILED(hr)) throw std::runtime_error(std::string(message) + ": " + std::to_string(hr));
}
void checked(std::optional<RenderError> error)
{
    if (error) throw std::runtime_error("Render error code=" + std::to_string(static_cast<int>(error->code)) + " hr=" + std::to_string(error->hresult) + " " + error->diagnostic);
}
double milliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE) return 0; // Benchmark owner closes only its own window.
    return DefWindowProcW(window, message, wparam, lparam);
}
struct Window
{
    HWND handle = nullptr;
    Window()
    {
        SetProcessDPIAware();
        WNDCLASSW cls{};
        cls.lpfnWndProc = window_proc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"Native3DGSInteractionBenchmark";
        if (!RegisterClassW(&cls)) throw std::runtime_error("RegisterClass");
        RECT rect{0, 0, 1920, 1080};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        handle = CreateWindowW(cls.lpszClassName, L"Native3DGS SDK continuous interaction benchmark",
            WS_OVERLAPPEDWINDOW, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, cls.hInstance, nullptr);
        if (!handle) throw std::runtime_error("CreateWindow");
        ShowWindow(handle, SW_SHOW);
        SetForegroundWindow(handle);
        UpdateWindow(handle);
    }
    ~Window() { if (handle) DestroyWindow(handle); }
    void pump()
    {
        MSG message{};
        while (PeekMessageW(&message, handle, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (IsIconic(handle) || !IsWindowVisible(handle))
            throw std::runtime_error("Native benchmark window hidden");
    }
};
struct TimerResolution
{
    TimerResolution() { timeBeginPeriod(1); }
    ~TimerResolution() { timeEndPeriod(1); }
};
struct Frame
{
    double time_ms, interval_ms;
    uint32_t accepted;
    double gpu_ms;
};
}
int wmain(int argc, wchar_t **argv)
{
    if (argc != 2) { std::cerr << "Usage: InteractionBench model.ply|spz\n"; return 2; }
    try
    {
        TimerResolution timer;
        Window window;
        QualityConfig quality{};
        quality.allow_memory_mitigation = false;
        quality.point_stride = 1;
        quality.sh_degree_cap = 3;
        bool ready = false;
        std::optional<RenderError> event_error;
        auto result = create_renderer(quality, [&](const RendererEvent &event) {
            if (event.error) event_error = event.error;
            if (event.kind == RendererEvent::Kind::SceneReady) ready = true;
        });
        if (auto error = std::get_if<RenderError>(&result)) throw std::runtime_error(error->diagnostic);
        auto renderer = std::move(std::get<std::unique_ptr<IRenderer>>(result));
        const auto generation = renderer->surface_generation();
        ComPtr<ID3D12CommandQueue> queue;
        queue.Attach(renderer->addref_surface_queue(generation));
        if (!queue) throw std::runtime_error("No SDK surface queue");
        ComPtr<IDXGIFactory2> factory;
        checked(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory");
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = 1920;
        description.Height = 1080;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        description.Scaling = DXGI_SCALING_STRETCH;
        ComPtr<IDXGISwapChain1> swap1;
        checked(factory->CreateSwapChainForComposition(queue.Get(), &description,
            nullptr, &swap1), "CreateSwapChainForComposition");
        ComPtr<IDCompositionDevice> composition;
        checked(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&composition)), "Composition device");
        ComPtr<IDCompositionTarget> composition_target;
        checked(composition->CreateTargetForHwnd(window.handle, TRUE, &composition_target), "Composition target");
        ComPtr<IDCompositionVisual> visual;
        checked(composition->CreateVisual(&visual), "Composition visual");
        checked(visual->SetContent(swap1.Get()), "Composition content");
        checked(composition_target->SetRoot(visual.Get()), "Composition root");
        checked(composition->Commit(), "Composition commit");
        ComPtr<IDXGISwapChain3> swap;
        checked(swap1.As(&swap), "SwapChain3");
        checked(renderer->attach_swapchain(generation, swap.Get()));
        checked(renderer->resize(generation, 1, {1920, 1080}));
        // Establish the surface before timing file read/decode/upload/full first frame.
        renderer->render_frame();
        const auto load_start = Clock::now();
        auto loaded = gs::io::make_model_loader()->load({argv[1]}, {}, {});
        if (auto error = std::get_if<gs::io::LoadError>(&loaded)) throw std::runtime_error(error->diagnostic);
        auto scene = std::get<gs::SceneHandle>(loaded);
        const double decode_ms = milliseconds(load_start);
        gs::engine::CameraController camera;
        checked(camera.fit_scene(*scene, quality, {1920, 1080}));
        const auto initial = camera.camera();
        auto upload = renderer->upload_scene(scene, initial);
        if (auto error = std::get_if<RenderError>(&upload)) throw std::runtime_error(error->diagnostic);
        const auto ticket = std::get<UploadTicket>(upload);
        while (!ready || renderer->get_stats().presented_frame_id == 0)
        {
            if (milliseconds(load_start) > 600000) throw std::runtime_error("Full load timeout");
            window.pump();
            renderer->render_frame();
            checked(event_error);
            std::this_thread::sleep_for(1ms);
        }
        const double load_ms = milliseconds(load_start);
        const auto stats = renderer->get_stats();
        if (stats.active_splats != scene->count || stats.active_sh_degree != 3 ||
            stats.active_point_stride != 1 || stats.memory_mitigation)
            throw std::runtime_error("Native scene incomplete or mitigated");
        const gs::Double3 center{(scene->bounds.min.x + scene->bounds.max.x) / 2,
            (scene->bounds.min.y + scene->bounds.max.y) / 2,
            (scene->bounds.min.z + scene->bounds.max.z) / 2};
        const double distance = initial.position_rub.z - center.z;
        if (!(distance > 0)) throw std::runtime_error("Unexpected initial fit pose");
        std::cout << std::setprecision(15);
        std::cout << "loaded count=" << scene->count << " degree=" << unsigned(scene->shDegree)
            << " decode_ms=" << decode_ms << " first_frame_ms=" << load_ms << '\n';
        std::cout << "pose " << initial.position_rub.x << ' ' << initial.position_rub.y << ' '
            << initial.position_rub.z << ' ' << center.x << ' ' << center.y << ' ' << center.z << '\n';
        std::vector<Frame> samples;
        uint32_t calls = 0;
        double sample_ms = 0;
        for (int phase_run = -1; phase_run < 1; ++phase_run)
        {
            const double duration = phase_run < 0 ? 5000 : 20000;
            const auto begin = Clock::now();
            auto next_tick = begin;
            UINT before = 0, previous = 0;
            checked(swap->GetLastPresentCount(&before), "Initial present counter");
            previous = before;
            double previous_frame_time = 0;
            while (milliseconds(begin) < duration)
            {
                std::this_thread::sleep_until(next_tick);
                window.pump();
                const double t = milliseconds(begin) / 1000;
                const double phase = std::fmod(t, 18.0);
                const double angle = 0.4 * std::sin(2 * std::numbers::pi * t / 12);
                const double scale = phase >= 6 && phase < 12 ?
                    0.7 + 0.3 * std::cos(2 * std::numbers::pi * (phase - 6) / 6) : 1;
                const double pan = phase >= 12 ?
                    0.15 * distance * std::sin(2 * std::numbers::pi * (phase - 12) / 6) : 0;
                CameraState pose = initial;
                pose.position_rub = {center.x + distance * scale * std::sin(angle) + pan,
                    center.y, center.z + distance * scale * std::cos(angle)};
                pose.orientation_xyzw = {0, std::sin(angle / 2), 0, std::cos(angle / 2)};
                pose.near_plane = std::max(1e-5, distance * scale * 1e-5);
                pose.far_plane = std::max(distance * scale * 100, 1000.0);
                checked(renderer->set_camera(ticket, pose));
                renderer->render_frame();
                checked(event_error);
                UINT current = 0;
                checked(swap->GetLastPresentCount(&current), "Present counter");
                const double now = milliseconds(begin);
                if (phase_run >= 0)
                {
                    ++calls;
                    const auto frame_stats = renderer->get_stats();
                    if (current != previous)
                    {
                        samples.push_back({now, now - previous_frame_time, current - previous,
                            frame_stats.gpu_frame_ms.value_or(-1)});
                        previous_frame_time = now;
                    }
                }
                previous = current;
                next_tick += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 120));
                // Drop missed host ticks; never replay obsolete camera input to catch up.
                if (next_tick < Clock::now()) next_tick = Clock::now();
            }
            if (phase_run >= 0)
            {
                UINT after = 0;
                checked(swap->GetLastPresentCount(&after), "Final present counter");
                sample_ms = milliseconds(begin);
                std::cout << "interaction duration_ms=" << sample_ms << " accepted_presents="
                    << after - before << " render_calls=" << calls << " fps="
                    << 1000.0 * (after - before) / sample_ms << '\n';
            }
        }
        if (samples.size() < 30) throw std::runtime_error("Insufficient accepted presents");
        std::cout << "time_ms,interval_ms,accepted,gpu_diagnostic_ms\n";
        for (const auto &frame : samples)
            std::cout << frame.time_ms << ',' << frame.interval_ms << ',' << frame.accepted
                << ',' << frame.gpu_ms << '\n';
        checked(renderer->set_camera(ticket, initial));
        renderer->clear_scene();
        renderer->render_frame();
        renderer->detach_swapchain(generation);
        renderer->render_frame();
        // Renderer destruction waits for owned GPU work on its owning thread.
        renderer.reset();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
