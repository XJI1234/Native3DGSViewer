#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
        winrt::make<winrt::Native3DGSViewer::implementation::App>();
    });
    winrt::Native3DGSViewer::implementation::wait_for_viewer_shutdown();
    return 0;
}
