#include "native3dgs/engine.h"
#include <atomic>
#include <chrono>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <gtest/gtest.h>
#include <thread>
#include <wrl/client.h>

namespace
{
using namespace gs::engine;
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
struct Fixture
{
    std::filesystem::path path;
    Fixture()
    {
        static std::atomic<uint64_t> serial{0};
        path = std::filesystem::temp_directory_path() /
               ("engine-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial) +
                ".ply");
        std::ofstream out(path, std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << "ply\nformat binary_little_endian 1.0\nelement vertex 2\n";
        for (auto name : {"x", "y", "z", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2",
                          "rot_3", "opacity", "f_dc_0", "f_dc_1", "f_dc_2"})
            out << "property float " << name << "\n";
        out << "end_header\n";
        const float values[] = {0, 0, 2, -2, -2, -2, 1, 0, 0, 0, 0, 1, 0, 0};
        for (int i = 0; i < 2; ++i)
            out.write(reinterpret_cast<const char *>(values), sizeof(values));
    }
    ~Fixture()
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};
bool until(std::function<bool()> predicate, std::chrono::milliseconds timeout = 15s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
struct Session
{
    std::unique_ptr<IEngine> engine;
    ComPtr<IDXGISwapChain3> surface;
    explicit Session(uint32_t capacity = 128, gs::render::QualityConfig quality = {})
    {
        auto result = create_engine({quality, {128, 128}, capacity});
        if (auto e = std::get_if<gs::render::RenderError>(&result))
            throw std::runtime_error(e->diagnostic);
        engine = std::move(std::get<std::unique_ptr<IEngine>>(result));
        bind();
    }
    void bind()
    {
        auto generation = engine->snapshot().surface_generation;
        ComPtr<ID3D12CommandQueue> queue;
        queue.Attach(engine->addref_surface_queue(generation));
        if (!queue)
            throw std::runtime_error("No surface queue");
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
            throw std::runtime_error("No DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = desc.Height = 128;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        ComPtr<IDXGISwapChain1> swap;
        if (FAILED(factory->CreateSwapChainForComposition(queue.Get(), &desc, nullptr, &swap)) ||
            FAILED(swap.As(&surface)))
            throw std::runtime_error("Cannot create surface");
        if (engine->attach_swapchain(generation, surface.Get()))
            throw std::runtime_error("Cannot attach surface");
    }
    RequestId open(const std::filesystem::path &path)
    {
        auto result = engine->open({path});
        if (!std::holds_alternative<RequestId>(result))
            throw std::runtime_error("Open rejected");
        return std::get<RequestId>(result);
    }
    ~Session()
    {
        engine->request_shutdown();
        EXPECT_TRUE(engine->wait_until_stopped(15s));
        surface.Reset();
    }
};
TEST(EngineRuntime, OpensControlsPreservesOldSceneOnFailureAndCloses)
{
    Fixture file;
    Session s;
    auto id = s.open(file.path);
    ASSERT_TRUE(until([&] {
        const auto state = s.engine->snapshot();
        return state.active_request == id && state.stats.active_ticket == state.active_ticket &&
               state.stats.presented_frame_id > 0;
    }));
    auto before = s.engine->snapshot();
    ASSERT_TRUE(before.active_scene);
    EXPECT_EQ(before.active_scene->count, 2u);
    EXPECT_GT(before.stats.presented_frame_id, 0u);
    EXPECT_FALSE(s.engine->camera_command({CameraAction::Orbit, 32, 12}));
    auto camera = s.engine->snapshot().camera;
    auto bad = s.open(file.path.wstring() + L".missing");
    ASSERT_TRUE(until([&] {
        auto v = s.engine->snapshot();
        return v.current_request == bad && v.error.has_value();
    }));
    auto after = s.engine->snapshot();
    EXPECT_EQ(after.active_request, id);
    EXPECT_EQ(after.active_ticket, before.active_ticket);
    EXPECT_DOUBLE_EQ(after.camera.position_rub.x, camera.position_rub.x);
    EXPECT_FALSE(s.engine->camera_command({CameraAction::Reset}));
    EXPECT_DOUBLE_EQ(s.engine->snapshot().camera.position_rub.x, before.camera.position_rub.x);
    const auto generation = s.engine->snapshot().surface_generation;
    ASSERT_FALSE(s.engine->resize(generation, {64, 128}));
    ASSERT_FALSE(s.engine->camera_command({CameraAction::FlyMode}));
    ASSERT_FALSE(s.engine->camera_command({CameraAction::Fit}));
    EXPECT_NE(s.engine->snapshot().camera.position_rub.z, before.camera.position_rub.z);
    ASSERT_FALSE(s.engine->camera_command({CameraAction::Reset}));
    EXPECT_DOUBLE_EQ(s.engine->snapshot().camera.position_rub.z, before.camera.position_rub.z);
    EXPECT_FALSE(s.engine->camera_command({CameraAction::Look, 2, 2}));
    EXPECT_TRUE(s.engine->camera_command({CameraAction::Pan, 2, 2}));
    s.engine->close();
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Empty; }));
    EXPECT_EQ(s.engine->snapshot().active_ticket, 0u);
    EXPECT_EQ(s.engine->snapshot().stats.active_ticket, 0u);
    EXPECT_EQ(s.engine->snapshot().stats.presented_frame_id, 0u);
}
TEST(EngineRuntime, LatestRequestWinsAndCancelPreservesActiveScene)
{
    Fixture file;
    Session s;
    auto active = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == active; }));
    for (int i = 0; i < 20; ++i)
        s.open(file.path.wstring() + L".missing");
    auto last = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == last; }));
    auto generation = s.engine->snapshot().surface_generation;
    EXPECT_FALSE(s.engine->resize(generation, {0, 0}));
    EXPECT_FALSE(s.engine->camera_command({CameraAction::Fit}));
    auto pending = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Uploading; }));
    s.engine->cancel(pending);
    EXPECT_FALSE(s.engine->resize(generation, {128, 128}));
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Ready; }));
    EXPECT_EQ(s.engine->snapshot().active_request, last);
}
TEST(EngineRuntime, AxisFlipsPersistAcrossPendingSceneActivation)
{
    Fixture file;
    Session s;
    auto first = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == first; }));
    const auto original = s.engine->snapshot().camera;
    const auto generation = s.engine->snapshot().surface_generation;
    ASSERT_FALSE(s.engine->resize(generation, {0, 0}));
    const auto second = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Uploading; }));
    CameraCommand flip{CameraAction::FlipY};
    flip.flip_enabled = true;
    ASSERT_FALSE(s.engine->camera_command(flip));
    EXPECT_EQ(s.engine->snapshot().flip_axes, 2);
    ASSERT_FALSE(s.engine->resize(generation, {128, 128}));
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == second; }));
    const auto state = s.engine->snapshot();
    ASSERT_TRUE(state.active_scene);
    EXPECT_NEAR(state.camera.orientation_xyzw.z, 1, 1e-9);
    flip.flip_enabled = false;
    ASSERT_FALSE(s.engine->camera_command(flip));
    EXPECT_EQ(s.engine->snapshot().flip_axes, 0);
    EXPECT_NEAR(s.engine->snapshot().camera.position_rub.z, original.position_rub.z, 1e-9);
    EXPECT_NEAR(s.engine->snapshot().camera.orientation_xyzw.w, 1, 1e-9);
    flip.action = CameraAction::FlipX;
    flip.flip_enabled = true;
    ASSERT_FALSE(s.engine->camera_command(flip));
    flip.action = CameraAction::FlipZ;
    ASSERT_FALSE(s.engine->camera_command(flip));
    EXPECT_EQ(s.engine->snapshot().flip_axes, 5);
    const double center_z = std::midpoint(state.active_scene->bounds.min.z,
                                          state.active_scene->bounds.max.z);
    EXPECT_NEAR(s.engine->snapshot().camera.position_rub.z,
                2 * center_z - original.position_rub.z, 1e-9);
    flip.action = CameraAction::FlipY;
    ASSERT_FALSE(s.engine->camera_command(flip));
    EXPECT_EQ(s.engine->snapshot().flip_axes, 7);
    EXPECT_NEAR(s.engine->snapshot().camera.orientation_xyzw.x, 1, 1e-9);
}
TEST(EngineRuntime, BoundedEventsAndShutdownRejectNewWork)
{
    Fixture file;
    Session s(2);
    for (int i = 0; i < 5; ++i)
    {
        auto id = s.open(file.path.wstring() + L".missing");
        ASSERT_TRUE(until([&] {
            auto v = s.engine->snapshot();
            return v.current_request == id && v.error.has_value();
        }));
    }
    EXPECT_GE(s.engine->snapshot().dropped_events, 3u);
    EXPECT_LE(s.engine->poll_events().size(), 2u);
    EXPECT_TRUE(s.engine->poll_events().empty());
    s.engine->request_shutdown();
    EXPECT_TRUE(std::holds_alternative<gs::render::RenderError>(s.engine->open({file.path})));
    ASSERT_TRUE(s.engine->wait_until_stopped(15s));
    EXPECT_EQ(s.engine->snapshot().phase, Phase::Stopped);
}
TEST(EngineRuntime, DeviceRecoveryWaitsForHostReleaseAndRebinds)
{
    Fixture file;
    Session s;
    auto active = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == active; }));
    auto generation = s.engine->snapshot().surface_generation;
    ComPtr<ID3D12CommandQueue> queue;
    queue.Attach(s.engine->addref_surface_queue(generation));
    ASSERT_TRUE(queue);
    ComPtr<ID3D12Device5> device;
    ASSERT_TRUE(SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))));
    device->RemoveDevice();
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Recovering; }));
    EXPECT_EQ(s.engine->snapshot().stats.active_ticket, 0u);
    EXPECT_FALSE(s.engine->snapshot().stats.gpu_frame_ms.has_value());
    EXPECT_EQ(s.engine->snapshot().surface_generation, generation);
    EXPECT_FALSE(s.engine->acknowledge_device_release(generation + 1));
    EXPECT_EQ(s.engine->snapshot().surface_generation, generation);
    s.surface.Reset();
    queue.Reset();
    device.Reset();
    EXPECT_TRUE(s.engine->acknowledge_device_release(generation));
    ASSERT_TRUE(until([&] { return s.engine->snapshot().surface_generation > generation; }));
    s.bind();
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Ready; }));
    EXPECT_EQ(s.engine->snapshot().active_request, active);
    EXPECT_EQ(s.engine->snapshot().stats.device_recovery_count, 1u);
}
TEST(EngineRuntime, MissingHostReleaseAcknowledgementBecomesPersistentFailure)
{
    Session s;
    auto generation = s.engine->snapshot().surface_generation;
    ComPtr<ID3D12CommandQueue> queue;
    queue.Attach(s.engine->addref_surface_queue(generation));
    ASSERT_TRUE(queue);
    ComPtr<ID3D12Device5> device;
    ASSERT_TRUE(SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))));
    device->RemoveDevice();
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Recovering; }));
    s.surface.Reset();
    queue.Reset();
    device.Reset();
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Failed; }, 12s));
    EXPECT_TRUE(s.engine->snapshot().error);
    EXPECT_FALSE(s.engine->acknowledge_device_release(generation));
    EXPECT_EQ(s.engine->snapshot().surface_generation, generation);
}
TEST(EngineRuntime, CloseDuringRecoveryClearsStaleActiveTicket)
{
    Fixture file;
    Session s;
    auto active = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == active; }));
    const auto old_generation = s.engine->snapshot().surface_generation;
    ComPtr<ID3D12CommandQueue> queue;
    queue.Attach(s.engine->addref_surface_queue(old_generation));
    ASSERT_TRUE(queue);
    ComPtr<ID3D12Device5> device;
    ASSERT_TRUE(SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))));
    device->RemoveDevice();
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Recovering; }));
    s.surface.Reset();
    queue.Reset();
    device.Reset();
    ASSERT_TRUE(s.engine->acknowledge_device_release(old_generation));
    ASSERT_TRUE(until([&] { return s.engine->snapshot().surface_generation > old_generation; }));
    s.engine->close();
    ASSERT_TRUE(until([&] { return s.engine->snapshot().phase == Phase::Empty; }));
    auto state = s.engine->snapshot();
    EXPECT_EQ(state.active_request, 0u);
    EXPECT_EQ(state.active_ticket, 0u);
    EXPECT_FALSE(state.active_scene);
}
TEST(EngineRuntime, ConcurrentCommandsDoNotBlockLoadingOrShutdown)
{
    Fixture file;
    Session s;
    std::thread producer([&] {
        for (int i = 0; i < 50; ++i)
        {
            auto id = s.open(file.path);
            s.engine->cancel(id);
        }
    });
    for (int i = 0; i < 100; ++i)
    {
        s.engine->snapshot();
        s.engine->poll_events();
    }
    producer.join();
    auto id = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == id; }));
    s.engine->request_shutdown();
    EXPECT_TRUE(s.engine->wait_until_stopped(15s));
}
TEST(EngineRuntime, RejectsInvalidConfiguration)
{
    EXPECT_TRUE(std::holds_alternative<gs::render::RenderError>(create_engine({{}, {0, 128}, 1})));
    EXPECT_TRUE(
        std::holds_alternative<gs::render::RenderError>(create_engine({{}, {128, 128}, 0})));
    gs::render::QualityConfig quality;
    quality.point_stride = 8;
    quality.max_point_stride = 4;
    auto rejected = create_engine({quality, {128, 128}, 128});
    ASSERT_TRUE(std::holds_alternative<gs::render::RenderError>(rejected));
    EXPECT_EQ(std::get<gs::render::RenderError>(rejected).code,
              gs::render::RenderErrorCode::InvalidQualityConfig);
}
TEST(EngineRuntime, ManualPointStrideIsReportedInSnapshot)
{
    Fixture file;
    gs::render::QualityConfig quality;
    quality.point_stride = quality.max_point_stride = 2;
    quality.allow_memory_mitigation = false;
    Session s(128, quality);
    const auto id = s.open(file.path);
    ASSERT_TRUE(until([&] { return s.engine->snapshot().active_request == id; }));
    const auto state = s.engine->snapshot();
    EXPECT_EQ(state.stats.active_ticket, state.active_ticket);
    EXPECT_EQ(state.stats.active_source_splats, 2u);
    EXPECT_EQ(state.stats.active_splats, 1u);
    EXPECT_EQ(state.stats.active_point_stride, 2u);
    EXPECT_FALSE(state.stats.memory_mitigation);
}
} // namespace
