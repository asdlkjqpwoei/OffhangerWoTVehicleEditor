#include "EditorWindow.h"
#include "VehicleFactory.h"

namespace OffHanger::Ui
{
using namespace winrt;
using namespace Controls;
Grid EditorWindow::BuildSavePage()
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
	for (auto Key : {"Save.Garage", "Save.AddVehicles", "Barracks.Title", "Save.AllSaveFields"})
	{
		PivotItem Item;
		Item.Header(box_value(Tr(Key)));
		SaveTabs.Items().Append(Item);
	}
	SaveTabs.SelectedIndex(CurrentSaveTab);
	SaveTabs.SelectionChanged(
		[this](auto&&, auto&&)
		{
			if (SaveTabs.SelectedIndex() != CurrentSaveTab)
				Run([&] { ActivateSaveTab(SaveTabs.SelectedIndex()); });
		});
	Grid::SetRow(SaveTabs, 3);
	Page.Children().Append(SaveTabs);
	RefreshSave();
	return Page;
}
void EditorWindow::ActivateSaveTab(int Index)
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
Grid EditorWindow::BuildInventoryTab()
{
	auto Page = Rows({Auto(), Star(), Auto(), Auto()});
	auto Filters =
		BuildVehicleFilters(Preferences.Chinese, GarageFilters, [this] { Run([&] { RefreshGarage(); }); });
	Page.Children().Append(Filters);
	GarageList = ListView();
	GarageList.SelectionMode(ListViewSelectionMode::Extended);
	Grid::SetRow(GarageList, 1);
	Page.Children().Append(GarageList);
	auto Actions = Stack(true, 6);
	Actions.Children().Append(Button("Save.DeleteSelected", [this] { DeleteSelected(); }));
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
		Button("Save.DeleteUnavailable", [this] { Cleanup(CleanupKind::Unavailable); }));
	CleanupActions.Children().Append(Button("Save.DeleteInvalid", [this] { Cleanup(CleanupKind::Invalid); }));
	Grid::SetRow(CleanupActions, 3);
	Page.Children().Append(CleanupActions);
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
Grid EditorWindow::BuildAddTab()
{
	auto Page = Rows({Auto(), Star(), Auto()});
	auto Filters =
		BuildVehicleFilters(Preferences.Chinese, AddFilters, [this] { Run([&] { RefreshAdd(); }); });
	Page.Children().Append(Filters);
	AddList = ListView();
	AddList.SelectionMode(ListViewSelectionMode::Multiple);
	Grid::SetRow(AddList, 1);
	Page.Children().Append(AddList);
	auto Actions = Stack(true, 12);
	AddWithCrew = CheckBox();
	AddWithCrew.Content(box_value(Tr("Save.IncludeCrew")));
	AddWithCrew.IsChecked(true);
	Actions.Children().Append(AddWithCrew);
	Actions.Children().Append(Button("Save.SelectFiltered", [this] { AddList.SelectAll(); }));
	Actions.Children().Append(Button("Save.AddSelectedVehicles", [this] { AddSelected(); }));
	Grid::SetRow(Actions, 2);
	Page.Children().Append(Actions);
	return Page;
}
Grid EditorWindow::BuildSaveFieldsTab()
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
void EditorWindow::RefreshSave()
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
	for (auto const& List : {GarageList, AddList, OwnedCrewList, RecruitList, SaveFieldList})
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
void EditorWindow::EnsureGarageInventory()
{
	if (InventoryRevision == SaveData.Revision())
		return;
	GarageInventory = SaveData.Account.Inventory(GameCatalog);
	InventoryRevision = SaveData.Revision();
}
void EditorWindow::RefreshGarage()
{
	auto Timing = Performance.Measure("Save.RefreshGarage");
	if (!GarageList)
		return;
	EnsureVehicleListings();
	EnsureGarageInventory();
	auto Filter = ReadVehicleFilter(GarageFilters);
	auto Query = Filter.Search;
	Filter.Search.clear();
	auto Ordered = VehicleListings.Select(Filter);
	std::unordered_map<uint32_t, size_t> Ranks;
	for (size_t Rank = 0; Rank < Ordered.size(); ++Rank)
		Ranks.try_emplace(GameCatalog.Vehicles[Ordered[Rank]].TypeCd, Rank);
	GarageRows.clear();
	for (auto const& Owned : GarageInventory)
	{
		auto Vehicle = GameCatalog.ByCd(Owned.TypeCd);
		if (Vehicle ? !Ranks.contains(Owned.TypeCd)
					: (Filter.Kind != "unavailable" || Filter.Nation >= 0 || Filter.Tier || Filter.Class))
			continue;
		if (!Query.empty() &&
			Lower(Owned.Name + " " + Owned.Display + " " + std::to_string(Owned.InventoryId) + " " +
				  std::to_string(Owned.TypeCd) + " " +
				  (Vehicle ? VehicleListings.At(size_t(Vehicle - GameCatalog.Vehicles.data())).Search : ""))
					.find(Query) == std::string::npos)
			continue;
		GarageRows.push_back(Owned);
	}
	std::stable_sort(GarageRows.begin(), GarageRows.end(),
					 [&](auto const& A, auto const& B)
					 {
						 auto RankA = Ranks.contains(A.TypeCd) ? Ranks.at(A.TypeCd) : SIZE_MAX;
						 auto RankB = Ranks.contains(B.TypeCd) ? Ranks.at(B.TypeCd) : SIZE_MAX;
						 return RankA != RankB ? RankA < RankB : A.InventoryId < B.InventoryId;
					 });
	std::vector<hstring> Labels;
	Labels.reserve(GarageRows.size());
	for (auto const& Owned : GarageRows)
	{
		auto Vehicle = GameCatalog.ByCd(Owned.TypeCd);
		auto Tier = Vehicle ? VehicleListings.At(size_t(Vehicle - GameCatalog.Vehicles.data())).Tier : 0;
		Labels.push_back(HString(std::to_string(Owned.InventoryId) + " · " + Owned.Display + " · " +
								 std::to_string(Tier) + "\n" + Owned.Name));
	}
	SetListRows(GarageList, Labels);
}
void EditorWindow::RefreshAdd()
{
	auto Timing = Performance.Measure("Save.RefreshAdd");
	if (!AddList)
		return;
	EnsureVehicleListings();
	EnsureGarageInventory();
	AddRows = VehicleListings.Select(ReadVehicleFilter(AddFilters));
	std::set<uint32_t> Owned;
	for (auto const& Item : GarageInventory)
		Owned.insert(Item.TypeCd);
	std::vector<hstring> Labels;
	Labels.reserve(AddRows.size());
	for (auto Index : AddRows)
	{
		auto const& Vehicle = GameCatalog.Vehicles[Index];
		Labels.push_back(HString(Vehicle.DisplayName + " · " +
								 std::to_string(VehicleListings.At(Index).Tier) +
								 (Owned.contains(Vehicle.TypeCd) ? " ✓" : "") + "\n" + Vehicle.Name));
	}
	SetListRows(AddList, Labels);
}
void EditorWindow::RefreshSaveFields()
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
std::set<int64_t> EditorWindow::SelectedGarage() const
{
	std::set<int64_t> Result;
	for (auto Index : SelectedIndices(GarageList))
		Result.insert(GarageRows.at(Index).InventoryId);
	return Result;
}
fire_and_forget EditorWindow::LoadSave(bool Locate)
{
	auto Lifetime = shared_from_this();
	if (Working)
		co_return;
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
		co_await OpenSavePath(Path);
		if (CloseRequested)
			RequestClose();
	}
	catch (...)
	{
		SetWorking(false);
		Run([] { throw; });
	}
}
fire_and_forget EditorWindow::CloseSave()
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
void EditorWindow::WriteSave(bool SaveAs)
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
hstring EditorWindow::DeletionPreview(std::vector<SaveIssue> const& Plan) const
{
	std::string Text =
		String(Tr(SaveData.Account.Json922 ? "Save.DeleteLegacyData" : "Save.DeleteVehicleData")) + "\n\n";
	for (auto const& Item : Plan)
	{
		Text += Item.VehicleName + "\n" + String(Tr("Save.InventoryId")) + ": " +
				(Item.InventoryKey ? Item.InventoryKey->Display() : std::to_string(Item.InventoryId)) +
				" · typeCd: " + std::to_string(Item.TypeCd) + "\n";
		if (!Item.Reason.empty())
			Text += String(Tr(("Save.Reason." + Item.Reason).c_str())) + "\n";
		for (auto const& Field : Item.DataFields)
			Text += "  " + Field + "\n";
		if (!Item.CrewIds.empty())
		{
			Text += String(Tr("Save.MovedCrewIds"));
			for (auto Id : Item.CrewIds)
				Text += " " + std::to_string(Id);
			Text += "\n";
		}
		Text += "\n";
	}
	return HString(Text);
}
fire_and_forget EditorWindow::DeleteSelected()
{
	auto Lifetime = shared_from_this();
	try
	{
		std::vector<SaveIssue> Plan;
		for (auto Index : SelectedIndices(GarageList))
			Plan.push_back(SaveSession::DescribeDeletion(GarageRows.at(Index)));
		if (Plan.empty())
			co_return;
		if (!(co_await Confirm(Tr("Save.DeleteVehicles"), DeletionPreview(Plan))))
			co_return;
		SaveData.Execute("Delete garage vehicles",
						 [&](SaveGame& Save)
						 {
							 for (auto const& Item : Plan)
								 Save.RemoveVehicleEntry(Item.InventoryKey);
						 });
		RefreshSave();
	}
	catch (...)
	{
		Run([] { throw; });
	}
}
fire_and_forget EditorWindow::Cleanup(CleanupKind Kind)
{
	auto Lifetime = shared_from_this();
	try
	{
		auto Plan = SaveData.PreviewCleanup(GameCatalog, Kind, *ParameterRules);
		if (Plan.empty())
		{
			Message(Tr("Save.CleanupPreview"), Tr("Save.NoMatchingVehicles"));
			co_return;
		}
		if (!(co_await Confirm(Tr("Save.CleanupPreviewDeleteTheseVehicles"), DeletionPreview(Plan))))
			co_return;
		SaveData.Execute("Clean garage",
						 [&](SaveGame& Save)
						 {
							 for (auto const& Item : Plan)
								 Save.RemoveVehicleEntry(Item.InventoryKey);
						 });
		RefreshSave();
	}
	catch (...)
	{
		Run([] { throw; });
	}
}
fire_and_forget EditorWindow::AddSelected()
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
			if (Owned.contains(Vehicle.TypeCd) || !Vehicle.Enabled || !Vehicle.Listed || Vehicle.Conflict ||
				!Vehicle.TypeCd || (Vehicle.DefinitionPath.empty() && Vehicle.StockDescriptor.empty()))
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
					VehicleFactory Factory(GameCatalog);
					for (auto Index : Vehicles)
					{
						if (CancelWork)
							throw std::runtime_error("Cancelled");
						Save.AddVehicle(GameCatalog.Vehicles[Index], GameCatalog, WithCrew, &CrewNames,
										&Factory);
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
} // namespace OffHanger::Ui
