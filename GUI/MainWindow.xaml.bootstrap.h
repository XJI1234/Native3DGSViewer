#pragma once

// Allows the first native XAML compile to finish before WinUI generates its implementation header.
namespace winrt::Native3DGSViewer::implementation
{
template <typename D, typename... I>
void MainWindowT<D, I...>::InitializeComponent()
{
    if (_contentLoaded) return;
    _contentLoaded = true;
    Microsoft::UI::Xaml::Application::LoadComponent(
        *this, Windows::Foundation::Uri{L"ms-appx:///MainWindow.xaml"});
}

template <typename D, typename... I>
void MainWindowT<D, I...>::Connect(int32_t, IInspectable const&)
{
}

template <typename D, typename... I>
Microsoft::UI::Xaml::Markup::IComponentConnector
MainWindowT<D, I...>::GetBindingConnector(int32_t, IInspectable const&)
{
    return nullptr;
}

template <typename D, typename... I>
void MainWindowT<D, I...>::UnloadObject(Microsoft::UI::Xaml::DependencyObject const&)
{
}

template <typename D, typename... I>
void MainWindowT<D, I...>::DisconnectUnloadedObject(int32_t)
{
}
}
