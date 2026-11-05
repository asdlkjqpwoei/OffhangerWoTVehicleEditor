#include "StudioWindow.h"
namespace OffHangar::Ui
{
using namespace winrt;
using namespace Controls;
Grid StudioWindow::BuildBaselinePage()
{
    auto Page = Rows({Star(), Auto(), Star()});
    auto Content = Stack(false, 18);
    Content.MaxWidth(760);
    Content.Margin({30, 20, 30, 20});
    Content.HorizontalAlignment(Xaml::HorizontalAlignment::Center);
    Content.Children().Append(Label(Tr("Baseline.Title"), 28));
    Content.Children().Append(Label(Tr("Baseline.Description"), 15));
    auto Choices = Columns({Star(), Star()});
    auto File = Button("Baseline.File", [this] { ImportBaseline(false); });
    auto Game = Button("Baseline.Game", [this] { ImportBaseline(true); });
    for (auto Item : {File, Game})
    {
        Item.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
        Item.MinHeight(46);
    }
    Choices.Children().Append(File);
    Grid::SetColumn(Game, 1);
    Choices.Children().Append(Game);
    Content.Children().Append(Choices);
    Content.Children().Append(
        Label(Tr("Baseline.Location") + HString(PathUtf8(ApplicationFolder() / L"script.pkg")), 12));
    if (!BaselineError.empty())
        Content.Children().Append(Label(HString(BaselineError), 12));
    Grid::SetRow(Content, 1);
    Page.Children().Append(Content);
    return Page;
}
fire_and_forget StudioWindow::ImportBaseline(bool FromGame)
{
    auto Lifetime = shared_from_this();
    try
    {
        EnsureWritesAllowed();
        auto Selected =
            PickFile(Window, false, L"Original client scripts", FromGame ? L"*" : L"*.pkg", L"", FromGame);
        if (Selected.empty())
            co_return;
        auto Source = Selected;
        if (FromGame)
        {
            Source = Selected / L"res/packages/scripts.pkg";
            if (!FileSystem::is_regular_file(Source))
                Source = Selected / L"res/packages/script.pkg";
        }
        if (!FileSystem::is_regular_file(Source))
            throw std::runtime_error(String(Tr("Baseline.NotFound")));
        Working = true;
        Busy.IsActive(true);
        Navigation.IsEnabled(false);
        SetStatus(Tr("Baseline.Importing"));
        apartment_context UiThread;
        auto Target = ApplicationFolder() / L"script.pkg";
        std::exception_ptr Failure;
        std::shared_ptr<DefaultResources const> Installed;
        co_await resume_background();
        try
        {
            DefaultResources::Install(Source, Target);
            Installed = std::make_shared<DefaultResources>(Target);
        }
        catch (...)
        {
            Failure = std::current_exception();
        }
        co_await UiThread;
        Working = false;
        Busy.IsActive(false);
        Navigation.IsEnabled(true);
        if (Failure)
            std::rethrow_exception(Failure);
        NativeDefaults = std::move(Installed);
        BaselineError.clear();
        if (FromGame)
            Preferences.GameFolder = Selected;
        if (!GameRunning())
            Preferences.Save();
        CurrentPage = 0;
        BuildShell();
        SetStatus(Tr("Baseline.Ready"));
        if (CloseRequested)
            RequestClose();
    }
    catch (...)
    {
        Working = false;
        if (Busy)
            Busy.IsActive(false);
        if (Navigation)
            Navigation.IsEnabled(true);
        if (CloseRequested)
            RequestClose();
        else
            Run([] { throw; });
    }
}
} // namespace OffHangar::Ui
