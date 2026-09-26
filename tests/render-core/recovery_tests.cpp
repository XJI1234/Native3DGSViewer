#include "test_support.h"
using namespace render_test;
namespace
{
bool has_fatal_event(const Session &s)
{
    return std::any_of(s.events.begin(), s.events.end(), [](const auto &e) {
        return e.kind == RendererEvent::Kind::FatalDeviceError;
    });
}
void remove_device(Session &s)
{
    ComPtr<ID3D12CommandQueue> queue;
    queue.Attach(s.renderer->addref_surface_queue(s.renderer->surface_generation()));
    ComPtr<ID3D12Device5> device;
    ASSERT_EQ(queue->GetDevice(IID_PPV_ARGS(&device)), S_OK);
    device->RemoveDevice();
    s.surface.Reset();
}
} // namespace
TEST(RenderRecovery, ActualDeviceRemovalRebindsAndRestoresRetainedScene)
{
    Session s;
    auto ticket = s.upload(make_scene());
    ASSERT_TRUE(s.pump_until([&] { return s.ready(ticket); }));
    auto old = s.renderer->surface_generation();
    ComPtr<ID3D12CommandQueue> queue;
    queue.Attach(s.renderer->addref_surface_queue(old));
    ComPtr<ID3D12Device5> device;
    ASSERT_EQ(queue->GetDevice(IID_PPV_ARGS(&device)), S_OK);
    const auto original_luid = device->GetAdapterLuid();
    device->RemoveDevice();
    device.Reset();
    queue.Reset();
    s.surface.Reset();
    ASSERT_TRUE(s.pump_until([&] { return s.renderer->surface_generation() != old; }));
    EXPECT_EQ(s.renderer->addref_surface_queue(old), nullptr);
    s.bind();
    queue.Attach(s.renderer->addref_surface_queue(s.renderer->surface_generation()));
    ASSERT_EQ(queue->GetDevice(IID_PPV_ARGS(&device)), S_OK);
    EXPECT_EQ(device->GetAdapterLuid().HighPart, original_luid.HighPart);
    EXPECT_EQ(device->GetAdapterLuid().LowPart, original_luid.LowPart);
    device.Reset();
    queue.Reset();
    ASSERT_TRUE(s.pump_until([&] {
        return std::any_of(s.events.begin(), s.events.end(), [](const auto &e) {
            return e.kind == RendererEvent::Kind::DeviceRestored;
        });
    }));
    EXPECT_EQ(s.renderer->get_stats().device_recovery_count, 1u);
    EXPECT_FALSE(s.renderer->set_camera(ticket, {}));
}
TEST(RenderRecovery, AllocationAndBudgetFailurePreserveOldScene)
{
    auto control = std::make_shared<RendererTestControl>();
    Session s(control);
    auto first = s.upload(make_scene());
    ASSERT_TRUE(s.pump_until([&] { return s.ready(first); }));
    control->reject_budget = true;
    auto rejected = s.renderer->upload_scene(make_scene(), {});
    ASSERT_TRUE(std::holds_alternative<RenderError>(rejected));
    EXPECT_EQ(std::get<RenderError>(rejected).code, RenderErrorCode::OutOfVideoMemory);
    control->reject_budget = false;
    control->fail_allocation = true;
    auto ticket = s.upload(make_scene());
    s.renderer->render_frame();
    EXPECT_FALSE(s.ready(ticket));
    EXPECT_FALSE(s.renderer->set_camera(first, {}));
    EXPECT_TRUE(std::any_of(s.events.begin(), s.events.end(), [&](const auto &e) {
        return e.ticket == ticket && e.kind == RendererEvent::Kind::SceneFailed;
    }));
}
TEST(RenderRecovery, NeverCompletedFenceHasBoundedRecovery)
{
    auto control = std::make_shared<RendererTestControl>();
    Session s(control);
    auto old = s.renderer->surface_generation();
    control->fence_timeout = true;
    auto start = std::chrono::steady_clock::now();
    s.renderer->render_frame();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(7));
    s.surface.Reset();
    ASSERT_TRUE(s.pump_until([&] { return s.renderer->surface_generation() > old; }));
    s.bind();
    EXPECT_TRUE(s.pump_until([&] {
        return std::any_of(s.events.begin(), s.events.end(), [](const auto &e) {
            return e.kind == RendererEvent::Kind::DeviceRestored;
        });
    }));
}

TEST(RenderRecovery, CancelledUploadWithHungCopyFenceTriggersRecovery)
{
    auto control = std::make_shared<RendererTestControl>();
    Session s(control);
    auto active = s.upload(make_scene());
    ASSERT_TRUE(s.pump_until([&] { return s.ready(active); }));
    const auto old_generation = s.renderer->surface_generation();
    control->copy_fence_timeout = true;
    auto pending = s.upload(make_scene(100000));
    s.renderer->render_frame();
    s.renderer->cancel_upload(pending);
    s.renderer->render_frame();
    EXPECT_FALSE(s.ready(pending));
    s.surface.Reset();
    ASSERT_TRUE(s.pump_until([&] { return s.renderer->surface_generation() > old_generation; }));
    s.bind();
    ASSERT_TRUE(s.pump_until([&] {
        return std::any_of(s.events.begin(), s.events.end(), [](const auto &e) {
            return e.kind == RendererEvent::Kind::DeviceRestored;
        });
    }));
    EXPECT_FALSE(s.renderer->set_camera(active, {}));
}

TEST(RenderRecovery, RebindTimeoutIsFatalWithoutRepeatedRecovery)
{
    Session s;
    s.renderer->render_frame();
    const auto old = s.renderer->surface_generation();
    remove_device(s);
    ASSERT_TRUE(s.pump_until([&] { return s.renderer->surface_generation() > old; }));
    ASSERT_TRUE(s.pump_until([&] { return has_fatal_event(s); }, 12000));
    EXPECT_EQ(s.renderer->get_stats().device_recovery_count, 1u);
    EXPECT_EQ(s.renderer->addref_surface_queue(s.renderer->surface_generation()), nullptr);
}

TEST(RenderRecovery, RecoveryReuploadFailureIsFatal)
{
    auto control = std::make_shared<RendererTestControl>();
    Session s(control);
    auto active = s.upload(make_scene());
    ASSERT_TRUE(s.pump_until([&] { return s.ready(active); }));
    remove_device(s);
    s.renderer->render_frame();
    control->fail_allocation = true;
    s.renderer->render_frame();
    EXPECT_TRUE(has_fatal_event(s));
    EXPECT_TRUE(s.renderer->set_camera(active, {}));
    EXPECT_FALSE(std::any_of(s.events.begin(), s.events.end(), [](const auto &e) {
        return e.kind == RendererEvent::Kind::DeviceRestored;
    }));
}
