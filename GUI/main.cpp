#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "app_log.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const bool logging = viewer::log::initialize();
    if (!logging)
        MessageBoxW(nullptr, L"无法在安装目录创建日志，请检查写入权限。", L"3DGS Viewer",
                    MB_OK | MB_ICONWARNING);
    viewer::log::write("info", "app_start", {{"version", "0.1.1"}});
    if (logging) viewer::log::record_host_diagnostics();
    int exit_code = 0;
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            winrt::make<winrt::Native3DGSViewer::implementation::App>();
        });
        winrt::Native3DGSViewer::implementation::wait_for_viewer_shutdown();
    }
    catch (const winrt::hresult_error &e)
    {
        viewer::log::write("error", "app_exception",
                            {{"hr", viewer::log::hresult(e.code())}});
        exit_code = 1;
    }
    catch (const std::exception &e)
    {
        viewer::log::write("error", "app_exception", {{"diagnostic", e.what()}});
        exit_code = 1;
    }
    viewer::log::write("info", "app_exit", {{"code", std::to_string(exit_code)}});
    viewer::log::close();
    return exit_code;
}
