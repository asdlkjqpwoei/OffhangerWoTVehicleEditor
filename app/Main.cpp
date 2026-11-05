#include "StudioWindow.h"
#include <shellapi.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

// The CMake dependency scanner can run NuGet's common initializer target twice.
// Use the SDK's generated registration helper explicitly, exactly once.
namespace Microsoft::Windows::Foundation::UndockedRegFreeWinRT::AutoInitialize
{
void Initialize();
}

namespace OffHangar::Ui
{
struct StudioApp : Xaml::ApplicationT<StudioApp, Xaml::Markup::IXamlMetadataProvider>
{
    std::shared_ptr<StudioWindow> MainWindow;
    Settings Preferences;
    Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider Metadata;
    explicit StudioApp(Settings Input) : Preferences(std::move(Input)) {}
    // Programmatic WinUI applications must supply the type provider that the
    // XAML compiler normally generates for an App.xaml project.
    Xaml::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& Type)
    {
        return Metadata.GetXamlType(Type);
    }
    Xaml::Markup::IXamlType GetXamlType(winrt::hstring const& Name)
    {
        return Metadata.GetXamlType(Name);
    }
    winrt::com_array<Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions()
    {
        return Metadata.GetXmlnsDefinitions();
    }
    void OnLaunched(Xaml::LaunchActivatedEventArgs const&)
    {
        try
        {
            Resources().MergedDictionaries().Append(Controls::XamlControlsResources());
            MainWindow = std::make_shared<StudioWindow>(Preferences);
            MainWindow->Initialize();
        }
        catch (winrt::hresult_error const& Error)
        {
            auto Message = L"Startup failed (" + std::to_wstring(uint32_t(Error.code().value)) + L"):\n" +
                           std::wstring(Error.message());
            MessageBoxW(nullptr, Message.c_str(), L"OffHanger WoT Offline Vehicle Editor", MB_ICONERROR);
            Exit();
        }
        catch (std::exception const& Error)
        {
            MessageBoxW(nullptr, Wide(Error.what()).c_str(), L"OffHanger WoT Offline Vehicle Editor",
                        MB_ICONERROR);
            Exit();
        }
    }
};
} // namespace OffHangar::Ui

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        Microsoft::Windows::Foundation::UndockedRegFreeWinRT::AutoInitialize::Initialize();
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        OffHangar::Ui::Settings Preferences;
        Preferences.Load();
        OffHangar::Ui::Xaml::Application::Start([Preferences](auto&&)
                                                { winrt::make<OffHangar::Ui::StudioApp>(Preferences); });
        return 0;
    }
    catch (winrt::hresult_error const& Error)
    {
        MessageBoxW(nullptr, Error.message().c_str(), L"OffHanger WoT Offline Vehicle Editor", MB_ICONERROR);
        return 1;
    }
    catch (std::exception const& Error)
    {
        MessageBoxW(nullptr, OffHangar::Wide(Error.what()).c_str(), L"OffHanger WoT Offline Vehicle Editor",
                    MB_ICONERROR);
        return 1;
    }
}
