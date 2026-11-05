#pragma once
#include <windows.h>
#undef GetCurrentTime
#include "Crew.h"
#include "SaveSession.h"
#include "VehicleWorkspace.h"
#include "VehicleParameters.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Text.h>
#include <chrono>

namespace OffHangar::Ui
{
namespace Xaml = winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::Foundation::IAsyncOperation;
using winrt::Windows::Foundation::IInspectable;

struct Settings
{
    FileSystem::path DataFolder;
    FileSystem::path GameFolder;
    FileSystem::path OverrideFolder;
    FileSystem::path LastSave;
    FileSystem::path CacheFolder;
    bool Chinese = true;
    bool ProfileUi = false;
    int Theme = 0;
    uint32_t Accent = 0xff5e9eff;
    void Load();
    void Save() const;
};
class UiPerformance
{
    struct Entry
    {
        std::string Name, Detail;
        double Milliseconds{};
        uint64_t PrivateBytes{};
    };
    std::vector<Entry> Entries;
    std::vector<winrt::weak_ref<Controls::Control>> RetiredControls;

  public:
    bool Enabled{};
    class Scope
    {
        UiPerformance* Owner{};
        std::string Name, Detail;
        std::chrono::steady_clock::time_point Started;

      public:
        Scope(UiPerformance& Trace, std::string Name, std::string Detail);
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;
        ~Scope();
    };
    Scope Measure(std::string Name, std::string Detail = {});
    void WatchRetired(Controls::Control const& Control);
    void Flush(FileSystem::path const& Target) const noexcept;
};
winrt::hstring Translate(bool Chinese, char const* Key);
winrt::hstring HString(std::string const& Utf8);
std::string String(winrt::hstring const& Text);
Controls::TextBlock Label(winrt::hstring const& Text, double Size = 14);
Controls::StackPanel Stack(bool Horizontal = false, double Spacing = 8);
Controls::ScrollViewer Scroll(Xaml::UIElement const& Content);
Controls::Grid Rows(std::initializer_list<Xaml::GridLength> Heights);
Controls::Grid Columns(std::initializer_list<Xaml::GridLength> Widths);
Xaml::GridLength Auto();
Xaml::GridLength Star(double Weight = 1);
Xaml::GridLength Pixels(double Size);
Controls::ComboBox Combo(std::vector<winrt::hstring> const& Items, int Selected = 0);
Controls::ListViewItem ListRow(winrt::hstring const& Text);
void SetListRows(Controls::ListView const& List, std::vector<winrt::hstring> const& Rows);
void ArrangeGridColumns(Controls::Grid const& Grid, int Columns);
FileSystem::path PickFile(Xaml::Window const& Window, bool Save, wchar_t const* FilterName,
                          wchar_t const* Pattern, wchar_t const* DefaultName = L"", bool Folder = false);
HWND NativeWindow(Xaml::Window const& Window);
int64_t NonnegativeInteger(winrt::hstring const& Text);
FileSystem::path DefaultSavePath();
FileSystem::path SavePathInFolder(FileSystem::path const& Folder);
FileSystem::path ApplicationFolder();
} // namespace OffHangar::Ui
