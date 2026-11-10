#include "EditorWindow.h"
namespace OffHanger::Ui
{
using namespace winrt;
using namespace Controls;
Grid EditorWindow::BuildParameterPage()
{
	auto Timing = Performance.Measure("Parameters.BuildPage", GameCatalog.Vehicles.at(ActiveVehicle).Name);
	ParameterBindings.clear();
	auto Generation = ++ParameterGeneration;
	auto Page = Rows({Auto(), Star()});
	auto const Vehicle = GameCatalog.Vehicles.at(ActiveVehicle);
	auto Header = Rows({Auto(), Auto()});
	Header.RowSpacing(6);
	auto Details = Label(Tr("Parameters.Immediate"), 12);
	Header.Children().Append(Details);
	auto Options = Columns({Auto(), Pixels(140), Star(), Auto()});
	auto CurrencyLabel = Label(Tr("Parameters.Currency"), 13);
	CurrencyLabel.VerticalAlignment(Xaml::VerticalAlignment::Center);
	Options.Children().Append(CurrencyLabel);
	try
	{
		auto Currency =
			Combo({Tr("Currency.Credits"), Tr("Currency.Gold")}, VehicleData.PriceCurrency(Vehicle) ? 1 : 0);
		Currency.IsEnabled(!WasGameRunning);
		WriteControls.push_back(make_weak(Currency.as<Control>()));
		Currency.VerticalAlignment(Xaml::VerticalAlignment::Center);
		Currency.SelectionChanged(
			[this, Vehicle](auto const& Sender, auto&&)
			{
				auto Currency = Sender.as<ComboBox>();
				Run(
					[&]
					{
						bool Gold = Currency.SelectedIndex() == 1;
						if (Gold == VehicleData.PriceCurrency(Vehicle))
							return;
						try
						{
							VehicleData.SetPriceCurrency(Vehicle, Gold);
						}
						catch (...)
						{
							Currency.SelectedIndex(VehicleData.PriceCurrency(Vehicle) ? 1 : 0);
							throw;
						}
						RefreshVehicles();
						SetStatus(Tr("Vehicle.Cached"));
					});
			});
		Grid::SetColumn(Currency, 1);
		Options.Children().Append(Currency);
	}
	catch (std::exception const&)
	{
		auto NoPrice = Label(Tr("Parameters.NoPrice"), 12);
		Grid::SetColumn(NoPrice, 1);
		Options.Children().Append(NoPrice);
	}
	Section const* Metadata = Vehicle.ListPath.empty()
								  ? nullptr
								  : VehicleData.Document(Vehicle.ListPath).Current.Root.Child(Vehicle.Model);
	auto Acquisition = ParameterRules->Classify(Vehicle, Metadata, &GameCatalog);
	auto AvailabilityKey = "Availability." + Acquisition.Availability;
	auto Availability = Label(Tr(AvailabilityKey.c_str()), 12);
	Availability.VerticalAlignment(Xaml::VerticalAlignment::Center);
	if (!Acquisition.Source.empty())
		ToolTipService::SetToolTip(Availability, box_value(HString(Acquisition.Source)));
	Grid::SetColumn(Availability, 2);
	Options.Children().Append(Availability);
	auto Reset = Button("Vehicle.ResetSelected", [this] { ResetCurrentVehicle(); });
	Reset.VerticalAlignment(Xaml::VerticalAlignment::Center);
	Grid::SetColumn(Reset, 3);
	Options.Children().Append(Reset);
	Grid::SetRow(Options, 1);
	Header.Children().Append(Options);
	Page.Children().Append(Header);
	auto Fields = std::make_shared<std::vector<ParameterInfo>>(
		[&]
		{
			auto DataTiming = Performance.Measure("Parameters.BuildData", Vehicle.Name);
			return ParameterRules->Build(VehicleData, Vehicle, Preferences.Chinese);
		}());
	Pivot Categories;
	for (auto Key : {"vehicle", "gun", "chassis", "engine", "turret", "shell"})
	{
		PivotItem Item;
		auto TitleKey = "Category." + std::string(Key);
		Item.Header(box_value(Tr(TitleKey.c_str())));
		Categories.Items().Append(Item);
	}
	auto Populate = [this, Fields, Vehicle, Generation](Pivot const& Tabs)
	{
		if (Generation != ParameterGeneration)
			return;
		static char const* Keys[] = {"vehicle", "gun", "chassis", "engine", "turret", "shell"};
		auto Index = Tabs.SelectedIndex();
		if (Index < 0 || Index >= 6)
			return;
		CurrentParameterCategory = Index;
		auto Item = Tabs.Items().GetAt(Index).as<PivotItem>();
		if (!Item.Content())
			Item.Content(BuildParameterCategory(*Fields, Vehicle, Keys[Index], Generation));
	};
	Categories.SelectedIndex(CurrentParameterCategory);
	Categories.SelectionChanged(
		[this, Populate](auto const& Sender, auto&&)
		{
			FlushTextEdits();
			Populate(Sender.as<Pivot>());
		});
	Populate(Categories);
	Grid::SetRow(Categories, 1);
	Page.Children().Append(Categories);
	return Page;
}
Xaml::UIElement EditorWindow::BuildParameterCategory(std::vector<ParameterInfo> const& Fields,
													 Vehicle const& Vehicle, char const* Key,
													 uint64_t Generation)
{
	auto Timing = Performance.Measure("Parameters.BuildCategory", Key);
	auto Content = Stack(false, 8);
	Content.Margin({0, 2, 10, 10});
	if (std::string(Key) == "gun")
	{
		DataSection Guns;
		Guns.Root = VehicleData.Document(Vehicle.DefinitionPath).Current.Root;
		Section SharedGuns;
		SharedGuns.Name = "guns";
		for (auto const& Scope : VehicleData.VehicleScopes(Vehicle))
			if (Scope.Component == "guns")
				SharedGuns.Children.push_back(
					VehicleWorkspace::Resolve(VehicleData.Document(Scope.Path).Current, Scope.Root));
		Guns.Root.Children.push_back(std::move(SharedGuns));
		auto MechanismKey = "Mechanism." + ParameterDefinitions::Mechanism(Guns);
		Content.Children().Append(Label(Tr("Parameters.Mechanism") + Tr(MechanismKey.c_str()), 12));
	}
	std::map<std::string, std::vector<ParameterInfo>> Groups;
	for (auto const& Info : Fields)
		if (Info.Category == Key)
			Groups[Info.Field.Module + " · " + Info.Field.Document].push_back(Info);
	for (auto const& [GroupKey, Parameters] : Groups)
	{
		auto Group = Stack(false, 6);
		auto Source = Label(HString(GroupKey), 11);
		Source.IsTextSelectionEnabled(true);
		Group.Children().Append(Source);
		auto Shared = Parameters.front().Field.SharedVehicles;
		if (Shared.size() > 1)
		{
			Expander Affected;
			Affected.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
			Affected.HorizontalContentAlignment(Xaml::HorizontalAlignment::Stretch);
			Affected.Header(box_value(Tr("Parameters.Shared") + HString(std::to_string(Shared.size()))));
			std::string Names;
			for (auto const& Name : Shared)
			{
				auto Definition = GameCatalog.ByName(Name);
				Names += (Definition ? Definition->DisplayName : Name) + " · " + Name + "\n";
			}
			auto Text = Label(HString(Names), 12);
			Text.IsTextSelectionEnabled(true);
			Affected.Content(Text);
			Group.Children().Append(Affected);
		}
		Grid Fields;
		Fields.RowSpacing(6);
		Fields.ColumnSpacing(24);
		for (auto const& Info : Parameters)
		{
			auto Row = Columns({Star(), Pixels(210)});
			auto Caption = Label(HString(Info.Label), 13);
			Caption.VerticalAlignment(Xaml::VerticalAlignment::Center);
			ToolTipService::SetToolTip(Caption, box_value(HString(Info.Help)));
			Row.Children().Append(Caption);
			auto Editing = Stack(false, 2);
			TextBox Input;
			Input.IsReadOnly(WasGameRunning);
			// A different category may have edited this shared field before its UI was created.
			auto const& Current = VehicleWorkspace::Resolve(VehicleData.Document(Info.Field.Document).Current,
															Info.Field.Address)
									  .Value;
			Input.Text(HString(ParameterDefinitions::DisplayValue(Info, Current)));
			Input.VerticalAlignment(Xaml::VerticalAlignment::Center);
			ToolTipService::SetToolTip(Input, box_value(HString(Info.Help)));
			auto Error = Label(L"", 11);
			Error.Visibility(Xaml::Visibility::Collapsed);
			auto EditableInfo = Info;
			EditableInfo.Field.SharedVehicles.clear();
			ParameterBindings.push_back({EditableInfo, Input});
			BindDeferredEdit(
				Input,
				[this, Error, Vehicle, Info = std::move(EditableInfo), Generation](TextBox const& Input)
				{
					if (Generation != ParameterGeneration || CurrentPage != 0 || CurrentVehicleTab != 0)
						return true;
					auto EditTiming = Performance.Measure("Parameters.Edit", Info.Field.DisplayPath);
					try
					{
						auto const& Field = Info.Field;
						auto Current = VehicleWorkspace::Resolve(VehicleData.Document(Field.Document).Current,
																 Field.Address)
										   .Value;
						auto Display = String(Input.Text());
						if (ParameterDefinitions::DisplayValue(Info, Current) == Display)
						{
							Error.Text(L"");
							Error.Visibility(Xaml::Visibility::Collapsed);
							return true;
						}
						auto Stored = ParameterDefinitions::StoredValue(Info, Display, Current);
						if (Stored != Current)
							VehicleData.Edit(Vehicle, Field.Document, Field.Address, Stored);
						Error.Text(L"");
						Error.Visibility(Xaml::Visibility::Collapsed);
						auto Updated = VehicleWorkspace::Resolve(VehicleData.Document(Field.Document).Current,
																 Field.Address)
										   .Value;
						for (auto const& Binding : ParameterBindings)
							if (Binding.Input != Input && Binding.Info.Field.Document == Field.Document &&
								Binding.Info.Field.Address == Field.Address)
								Binding.Input.Text(
									HString(ParameterDefinitions::DisplayValue(Binding.Info, Updated)));
						if (Field.Document == Vehicle.ListPath)
							RefreshVehicles();
						SetStatus(Tr("Vehicle.Cached"));
						return true;
					}
					catch (std::exception const& Failure)
					{
						Error.Text(HString(Failure.what()));
						Error.Visibility(Xaml::Visibility::Visible);
						return false;
					}
				});
			Editing.Children().Append(Input);
			Editing.Children().Append(Error);
			Grid::SetColumn(Editing, 1);
			Row.Children().Append(Editing);
			Fields.Children().Append(Row);
		}
		ArrangeGridColumns(Fields, 1);
		Fields.SizeChanged([](auto const& Sender, Xaml::SizeChangedEventArgs const& Event)
						   { ArrangeGridColumns(Sender.as<Grid>(), Event.NewSize().Width >= 960 ? 2 : 1); });
		Group.Children().Append(Fields);
		Expander Section;
		Section.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
		Section.HorizontalContentAlignment(Xaml::HorizontalAlignment::Stretch);
		Section.Header(box_value(HString(Parameters.front().Field.Module)));
		Section.IsExpanded(true);
		Section.Content(Group);
		Content.Children().Append(Section);
	}
	if (Groups.empty())
		Content.Children().Append(Label(Tr("Parameters.Empty")));
	return Scroll(Content);
}
} // namespace OffHanger::Ui
