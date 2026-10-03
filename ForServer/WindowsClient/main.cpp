#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include <psapi.h>
#include <shellapi.h>

namespace cloud
{
std::wstring smoke_config;
int smoke_exit_code = 1;
gs::server::Endpoint shutdown_endpoint;
} // namespace cloud
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        int count = 0;
        auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (arguments)
        {
            if (count == 3 && std::wstring(arguments[1]) == L"--smoke-test")
                cloud::smoke_config = arguments[2];
            LocalFree(arguments);
        }
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto &&) { winrt::make<winrt::Native3DGSCloud::implementation::App>(); });
        if (!cloud::shutdown_endpoint.session.empty())
        {
            try
            {
                gs::server::request_http(cloud::shutdown_endpoint, "/sessions/close",
                                         cloud::shutdown_endpoint.session);
            }
            catch (...)
            {
            }
        }
        return cloud::smoke_config.empty() ? 0 : cloud::smoke_exit_code;
    }
    catch (const std::exception &error)
    {
        if (cloud::smoke_config.empty())
            MessageBoxA(nullptr, error.what(), "3DGS Cloud Viewer", MB_OK | MB_ICONERROR);
    }
    catch (const winrt::hresult_error &error)
    {
        if (cloud::smoke_config.empty())
            MessageBoxW(nullptr, error.message().c_str(), L"3DGS Cloud Viewer",
                        MB_OK | MB_ICONERROR);
    }
    return 1;
}
