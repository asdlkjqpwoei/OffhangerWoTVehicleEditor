#include "EditorWindow.h"
namespace OffHanger::Ui
{
using namespace winrt;
using namespace Controls;
Grid EditorWindow::BuildVehiclePage()
{
	auto Timing = Performance.Measure("Vehicle.BuildPage");
	BuildingVehicle = true;
	auto Page = Columns({Pixels(330), Star()});
	Page.Margin({8, 0, 0, 0});
	auto Sidebar = Rows({Auto(), Auto(), Auto(), Auto(), Star()});
	Sidebar.Children().Append(Label(Tr("Vehicle.GameFolder"), 14));
	GamePath = TextBox();
	GamePath.Text(Preferences.GameFolder.wstring());
	GamePath.PlaceholderText(Tr("Vehicle.GameFolderHint"));
	Grid::SetRow(GamePath, 1);
	Sidebar.Children().Append(GamePath);
	auto Location = Columns({Star(), Star()});
	auto Browse = Button("Vehicle.Browse",
						 [this]
						 {
							 auto Path = PickFile(Window, false, L"Folder", L"*", L"", true);
							 if (!Path.empty())
								 GamePath.Text(Path.wstring());
						 });
	Browse.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
	Location.Children().Append(Browse);
	auto Load = Button("Vehicle.Load",
					   [this]
					   {
						   if (!Working)
							   ScanGame();
					   });
	Load.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
	Grid::SetColumn(Load, 1);
	Location.Children().Append(Load);
	Grid::SetRow(Location, 2);
	Sidebar.Children().Append(Location);
	auto Filters = BuildVehicleFilters(
		Preferences.Chinese, VehicleFilters,
		[this]
		{
			if (!BuildingVehicle)
				Run([&] { RefreshVehicles(); });
		},
		true);
	Grid::SetRow(Filters, 3);
	Sidebar.Children().Append(Filters);
	VehicleList = ListView();
	Grid::SetRow(VehicleList, 4);
	Sidebar.Children().Append(VehicleList);
	Page.Children().Append(Sidebar);
	auto Editor = Rows({Auto(), Auto(), Star()});
	Grid::SetColumn(Editor, 1);
	Page.Children().Append(Editor);
	Grid Actions;
	Actions.ColumnSpacing(6);
	Actions.RowSpacing(6);
	std::vector<Controls::Button> Commands{Button("Vehicle.Save", [this] { ApplyVehicleChanges(); }),
										   Button("Vehicle.ResetAll", [this] { ResetAllVehicles(); }),
										   Button("Vehicle.ExportAll", [this] { ExportChanges(false); }),
										   Button("Vehicle.ImportAll", [this] { ImportChanges(false); }),
										   Button("Vehicle.ImportSelected", [this] { ImportChanges(true); }),
										   Button("Vehicle.ExportSelected", [this] { ExportChanges(true); })};
	static char const* Captions[] = {"Vehicle.Save",
									 "Vehicle.ResetAll",
									 "Vehicle.ExportAllCaption",
									 "Vehicle.ImportAllCaption",
									 "Vehicle.ImportSelectedCaption",
									 "Vehicle.ExportSelectedCaption"};
	for (size_t Index = 0; Index < Commands.size(); ++Index)
	{
		auto const& Command = Commands[Index];
		Xaml::Automation::AutomationProperties::SetName(Command, unbox_value<hstring>(Command.Content()));
		auto Caption = Label(Tr(Captions[Index]), 12);
		Caption.TextAlignment(Xaml::TextAlignment::Center);
		Command.Content(Caption);
		Command.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
		Command.VerticalAlignment(Xaml::VerticalAlignment::Stretch);
		Command.MinHeight(46);
		Command.Padding({6, 5, 6, 5});
		Actions.Children().Append(Command);
	}
	ArrangeGridColumns(Actions, 6);
	Actions.SizeChanged(
		[](auto const& Sender, Xaml::SizeChangedEventArgs const& Event)
		{
			auto Width = Event.NewSize().Width;
			ArrangeGridColumns(Sender.as<Grid>(), Width >= 940 ? 6 : Width >= 550 ? 3 : 2);
		});
	Editor.Children().Append(Actions);
	VehicleTitle = Label(Tr("Vehicle.SelectFirst"), 24);
	Grid::SetRow(VehicleTitle, 1);
	Editor.Children().Append(VehicleTitle);
	VehicleTabs = Pivot();
	ParameterItem = PivotItem();
	ParameterItem.Header(box_value(Tr("Tab.Parameters")));
	XmlItem = PivotItem();
	XmlItem.Header(box_value(Tr("Tab.Xml")));
	ArmorItem = PivotItem();
	ArmorItem.Header(box_value(Tr("Tab.Armor")));
	for (auto Item : {ParameterItem, XmlItem, ArmorItem})
		VehicleTabs.Items().Append(Item);
	VehicleTabs.SelectedIndex(CurrentVehicleTab);
	Grid::SetRow(VehicleTabs, 2);
	Editor.Children().Append(VehicleTabs);
	VehicleTabs.SelectionChanged(
		[this](auto&&, auto&&)
		{
			if (!BuildingVehicle && VehicleTabs.SelectedIndex() != CurrentVehicleTab)
				Run([&] { ActivateVehicleTab(VehicleTabs.SelectedIndex()); });
		});
	VehicleList.SelectionChanged(
		[this](auto&&, auto&&)
		{
			if (!BuildingVehicle)
				Run([&] { SelectVehicle(); });
		});
	RefreshVehicles();
	BuildingVehicle = false;
	if (ActiveVehicle < GameCatalog.Vehicles.size())
	{
		auto Found = std::find(VehicleRows.begin(), VehicleRows.end(), ActiveVehicle);
		if (Found != VehicleRows.end())
			VehicleList.SelectedIndex(int(Found - VehicleRows.begin()));
	}
	ActivateVehicleTab(CurrentVehicleTab);
	return Page;
}
void EditorWindow::EnsureVehicleListings()
{
	if (ParameterRules)
		VehicleListings.Refresh(GameCatalog, VehicleData, *ParameterRules);
}
void EditorWindow::RefreshVehicles()
{
	auto Timing = Performance.Measure("Vehicle.RefreshList");
	if (!VehicleList)
		return;
	bool PreviousBuilding = BuildingVehicle;
	BuildingVehicle = true;
	auto Previous = ActiveVehicle;
	auto PreviousRows = std::move(VehicleRows);
	VehicleRows.clear();
	EnsureVehicleListings();
	VehicleRows = VehicleListings.Select(ReadVehicleFilter(VehicleFilters));
	std::vector<hstring> Labels;
	Labels.reserve(VehicleRows.size());
	for (auto Index : VehicleRows)
	{
		auto const& Vehicle = GameCatalog.Vehicles[Index];
		auto Tier = VehicleListings.At(Index).Tier;
		Labels.push_back(HString(Vehicle.DisplayName + " · " + std::to_string(Tier) + "\n" + Vehicle.Name));
	}
	if (!VehicleList.ItemsSource() || VehicleRows != PreviousRows || Labels != VehicleLabels)
	{
		VehicleLabels = Labels;
		SetListRows(VehicleList, Labels);
	}
	auto Found = std::find(VehicleRows.begin(), VehicleRows.end(), Previous);
	if (Found != VehicleRows.end())
	{
		auto Index = int(Found - VehicleRows.begin());
		if (VehicleList.SelectedIndex() != Index)
			VehicleList.SelectedIndex(Index);
	}
	else if (Previous != SIZE_MAX)
	{
		ActiveVehicle = SIZE_MAX;
		VehicleTitle.Text(Tr("Vehicle.SelectFirst"));
		ActivateVehicleTab(CurrentVehicleTab);
	}
	BuildingVehicle = PreviousBuilding;
}
void EditorWindow::SelectVehicle()
{
	auto Timing = Performance.Measure("Vehicle.Select");
	auto Index = VehicleList.SelectedIndex();
	if (Index < 0 || size_t(Index) >= VehicleRows.size())
		return;
	if (ActiveVehicle == VehicleRows[Index])
		return;
	if (!FlushTextEdits())
	{
		BuildingVehicle = true;
		auto Previous = std::find(VehicleRows.begin(), VehicleRows.end(), ActiveVehicle);
		VehicleList.SelectedIndex(Previous == VehicleRows.end() ? -1 : int(Previous - VehicleRows.begin()));
		BuildingVehicle = false;
		RequireTextEdits();
	}
	ActiveVehicle = VehicleRows[Index];
	VehicleTitle.Text(HString(GameCatalog.Vehicles[ActiveVehicle].DisplayName));
	ActivateVehicleTab(CurrentVehicleTab);
}
void EditorWindow::ActivateVehicleTab(int Index)
{
	RequireTextEdits();
	DeactivateArmor();
	++ParameterGeneration;
	for (auto const& Binding : ParameterBindings)
		Performance.WatchRetired(Binding.Input);
	ParameterBindings.clear();
	CurrentVehicleTab = std::clamp(Index, 0, 2);
	if (CurrentPage != 0 || !VehicleTabs)
		return;
	ParameterItem.Content(nullptr);
	XmlItem.Content(nullptr);
	ArmorItem.Content(nullptr);
	if (ActiveVehicle >= GameCatalog.Vehicles.size())
	{
		auto Prompt = Label(Tr("Vehicle.SelectFirst"));
		if (CurrentVehicleTab == 0)
			ParameterItem.Content(Prompt);
		else if (CurrentVehicleTab == 1)
			XmlItem.Content(Prompt);
		else
			ArmorItem.Content(Prompt);
		return;
	}
	if (CurrentVehicleTab == 0)
		ParameterItem.Content(BuildParameterPage());
	else if (CurrentVehicleTab == 1)
		XmlItem.Content(BuildXmlPage());
	else
	{
		ArmorItem.Content(BuildModelPanel());
		LoadModel();
	}
}
} // namespace OffHanger::Ui
