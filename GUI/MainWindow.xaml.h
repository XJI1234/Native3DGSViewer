#pragma once
#include "MainWindow.g.h"
#include "native3dgs/engine.h"
#include <dxgi1_4.h>
#include <winrt/Microsoft.UI.Windowing.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string_view>

namespace winrt::Native3DGSViewer::implementation
{
struct MainWindow : MainWindowT<MainWindow>
{
    MainWindow();
  private:
    void build_interface();
    void initialize_viewer();
    void connect_events();
    void create_surface();
    void release_surface();
    gs::render::Viewport viewport() const;
    void resize_surface();
    void set_busy(std::wstring_view text);
    void finish_busy();
    void set_status(std::wstring_view text);
    void update_engine();
    void camera_command(gs::engine::CameraAction action, double x = 0, double y = 0,
                        double z = 0, double seconds = 0, bool fast = false);
    void set_flip_y(bool enabled);
    void open_path(std::filesystem::path path);
    void try_open_initial();
    winrt::fire_and_forget pick_file();
    winrt::fire_and_forget accept_drop(Microsoft::UI::Xaml::DragEventArgs args);
    void on_pointer_move(Microsoft::UI::Xaml::Input::PointerRoutedEventArgs args);
    void on_key(UINT message, WPARAM key);
    static LRESULT CALLBACK keyboard_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    Microsoft::UI::Xaml::Controls::Grid root_{nullptr};
    Microsoft::UI::Xaml::Controls::SwapChainPanel scene_panel_{nullptr};
    Microsoft::UI::Xaml::Controls::Button open_button_{nullptr}, close_button_{nullptr};
    Microsoft::UI::Xaml::Controls::Button fit_button_{nullptr}, reset_button_{nullptr};
    Microsoft::UI::Xaml::Controls::Primitives::ToggleButton flip_y_button_{nullptr};
    Microsoft::UI::Xaml::Controls::Button cancel_button_{nullptr};
    Microsoft::UI::Xaml::Controls::RadioButton orbit_button_{nullptr}, fly_button_{nullptr};
    Microsoft::UI::Xaml::Controls::TextBlock empty_text_{nullptr}, busy_text_{nullptr};
    Microsoft::UI::Xaml::Controls::TextBlock status_text_{nullptr}, model_text_{nullptr};
    Microsoft::UI::Xaml::Controls::Border busy_panel_{nullptr};
    Microsoft::UI::Xaml::DispatcherTimer timer_{nullptr};
    std::unique_ptr<gs::engine::IEngine> engine_;
    std::filesystem::path initial_path_;
    std::filesystem::path pending_path_;
    winrt::com_ptr<IDXGISwapChain3> swapchain_;
    gs::render::SurfaceGeneration generation_ = 0;
    gs::engine::RequestId request_id_ = 0;
    HWND hwnd_ = nullptr;
    std::array<bool, 6> keys_{};
    bool shift_ = false, dragging_ = false, right_drag_ = false, fly_capture_ = false;
    bool closing_ = false;
    bool waiting_for_detach_ = false;
    bool initialized_ = false;
    bool flip_y_ = false, fly_mode_ = false, updating_flip_button_ = false;
    bool scene_ready_ = false;
    Microsoft::UI::Xaml::XamlRoot xaml_root_{nullptr};
    winrt::event_token xaml_root_changed_{};
    Microsoft::UI::Windowing::AppWindow app_window_{nullptr};
    winrt::event_token app_window_closing_{};
    Windows::Foundation::Point last_pointer_{};
    std::chrono::steady_clock::time_point last_tick_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point detach_started_{};
};
void wait_for_viewer_shutdown();
}
namespace winrt::Native3DGSViewer::factory_implementation
{
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
