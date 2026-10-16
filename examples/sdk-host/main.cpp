#include <chrono>
#include <cstdlib>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <iostream>
#include <native3dgs/measurement.h>
#include <native3dgs/engine.h>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
int wmain(int argc, wchar_t **argv)
{
    using namespace gs::engine;
    auto result = create_engine({{}, {640, 480}, 128});
    if (auto e = std::get_if<gs::render::RenderError>(&result))
    {
        std::cerr << e->diagnostic << "\n";
        return 1;
    }
    auto engine = std::move(std::get<std::unique_ptr<IEngine>>(result));
    ComPtr<IDXGISwapChain3> surface;
    int exit_code = 0;
    try
    {
        const auto metric = parse_metric(R"({"schema_version":1,
"frame":{"source":"ply_local","target":"local_enu","units":"meters",
"matrix_convention":"row_serialized_column_vector_left_multiply"},
"quality":{"status":"estimated"},"meters_per_model_unit":2,
"enu_from_ply":[[2,0,0,0],[0,2,0,0],[0,0,2,0],[0,0,0,1]]})");
        if (!std::holds_alternative<MetricCalibration>(metric) ||
            distance_meters({0, 0, 0}, {3, 4, 0}, 2) != 10)
            throw std::runtime_error("Metric SDK contract failed");
        auto require = [](HRESULT hr, const char *operation) {
            if (FAILED(hr))
            {
                std::ostringstream diagnostic;
                diagnostic << operation << " HRESULT=0x" << std::hex << unsigned(hr);
                throw std::runtime_error(diagnostic.str());
            }
        };
        auto generation = engine->snapshot().surface_generation;
        ComPtr<ID3D12CommandQueue> queue;
        queue.Attach(engine->addref_surface_queue(generation));
        if (!queue)
            throw std::runtime_error("No surface queue");
        ComPtr<IDXGIFactory2> factory;
        require(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 640;
        desc.Height = 480;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        ComPtr<IDXGISwapChain1> swap;
        require(factory->CreateSwapChainForComposition(queue.Get(), &desc, nullptr, &swap),
                "Composition swapchain");
        require(swap.As(&surface), "Swapchain3 interface");
        if (engine->attach_swapchain(generation, surface.Get()))
            throw std::runtime_error("Attach failed");
        RequestId request = 0;
        if (argc > 1)
        {
            auto opened = engine->open({argv[1]});
            if (auto e = std::get_if<gs::render::RenderError>(&opened))
                throw std::runtime_error(e->diagnostic);
            request = std::get<RequestId>(opened);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (;;)
        {
            auto state = engine->snapshot();
            if (state.error)
                throw std::runtime_error("Engine reported a load/render error");
            if (state.stats.presented_frame_id >= 10 &&
                (!request || (state.active_request == request && state.stats.candidate_splats > 0)))
            {
                std::cout << "SDK OK frames=" << state.stats.presented_frame_id
                          << " active=" << state.active_request
                          << " splats=" << state.stats.candidate_splats << "\n";
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Host timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (request && argc > 2)
        {
            if (engine->set_measurement_enabled(true) || engine->import_metric(argv[2]))
                throw std::runtime_error("Metric import rejected");
            auto wait_measurement = [&](auto predicate) {
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                for (;;)
                {
                    const auto measurement = engine->snapshot().measurement;
                    if (measurement.error)
                        throw std::runtime_error(
                            "Measurement error=" +
                            std::to_string(static_cast<unsigned>(*measurement.error)));
                    if (predicate(measurement))
                        return measurement;
                    if (std::chrono::steady_clock::now() >= until)
                        throw std::runtime_error("Measurement timeout");
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            };
            auto calibration = wait_measurement([](const auto &m) { return !m.importing; });
            if (!calibration.calibration)
                throw std::runtime_error("Missing calibration");
            const auto begin = std::chrono::steady_clock::now();
            bool completed = false;
            for (int y = 48; y < 480 && !completed; y += 48)
                for (int x = 64; x < 640 && !completed; x += 64)
                {
                    if (engine->measure_at(x, y))
                        throw std::runtime_error("Pick rejected");
                    const auto m =
                        wait_measurement([](const auto &value) { return !value.picking; });
                    if (!m.distance_m)
                        continue;
                    const auto expected =
                        distance_meters(m.a->position_source, m.b->position_source,
                                        m.calibration->meters_per_model_unit);
                    if (!expected || *expected != *m.distance_m)
                        throw std::runtime_error("Inconsistent distance");
                    std::cout << "MEASUREMENT OK meters=" << *m.distance_m
                              << " scale=" << m.calibration->meters_per_model_unit
                              << " estimated=" << m.calibration->estimated << " elapsed_ms="
                              << std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - begin)
                                     .count()
                              << "\n";
                    completed = true;
                }
            if (!completed)
                throw std::runtime_error("No measurable pair in sample grid");
            engine->clear_measurement();
            if (engine->set_measurement_enabled(false))
                throw std::runtime_error("Exit measurement rejected");
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\n";
        exit_code = 1;
    }
    engine->request_shutdown();
    if (!engine->wait_until_stopped(std::chrono::seconds(15)))
    {
        std::cerr << "Engine shutdown exceeded 15 seconds\n";
        std::quick_exit(2);
    }
    surface.Reset();
    return exit_code;
}
