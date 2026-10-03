#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.xaml.g.hpp")
#include "MainWindow.xaml.g.hpp"
#elif __has_include("MainWindow.xaml.g.hpp.backup")
#include "MainWindow.xaml.g.hpp.backup"
#else
#include "MainWindow.xaml.bootstrap.h"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <microsoft.ui.xaml.window.h>
#include <psapi.h>
#include <robuffer.h>

namespace winrt::Native3DGSCloud::implementation
{
namespace
{
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using Clock = std::chrono::steady_clock;
std::string text_file(const std::wstring &path)
{
    std::ifstream input{std::filesystem::path{path}, std::ios::binary};
    if (!input)
        throw std::runtime_error("Cannot read configuration/token file");
    std::string result{std::istreambuf_iterator<char>(input), {}};
    if (result.size() > 65536)
        throw std::runtime_error("Configuration file budget");
    return result;
}
std::string packet_text(const gs::server::Download &reply)
{
    return {reply.packet.begin(), reply.packet.end()};
}
} // namespace
MainWindow::MainWindow()
{
    InitializeComponent();
    Title(L"3DGS Cloud Viewer");
    Activated([this](auto &&, auto &&) {
        if (!initialized_)
        {
            initialized_ = true;
            initialize();
        }
    });
    Closed([this](auto &&, auto &&) {
        closed_ = true;
        ++generation_;
        pending_.reset();
        predicted_.reset();
        packet_cache_.clear();
        if (active_download_)
            active_download_->cancel();
        if (predicted_download_)
            predicted_download_->cancel();
        if (smoke_ && !smoke_output_.empty())
        {
            std::ofstream(std::filesystem::path{smoke_output_} / "cache-after-close.json")
                << "{\"packet_cache_bytes\":" << packet_cache_.bytes() << "}";
        }
        if (heartbeat_timer_)
            heartbeat_timer_.Stop();
        cloud::shutdown_endpoint = endpoint_;
        if (resize_timer_)
            resize_timer_.Stop();
        if (smoke_timer_)
            smoke_timer_.Stop();
    });
}
void MainWindow::initialize()
{
    root_ = Content().as<Grid>();
    connection_page_ = root_.FindName(L"ConnectionPage").as<Grid>();
    viewer_page_ = root_.FindName(L"ViewerPage").as<Grid>();
    address_ = root_.FindName(L"AddressBox").as<TextBox>();
    port_ = root_.FindName(L"PortBox").as<TextBox>();
    token_ = root_.FindName(L"TokenBox").as<PasswordBox>();
    https_ = root_.FindName(L"HttpsBox").as<CheckBox>();
    private_http_ = root_.FindName(L"PrivateHttpBox").as<CheckBox>();
    flip_y_ = root_.FindName(L"FlipYBox").as<CheckBox>();
    prefetch_enabled_ = root_.FindName(L"PrefetchBox").as<CheckBox>();
    prefetch_depth_ = root_.FindName(L"PrefetchDepthBox").as<ComboBox>();
    flip_y_.Checked([this](auto &&, auto &&) {
        view_.flip_y = true;
        queue_render();
    });
    flip_y_.Unchecked([this](auto &&, auto &&) {
        view_.flip_y = false;
        queue_render();
    });
    prefetch_enabled_.Unchecked([this](auto &&, auto &&) {
        predicted_.reset();
        if (predicted_download_)
            predicted_download_->cancel();
    });
    prefetch_enabled_.Checked([this](auto &&, auto &&) {
        last_requested_key_.clear();
        queue_render();
    });
    prefetch_depth_.SelectionChanged([this](auto &&, auto &&) {
        last_requested_key_.clear();
        queue_render();
    });
    connect_ = root_.FindName(L"ConnectButton").as<Button>();
    connection_status_ = root_.FindName(L"ConnectionStatus").as<TextBlock>();
    models_box_ = root_.FindName(L"ModelsBox").as<ComboBox>();
    quality_ = root_.FindName(L"QualityBox").as<ComboBox>();
    status_ = root_.FindName(L"StatusText").as<TextBlock>();
    model_text_ = root_.FindName(L"ModelText").as<TextBlock>();
    empty_ = root_.FindName(L"EmptyText").as<TextBlock>();
    busy_ = root_.FindName(L"BusyPanel").as<Border>();
    scene_host_ = root_.FindName(L"SceneHost").as<ScrollViewer>();
    image_ = root_.FindName(L"SceneImage").as<Image>();
    close_ = root_.FindName(L"CloseButton").as<Button>();
    fit_ = root_.FindName(L"FitButton").as<Button>();
    reset_ = root_.FindName(L"ResetButton").as<Button>();
    left_ = root_.FindName(L"LeftButton").as<Button>();
    up_ = root_.FindName(L"UpButton").as<Button>();
    down_ = root_.FindName(L"DownButton").as<Button>();
    right_ = root_.FindName(L"RightButton").as<Button>();
    connect_.Click([this](auto &&, auto &&) { connect_async(); });
    root_.FindName(L"OpenButton").as<Button>().Click([this](auto &&, auto &&) { open_model(); });
    root_.FindName(L"DisconnectButton").as<Button>().Click([this](auto &&, auto &&) {
        disconnect();
    });
    close_.Click([this](auto &&, auto &&) { close_model(); });
    fit_.Click([this](auto &&, auto &&) { reset_view(); });
    reset_.Click([this](auto &&, auto &&) { reset_view(); });
    left_.Click([this](auto &&, auto &&) { navigate(-2, 0); });
    right_.Click([this](auto &&, auto &&) { navigate(2, 0); });
    up_.Click([this](auto &&, auto &&) { navigate(0, -2); });
    down_.Click([this](auto &&, auto &&) { navigate(0, 2); });
    models_box_.SelectionChanged([this](auto &&, auto &&) {
        if (opened_)
            open_model();
    });
    quality_.SelectionChanged([this](auto &&, auto &&) { queue_render(); });
    scene_host_.PointerPressed(
        [this](auto &&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &args) {
            scene_host_.Focus(FocusState::Pointer);
            const auto point = args.GetCurrentPoint(scene_host_);
            if (opened_ && point.Properties().IsLeftButtonPressed())
            {
                dragging_ = true;
                drag_position_ = point.Position();
                scene_host_.CapturePointer(args.Pointer());
                args.Handled(true);
            }
        });
    scene_host_.PointerMoved(
        [this](auto &&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &args) {
            if (dragging_)
            {
                const auto position = args.GetCurrentPoint(scene_host_).Position();
                navigate((position.X - drag_position_.X) * 0.3,
                         (position.Y - drag_position_.Y) * 0.3);
                drag_position_ = position;
                args.Handled(true);
            }
        });
    scene_host_.PointerReleased([this](auto &&, auto &&) {
        dragging_ = false;
        scene_host_.ReleasePointerCaptures();
    });
    scene_host_.PointerCaptureLost([this](auto &&, auto &&) { dragging_ = false; });
    scene_host_.PointerWheelChanged(
        [this](auto &&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const &args) {
            if (opened_)
            {
                zoom(args.GetCurrentPoint(scene_host_).Properties().MouseWheelDelta() / 120.0);
                args.Handled(true);
            }
        });
    scene_host_.KeyDown(
        [this](auto &&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const &args) {
            if (!opened_)
                return;
            using Windows::System::VirtualKey;
            switch (args.Key())
            {
            case VirtualKey::Left:
                navigate(-2, 0);
                break;
            case VirtualKey::Right:
                navigate(2, 0);
                break;
            case VirtualKey::Up:
                navigate(0, -2);
                break;
            case VirtualKey::Down:
                navigate(0, 2);
                break;
            default:
                return;
            }
            args.Handled(true);
        });
    resize_timer_ = DispatcherTimer();
    heartbeat_timer_ = DispatcherTimer();
    heartbeat_timer_.Interval(std::chrono::seconds(10));
    heartbeat_timer_.Tick([this](auto &&, auto &&) { heartbeat(); });
    resize_timer_.Interval(std::chrono::milliseconds(100));
    resize_timer_.Tick([this](auto &&, auto &&) {
        resize_timer_.Stop();
        queue_render();
    });
    scene_host_.SizeChanged([this](auto &&, auto &&) {
        resize_timer_.Stop();
        resize_timer_.Start();
    });
    winrt::com_ptr<IWindowNative> native;
    check_hresult(QueryInterface(IID_PPV_ARGS(native.put())));
    HWND handle = nullptr;
    check_hresult(native->get_WindowHandle(&handle));
    const auto scale = GetDpiForWindow(handle) / 96.0;
    const auto work = Microsoft::UI::Windowing::DisplayArea::GetFromWindowId(
                          AppWindow().Id(), Microsoft::UI::Windowing::DisplayAreaFallback::Primary)
                          .WorkArea();
    AppWindow().Resize({std::min(work.Width - 40, int(1280 * scale)),
                        std::min(work.Height - 40, int(800 * scale))});
    if (!cloud::smoke_config.empty())
    {
        smoke_ = true;
        try
        {
            const auto config = JsonObject::Parse(to_hstring(text_file(cloud::smoke_config)));
            const Windows::Foundation::Uri uri{config.GetNamedString(L"url")};
            address_.Text(uri.Host());
            port_.Text(to_hstring(uri.Port()));
            https_.IsChecked(uri.SchemeName() == L"https");
            private_http_.IsChecked(config.GetNamedBoolean(L"private_http", false));
            auto token = text_file(std::wstring(config.GetNamedString(L"token_file")));
            while (!token.empty() && (token.back() == '\r' || token.back() == '\n'))
                token.pop_back();
            token_.Password(to_hstring(token));
            smoke_output_ = config.GetNamedString(L"output");
            smoke_idle_seconds_ =
                int(std::clamp(config.GetNamedNumber(L"idle_seconds", 0), 0.0, 180.0));
            smoke_require_recovery_ = config.GetNamedBoolean(L"require_session_recovery", false);
            connect_async();
        }
        catch (const std::exception &error)
        {
            finish_smoke(false, error.what());
        }
        catch (const hresult_error &error)
        {
            finish_smoke(false, to_string(error.message()));
        }
    }
}
void MainWindow::set_open(bool enabled)
{
    opened_ = enabled;
    for (const auto &button : {close_, fit_, reset_, left_, up_, down_, right_})
        button.IsEnabled(enabled);
}
void MainWindow::open_model()
{
    const auto selected = models_box_.SelectedIndex();
    if (!connected_ || selected < 0 || size_t(selected) >= model_ids_.size())
        return;
    release_session(endpoint_);
    endpoint_.session.clear();
    predicted_.reset();
    packet_cache_.clear();
    view_ = {};
    last_requested_key_.clear();
    view_.flip_y = flip_y_.IsChecked().GetBoolean();
    view_.model_id = model_ids_[selected];
    image_.Source(nullptr);
    bitmap_ = nullptr;
    empty_.Visibility(Visibility::Visible);
    empty_.Text(L"正在加载云端模型");
    model_text_.Text(unbox_value<hstring>(models_box_.SelectedItem()));
    set_open(true);
    queue_render();
}
void MainWindow::close_model()
{
    if (active_download_)
        active_download_->cancel();
    if (predicted_download_)
        predicted_download_->cancel();
    release_session(endpoint_);
    endpoint_.session.clear();
    predicted_.reset();
    packet_cache_.clear();
    if (heartbeat_timer_)
        heartbeat_timer_.Stop();
    last_requested_key_.clear();
    ++generation_;
    pending_.reset();
    set_open(false);
    dragging_ = false;
    scene_host_.ReleasePointerCaptures();
    image_.Source(nullptr);
    bitmap_ = nullptr;
    empty_.Visibility(Visibility::Visible);
    empty_.Text(L"选择云端模型并点击打开");
    busy_.Visibility(Visibility::Collapsed);
    model_text_.Text(L"");
    status_.Text(L"已关闭；已释放查看会话，服务器将在空闲宽限期后卸载实例");
}
void MainWindow::disconnect()
{
    close_model();
    connected_ = false;
    endpoint_ = {};
    token_.Password(L"");
    model_ids_.clear();
    models_box_.Items().Clear();
    viewer_page_.Visibility(Visibility::Collapsed);
    connection_page_.Visibility(Visibility::Visible);
    connection_status_.Text(L"已断开；请输入服务器信息重新检查");
}
void MainWindow::navigate(double yaw, double pitch)
{
    if (!opened_)
        return;
    view_ = gs::server::navigate_view(view_, yaw, pitch);
    queue_render();
}
void MainWindow::zoom(double steps)
{
    if (!opened_)
        return;
    const auto index = std::clamp(int(std::round(40 + 40 * std::log10(view_.zoom) - steps)), 0, 80);
    view_.zoom = std::pow(10.0, (index - 40) / 40.0);
    queue_render();
}
void MainWindow::reset_view()
{
    if (!opened_)
        return;
    view_.yaw = 0;
    view_.pitch = 14;
    view_.zoom = 1;
    queue_render();
}
winrt::fire_and_forget MainWindow::connect_async()
{
    if (connecting_ || closed_)
        co_return;
    auto lifetime = get_strong();
    apartment_context ui;
    connecting_ = true;
    connect_.IsEnabled(false);
    connection_status_.Text(L"正在检查服务与访问权限…");
    const auto generation = ++generation_;
    gs::server::Endpoint endpoint;
    std::vector<std::pair<std::string, hstring>> models;
    std::string error;
    try
    {
        const auto host = to_string(address_.Text());
        const auto port = to_string(port_.Text());
        if (host.empty() ||
            host.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:[]") !=
                std::string::npos ||
            port.empty() || port.find_first_not_of("0123456789") != std::string::npos ||
            std::stoul(port) < 1 || std::stoul(port) > 65535)
            throw std::runtime_error("请输入有效服务器地址和1–65535端口");
        const auto authority =
            host.find(':') != std::string::npos && host.front() != '[' ? "[" + host + "]" : host;
        endpoint = {(https_.IsChecked().GetBoolean() ? "https://" : "http://") + authority + ":" +
                        port,
                    to_string(token_.Password()), private_http_.IsChecked().GetBoolean()};
        gs::server::validate_endpoint(endpoint);
        co_await resume_background();
        const auto health = JsonObject::Parse(
            to_hstring(packet_text(gs::server::request_http(endpoint, "/health"))));
        if (health.GetNamedNumber(L"protocol") != 2 ||
            health.GetNamedNumber(L"api_version", 0) != 3 ||
            health.GetNamedNumber(L"available") < 1)
            throw std::runtime_error("服务未就绪或不是v2图像协议");
        const auto catalog = JsonArray::Parse(
            to_hstring(packet_text(gs::server::request_http(endpoint, "/models"))));
        for (const auto &entry : catalog)
        {
            const auto model = entry.GetObject();
            models.emplace_back(to_string(model.GetNamedString(L"id")),
                                model.GetNamedString(L"name"));
        }
        if (models.empty())
            throw std::runtime_error("服务器没有可用模型");
    }
    catch (const std::exception &failure)
    {
        error = failure.what();
    }
    catch (const hresult_error &failure)
    {
        error = to_string(failure.message());
    }
    try
    {
        co_await ui;
    }
    catch (...)
    {
        co_return;
    }
    connecting_ = false;
    if (closed_ || generation != generation_)
        co_return;
    connect_.IsEnabled(true);
    if (!error.empty())
    {
        connection_status_.Text(L"连接失败：" + to_hstring(error));
        if (smoke_)
            finish_smoke(false, error);
        co_return;
    }
    endpoint_ = std::move(endpoint);
    models_box_.Items().Clear();
    model_ids_.clear();
    for (const auto &[id, name] : models)
    {
        model_ids_.push_back(id);
        models_box_.Items().Append(box_value(name));
    }
    models_box_.SelectedIndex(0);
    connected_ = true;
    connection_page_.Visibility(Visibility::Collapsed);
    viewer_page_.Visibility(Visibility::Visible);
    status_.Text(L"已连接 · " + to_hstring(endpoint_.url) + L" · 选择云端模型");
    if (smoke_)
        open_model();
}
void MainWindow::queue_render()
{
    if (!opened_ || !connected_ || closed_)
        return;
    const auto scale = root_.XamlRoot() ? root_.XamlRoot().RasterizationScale() : 1.0;
    const auto width = std::max(64.0, scene_host_.ActualWidth() * scale);
    const auto height = std::max(64.0, scene_host_.ActualHeight() * scale);
    const auto reduction = std::min({1.0, 1920.0 / width, 1080.0 / height});
    view_.width = uint32_t(std::round(width * reduction));
    view_.height = uint32_t(std::round(height * reduction));
    const gs::server::Profile profiles[]{gs::server::Profile::Rgba, gs::server::Profile::Jpeg95,
                                         gs::server::Profile::Jpeg90, gs::server::Profile::Jpeg85};
    view_.profile = profiles[std::clamp(quality_.SelectedIndex(), 0, 3)];
    const auto key = gs::server::view_key(view_);
    if (key == last_requested_key_)
        return;
    if (active_download_)
        active_download_->cancel();
    if (predicted_download_)
        predicted_download_->cancel();
    last_requested_key_ = key;
    view_.request_id = ++generation_;
    pending_ = gs::server::canonical_request(view_);
    std::vector<std::string> protected_keys{gs::server::view_key(*pending_)};
    const auto nearest = gs::server::predicted_views(*pending_, 5);
    for (size_t index = 0; index < std::min(size_t(4), nearest.size()); ++index)
        protected_keys.push_back(gs::server::view_key(nearest[index]));
    packet_cache_.protect(protected_keys);
    busy_.Visibility(Visibility::Visible);
    status_.Text(L"正在加载/渲染云端视图；可继续调整，最后一次操作优先");
    if (!rendering_)
        render_loop();
}
winrt::fire_and_forget MainWindow::render_loop()
{
    auto lifetime = get_strong();
    rendering_ = true;
    apartment_context ui;
    while (pending_ && !closed_)
    {
        const auto request = *pending_;
        pending_.reset();
        auto endpoint = endpoint_;
        endpoint.cancellation = std::make_shared<gs::server::HttpCancellation>();
        active_download_ = endpoint.cancellation;
        auto local = packet_cache_.get(gs::server::view_key(request));
        gs::server::Download download;
        gs::server::Frame frame;
        double decode_ms = 0;
        std::string error;
        co_await resume_background();
        try
        {
            if (endpoint.session.empty())
            {
                const auto response =
                    gs::server::request_http(endpoint, "/sessions", request.model_id);
                endpoint.session = to_string(JsonObject::Parse(to_hstring(packet_text(response)))
                                                 .GetNamedString(L"session_id"));
                co_await ui;
                if (closed_ || !opened_ || view_.model_id != request.model_id ||
                    endpoint_.url != endpoint.url)
                {
                    release_session(endpoint);
                    if (closed_)
                        co_return;
                    continue;
                }
                if (endpoint_.session.empty())
                    endpoint_.session = endpoint.session;
                else if (endpoint_.session != endpoint.session)
                {
                    release_session(endpoint);
                    endpoint.session = endpoint_.session;
                }
                heartbeat_timer_.Start();
                lease_health_.success();
                co_await resume_background();
            }
            download = local ? *local : gs::server::request_frame(endpoint, request);
            if (local)
            {
                download.cache_state = "local";
                download.milliseconds = 0;
            }
            const auto start = Clock::now();
            frame = gs::server::decode_frame(download.packet);
            for (size_t offset = 0; offset < frame.rgba.size(); offset += 4)
            {
                std::swap(frame.rgba[offset], frame.rgba[offset + 2]);
                frame.rgba[offset + 3] = 255;
            }
            decode_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }
        catch (const std::exception &failure)
        {
            error = failure.what();
        }
        catch (const hresult_error &failure)
        {
            error = to_string(failure.message());
        }
        try
        {
            co_await ui;
        }
        catch (...)
        {
            co_return;
        }
        if (closed_)
            co_return;
        if (active_download_ == endpoint.cancellation)
            active_download_.reset();
        if (error.empty() && opened_ && view_.model_id == request.model_id &&
            endpoint_.session == endpoint.session)
            packet_cache_.put(gs::server::view_key(request), download);
        if (request.request_id != generation_)
        {
            ++dropped_;
            continue;
        }
        if (!error.empty())
        {
            if (local)
                packet_cache_.erase(gs::server::view_key(request));
            last_requested_key_.clear();
            status_.Text(L"渲染失败：" + to_hstring(error));
            empty_.Text(L"云端渲染失败，可重试或切换模型");
            if (smoke_)
                finish_smoke(false, error);
        }
        else
        {
            try
            {
                display_frame(frame);
                if (local)
                    ++local_hits_;
                status_.Text(to_hstring(gs::server::profile_name(frame.profile)) + L" · " +
                             to_hstring(frame.width) + L"×" + to_hstring(frame.height) + L" · " +
                             to_hstring(download.packet.size() / 1024) + L" KiB · 网络/服务 " +
                             to_hstring(int(download.milliseconds)) + L" ms · 解码 " +
                             to_hstring(int(decode_ms)) + L" ms · " +
                             to_hstring(gs::server::view_code(request)) + L" · 缓存 " +
                             to_hstring(download.cache_state) + L" · 预渲染 " +
                             to_hstring(prefetched_));
                if (prefetch_enabled_.IsChecked().GetBoolean())
                {
                    predicted_ = request;
                    if (!prefetching_)
                        prefetch_loop();
                }
                if (smoke_)
                    advance_smoke(request, download, frame, decode_ms);
            }
            catch (const std::exception &failure)
            {
                status_.Text(to_hstring(failure.what()));
                if (smoke_)
                    finish_smoke(false, failure.what());
            }
            catch (const hresult_error &failure)
            {
                status_.Text(failure.message());
                if (smoke_)
                    finish_smoke(false, to_string(failure.message()));
            }
        }
    }
    rendering_ = false;
    if (!closed_)
        busy_.Visibility(Visibility::Collapsed);
}
winrt::fire_and_forget MainWindow::release_session(gs::server::Endpoint endpoint)
{
    if (endpoint.session.empty())
        co_return;
    co_await resume_background();
    try
    {
        gs::server::request_http(endpoint, "/sessions/close", endpoint.session);
    }
    catch (...)
    {
    }
}
winrt::fire_and_forget MainWindow::heartbeat()
{
    if (heartbeating_ || closed_ || !opened_ || endpoint_.session.empty())
        co_return;
    auto lifetime = get_strong();
    apartment_context ui;
    heartbeating_ = true;
    auto endpoint = endpoint_;
    const auto model = view_.model_id;
    std::string replacement, error;
    co_await resume_background();
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        try
        {
            try
            {
                const auto response =
                    gs::server::request_http(endpoint, "/sessions/heartbeat", endpoint.session);
                if (!JsonObject::Parse(to_hstring(packet_text(response))).GetNamedBoolean(L"ok"))
                    throw std::runtime_error("Session renewal rejected");
            }
            catch (const gs::server::HttpError &failure)
            {
                if (failure.status() != 410)
                    throw;
                const auto response = gs::server::request_http(endpoint, "/sessions", model);
                replacement = to_string(JsonObject::Parse(to_hstring(packet_text(response)))
                                            .GetNamedString(L"session_id"));
            }
            error.clear();
            break;
        }
        catch (const gs::server::HttpError &failure)
        {
            error = failure.what();
            if (failure.status() == 401 || failure.status() == 404)
                break;
        }
        catch (const std::exception &failure)
        {
            error = failure.what();
        }
        catch (const hresult_error &failure)
        {
            error = to_string(failure.message());
        }
        if (attempt < 2)
            co_await resume_after(std::chrono::milliseconds(250 * (attempt + 1)));
    }
    try
    {
        co_await ui;
    }
    catch (...)
    {
        co_return;
    }
    heartbeating_ = false;
    const bool current = !closed_ && opened_ && model == view_.model_id &&
                         endpoint_.url == endpoint.url && endpoint_.session == endpoint.session;
    if (!current)
    {
        if (!replacement.empty())
        {
            endpoint.session = replacement;
            release_session(endpoint);
        }
        co_return;
    }
    if (error.empty())
    {
        ++heartbeat_successes_;
        lease_health_.success();
        if (lease_warning_)
            status_.Text(L"连接已恢复 · " + to_hstring(gs::server::view_code(view_)));
        lease_warning_ = false;
    }
    else if (lease_health_.failure())
    {
        lease_warning_ = true;
        status_.Text(L"连接暂时不可用，正在自动重试：" + to_hstring(error));
    }
    if (!replacement.empty())
    {
        if (!closed_ && opened_ && model == view_.model_id && endpoint_.session == endpoint.session)
        {
            endpoint_.session = replacement;
            ++session_recoveries_;
            packet_cache_.clear();
            last_requested_key_.clear();
            queue_render();
        }
        else
        {
            endpoint.session = replacement;
            release_session(endpoint);
        }
    }
}
winrt::fire_and_forget MainWindow::prefetch_loop()
{
    auto lifetime = get_strong();
    apartment_context ui;
    prefetching_ = true;
    while (predicted_ && !closed_ && opened_ && prefetch_enabled_.IsChecked().GetBoolean())
    {
        const auto center = *predicted_;
        predicted_.reset();
        const auto generation = center.request_id;
        const auto depth = gs::server::preview_depth(prefetch_depth_.SelectedIndex());
        auto endpoint = endpoint_;
        endpoint.prefetch = true;
        for (auto next : gs::server::predicted_views(center, depth))
        {
            if (closed_ || !opened_ || generation != generation_ ||
                !prefetch_enabled_.IsChecked().GetBoolean())
                break;
            const auto key = gs::server::view_key(next);
            if (packet_cache_.contains(key))
                continue;
            next.request_id = ++prefetch_identity_;
            endpoint.cancellation = std::make_shared<gs::server::HttpCancellation>();
            predicted_download_ = endpoint.cancellation;
            gs::server::Download packet;
            bool success = false;
            co_await resume_background();
            try
            {
                packet = gs::server::request_frame(endpoint, next);
                success = true;
            }
            catch (...)
            {
            }
            try
            {
                co_await ui;
            }
            catch (...)
            {
                co_return;
            }
            if (closed_)
                co_return;
            if (success && opened_ && view_.model_id == next.model_id &&
                endpoint_.session == endpoint.session)
            {
                packet_cache_.put(key, std::move(packet));
                ++prefetched_;
            }
        }
    }
    prefetching_ = false;
}
void MainWindow::display_frame(const gs::server::Frame &frame)
{
    if (!bitmap_ || bitmap_.PixelWidth() != int(frame.width) ||
        bitmap_.PixelHeight() != int(frame.height))
    {
        bitmap_ = Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap(int(frame.width),
                                                                       int(frame.height));
        image_.Source(bitmap_);
    }
    auto buffer = bitmap_.PixelBuffer();
    if (buffer.Capacity() != frame.rgba.size())
        throw std::runtime_error("WinUI pixel buffer layout mismatch");
    uint8_t *bytes = nullptr;
    check_hresult(buffer.as<::Windows::Storage::Streams::IBufferByteAccess>()->Buffer(&bytes));
    std::memcpy(bytes, frame.rgba.data(), frame.rgba.size());
    bitmap_.Invalidate();
    empty_.Visibility(Visibility::Collapsed);
}
void MainWindow::advance_smoke(const gs::server::FrameRequest &request,
                               const gs::server::Download &download, const gs::server::Frame &frame,
                               double decode_ms)
{
    JsonObject entry;
    entry.SetNamedValue(L"request_id", JsonValue::CreateNumberValue(double(request.request_id)));
    entry.SetNamedValue(L"model", JsonValue::CreateStringValue(to_hstring(request.model_id)));
    entry.SetNamedValue(L"profile", JsonValue::CreateStringValue(
                                        to_hstring(gs::server::profile_name(frame.profile))));
    entry.SetNamedValue(L"network_ms", JsonValue::CreateNumberValue(download.milliseconds));
    entry.SetNamedValue(L"decode_and_bgra_ms", JsonValue::CreateNumberValue(decode_ms));
    entry.SetNamedValue(L"packet_bytes",
                        JsonValue::CreateNumberValue(double(download.packet.size())));
    entry.SetNamedValue(L"yaw", JsonValue::CreateNumberValue(request.yaw));
    entry.SetNamedValue(L"pitch", JsonValue::CreateNumberValue(request.pitch));
    entry.SetNamedValue(L"zoom", JsonValue::CreateNumberValue(request.zoom));
    entry.SetNamedValue(L"view_code",
                        JsonValue::CreateStringValue(to_hstring(gs::server::view_code(request))));
    entry.SetNamedValue(L"flip_y", JsonValue::CreateBooleanValue(request.flip_y));
    entry.SetNamedValue(L"cache", JsonValue::CreateStringValue(to_hstring(download.cache_state)));
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)))
    {
        entry.SetNamedValue(L"working_set_bytes",
                            JsonValue::CreateNumberValue(double(memory.WorkingSetSize)));
        entry.SetNamedValue(L"peak_working_set_bytes",
                            JsonValue::CreateNumberValue(double(memory.PeakWorkingSetSize)));
    }
    smoke_results_.Append(entry);
    switch (smoke_stage_++)
    {
    case 0:
        navigate(15, 5);
        zoom(1);
        break;
    case 1:
        quality_.SelectedIndex(0);
        break;
    case 2:
        quality_.SelectedIndex(1);
        break;
    case 3:
        quality_.SelectedIndex(2);
        break;
    case 4:
        quality_.SelectedIndex(3);
        break;
    case 5:
        if (model_ids_.size() > 1)
            models_box_.SelectedIndex(1);
        else
            reset_view();
        break;
    case 6:
        navigate(10, -10);
        smoke_timer_ = DispatcherTimer();
        smoke_timer_.Interval(std::chrono::milliseconds(1));
        smoke_timer_.Tick([this](auto &&, auto &&) {
            smoke_timer_.Stop();
            navigate(20, 5);
            zoom(-1);
        });
        smoke_timer_.Start();
        break;
    case 7:
        flip_y_.IsChecked(!flip_y_.IsChecked().GetBoolean());
        break;
    case 8:
        smoke_timer_ = DispatcherTimer();
        smoke_timer_.Interval(std::chrono::milliseconds(50));
        smoke_timer_.Tick([this](auto &&, auto &&) {
            auto next = gs::server::canonical_request(view_);
            next = gs::server::navigate_view(next, 2, 0);
            if (!prefetching_ && packet_cache_.contains(gs::server::view_key(next)))
            {
                smoke_timer_.Stop();
                navigate(2, 0);
            }
            else if (++smoke_wait_ > 600)
            {
                smoke_timer_.Stop();
                finish_smoke(false, "Prefetch did not deliver adjacent frame");
            }
        });
        smoke_timer_.Start();
        break;
    case 9:
        if (!local_hits_)
            finish_smoke(false, "Predicted navigation missed client cache");
        else
        {
            view_.pitch = 90;
            queue_render();
        }
        break;
    default:
        if (smoke_idle_seconds_ == 0)
            finish_smoke(true);
        else if (!smoke_idle_started_)
        {
            smoke_idle_started_ = true;
            smoke_idle_start_ = Clock::now();
            smoke_timer_ = DispatcherTimer();
            smoke_timer_.Interval(std::chrono::seconds(1));
            smoke_timer_.Tick([this](auto &&, auto &&) {
                if (Clock::now() - smoke_idle_start_ < std::chrono::seconds(smoke_idle_seconds_))
                    return;
                smoke_timer_.Stop();
                const bool healthy = heartbeat_successes_ >= 2 && !lease_warning_ &&
                                     (!smoke_require_recovery_ || session_recoveries_ > 0);
                finish_smoke(healthy, healthy ? "" : "Idle session did not remain healthy");
            });
            smoke_timer_.Start();
        }
        break;
    }
}
winrt::fire_and_forget MainWindow::finish_smoke(bool success, std::string error)
{
    auto lifetime = get_strong();
    try
    {
        JsonObject result;
        result.SetNamedValue(L"success", JsonValue::CreateBooleanValue(success));
        result.SetNamedValue(L"error", JsonValue::CreateStringValue(to_hstring(error)));
        result.SetNamedValue(L"stale_frames_dropped",
                             JsonValue::CreateNumberValue(double(dropped_)));
        result.SetNamedValue(L"frames", smoke_results_);
        result.SetNamedValue(L"local_cache_hits",
                             JsonValue::CreateNumberValue(double(local_hits_)));
        result.SetNamedValue(L"prefetched_frames",
                             JsonValue::CreateNumberValue(double(prefetched_)));
        result.SetNamedValue(L"packet_cache_bytes",
                             JsonValue::CreateNumberValue(double(packet_cache_.bytes())));
        result.SetNamedValue(L"heartbeat_successes",
                             JsonValue::CreateNumberValue(double(heartbeat_successes_)));
        result.SetNamedValue(L"session_recoveries",
                             JsonValue::CreateNumberValue(double(session_recoveries_)));
        result.SetNamedValue(L"preview_depth",
                             JsonValue::CreateNumberValue(
                                 gs::server::preview_depth(prefetch_depth_.SelectedIndex())));
        const auto folder = std::filesystem::path{smoke_output_};
        std::filesystem::create_directories(folder);
        const auto json = to_string(result.Stringify());
        std::ofstream report(folder / "winui-smoke.json");
        report << json;
        report.close();
        if (!report)
            throw std::runtime_error("Smoke report write failed");
        Microsoft::UI::Xaml::Media::Imaging::RenderTargetBitmap screenshot;
        co_await screenshot.RenderAsync(root_);
        const auto pixels = co_await screenshot.GetPixelsAsync();
        auto storage =
            co_await Windows::Storage::StorageFolder::GetFolderFromPathAsync(folder.wstring());
        auto file = co_await storage.CreateFileAsync(
            L"winui-viewer.png", Windows::Storage::CreationCollisionOption::ReplaceExisting);
        auto stream = co_await file.OpenAsync(Windows::Storage::FileAccessMode::ReadWrite);
        auto encoder = co_await Windows::Graphics::Imaging::BitmapEncoder::CreateAsync(
            Windows::Graphics::Imaging::BitmapEncoder::PngEncoderId(), stream);
        uint8_t *bytes = nullptr;
        check_hresult(pixels.as<::Windows::Storage::Streams::IBufferByteAccess>()->Buffer(&bytes));
        encoder.SetPixelData(Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                             Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied,
                             screenshot.PixelWidth(), screenshot.PixelHeight(), 96, 96,
                             array_view<const uint8_t>(bytes, bytes + pixels.Length()));
        co_await encoder.FlushAsync();
        cloud::smoke_exit_code = success ? 0 : 1;
    }
    catch (...)
    {
    }
    if (!closed_)
    {
        Close();
    }
}
} // namespace winrt::Native3DGSCloud::implementation

#include "MainWindow.g.cpp"
