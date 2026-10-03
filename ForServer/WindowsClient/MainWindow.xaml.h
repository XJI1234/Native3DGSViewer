#pragma once
#include "MainWindow.g.h"
#include "gs_server/packet_cache.h"
#include "gs_server/viewer_controls.h"
#include "gs_server/windows_client.h"
#include <optional>
#include <vector>

namespace cloud
{
extern std::wstring smoke_config;
extern int smoke_exit_code;
extern gs::server::Endpoint shutdown_endpoint;
} // namespace cloud
namespace winrt::Native3DGSCloud::implementation
{
struct MainWindow : MainWindowT<MainWindow>
{
    MainWindow();

  private:
    void initialize();
    void set_open(bool enabled);
    void open_model();
    void close_model();
    void disconnect();
    void navigate(double yaw, double pitch);
    void zoom(double steps);
    void reset_view();
    void queue_render();
    void display_frame(const gs::server::Frame &frame);
    winrt::fire_and_forget connect_async();
    winrt::fire_and_forget render_loop();
    winrt::fire_and_forget prefetch_loop();
    winrt::fire_and_forget heartbeat();
    winrt::fire_and_forget release_session(gs::server::Endpoint endpoint);
    winrt::fire_and_forget finish_smoke(bool success, std::string error = "");
    void advance_smoke(const gs::server::FrameRequest &request,
                       const gs::server::Download &download, const gs::server::Frame &frame,
                       double decode_ms);
    Microsoft::UI::Xaml::Controls::Grid root_{nullptr}, connection_page_{nullptr},
        viewer_page_{nullptr};
    Microsoft::UI::Xaml::Controls::TextBox address_{nullptr}, port_{nullptr};
    Microsoft::UI::Xaml::Controls::PasswordBox token_{nullptr};
    Microsoft::UI::Xaml::Controls::CheckBox https_{nullptr}, private_http_{nullptr};
    Microsoft::UI::Xaml::Controls::CheckBox flip_y_{nullptr}, prefetch_enabled_{nullptr};
    Microsoft::UI::Xaml::Controls::ComboBox prefetch_depth_{nullptr};
    Microsoft::UI::Xaml::Controls::Button connect_{nullptr}, close_{nullptr}, fit_{nullptr},
        reset_{nullptr}, left_{nullptr}, up_{nullptr}, down_{nullptr}, right_{nullptr};
    Microsoft::UI::Xaml::Controls::ComboBox models_box_{nullptr}, quality_{nullptr};
    Microsoft::UI::Xaml::Controls::TextBlock connection_status_{nullptr}, status_{nullptr},
        model_text_{nullptr}, empty_{nullptr};
    Microsoft::UI::Xaml::Controls::Border busy_{nullptr};
    Microsoft::UI::Xaml::Controls::ScrollViewer scene_host_{nullptr};
    Microsoft::UI::Xaml::Controls::Image image_{nullptr};
    Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap bitmap_{nullptr};
    Microsoft::UI::Xaml::DispatcherTimer resize_timer_{nullptr}, smoke_timer_{nullptr};
    Microsoft::UI::Xaml::DispatcherTimer heartbeat_timer_{nullptr};
    gs::server::Endpoint endpoint_;
    std::shared_ptr<gs::server::HttpCancellation> active_download_, predicted_download_;
    gs::server::FrameRequest view_;
    std::vector<std::string> model_ids_;
    std::optional<gs::server::FrameRequest> pending_;
    std::optional<gs::server::FrameRequest> predicted_;
    gs::server::PacketCache packet_cache_;
    uint64_t prefetch_identity_ = 1ull << 40, local_hits_ = 0, prefetched_ = 0;
    std::string last_requested_key_;
    bool prefetching_ = false, heartbeating_ = false;
    gs::server::LeaseHealth lease_health_;
    uint64_t heartbeat_successes_ = 0, session_recoveries_ = 0;
    bool lease_warning_ = false;
    uint64_t generation_ = 0, dropped_ = 0;
    bool initialized_ = false, closed_ = false, connected_ = false, opened_ = false,
         connecting_ = false, rendering_ = false, dragging_ = false;
    Windows::Foundation::Point drag_position_{};
    bool smoke_ = false;
    int smoke_stage_ = 0, smoke_wait_ = 0;
    int smoke_idle_seconds_ = 0;
    bool smoke_idle_started_ = false, smoke_require_recovery_ = false;
    std::chrono::steady_clock::time_point smoke_idle_start_;
    std::wstring smoke_output_;
    Windows::Data::Json::JsonArray smoke_results_;
};
} // namespace winrt::Native3DGSCloud::implementation
namespace winrt::Native3DGSCloud::factory_implementation
{
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
{
};
} // namespace winrt::Native3DGSCloud::factory_implementation
