#include "EditorWindow.h"
#include <set>

namespace OffHanger::Ui
{
using namespace winrt;
using namespace Controls;
namespace
{
std::vector<hstring> Nations()
{
	return {L"ussr",  L"germany", L"usa",	 L"china",	L"france", L"uk",
			L"japan", L"czech",	  L"sweden", L"poland", L"italy"};
}
int FilterRole(ComboBox const& Filter)
{
	static int const Roles[] = {0, 1, 4, 3, 5, 2};
	return Roles[std::clamp(Filter.SelectedIndex(), 0, 5)];
}
ComboBoxItem ThemeOption(hstring const& Text)
{
	// Native aliases can share a caption. Give each item reference identity so
	// WinUI automation never treats equal boxed strings as the same sibling.
	ComboBoxItem Item;
	Item.Content(box_value(Text));
	return Item;
}
} // namespace
hstring EditorWindow::CrewRole(int Role) const
{
	static char const* Keys[] = {"Barracks.Unknown", "Barracks.Commander", "Barracks.Radioman",
								 "Barracks.Driver",	 "Barracks.Gunner",	   "Barracks.Loader"};
	return Tr(Keys[Role >= 1 && Role <= 5 ? Role : 0]);
}
hstring EditorWindow::CrewClass(int Class) const
{
	static char const* Keys[] = {"Barracks.Unknown", "Filter.Light", "Filter.Medium",
								 "Filter.Heavy",	 "Filter.Td",	 "Filter.Spg"};
	return Tr(Keys[Class >= 1 && Class <= 5 ? Class : 0]);
}
hstring EditorWindow::CrewTheme(std::string const& Theme, std::string const& Text) const
{
	if (Theme == "ordinary")
		return Tr("Barracks.Ordinary");
	if (Theme == "unknown")
		return Tr("Barracks.Unknown");
	if (Theme.starts_with("ny") && Theme.size() >= 4 && std::isdigit(static_cast<unsigned char>(Theme[2])) &&
		std::isdigit(static_cast<unsigned char>(Theme[3])))
		return Tr("Barracks.HolidayOps") +
			   HString("20" + Theme.substr(2, 2) + (Theme.ends_with("ruby") ? " (RU)" : ""));
	if (Theme == "twitch")
		return L"Twitch / Prime Gaming";
	for (auto const& [Key, Caption] :
		 std::vector<std::pair<std::string, char const*>>{{"girls_und_panzer", "Barracks.GirlsUndPanzer"},
														  {"sabaton", "Barracks.Sabaton"},
														  {"offspring", "Barracks.Offspring"},
														  {"theboys", "Barracks.TheBoys"},
														  {"wt", "Barracks.Waffentrager"}})
		if (Theme == Key)
			return Tr(Caption);
	if (Theme.starts_with("bp") && Theme.size() > 2 && std::isdigit(static_cast<unsigned char>(Theme[2])))
		return Tr(Theme.size() < 6 ? "Barracks.BattlePass" : "Barracks.BattlePassSpecial") +
			   HString(Theme.substr(2));
	for (auto const& [Prefix, Caption] : std::vector<std::pair<std::string, char const*>>{
			 {"hw", "Barracks.Halloween"}, {"bob", "Barracks.TeamClash"}, {"race", "Barracks.Racing"}})
		if (Theme.starts_with(Prefix) && Theme.size() == Prefix.size() + 2)
			return Tr(Caption) + HString("20" + Theme.substr(Prefix.size()));
	if (Theme.starts_with("wt_"))
		return Tr("Barracks.Waffentrager") + L" " + HString(Theme.substr(3));
	if (Theme.starts_with("wt") && Theme.size() == 4)
		return Tr("Barracks.Waffentrager") + HString(" 20" + Theme.substr(2));
	return HString(Text.empty() ? Theme : Text);
}
Grid EditorWindow::BuildCrewTab()
{
	auto Page = Rows({Star()});
	BarracksTabs = Pivot();
	Page.Children().Append(BarracksTabs);
	auto NationChoices = Nations();
	NationChoices.insert(NationChoices.begin(), Tr("Filter.AllNations"));
	std::vector<hstring> Roles{Tr("Barracks.AllRoles")};
	for (int Role : {1, 4, 3, 5, 2})
		Roles.push_back(CrewRole(Role));
	auto Owned = Rows({Auto(), Star(), Auto(), Auto()});
	auto Filters = Columns({Star(), Star(), Star(), Star()});
	OwnedTheme = Combo({Tr("Barracks.AllThemes")});
	OwnedTheme.Header(box_value(Tr("Barracks.Theme")));
	OwnedNation = Combo(NationChoices);
	OwnedNation.Header(box_value(Tr("Barracks.Nation")));
	OwnedClass = Combo(
		{Tr("Filter.AllClasses"), CrewClass(1), CrewClass(2), CrewClass(3), CrewClass(4), CrewClass(5)});
	OwnedClass.Header(box_value(Tr("Barracks.Class")));
	OwnedRole = Combo(Roles);
	OwnedRole.Header(box_value(Tr("Barracks.Role")));
	int Column = 0;
	for (auto const& Filter : {OwnedTheme, OwnedNation, OwnedClass, OwnedRole})
	{
		Grid::SetColumn(Filter, Column++);
		Filters.Children().Append(Filter);
	}
	Owned.Children().Append(Filters);
	OwnedCrewList = ListView();
	Grid::SetRow(OwnedCrewList, 1);
	Owned.Children().Append(OwnedCrewList);
	auto Actions = Stack(true, 8);
	Actions.Children().Append(Label(Tr("Barracks.Experience")));
	CrewXp = TextBox();
	CrewXp.Width(190);
	Xaml::Automation::AutomationProperties::SetName(CrewXp, Tr("Barracks.Experience"));
	Actions.Children().Append(CrewXp);
	Actions.Children().Append(
		Button("Barracks.SetExperience",
			   [this]
			   {
				   auto Index = OwnedCrewList.SelectedIndex();
				   if (Index < 0 || size_t(Index) >= OwnedCrewRows.size())
					   return;
				   auto Amount = NonnegativeInteger(CrewXp.Text());
				   if (Amount > 4000000000ll)
					   throw std::runtime_error(String(Tr("Barracks.ExperienceRange")));
				   auto Crew = Roster.at(OwnedCrewRows.at(Index));
				   SaveData.Execute("Edit crew experience",
									[&](SaveGame& Save) { SetCrewExperience(Save, Crew, uint32_t(Amount)); });
				   RefreshSave();
			   }));
	Actions.Children().Append(Button("Barracks.MoveToBarracks", [this] { RemoveCrew(false); }));
	Actions.Children().Append(Button("Barracks.RemoveFromAccount", [this] { RemoveCrew(true); }));
	for (auto const& Action : Actions.Children())
		Action.as<Xaml::FrameworkElement>().VerticalAlignment(Xaml::VerticalAlignment::Center);
	auto ActionScroll = Scroll(Actions);
	ActionScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
	ActionScroll.HorizontalScrollMode(ScrollMode::Enabled);
	Grid::SetRow(ActionScroll, 2);
	Owned.Children().Append(ActionScroll);
	auto ExperienceHelp = Label(Tr("Barracks.ExperienceHelp"), 12);
	Grid::SetRow(ExperienceHelp, 3);
	Owned.Children().Append(ExperienceHelp);
	PivotItem OwnedItem;
	OwnedItem.Header(box_value(Tr("Barracks.Owned")));
	OwnedItem.Content(Owned);
	BarracksTabs.Items().Append(OwnedItem);
	auto Recruit = Rows({Auto(), Star(), Auto(), Auto()});
	Filters = Columns({Star(), Star(), Star()});
	RecruitTheme = Combo({Tr("Barracks.AllThemes")});
	RecruitTheme.Header(box_value(Tr("Barracks.Theme")));
	RecruitNation = Combo(NationChoices);
	RecruitNation.Header(box_value(Tr("Barracks.NationRestriction")));
	RecruitRole = Combo(Roles);
	RecruitRole.Header(box_value(Tr("Barracks.RoleRestriction")));
	Column = 0;
	for (auto const& Filter : {RecruitTheme, RecruitNation, RecruitRole})
	{
		Grid::SetColumn(Filter, Column++);
		Filters.Children().Append(Filter);
	}
	Recruit.Children().Append(Filters);
	RecruitList = ListView();
	Grid::SetRow(RecruitList, 1);
	Recruit.Children().Append(RecruitList);
	auto RecruitButton = Button("Barracks.Recruit", [this] { RecruitSelectedCrew(); });
	Grid::SetRow(RecruitButton, 2);
	Recruit.Children().Append(RecruitButton);
	auto Help = Label(Tr("Barracks.RecruitHelp"), 12);
	Grid::SetRow(Help, 3);
	Recruit.Children().Append(Help);
	PivotItem RecruitItem;
	RecruitItem.Header(box_value(Tr("Barracks.Pending")));
	RecruitItem.Content(Recruit);
	BarracksTabs.Items().Append(RecruitItem);
	for (auto const& Filter : {OwnedTheme, OwnedNation, OwnedClass, OwnedRole})
		Filter.SelectionChanged(
			[this](auto&&, auto&&)
			{
				if (!BuildingBarracks)
					Run([&] { RefreshOwnedCrew(); });
			});
	for (auto const& Filter : {RecruitTheme, RecruitNation, RecruitRole})
		Filter.SelectionChanged(
			[this](auto&&, auto&&)
			{
				if (!BuildingBarracks)
					Run([&] { RefreshRecruits(); });
			});
	OwnedCrewList.SelectionChanged(
		[this](auto&&, auto&&)
		{
			auto Index = OwnedCrewList.SelectedIndex();
			CrewXp.Text(Index >= 0 && size_t(Index) < OwnedCrewRows.size()
							? HString(std::to_string(Roster.at(OwnedCrewRows[Index]).Info.FreeXp))
							: L"");
		});
	return Page;
}
void EditorWindow::RefreshCrew()
{
	auto Timing = Performance.Measure("Save.RefreshBarracks");
	if (!BarracksTabs)
		return;
	BuildingBarracks = true;
	if (RosterRevision != SaveData.Revision())
	{
		auto Selected = OwnedTheme.SelectedIndex();
		auto Theme = Selected > 0 && size_t(Selected) <= OwnedThemes.size() ? OwnedThemes[Selected - 1] : "";
		Roster = BarracksRoster(SaveData.Account, GameCatalog, CrewNames);
		RecruitedKeys.clear();
		for (auto const& Crew : Roster)
			if (!Crew.RecruitKey.empty())
				RecruitedKeys.insert(Crew.RecruitKey);
		RosterRevision = SaveData.Revision();
		std::map<std::string, std::string> Themes;
		for (auto const& Crew : Roster)
			Themes.try_emplace(Crew.Theme, Crew.ThemeLabel);
		OwnedThemes.clear();
		OwnedTheme.Items().Clear();
		OwnedTheme.Items().Append(ThemeOption(Tr("Barracks.AllThemes")));
		int Selection = 0;
		for (auto const& [Key, Label] : Themes)
		{
			OwnedThemes.push_back(Key);
			OwnedTheme.Items().Append(ThemeOption(CrewTheme(Key, Label)));
			if (Key == Theme)
				Selection = int(OwnedThemes.size());
		}
		OwnedTheme.SelectedIndex(Selection);
	}
	if (Candidates.empty())
	{
		Candidates = RecruitCandidates(CrewNames);
		std::map<std::string, std::string> Themes;
		for (auto const& Candidate : Candidates)
			Themes.try_emplace(Candidate.Theme, Candidate.ThemeLabel);
		RecruitThemes.clear();
		RecruitTheme.Items().Clear();
		RecruitTheme.Items().Append(ThemeOption(Tr("Barracks.AllThemes")));
		for (auto const& [Key, Label] : Themes)
		{
			RecruitThemes.push_back(Key);
			RecruitTheme.Items().Append(ThemeOption(CrewTheme(Key, Label)));
		}
		RecruitTheme.SelectedIndex(0);
	}
	BuildingBarracks = false;
	RefreshOwnedCrew();
	RefreshRecruits();
}
void EditorWindow::RefreshOwnedCrew()
{
	if (!OwnedCrewList)
		return;
	OwnedCrewRows.clear();
	std::vector<hstring> Labels;
	auto ThemeIndex = OwnedTheme.SelectedIndex();
	auto Nation = OwnedNation.SelectedIndex() - 1;
	auto Class = OwnedClass.SelectedIndex();
	auto Role = FilterRole(OwnedRole);
	auto NationNames = Nations();
	for (size_t Index = 0; Index < Roster.size(); ++Index)
	{
		auto const& Crew = Roster[Index];
		if (ThemeIndex > 0 && Crew.Theme != OwnedThemes.at(ThemeIndex - 1))
			continue;
		if (Nation >= 0 && (!Crew.Valid || Crew.Info.Nation != Nation))
			continue;
		if (Class > 0 && Crew.Class != Class)
			continue;
		if (Role && (!Crew.Valid || Crew.Info.Role != Role))
			continue;
		OwnedCrewRows.push_back(Index);
		auto NationText = Crew.Valid ? NationNames[Crew.Info.Nation] : Tr("Barracks.Unknown");
		auto Name = Crew.Name.empty() ? L"#" + HString(Crew.Key->Display()) : HString(Crew.Name);
		auto Vehicle = Crew.VehicleName.empty()
						   ? Tr("Barracks.Unknown") + HString(" (" + std::to_string(Crew.TrainingCd) + ")")
						   : HString(Crew.VehicleName);
		auto State = Crew.VehicleId ? Tr("Barracks.Mounted") + HString(Crew.MountedVehicle + " (#" +
																	   std::to_string(Crew.VehicleId) + ")")
									: Tr("Barracks.Standby");
		Labels.push_back(Name + L" · " + CrewTheme(Crew.Theme, Crew.ThemeLabel) + L"\n" + NationText +
						 L" · " + CrewClass(Crew.Class) + L" · " + Vehicle + L" · " +
						 CrewRole(Crew.Info.Role) + L"\n" + State);
	}
	SetListRows(OwnedCrewList, Labels);
}
void EditorWindow::RefreshRecruits()
{
	if (!RecruitList)
		return;
	RecruitRows.clear();
	std::vector<hstring> Labels;
	auto Theme = RecruitTheme.SelectedIndex();
	auto Nation = RecruitNation.SelectedIndex() - 1;
	auto Role = FilterRole(RecruitRole);
	auto NationNames = Nations();
	for (size_t Index = 0; Index < Candidates.size(); ++Index)
	{
		auto const& Candidate = Candidates[Index];
		if (!Candidate.Repeatable && RecruitedKeys.contains(Candidate.Key))
			continue;
		if (Theme > 0 && Candidate.Theme != RecruitThemes.at(Theme - 1))
			continue;
		if (!Candidate.Allows(Nation, Role))
			continue;
		RecruitRows.push_back(Index);
		hstring NationsText, RolesText;
		int NationCount = 0, RoleCount = 0;
		for (int N = 0; N < 11; ++N)
			if (Candidate.Allows(N))
			{
				if (NationCount++)
					NationsText = NationsText + L", ";
				NationsText = NationsText + NationNames[N];
			}
		for (int R : {1, 4, 3, 5, 2})
			if (Candidate.Allows(-1, R))
			{
				if (RoleCount++)
					RolesText = RolesText + L", ";
				RolesText = RolesText + CrewRole(R);
			}
		if (NationCount == 11)
			NationsText = Tr("Barracks.Unrestricted");
		if (RoleCount == 5)
			RolesText = Tr("Barracks.Unrestricted");
		auto Name = Candidate.Repeatable ? Tr(Candidate.Variants.front().Female ? "Barracks.OrdinaryFemale"
																				: "Barracks.OrdinaryMale")
										 : HString(Candidate.Name);
		Labels.push_back(Name + L" · " + CrewTheme(Candidate.Theme, Candidate.ThemeLabel) + L"\n" +
						 Tr("Barracks.RoleRestriction") + L": " + RolesText + L" · " +
						 Tr("Barracks.NationRestriction") + L": " + NationsText);
	}
	SetListRows(RecruitList, Labels);
}
fire_and_forget EditorWindow::RemoveCrew(bool FromAccount)
{
	auto Lifetime = shared_from_this();
	try
	{
		auto Index = OwnedCrewList.SelectedIndex();
		if (Index < 0 || size_t(Index) >= OwnedCrewRows.size())
			co_return;
		auto Crew = Roster.at(OwnedCrewRows.at(Index));
		if (!FromAccount && !Crew.VehicleId)
			co_return;
		auto Detail = HString(Crew.Name + " (#" + Crew.Key->Display() + ")") + L"\n" +
					  CrewTheme(Crew.Theme, Crew.ThemeLabel) + L" · " + CrewRole(Crew.Info.Role) + L"\n" +
					  HString(Crew.VehicleName) + L"\n" +
					  Tr(FromAccount ? "Barracks.RemoveWarning" : "Barracks.DetachWarning");
		if (!(co_await Confirm(Tr(FromAccount ? "Barracks.RemoveFromAccount" : "Barracks.MoveToBarracks"),
							   Detail)))
			co_return;
		SaveData.Execute("Remove selected crew",
						 [&](SaveGame& Save) { DismissCrew(Save, Crew, FromAccount); });
		RefreshSave();
	}
	catch (...)
	{
		Run([] { throw; });
	}
}
fire_and_forget EditorWindow::RecruitSelectedCrew()
{
	auto Lifetime = shared_from_this();
	try
	{
		auto Index = RecruitList.SelectedIndex();
		if (Index < 0 || size_t(Index) >= RecruitRows.size())
			co_return;
		if (!SaveData.Account.Root)
			throw std::runtime_error(String(Tr("Save.NoSaveOpen")));
		if (SaveData.Account.Json922)
			throw std::runtime_error(String(Tr("Barracks.LegacyUnsupported")));
		auto Candidate = Candidates.at(RecruitRows.at(Index));
		EnsureVehicleListings();
		auto EligibleVehicles = std::make_shared<std::vector<size_t>>(VehicleListings.Select({}));
		std::erase_if(*EligibleVehicles,
					  [&](size_t Index)
					  {
						  auto const& Vehicle = GameCatalog.Vehicles[Index];
						  return !Vehicle.Listed || !Vehicle.Enabled || Vehicle.Conflict || !Vehicle.TypeCd ||
								 Vehicle.Tier < 1 || Vehicle.Tier > 11 || !VehicleClass(Vehicle.Tags) ||
								 !Candidate.AllowsVehicle(Vehicle) ||
								 !GameCatalog.Resources.Find(Vehicle.DefinitionPath);
					  });
		struct Form
		{
			ComboBox Nation{nullptr}, Class{nullptr}, Vehicle{nullptr}, Role{nullptr};
			ContentDialog Dialog{nullptr};
			std::vector<int> Nations, Classes, Roles;
			std::vector<size_t> Vehicles;
			bool Updating{};
		};
		auto State = std::make_shared<Form>();
		State->Dialog = ContentDialog();
		std::weak_ptr<Form> Weak = State;
		auto Content = Stack();
		Content.Children().Append(
			Label(HString(Candidate.Name) + L" · " + CrewTheme(Candidate.Theme, Candidate.ThemeLabel), 18));
		auto NationNames = Nations();
		std::vector<hstring> Labels;
		for (int N = 0; N < 11; ++N)
			if (Candidate.Allows(N))
			{
				State->Nations.push_back(N);
				Labels.push_back(NationNames[N]);
			}
		if (Labels.empty())
			throw std::runtime_error("No permitted recruitment nation");
		State->Nation = Combo(Labels);
		State->Nation.Header(box_value(Tr("Barracks.Nation")));
		State->Nation.IsEnabled(Labels.size() > 1);
		State->Class = Combo({});
		State->Class.Header(box_value(Tr("Barracks.Class")));
		State->Vehicle = Combo({});
		State->Vehicle.Header(box_value(Tr("Barracks.Vehicle")));
		State->Role = Combo({});
		State->Role.Header(box_value(Tr("Barracks.Role")));
		for (auto Control : {State->Nation, State->Class, State->Vehicle, State->Role})
		{
			Control.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
			Content.Children().Append(Control);
		}
		Content.Children().Append(Label(Tr("Barracks.RecruitHelp"), 12));
		auto UpdateRoles = [this, Weak, Candidate]
		{
			auto Form = Weak.lock();
			if (!Form)
				return;
			Form->Roles.clear();
			Form->Role.Items().Clear();
			auto V = Form->Vehicle.SelectedIndex();
			if (V >= 0 && size_t(V) < Form->Vehicles.size())
			{
				auto const& Vehicle = GameCatalog.Vehicles[Form->Vehicles[V]];
				for (auto Role : VehicleCrewRoles(GameCatalog, Vehicle))
					if (Candidate.Allows(Vehicle.NationId, Role))
					{
						Form->Roles.push_back(Role);
						Form->Role.Items().Append(box_value(CrewRole(Role)));
					}
			}
			Form->Role.SelectedIndex(Form->Roles.empty() ? -1 : 0);
			Form->Role.IsEnabled(Form->Roles.size() > 1);
			Form->Dialog.IsPrimaryButtonEnabled(!Form->Roles.empty());
		};
		auto UpdateVehicles = [this, Weak, EligibleVehicles, UpdateRoles]
		{
			auto Form = Weak.lock();
			if (!Form)
				return;
			Form->Updating = true;
			Form->Vehicles.clear();
			Form->Vehicle.Items().Clear();
			auto C = Form->Class.SelectedIndex();
			auto N = Form->Nation.SelectedIndex();
			if (C >= 0 && N >= 0)
				for (auto V : *EligibleVehicles)
				{
					auto const& Vehicle = GameCatalog.Vehicles[V];
					if (Vehicle.NationId == Form->Nations.at(N) &&
						VehicleClass(Vehicle.Tags) == Form->Classes.at(C))
					{
						Form->Vehicles.push_back(V);
						Form->Vehicle.Items().Append(
							box_value(HString(Vehicle.DisplayName + " · " + Vehicle.Model)));
					}
				}
			Form->Vehicle.SelectedIndex(Form->Vehicles.empty() ? -1 : 0);
			Form->Vehicle.IsEnabled(Form->Vehicles.size() > 1);
			Form->Updating = false;
			UpdateRoles();
		};
		auto UpdateClasses = [this, Weak, EligibleVehicles, UpdateVehicles]
		{
			auto Form = Weak.lock();
			if (!Form)
				return;
			Form->Updating = true;
			Form->Classes.clear();
			Form->Class.Items().Clear();
			auto N = Form->Nation.SelectedIndex();
			std::set<int> Classes;
			if (N >= 0)
				for (auto Index : *EligibleVehicles)
				{
					auto const& Vehicle = GameCatalog.Vehicles[Index];
					if (Vehicle.NationId == Form->Nations.at(N))
						if (auto Class = VehicleClass(Vehicle.Tags))
							Classes.insert(Class);
				}
			for (auto Class : Classes)
			{
				Form->Classes.push_back(Class);
				Form->Class.Items().Append(box_value(CrewClass(Class)));
			}
			Form->Class.SelectedIndex(Form->Classes.empty() ? -1 : 0);
			Form->Class.IsEnabled(Form->Classes.size() > 1);
			Form->Updating = false;
			UpdateVehicles();
		};
		State->Nation.SelectionChanged(
			[Weak, UpdateClasses](auto&&, auto&&)
			{
				if (auto S = Weak.lock(); S && !S->Updating)
					UpdateClasses();
			});
		State->Class.SelectionChanged(
			[Weak, UpdateVehicles](auto&&, auto&&)
			{
				if (auto S = Weak.lock(); S && !S->Updating)
					UpdateVehicles();
			});
		State->Vehicle.SelectionChanged(
			[Weak, UpdateRoles](auto&&, auto&&)
			{
				if (auto S = Weak.lock(); S && !S->Updating)
					UpdateRoles();
			});
		UpdateClasses();
		auto Dialog = State->Dialog;
		Dialog.XamlRoot(Root.XamlRoot());
		Dialog.Title(box_value(Tr("Barracks.Recruit")));
		Dialog.Content(Scroll(Content));
		Dialog.PrimaryButtonText(Tr("Barracks.Recruit"));
		Dialog.CloseButtonText(Tr("Save.Cancel"));
		Dialog.PrimaryButtonClick(
			[Weak](auto&&, ContentDialogButtonClickEventArgs const& Event)
			{
				if (auto S = Weak.lock(); !S || S->Nation.SelectedIndex() < 0 ||
										  S->Class.SelectedIndex() < 0 || S->Vehicle.SelectedIndex() < 0 ||
										  S->Role.SelectedIndex() < 0)
					Event.Cancel(true);
			});
		if (co_await Dialog.ShowAsync() != ContentDialogResult::Primary)
			co_return;
		auto N = State->Nations.at(State->Nation.SelectedIndex());
		auto C = State->Classes.at(State->Class.SelectedIndex());
		auto V = GameCatalog.Vehicles.at(State->Vehicles.at(State->Vehicle.SelectedIndex())).TypeCd;
		auto R = State->Roles.at(State->Role.SelectedIndex());
		SaveData.Execute("Recruit selected crew", [&](SaveGame& Save)
						 { RecruitCrew(Save, GameCatalog, CrewNames, Candidate, N, C, V, R); });
		RefreshSave();
		SetStatus(Tr("Save.NewCrewAddedToTheBarracksNotSaved"));
	}
	catch (...)
	{
		Run([] { throw; });
	}
}
} // namespace OffHanger::Ui
