#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.xaml.g.hpp")
#include "MainWindow.xaml.g.hpp"
#elif __has_include("MainWindow.xaml.g.hpp.backup")
// The WinUI XAML target moves the second-pass generated implementation to .backup.
#include "MainWindow.xaml.g.hpp.backup"
#else
#include "MainWindow.xaml.bootstrap.h"
#endif

#include <commctrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <microsoft.ui.xaml.media.dxinterop.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace winrt::Native3DGSViewer::implementation
{
namespace
{
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
std::jthread shutdown_thread;
} // namespace

void wait_for_viewer_shutdown()
{
    if (shutdown_thread.joinable()) shutdown_thread.join();
}

MainWindow::MainWindow()
{
    InitializeComponent();
    Title(L"3DGS Viewer");
    Activated([this](auto&&, auto&&) {
        if (!initialized_)
        {
            initialized_ = true;
            initialize_viewer();
        }
    });
}

void MainWindow::initialize_viewer()
{
    build_interface();
    open_button_.IsEnabled(false);
    connect_events();
    winrt::com_ptr<IWindowNative> native;
    winrt::check_hresult(QueryInterface(IID_PPV_ARGS(native.put())));
    winrt::check_hresult(native->get_WindowHandle(&hwnd_));
    SetWindowSubclass(hwnd_, keyboard_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    auto created = gs::engine::create_engine();
    if (auto *ready = std::get_if<std::unique_ptr<gs::engine::IEngine>>(&created))
    {
        engine_ = std::move(*ready);
        open_button_.IsEnabled(true);
        try { create_surface(); if (generation_) set_status(L"就绪"); }
        catch (...) { set_status(L"无法创建渲染视口"); }
    }
    else set_status(L"当前设备不支持此渲染器");
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args)
    {
        if (count == 2) initial_path_ = args[1];
        LocalFree(args);
    }
    try_open_initial();
}

void MainWindow::build_interface()
{
    root_ = Content().as<Grid>();
    scene_panel_ = root_.FindName(L"ScenePanel").as<SwapChainPanel>();
    open_button_ = root_.FindName(L"OpenButton").as<Button>();
    close_button_ = root_.FindName(L"CloseButton").as<Button>();
    fit_button_ = root_.FindName(L"FitButton").as<Button>();
    reset_button_ = root_.FindName(L"ResetButton").as<Button>();
    flip_y_button_ = root_.FindName(L"FlipYButton").as<Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
    cancel_button_ = root_.FindName(L"CancelButton").as<Button>();
    orbit_button_ = root_.FindName(L"OrbitButton").as<RadioButton>();
    fly_button_ = root_.FindName(L"FlyButton").as<RadioButton>();
    empty_text_ = root_.FindName(L"EmptyText").as<TextBlock>();
    busy_text_ = root_.FindName(L"BusyText").as<TextBlock>();
    status_text_ = root_.FindName(L"StatusText").as<TextBlock>();
    model_text_ = root_.FindName(L"ModelText").as<TextBlock>();
    busy_panel_ = root_.FindName(L"BusyPanel").as<Border>();
}

void MainWindow::connect_events()
{
    open_button_.Click([this](auto&&, auto&&) { pick_file(); });
    close_button_.Click([this](auto&&, auto&&) { if (engine_) engine_->close(); });
    cancel_button_.Click([this](auto&&, auto&&) {
        if (engine_) engine_->cancel(request_id_);
        finish_busy();
        set_status(engine_ && engine_->snapshot().active_scene ? L"已取消，继续显示当前模型" : L"已取消");
    });
    fit_button_.Click([this](auto&&, auto&&) { camera_command(gs::engine::CameraAction::Fit); });
    reset_button_.Click([this](auto&&, auto&&) { camera_command(gs::engine::CameraAction::Reset); });
    flip_y_button_.Checked([this](auto&&, auto&&) {
        if (!updating_flip_button_) set_flip_y(true);
    });
    flip_y_button_.Unchecked([this](auto&&, auto&&) {
        if (!updating_flip_button_) set_flip_y(false);
    });
    orbit_button_.Checked([this](auto&&, auto&&) {
        fly_mode_ = false;
        camera_command(gs::engine::CameraAction::OrbitMode);
        fly_capture_ = false; keys_.fill(false); scene_panel_.ReleasePointerCaptures();
    });
    fly_button_.Checked([this](auto&&, auto&&) {
        fly_mode_ = true;
        camera_command(gs::engine::CameraAction::FlyMode); keys_.fill(false);
    });
    scene_panel_.SizeChanged([this](auto&&, auto&&) {
        if (flip_y_)
            scene_panel_.RenderTransform().as<Microsoft::UI::Xaml::Media::ScaleTransform>().CenterX(scene_panel_.ActualWidth() / 2);
        if (!generation_ && engine_) create_surface();
        else resize_surface();
        try_open_initial();
    });
    xaml_root_ = scene_panel_.XamlRoot();
    if (xaml_root_)
        xaml_root_changed_ = xaml_root_.Changed([this](auto&&, auto&&) { resize_surface(); });
    scene_panel_.PointerPressed([this](auto&&, auto&& args) {
        last_pointer_ = args.GetCurrentPoint(scene_panel_).Position();
        right_drag_ = args.GetCurrentPoint(scene_panel_).Properties().IsRightButtonPressed();
        dragging_ = true;
        if (fly_mode_) fly_capture_ = true;
        scene_panel_.CapturePointer(args.Pointer());
    });
    scene_panel_.PointerMoved([this](auto&&, auto&& args) { on_pointer_move(args); });
    scene_panel_.PointerReleased([this](auto&&, auto&&) {
        dragging_ = false;
        if (!fly_capture_) scene_panel_.ReleasePointerCaptures();
    });
    scene_panel_.PointerCanceled([this](auto&&, auto&&) {
        dragging_ = fly_capture_ = false; keys_.fill(false);
        scene_panel_.ReleasePointerCaptures();
    });
    scene_panel_.PointerWheelChanged([this](auto&&, auto&& args) {
        if (!engine_ || !scene_ready_ || fly_mode_) return;
        camera_command(gs::engine::CameraAction::Dolly,
                       args.GetCurrentPoint(scene_panel_).Properties().MouseWheelDelta() / 120.0);
    });
    scene_panel_.DragOver([](auto&&, auto&& args) {
        args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
    });
    scene_panel_.Drop([this](auto&&, auto&& args) { accept_drop(args); });
    timer_ = DispatcherTimer{};
    timer_.Interval(std::chrono::milliseconds(16));
    timer_.Tick([this](auto&&, auto&&) {
        update_engine();
        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration<double>(now - last_tick_).count();
        last_tick_ = now;
        if (waiting_for_detach_ && now - detach_started_ > std::chrono::seconds(5))
        {
            waiting_for_detach_ = false;
            release_surface();
            Close();
            return;
        }
        if (engine_ && scene_ready_ && fly_capture_ &&
            std::any_of(keys_.begin(), keys_.end(), [](bool v) { return v; }))
        {
            camera_command(gs::engine::CameraAction::Fly,
                           double(keys_[3]) - double(keys_[2]),
                           double(keys_[5]) - double(keys_[4]),
                           double(keys_[0]) - double(keys_[1]), dt, shift_);
        }
    });
    timer_.Start();
    app_window_ = AppWindow();
    app_window_closing_ = app_window_.Closing([this](auto&&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args) {
        if (!generation_ || !engine_) return;
        args.Cancel(true);
        if (waiting_for_detach_) return;
        waiting_for_detach_ = true;
        detach_started_ = std::chrono::steady_clock::now();
        engine_->detach_swapchain(generation_);
    });
    Closed([this](auto&&, auto&&) {
        closing_ = true;
        timer_.Stop();
        if (xaml_root_) xaml_root_.Changed(xaml_root_changed_);
        if (app_window_) app_window_.Closing(app_window_closing_);
        RemoveWindowSubclass(hwnd_, keyboard_proc, 1);
        release_surface();
        if (engine_)
        {
            engine_->request_shutdown();
            shutdown_thread = std::jthread([engine = std::move(engine_)]() mutable { engine.reset(); });
        }
    });
}

gs::render::Viewport MainWindow::viewport() const
{
    if (!scene_panel_ || !scene_panel_.XamlRoot()) return {};
    const double scale = scene_panel_.XamlRoot().RasterizationScale();
    const double width = scene_panel_.ActualWidth() * scale;
    const double height = scene_panel_.ActualHeight() * scale;
    if (width < 1 || height < 1) return {};
    return {static_cast<uint32_t>(std::min(16384.0, std::round(width))),
            static_cast<uint32_t>(std::min(16384.0, std::round(height)))};
}

void MainWindow::create_surface()
{
    auto size = viewport();
    if (!engine_ || !size.physical_width || !size.physical_height) return;
    const auto generation = engine_->snapshot().surface_generation;
    if (!generation) return;
    winrt::com_ptr<ID3D12CommandQueue> queue;
    queue.attach(engine_->addref_surface_queue(generation));
    if (!queue) { set_status(L"无法初始化图形设备"); return; }
    winrt::com_ptr<IDXGIFactory2> factory;
    winrt::check_hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = size.physical_width;
    desc.Height = size.physical_height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    winrt::com_ptr<IDXGISwapChain1> base;
    winrt::check_hresult(factory->CreateSwapChainForComposition(queue.get(), &desc, nullptr, base.put()));
    swapchain_ = base.as<IDXGISwapChain3>();
    const float inverse_scale = 1.0f / static_cast<float>(scene_panel_.XamlRoot().RasterizationScale());
    DXGI_MATRIX_3X2_F matrix{inverse_scale, 0, 0, inverse_scale, 0, 0};
    winrt::check_hresult(swapchain_->SetMatrixTransform(&matrix));
    auto native = scene_panel_.as<ISwapChainPanelNative>();
    winrt::check_hresult(native->SetSwapChain(swapchain_.get()));
    const bool attached = !engine_->attach_swapchain(generation, swapchain_.get());
    if (!attached)
    {
        release_surface();
        set_status(L"无法绑定渲染视口");
        return;
    }
    generation_ = generation;
    resize_surface();
}

void MainWindow::release_surface()
{
    if (scene_panel_)
        if (auto native = scene_panel_.try_as<ISwapChainPanelNative>()) native->SetSwapChain(nullptr);
    swapchain_ = nullptr;
    generation_ = 0;
}

void MainWindow::resize_surface()
{
    if (swapchain_ && scene_panel_.XamlRoot())
    {
        const float inverse_scale = 1.0f / static_cast<float>(scene_panel_.XamlRoot().RasterizationScale());
        DXGI_MATRIX_3X2_F matrix{inverse_scale, 0, 0, inverse_scale, 0, 0};
        winrt::check_hresult(swapchain_->SetMatrixTransform(&matrix));
    }
    if (engine_ && generation_)
        engine_->resize(generation_, viewport());
}
void MainWindow::set_busy(std::wstring_view text)
{
    if (busy_panel_.Visibility() != Visibility::Visible ||
        std::wstring_view{busy_text_.Text()} != text)
        busy_text_.Text(hstring{text});
    if (busy_panel_.Visibility() != Visibility::Visible)
        busy_panel_.Visibility(Visibility::Visible);
}
void MainWindow::finish_busy() { busy_panel_.Visibility(Visibility::Collapsed); }
void MainWindow::set_status(std::wstring_view text) { status_text_.Text(hstring{text}); }
void MainWindow::camera_command(gs::engine::CameraAction action, double x, double y,
                                double z, double seconds, bool fast)
{
    if (engine_ && engine_->camera_command({action, x, y, z, seconds, fast}) &&
        (action == gs::engine::CameraAction::Fit || action == gs::engine::CameraAction::Reset))
        set_status(L"无法调整视角");
}
void MainWindow::set_flip_y(bool enabled)
{
    gs::engine::CameraCommand command{gs::engine::CameraAction::FlipY};
    command.flip_enabled = enabled;
    if (!engine_ || engine_->camera_command(command))
    {
        updating_flip_button_ = true;
        flip_y_button_.IsChecked(flip_y_);
        updating_flip_button_ = false;
        set_status(L"无法翻转视角");
        return;
    }
    flip_y_ = enabled;
    if (enabled)
    {
        auto transform = Microsoft::UI::Xaml::Media::ScaleTransform{};
        transform.ScaleX(-1);
        transform.CenterX(scene_panel_.ActualWidth() / 2);
        scene_panel_.RenderTransform(transform);
    }
    else scene_panel_.RenderTransform(Microsoft::UI::Xaml::Media::Transform{nullptr});
}
void MainWindow::open_path(std::filesystem::path path)
{
    if (!engine_) return;
    auto request = engine_->open({path});
    if (auto *id = std::get_if<gs::engine::RequestId>(&request))
        request_id_ = *id;
    else { set_status(L"无法打开此模型文件"); return; }
    pending_path_ = std::move(path);
    set_busy(L"正在读取模型");
    set_status(L"正在打开模型");
}

void MainWindow::try_open_initial()
{
    if (initial_path_.empty() || !engine_ ||
        !viewport().physical_width) return;
    auto path = std::move(initial_path_);
    initial_path_.clear();
    open_path(std::move(path));
}

winrt::fire_and_forget MainWindow::pick_file()
{
    auto lifetime = get_strong();
    keys_.fill(false); dragging_ = fly_capture_ = false;
    scene_panel_.ReleasePointerCaptures();
    try
    {
        Windows::Storage::Pickers::FileOpenPicker picker;
        picker.FileTypeFilter().Append(L".ply");
        picker.FileTypeFilter().Append(L".spz");
        auto initialize = picker.as<IInitializeWithWindow>();
        winrt::check_hresult(initialize->Initialize(hwnd_));
        auto file = co_await picker.PickSingleFileAsync();
        if (!closing_ && file) open_path(std::filesystem::path{file.Path().c_str()});
    }
    catch (...) { if (!closing_) set_status(L"无法打开文件选择器"); }
}

winrt::fire_and_forget MainWindow::accept_drop(DragEventArgs args)
{
    auto lifetime = get_strong();
    auto deferral = args.GetDeferral();
    try
    {
        if (!args.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
            set_status(L"请选择本地模型文件");
        else
        {
            auto items = co_await args.DataView().GetStorageItemsAsync();
            if (!closing_)
            {
                if (items.Size() != 1 || !items.GetAt(0).IsOfType(Windows::Storage::StorageItemTypes::File))
                    set_status(L"一次只能打开一个模型文件");
                else
                    open_path(std::filesystem::path{items.GetAt(0).Path().c_str()});
            }
        }
    }
    catch (...) { if (!closing_) set_status(L"无法读取拖入的文件"); }
    deferral.Complete();
}

void MainWindow::on_pointer_move(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs args)
{
    if (!engine_ || !scene_ready_ || (!dragging_ && !fly_capture_)) return;
    auto point = args.GetCurrentPoint(scene_panel_).Position();
    const double dx = point.X - last_pointer_.X;
    const double dy = point.Y - last_pointer_.Y;
    last_pointer_ = point;
    if (fly_mode_)
        camera_command(gs::engine::CameraAction::Look, dx, dy);
    else if (right_drag_)
        camera_command(gs::engine::CameraAction::Pan, dx, dy);
    else
        camera_command(gs::engine::CameraAction::Orbit, -dx, -dy);
}

void MainWindow::on_key(UINT message, WPARAM key)
{
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATE && LOWORD(key) == WA_INACTIVE))
    {
        keys_.fill(false); shift_ = false; fly_capture_ = false;
        scene_panel_.ReleasePointerCaptures();
        return;
    }
    if (message != WM_KEYDOWN && message != WM_KEYUP) return;
    const bool down = message == WM_KEYDOWN;
    if (key == VK_ESCAPE && down)
    {
        fly_capture_ = false; dragging_ = false; keys_.fill(false);
        scene_panel_.ReleasePointerCaptures();
    }
    if (key == VK_SHIFT) shift_ = down;
    const wchar_t* names = L"WSADQE";
    for (size_t i = 0; i < 6; ++i) if (key == names[i]) keys_[i] = down;
}

