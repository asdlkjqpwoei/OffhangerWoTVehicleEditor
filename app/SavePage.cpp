#include "StudioWindow.h"

namespace OffHangar::Ui
{
using namespace winrt;
using namespace Controls;
Grid StudioWindow::BuildSavePage()
{
    auto Timing = Performance.Measure("Save.BuildPage");
    auto Page = Rows({Auto(), Auto(), Auto(), Star()});
    Page.Margin({12, 4, 0, 0});
    auto Toolbar = Stack(true, 6);
    Toolbar.Children().Append(Button("Save.LocateActiveSave", [this] { LoadSave(true); }));
    Toolbar.Children().Append(Button("Save.Open", [this] { LoadSave(false); }));
    Toolbar.Children().Append(Button("Save.Save", [this] { WriteSave(false); }));
    Toolbar.Children().Append(Button("Save.SaveAs", [this] { WriteSave(true); }));
    Toolbar.Children().Append(Button("Save.CloseSave", [this] { CloseSave(); }));
    auto ToolbarScroll = Scroll(Toolbar);
    ToolbarScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    ToolbarScroll.HorizontalScrollMode(ScrollMode::Enabled);
    Page.Children().Append(ToolbarScroll);
    SavePath = Label(L"", 12);
    SavePath.IsTextSelectionEnabled(true);
    Grid::SetRow(SavePath, 1);
    Page.Children().Append(SavePath);
    auto Wallet = Stack(false, 6);
    auto WalletFields = Stack(true, 8);
    CurrencyInputs.clear();
    struct CurrencyField
    {
        char const* Key;
        char const* TextKey;
    };
    for (auto const& Field : {CurrencyField{"credits", "Save.Currency.credits"},
                              {"gold", "Save.Currency.gold"},
                              {"crystal", "Save.Currency.crystal"},
                              {"freeXP", "Save.Currency.freeXP"},
                              {"equipCoin", "Save.Currency.equipCoin"},
                              {"bpcoin", "Save.Currency.bpcoin"},
                              {"slots", "Save.Currency.slots"}})
    {
        TextBox Input;
        Input.Header(box_value(Tr(Field.TextKey)));
        Input.Width(135);
        CurrencyInputs.emplace(Field.Key, Input);
        WalletFields.Children().Append(Input);
    }
    auto WalletScroll = Scroll(WalletFields);
    WalletScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    WalletScroll.HorizontalScrollMode(ScrollMode::Enabled);
    Wallet.Children().Append(WalletScroll);
    Wallet.Children().Append(Button("Save.UpdateAccountValues",
                                    [this]
                                    {
                                        std::map<std::string, int64_t> Values;
                                        for (auto const& [Key, Input] : CurrencyInputs)
                                            if (Input.IsEnabled())
                                                Values[Key] = NonnegativeInteger(Input.Text());
                                        SaveData.Execute("Edit account values",
                                                         [&](SaveGame& Save)
                                                         {
                                                             for (auto const& [Key, Value] : Values)
                                                                 Save.SetCurrency(Key, Value);
                                                         });
                                        RefreshSave();
                                    }));
    Grid::SetRow(Wallet, 2);
    Page.Children().Append(Wallet);
    SaveTabs = Pivot();
    for (auto Key : {"Save.Garage", "Save.AddVehicles", "Save.UniqueAndThemedCrew", "Save.AllSaveFields"})
    {
        PivotItem Item;
        Item.Header(box_value(Tr(Key)));
        SaveTabs.Items().Append(Item);
    }
    SaveTabs.SelectedIndex(CurrentSaveTab);
    SaveTabs.SelectionChanged([this](auto&&, auto&&)
                              { Run([&] { ActivateSaveTab(SaveTabs.SelectedIndex()); }); });
    Grid::SetRow(SaveTabs, 3);
    Page.Children().Append(SaveTabs);
    RefreshSave();
    return Page;
}
void StudioWindow::ActivateSaveTab(int Index)
{
    if (!SaveTabs || Index < 0 || Index >= 4)
        return;
    CurrentSaveTab = Index;
    auto Timing = Performance.Measure("Save.ActivateTab", std::to_string(Index));
    auto Item = SaveTabs.Items().GetAt(Index).as<PivotItem>();
    if (!Item.Content())
    {
        if (Index == 0)
            Item.Content(BuildInventoryTab());
        else if (Index == 1)
            Item.Content(BuildAddTab());
        else if (Index == 2)
            Item.Content(BuildCrewTab());
        else
            Item.Content(BuildSaveFieldsTab());
    }
    if (!SaveTabDirty[Index])
        return;
    if (Index == 0)
        RefreshGarage();
    else if (Index == 1)
        RefreshAdd();
    else if (Index == 2)
        RefreshCrew();
    else
        RefreshSaveFields();
    SaveTabDirty[Index] = false;
}
Grid StudioWindow::BuildInventoryTab()
{
    auto Page = Rows({Auto(), Star(), Auto(), Auto()});
    auto Filters = Columns({Star(), Pixels(180)});
    GarageSearch = TextBox();
    GarageSearch.PlaceholderText(Tr("Save.FilterGarageVehicles"));
    Filters.Children().Append(GarageSearch);
    GarageSort = Combo({Tr("Save.InventoryId"), Tr("Save.VehicleName"), Tr("Save.TierDescending")});
    Grid::SetColumn(GarageSort, 1);
    Filters.Children().Append(GarageSort);
    Page.Children().Append(Filters);
    GarageList = ListView();
    GarageList.SelectionMode(ListViewSelectionMode::Extended);
    Grid::SetRow(GarageList, 1);
    Page.Children().Append(GarageList);
    auto Actions = Stack(true, 6);
    Actions.Children().Append(Button("Save.DeleteSelected", [this] { DeleteSelected(); }));
    Actions.Children().Append(Button("Save.CloneVehicleAndCrew",
                                     [this]
                                     {
                                         auto Ids = SelectedGarage();
                                         SaveData.Execute("Clone garage vehicles",
                                                          [&](SaveGame& Save)
                                                          {
                                                              for (auto Id : Ids)
                                                                  Save.CloneVehicle(Id, true);
                                                          });
                                         RefreshSave();
                                     }));
    XpValue = TextBox();
    XpValue.Width(140);
    XpValue.PlaceholderText(Tr("Save.VehicleXp"));
    Actions.Children().Append(XpValue);
    Actions.Children().Append(Button("Save.UpdateVehicleXp",
                                     [this]
                                     {
                                         auto Amount = NonnegativeInteger(XpValue.Text());
                                         auto Selection = SelectedIndices(GarageList);
                                         SaveData.Execute("Edit vehicle XP",
                                                          [&](SaveGame& Save)
                                                          {
                                                              for (auto Index : Selection)
                                                                  Save.SetVehicleXp(
                                                                      GarageRows.at(Index).TypeCd, Amount);
                                                          });
                                         RefreshSave();
                                     }));
    Grid::SetRow(Actions, 2);
    Page.Children().Append(Actions);
    auto CleanupActions = Stack(true, 6);
    CleanupActions.Children().Append(
        Button("Save.PreviewDeduplication", [this] { Cleanup(CleanupKind::Duplicates); }));
    CleanupActions.Children().Append(
        Button("Save.PreviewInvalidVehicles", [this] { Cleanup(CleanupKind::Invalid); }));
    CleanupActions.Children().Append(
        Button("Save.PreviewSpecialModeVehicles", [this] { Cleanup(CleanupKind::Nonstandard); }));
    CleanupActions.Children().Append(
        Button("Save.PreviewRentalDemo", [this] { Cleanup(CleanupKind::RentalDemo); }));
    Grid::SetRow(CleanupActions, 3);
    Page.Children().Append(CleanupActions);
    GarageSearch.TextChanged([this](auto&&, auto&&) { Run([&] { RefreshGarage(); }); });
    GarageSort.SelectionChanged([this](auto&&, auto&&) { Run([&] { RefreshGarage(); }); });
    GarageList.SelectionChanged(
        [this](auto&&, auto&&)
        {
            Run(
                [&]
                {
                    auto Index = GarageList.SelectedIndex();
                    if (Index >= 0 && size_t(Index) < GarageRows.size())
                        XpValue.Text(
                            HString(std::to_string(SaveData.Account.VehicleXp(GarageRows[Index].TypeCd))));
                });
        });
    return Page;
}
Grid StudioWindow::BuildAddTab()
{
    auto Page = Rows({Auto(), Auto(), Star(), Auto()});
    AddSearch = TextBox();
    AddSearch.PlaceholderText(Tr("Save.VehicleNameCodeOrTags"));
    Page.Children().Append(AddSearch);
    auto Filters = Columns({Star(), Star(), Star(), Star()});
    AddNation = Combo({Tr("Save.AllNations"), L"ussr", L"germany", L"usa", L"china", L"france", L"uk",
                       L"japan", L"czech", L"sweden", L"poland", L"italy"});
    AddTier =
        Combo({Tr("Save.AllTiers"), L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"10", L"11"});
    AddClass = Combo({Tr("Save.AllClasses"), L"lightTank", L"mediumTank", L"heavyTank", L"AT-SPG", L"SPG"});
    AddCategory = Combo({Tr("Save.AllVehicles"), Tr("Save.NotOwned"), Tr("Save.Owned"), Tr("Save.Hidden"),
                         Tr("Save.GarageAvailable"), Tr("Save.ExtensionsMods")});
    int Column = 0;
    for (auto Filter : {AddNation, AddTier, AddClass, AddCategory})
    {
        Grid::SetColumn(Filter, Column++);
        Filters.Children().Append(Filter);
    }
    Grid::SetRow(Filters, 1);
    Page.Children().Append(Filters);
    AddList = ListView();
    AddList.SelectionMode(ListViewSelectionMode::Multiple);
    Grid::SetRow(AddList, 2);
    Page.Children().Append(AddList);
    auto Actions = Stack(true, 12);
    AddWithCrew = CheckBox();
    AddWithCrew.Content(box_value(Tr("Save.IncludeCrew")));
    AddWithCrew.IsChecked(true);
    Actions.Children().Append(AddWithCrew);
    Actions.Children().Append(Button("Save.SelectFiltered", [this] { AddList.SelectAll(); }));
    Actions.Children().Append(Button("Save.AddSelectedVehicles", [this] { AddSelected(); }));
    Grid::SetRow(Actions, 3);
    Page.Children().Append(Actions);
    AddSearch.TextChanged([this](auto&&, auto&&) { Run([&] { RefreshAdd(); }); });
    for (auto Filter : {AddNation, AddTier, AddClass, AddCategory})
        Filter.SelectionChanged([this](auto&&, auto&&) { Run([&] { RefreshAdd(); }); });
    return Page;
}
Grid StudioWindow::BuildCrewTab()
{
    auto Page = Rows({Auto(), Star(), Auto(), Auto()});
    CrewSearch = TextBox();
    CrewSearch.PlaceholderText(Tr("Save.FilterByNationThemeOrTags"));
    Page.Children().Append(CrewSearch);
    CrewList = ListView();
    CrewList.SelectionMode(ListViewSelectionMode::Multiple);
    Grid::SetRow(CrewList, 1);
    Page.Children().Append(CrewList);
    auto Actions = Stack(true, 8);
    Actions.Children().Append(Button("Save.AssignToSelectedVehiclesGarage", [this] { AssignCrew(); }));
    Actions.Children().Append(Button("Save.RestoreVehicleSpecificCrew", [this] { RestoreUniqueCrew(); }));
    RecruitCount = TextBox();
    RecruitCount.Width(180);
    RecruitCount.Header(box_value(Tr("Save.RecruitsPerGroup0All")));
    RecruitCount.Text(L"0");
    Actions.Children().Append(RecruitCount);
    Actions.Children().Append(Button("Save.RecruitSelectedGroups", [this] { RecruitThemes(false); }));
    Actions.Children().Append(Button("Save.RecruitAllThemes", [this] { RecruitThemes(true); }));
    auto ActionsScroll = Scroll(Actions);
    ActionsScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
    ActionsScroll.HorizontalScrollMode(ScrollMode::Enabled);
    Grid::SetRow(ActionsScroll, 2);
    Page.Children().Append(ActionsScroll);
    auto Help = Label(Tr("Save.CombineMultipleThemesWithNoVehicleSelectionAssignment"), 12);
    Grid::SetRow(Help, 3);
    Page.Children().Append(Help);
    CrewSearch.TextChanged([this](auto&&, auto&&) { Run([&] { RefreshCrew(); }); });
    return Page;
}
Grid StudioWindow::BuildSaveFieldsTab()
{
    auto Page = Rows({Auto(), Star(), Auto()});
    SaveFieldSearch = TextBox();
    SaveFieldSearch.PlaceholderText(Tr("Save.SearchAllSaveFieldPaths"));
    Page.Children().Append(SaveFieldSearch);
    SaveFieldList = ListView();
    Grid::SetRow(SaveFieldList, 1);
    Page.Children().Append(SaveFieldList);
    auto Actions = Columns({Star(), Auto()});
    SaveFieldValue = TextBox();
    Actions.Children().Append(SaveFieldValue);
    auto Update = Button("Save.UpdateField",
                         [this]
                         {
                             auto Index = SaveFieldList.SelectedIndex();
                             if (Index < 0)
                                 return;
                             auto Field = SaveFields.at(Index);
                             auto Input = String(SaveFieldValue.Text());
                             SaveData.Execute("Edit save field", [&](SaveGame& Save)
                                              { Save.EditProperty(Field.Address, Input); });
                             RefreshSave();
                         });
    Grid::SetColumn(Update, 1);
    Actions.Children().Append(Update);
    Grid::SetRow(Actions, 2);
    Page.Children().Append(Actions);
    SaveFieldSearch.TextChanged([this](auto&&, auto&&) { Run([&] { RefreshSaveFields(); }); });
    SaveFieldList.SelectionChanged(
        [this](auto&&, auto&&)
        {
            Run(
                [&]
                {
                    auto Index = SaveFieldList.SelectedIndex();
                    if (Index < 0 || size_t(Index) >= SaveFields.size())
                        return;
                    auto Value = SaveFields[Index].CurrentValue;
                    SaveFieldValue.Text(HString(Value->Display()));
                    SaveFieldValue.IsReadOnly(!Value->ScalarEditable());
                });
        });
    return Page;
}
void StudioWindow::RefreshSave()
{
    if (!SavePath)
        return;
    auto const& Save = SaveData.Account;
    SavePath.Text(Save.Root ? HString(PathUtf8(Save.Path)) + (Save.Dirty ? L" *" : L"")
                            : Tr("Save.NoSaveOpen"));
    for (auto const& [Key, Input] : CurrencyInputs)
    {
        Input.Text(HString(std::to_string(Save.Currency(Key))));
        Input.IsEnabled(bool(Save.Root) && (!Save.Json922 || (Key != "equipCoin" && Key != "bpcoin")));
    }
    // Hidden lists refresh later; their old selections must never target a new account or batch.
    for (auto const& List : {GarageList, AddList, CrewList, SaveFieldList})
        if (List)
        {
            if (List.SelectionMode() == ListViewSelectionMode::Single)
                List.SelectedIndex(-1);
            else
                List.SelectedItems().Clear();
        }
    SaveTabDirty.fill(true);
    ActivateSaveTab(CurrentSaveTab);
}
void StudioWindow::RefreshGarage()
{
    if (!GarageList)
        return;
    GarageRows.clear();
    std::vector<hstring> Labels;
    auto Search = Lower(String(GarageSearch.Text()));
    for (auto const& Owned : SaveData.Account.Inventory(GameCatalog))
        if (Search.empty() ||
            Lower(Owned.Name + " " + Owned.Display + " " + std::to_string(Owned.TypeCd) + " " +
                  std::to_string(Owned.Tier) + " " + std::to_string(Owned.InventoryId))
                    .find(Search) != std::string::npos)
            GarageRows.push_back(Owned);
    auto Order = GarageSort.SelectedIndex();
    if (Order == 1)
        std::stable_sort(GarageRows.begin(), GarageRows.end(),
                         [](auto const& Left, auto const& Right) { return Left.Display < Right.Display; });
    if (Order == 2)
        std::stable_sort(GarageRows.begin(), GarageRows.end(),
                         [](auto const& Left, auto const& Right) { return Left.Tier > Right.Tier; });
    for (auto const& Owned : GarageRows)
        Labels.push_back(HString(std::to_string(Owned.InventoryId) + "  ·  " +
                                 (Owned.Known ? Owned.Display : String(Tr("Save.UnknownVehicle"))) + "  ·  " +
                                 std::to_string(Owned.Tier) + "\n" + Owned.Name));
    SetListRows(GarageList, Labels);
}
void StudioWindow::RefreshAdd()
{
    auto Timing = Performance.Measure("Save.RefreshAdd");
    if (!AddList)
        return;
    AddRows.clear();
    std::vector<hstring> Labels;
    std::set<uint32_t> Owned;
    for (auto const& Item : SaveData.Account.Inventory(GameCatalog))
        Owned.insert(Item.TypeCd);
    auto Category = AddCategory.SelectedIndex();
    auto Nation = AddNation.SelectedIndex(), Tier = AddTier.SelectedIndex(), Class = AddClass.SelectedIndex();
    auto ClassName = Class > 0 ? Lower(String(unbox_value<hstring>(AddClass.SelectedItem()))) : "";
    for (auto Index : GameCatalog.Search(String(AddSearch.Text())))
    {
        auto const& Vehicle = GameCatalog.Vehicles[Index];
        if (Nation > 0 && Vehicle.NationId != Nation - 1)
            continue;
        if (Tier > 0 && Vehicle.Tier != Tier)
            continue;
        if (Class > 0 && Lower(Vehicle.Tags).find(ClassName) == std::string::npos)
            continue;
        if (Category == 1 && Owned.contains(Vehicle.TypeCd))
            continue;
        if (Category == 2 && !Owned.contains(Vehicle.TypeCd))
            continue;
        if (Category == 3 && !Vehicle.Hidden)
            continue;
        if (Category == 4 && (!Vehicle.Enabled || Vehicle.Conflict || !Vehicle.TypeCd))
            continue;
        if (Category == 5 && !Vehicle.Extension)
            continue;
        AddRows.push_back(Index);
        Labels.push_back(HString(Vehicle.DisplayName + "  ·  " + std::to_string(Vehicle.Tier) +
                                 (Owned.contains(Vehicle.TypeCd) ? "  ✓" : "") + "\n" + Vehicle.Name));
    }
    SetListRows(AddList, Labels);
}
void StudioWindow::RefreshCrew()
{
    if (!CrewList)
        return;
    CrewRows.clear();
    std::vector<hstring> Labels;
    auto Search = Lower(String(CrewSearch.Text()));
    for (size_t Index = 0; Index < CrewNames.Groups.size(); ++Index)
    {
        auto const& Group = CrewNames.Groups[Index];
        if (!Group.Special)
            continue;
        if (!Search.empty() &&
            Lower(Group.Nation + " " + Group.Label + " " + Group.Key + " " + Group.Tags).find(Search) ==
                std::string::npos)
            continue;
        CrewRows.push_back(Index);
        Labels.push_back(HString(Group.Label + "  ·  " + Group.Nation + "\n" + Group.Key + "  ·  " +
                                 std::to_string(Group.Members.size())));
    }
    SetListRows(CrewList, Labels);
}
void StudioWindow::RefreshSaveFields()
{
    auto Timing = Performance.Measure("Save.RefreshFields");
    if (!SaveFieldList)
        return;
    SaveFields = SaveData.Account.Properties(String(SaveFieldSearch.Text()));
    std::vector<hstring> Labels;
    Labels.reserve(SaveFields.size());
    for (auto const& Field : SaveFields)
        Labels.push_back(HString(Field.Path + "   =   " + Field.CurrentValue->Display()));
    SetListRows(SaveFieldList, Labels);
}
std::set<int64_t> StudioWindow::SelectedGarage() const
{
    std::set<int64_t> Result;
    for (auto Index : SelectedIndices(GarageList))
        Result.insert(GarageRows.at(Index).InventoryId);
    return Result;
}
fire_and_forget StudioWindow::LoadSave(bool Locate)
{
    auto Lifetime = shared_from_this();
    try
    {
        if (SaveData.Account.Dirty &&
            !(co_await Confirm(Tr("Save.UnsavedChanges"),
                               Tr("Save.OpeningAFileDiscardsUnsavedChangesContinue"))))
            co_return;
        auto Path = Locate ? DefaultSavePath() : PickFile(Window, false, L"Save folder", L"*", L"", true);
        if (Path.empty())
            co_return;
        if (!Locate)
            Path = SavePathInFolder(Path);
        SaveData.Open(Path);
        Preferences.LastSave = Path;
        if (!GameRunning())
            Preferences.Save();
        RefreshSave();
        SetStatus(Tr("Save.SaveLoaded"));
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
fire_and_forget StudioWindow::CloseSave()
{
    auto Lifetime = shared_from_this();
    try
    {
        if (SaveData.Account.Dirty &&
            !(co_await Confirm(Tr("Save.CloseSave"), Tr("Save.DiscardUnsavedAccountChangesAndClose"))))
            co_return;
        SaveData.Close();
        RefreshSave();
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
void StudioWindow::WriteSave(bool SaveAs)
{
    if (!SaveData.Account.Root)
        throw std::runtime_error("Open an account save first");
    auto Target = SaveAs ? PickFile(Window, true, L"OffHangar account", L"*.dat;*.json",
                                    SaveData.Account.Json922 ? L"garage_state.json" : L"account.dat")
                         : SaveData.Account.Path;
    if (Target.empty())
        return;
    auto Backup = SaveData.Account.Save(Target);
    RefreshSave();
    SetStatus(Tr("Save.SaveWritten") + (Backup.empty() ? L"" : L" · " + HString(PathUtf8(Backup))));
    Log("Saved account to " + PathUtf8(Target));
}
fire_and_forget StudioWindow::DeleteSelected()
{
    auto Lifetime = shared_from_this();
    try
    {
        auto Ids = SelectedGarage();
        if (Ids.empty())
            co_return;
        if (!(co_await Confirm(Tr("Save.DeleteVehicles"),
                               HString(std::to_string(Ids.size())) +
                                   Tr("Save.VehiclesWillLeaveTheGarageCrewMoveTo"))))
            co_return;
        SaveData.Execute("Delete garage vehicles",
                         [&](SaveGame& Save)
                         {
                             for (auto Id : Ids)
                                 Save.RemoveVehicle(Id);
                         });
        RefreshSave();
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
fire_and_forget StudioWindow::Cleanup(CleanupKind Kind)
{
    auto Lifetime = shared_from_this();
    try
    {
        auto Plan = SaveData.PreviewCleanup(GameCatalog, Kind);
        if (Plan.empty())
        {
            Message(Tr("Save.CleanupPreview"), Tr("Save.NoMatchingVehicles"));
            co_return;
        }
        std::string Preview;
        for (auto const& Item : Plan)
            Preview +=
                std::to_string(Item.InventoryId) + " · " + Item.VehicleName + " · " + Item.Reason + "\n";
        if (!(co_await Confirm(Tr("Save.CleanupPreviewDeleteTheseVehicles"), HString(Preview))))
            co_return;
        SaveData.Execute("Clean garage",
                         [&](SaveGame& Save)
                         {
                             for (auto const& Item : Plan)
                                 Save.RemoveVehicle(Item.InventoryId);
                         });
        RefreshSave();
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
fire_and_forget StudioWindow::AddSelected()
{
    auto Lifetime = shared_from_this();
    try
    {
        auto Rows = SelectedIndices(AddList);
        std::vector<size_t> Vehicles;
        std::set<uint32_t> Owned;
        for (auto const& Item : SaveData.Account.Inventory(GameCatalog))
            Owned.insert(Item.TypeCd);
        std::string Preview;
        size_t Skipped = 0;
        for (auto Row : Rows)
        {
            auto Index = AddRows.at(Row);
            auto const& Vehicle = GameCatalog.Vehicles[Index];
            if (Owned.contains(Vehicle.TypeCd) || !Vehicle.Enabled || Vehicle.Conflict || !Vehicle.TypeCd ||
                (Vehicle.DefinitionPath.empty() && Vehicle.StockDescriptor.empty()))
            {
                ++Skipped;
                continue;
            }
            Vehicles.push_back(Index);
            Owned.insert(Vehicle.TypeCd);
            Preview += Vehicle.DisplayName + " · " + Vehicle.Name + "\n";
        }
        if (Skipped)
            Preview += String(Tr("Save.SkippedOwnedUnregisteredOrConflictingVehicles")) +
                       std::to_string(Skipped) + "\n";
        if (Vehicles.empty())
        {
            Message(Tr("Save.AddVehiclesPreview"), HString(Preview));
            co_return;
        }
        bool WithCrew = AddWithCrew.IsChecked().Value();
        if (!(co_await Confirm(Tr("Save.AddVehiclesPreview"), HString(Preview))))
            co_return;
        Working = true;
        CancelWork = false;
        Busy.IsActive(true);
        Navigation.IsEnabled(false);
        apartment_context UiThread;
        auto Queue = Root.DispatcherQueue();
        std::exception_ptr Failure;
        co_await resume_background();
        try
        {
            SaveData.Execute(
                "Add vehicles",
                [&](SaveGame& Save)
                {
                    size_t Completed = 0;
                    for (auto Index : Vehicles)
                    {
                        if (CancelWork)
                            throw std::runtime_error("Cancelled");
                        Save.AddVehicle(GameCatalog.Vehicles[Index], GameCatalog, WithCrew);
                        auto Progress = std::to_string(++Completed) + " / " + std::to_string(Vehicles.size());
                        Queue.TryEnqueue([Lifetime, Progress] { Lifetime->SetStatus(HString(Progress)); });
                    }
                    if (CancelWork)
                        throw std::runtime_error("Cancelled");
                });
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
        RefreshSave();
        SetStatus(Tr("Save.VehiclesAddedToTheOpenAccountNotSaved"));
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
fire_and_forget StudioWindow::AssignCrew()
{
    auto Lifetime = shared_from_this();
    try
    {
        std::vector<size_t> Groups;
        for (auto Index : SelectedIndices(CrewList))
            Groups.push_back(CrewRows.at(Index));
        auto Vehicles = SelectedGarage();
        auto Count = AssignCrewGroup(SaveData.Account, CrewNames, Groups, Vehicles, true);
        if (!(co_await Confirm(Tr("Save.ThemedCrewAssignmentPreview"),
                               HString(std::to_string(Count)) +
                                   Tr("Save.CrewIdentitiesWillChangeExistingSkillsAndXp"))))
            co_return;
        SaveData.Execute("Assign themed crew",
                         [&](SaveGame& Save) { AssignCrewGroup(Save, CrewNames, Groups, Vehicles); });
        RefreshSave();
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
fire_and_forget StudioWindow::RecruitThemes(bool All)
{
    auto Lifetime = shared_from_this();
    try
    {
        std::vector<size_t> Groups;
        if (All)
        {
            for (size_t Index = 0; Index < CrewNames.Groups.size(); ++Index)
                if (CrewNames.Groups[Index].Special)
                    Groups.push_back(Index);
        }
        else
            for (auto Row : SelectedIndices(CrewList))
                Groups.push_back(CrewRows.at(Row));
        if (Groups.empty())
            throw std::runtime_error("Select one or more crew themes to recruit");
        auto Count = size_t(NonnegativeInteger(RecruitCount.Text()));
        if (Count > 1000)
            throw std::runtime_error("Recruit at most 1000 members per group");
        if (!(co_await Confirm(Tr("Save.RecruitThemedCrew"),
                               HString(std::to_string(Groups.size())) +
                                   Tr("Save.GroupsWillBeRecruitedToTheBarracksExisting"))))
            co_return;
        size_t Added = 0;
        SaveData.Execute("Recruit themed crew",
                         [&](SaveGame& Save)
                         {
                             for (auto Index : Groups)
                             {
                                 auto const& Group = CrewNames.Groups[Index];
                                 Added += RecruitCrewGroup(Save, Group, Count ? Count : Group.Members.size());
                             }
                         });
        RefreshSave();
        SetStatus(HString(std::to_string(Added)) + Tr("Save.NewCrewAddedToTheBarracksNotSaved"));
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
fire_and_forget StudioWindow::RestoreUniqueCrew()
{
    auto Lifetime = shared_from_this();
    try
    {
        if (!SaveData.Account.Root)
            throw std::runtime_error("Open an account save first");
        auto Garage = SaveData.Account.Inventory(GameCatalog);
        auto Selected = SelectedGarage();
        std::vector<std::pair<size_t, size_t>> Choices;
        ListView Vehicles;
        Vehicles.SelectionMode(ListViewSelectionMode::Multiple);
        Vehicles.MaxHeight(420);
        for (size_t GroupIndex = 0; GroupIndex < CrewNames.Groups.size(); ++GroupIndex)
            for (auto const& Name : CrewNames.Groups[GroupIndex].BoundVehicles)
                if (auto Vehicle = GameCatalog.ByName(Name))
                {
                    size_t Index = size_t(Vehicle - GameCatalog.Vehicles.data());
                    bool Owned = false, IsSelected = false;
                    for (auto const& Item : Garage)
                        if (Item.TypeCd == Vehicle->TypeCd)
                        {
                            Owned = true;
                            IsSelected |= Selected.contains(Item.InventoryId);
                        }
                    Choices.emplace_back(Index, GroupIndex);
                    auto Caption =
                        HString(Vehicle->DisplayName + " · " + CrewNames.Groups[GroupIndex].Label) +
                        (Owned ? L"" : Tr("Save.VehicleWillBeAddedFirst"));
                    auto Item = ListRow(Caption);
                    Vehicles.Items().Append(Item);
                    if (Selected.empty() ? Owned : IsSelected)
                        Vehicles.SelectedItems().Append(Item);
                }
        if (Choices.empty())
            throw std::runtime_error("No vehicle-specific crew groups are defined by this client");
        ContentDialog Dialog;
        Dialog.XamlRoot(Root.XamlRoot());
        Dialog.Title(box_value(Tr("Save.RestoreVehicleSpecificCrew")));
        auto Content = Stack();
        Content.Children().Append(Label(Tr("Save.SelectVehiclesExistingCrewRetainSkillsAndXp"), 12));
        Content.Children().Append(Vehicles);
        Dialog.Content(Content);
        Dialog.PrimaryButtonText(Tr("Save.ApplyToOpenAccount"));
        Dialog.CloseButtonText(Tr("Save.Cancel"));
        if (co_await Dialog.ShowAsync() != ContentDialogResult::Primary)
            co_return;
        auto Rows = SelectedIndices(Vehicles);
        SaveData.Execute("Restore vehicle-specific crew",
                         [&](SaveGame& Save)
                         {
                             for (auto Row : Rows)
                             {
                                 auto [VehicleIndex, GroupIndex] = Choices.at(Row);
                                 auto const& Vehicle = GameCatalog.Vehicles[VehicleIndex];
                                 std::set<int64_t> Owners;
                                 for (auto const& Item : Save.Inventory(GameCatalog))
                                     if (Item.TypeCd == Vehicle.TypeCd)
                                         Owners.insert(Item.InventoryId);
                                 if (Owners.empty())
                                     Owners.insert(Save.AddVehicle(Vehicle, GameCatalog, true));
                                 AssignCrewGroup(Save, CrewNames, {GroupIndex}, Owners);
                             }
                         });
        RefreshSave();
    }
    catch (...)
    {
        Run([] { throw; });
    }
}
} // namespace OffHangar::Ui