LRESULT CALLBACK MainWindow::keyboard_proc(HWND hwnd, UINT message, WPARAM key, LPARAM other,
                                           UINT_PTR, DWORD_PTR data)
{
    auto *self = reinterpret_cast<MainWindow *>(data);
    if (self && !self->closing_) self->on_key(message, key);
    return DefSubclassProc(hwnd, message, key, other);
}

void MainWindow::update_engine()
{
    if (!engine_ || closing_) return;
    for (const auto &event : engine_->poll_events())
    {
        using Kind = gs::engine::EngineEvent::Kind;
        switch (event.kind)
        {
        case Kind::DeviceLost:
            keys_.fill(false); fly_capture_ = dragging_ = false;
            flip_y_button_.IsEnabled(false);
            release_surface();
            if (waiting_for_detach_) { waiting_for_detach_ = false; Close(); return; }
            set_busy(L"正在恢复图形设备");
            engine_->acknowledge_device_release(event.generation);
            break;
        case Kind::SurfaceDetached:
            if (waiting_for_detach_ && event.generation == generation_)
            {
                release_surface(); waiting_for_detach_ = false; Close(); return;
            }
            break;
        case Kind::SurfaceRebindRequired:
            release_surface();
            try { create_surface(); } catch (...) { set_status(L"无法恢复渲染视口"); }
            break;
        case Kind::SceneReady:
            if (event.request == request_id_)
            {
                model_text_.Text(hstring{pending_path_.filename().wstring()});
                empty_text_.Visibility(Visibility::Collapsed);
                close_button_.IsEnabled(true); fit_button_.IsEnabled(true);
                reset_button_.IsEnabled(true); flip_y_button_.IsEnabled(true);
                camera_command(fly_mode_ ? gs::engine::CameraAction::FlyMode
                                         : gs::engine::CameraAction::OrbitMode);
                finish_busy(); set_status(L"就绪");
            }
            break;
        case Kind::SceneFailed:
            if (event.request == request_id_ &&
                (!event.error || !std::holds_alternative<gs::render::RenderError>(*event.error) ||
                 std::get<gs::render::RenderError>(*event.error).code !=
                     gs::render::RenderErrorCode::Cancelled))
            {
                finish_busy();
                const bool out_of_memory = event.error &&
                    ((std::holds_alternative<gs::io::LoadError>(*event.error) &&
                      std::get<gs::io::LoadError>(*event.error).code == gs::io::LoadErrorCode::OutOfMemory) ||
                     (std::holds_alternative<gs::render::RenderError>(*event.error) &&
                      std::get<gs::render::RenderError>(*event.error).code ==
                          gs::render::RenderErrorCode::OutOfVideoMemory));
                set_status(out_of_memory ? L"内存或显存不足，当前模型仍可浏览" : L"无法打开此模型文件");
            }
            break;
        case Kind::SceneCleared:
            model_text_.Text(L""); empty_text_.Visibility(Visibility::Visible);
            close_button_.IsEnabled(false); fit_button_.IsEnabled(false);
            reset_button_.IsEnabled(false); flip_y_button_.IsEnabled(false);
            finish_busy(); set_status(L"就绪");
            break;
        case Kind::DeviceRestored:
            flip_y_button_.IsEnabled(engine_->snapshot().active_scene.has_value());
            finish_busy(); set_status(L"就绪"); break;
        case Kind::Fault:
            finish_busy(); set_status(L"图形设备恢复失败"); break;
        default: break;
        }
    }
    const auto state = engine_->snapshot();
    scene_ready_ = state.active_scene.has_value() &&
                   state.phase != gs::engine::Phase::Recovering &&
                   state.phase != gs::engine::Phase::Closing &&
                   state.phase != gs::engine::Phase::Failed;
    if (state.phase == gs::engine::Phase::Uploading)
        set_busy(L"正在准备画面");
    else if (state.phase == gs::engine::Phase::Loading && state.load_progress)
    {
        switch (state.load_progress->stage)
        {
        case gs::io::LoadStage::Opening: set_busy(L"正在打开文件"); break;
        case gs::io::LoadStage::Inspecting: set_busy(L"正在检查模型"); break;
        case gs::io::LoadStage::Decoding: set_busy(L"正在读取模型"); break;
        case gs::io::LoadStage::Validating: set_busy(L"正在验证模型"); break;
        default: break;
        }
    }
}
}

#include "MainWindow.g.cpp"
