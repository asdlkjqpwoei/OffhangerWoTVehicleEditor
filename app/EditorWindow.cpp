#include "EditorWindow.h"
#include <fstream>
#include <shellapi.h>
#include <chrono>
namespace OffHanger::Ui
{
using namespace winrt;
using namespace Controls;
hstring EditorWindow::Tr(char const* Key) const
{
	return Translate(Preferences.Chinese, Key);
}
Controls::Button EditorWindow::Button(char const* Key, std::function<void()> Action)
{
	Controls::Button Result;
	Result.Content(box_value(Tr(Key)));
	auto Name = std::string(Key);
	bool ReadOnly = Name == "Vehicle.Browse" || Name == "Vehicle.Load" || Name == "Save.Open" ||
					Name == "Save.LocateActiveSave" || Name == "Save.CloseSave";
	if (!ReadOnly)
	{
		Result.IsEnabled(!WasGameRunning);
		WriteControls.push_back(make_weak(Result.as<Control>()));
	}
	Result.Click(
		[this, Action = std::move(Action), ReadOnly](auto&&, auto&&)
		{
			Run(
				[&]
				{
					if (!ReadOnly)
						EnsureWritesAllowed();
					Action();
				});
		});
	return Result;
}
void EditorWindow::Log(std::string const& Text) noexcept
{
	try
	{
		if (GameRunning())
			return;
		EnsureWritesAllowed();
		FileSystem::create_directories(NativePath(Preferences.CacheFolder / L"Logs"));
		EnsureWritesAllowed();
		std::ofstream Stream(NativePath(Preferences.CacheFolder / L"Logs/editor.log"),
							 std::ios::app | std::ios::binary);
		Stream << Timestamp() << " " << Text << '\n';
	}
	catch (...)
	{
	}
}
void EditorWindow::Run(std::function<void()> const& Action)
{
	try
	{
		Action();
	}
	catch (hresult_error const& Error)
	{
		Log(String(Error.message()));
		Message(Tr("General.Failed"), Error.message());
	}
	catch (std::exception const& Error)
	{
		Log(Error.what());
		Message(Tr("General.Failed"), HString(Error.what()));
	}
}
void EditorWindow::SetStatus(hstring const& Text)
{
	if (Status)
		Status.Text(Text);
}
fire_and_forget EditorWindow::Message(hstring Title, hstring Body)
{
	auto Lifetime = shared_from_this();
	ContentDialog Dialog;
	Dialog.XamlRoot(Root.XamlRoot());
	Dialog.RequestedTheme(Root.ActualTheme());
	Dialog.Title(box_value(Title));
	auto Text = Label(Body);
	Text.IsTextSelectionEnabled(true);
	auto Scroller = Scroll(Text);
	Scroller.MaxHeight(480);
	Dialog.Content(Scroller);
	Dialog.CloseButtonText(Tr("General.Close"));
	try
	{
		co_await Dialog.ShowAsync();
	}
	catch (...)
	{
		SetStatus(Body);
	}
}
IAsyncOperation<bool> EditorWindow::Confirm(hstring Title, hstring Body)
{
	ContentDialog Dialog;
	Dialog.XamlRoot(Root.XamlRoot());
	Dialog.RequestedTheme(Root.ActualTheme());
	Dialog.Title(box_value(Title));
	auto Scroller = Scroll(Label(Body));
	Scroller.MaxHeight(460);
	Dialog.Content(Scroller);
	Dialog.PrimaryButtonText(Tr("General.Continue"));
	Dialog.CloseButtonText(Tr("General.Cancel"));
	Dialog.DefaultButton(ContentDialogButton::Close);
	co_return co_await Dialog.ShowAsync() == ContentDialogResult::Primary;
}
void EditorWindow::Initialize()
{
	Performance.Enabled = Preferences.ProfileUi;
	WasGameRunning = GameRunning();
	ParameterRules = std::make_unique<ParameterDefinitions>(ApplicationFolder() / L"Config/parameters.yaml");
	Window = Xaml::Window();
	Window.Title(Tr("Editor.Name"));
	Window.AppWindow().Resize({1540, 980});
	BuildShell();
	Window.AppWindow().Closing(
		[this](auto&&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& Event)
		{
			if (AllowClose)
				return;
			Event.Cancel(true);
			if (Working)
			{
				CancelWork = true;
				CloseRequested = true;
				SetStatus(Tr("General.Finishing"));
			}
			else if (!ClosePromptOpen)
				RequestClose();
		});
	Window.Closed(
		[this](auto&&, auto&&)
		{
			CancelWork = true;
			if (WriteStateTimer)
				WriteStateTimer.Stop();
			if (EditTimer)
				EditTimer.Stop();
			DeactivateArmor();
		});
	Window.Activate();
	WriteStateTimer = Xaml::DispatcherTimer();
	WriteStateTimer.Interval(std::chrono::seconds(1));
	WriteStateTimer.Tick(
		[this](auto&&, auto&&)
		{
			if (Working)
				return;
			auto Running = GameRunning();
			std::erase_if(WriteControls, [](auto const& Weak) { return !Weak.get(); });
			if (Running == WasGameRunning)
				return;
			for (auto const& Weak : WriteControls)
				if (auto Button = Weak.get())
					Button.IsEnabled(!Running);
			for (auto const& Binding : ParameterBindings)
				Binding.Input.IsReadOnly(Running);
			if (CurrentPage == 0 && CurrentVehicleTab == 1 && SelectedXmlNode && XmlValue)
				XmlValue.IsReadOnly(Running ||
									VehicleWorkspace::IsReadOnly(VehicleWorkspace::Resolve(
										VehicleData.Document(ActiveDocument).Current, SelectedXmlAddress)));
			if (ArmorVisible() && ArmorThickness && ArmorMaterials)
				ArmorThickness.IsReadOnly(Running ||
										  !ArmorFields.contains(uint32_t(ArmorMaterials.SelectedIndex())));
			if (Running != WasGameRunning)
			{
				WasGameRunning = Running;
				SetStatus(Tr(Running ? "Game.ReadOnly" : "General.Ready"));
			}
		});
	WriteStateTimer.Start();
	EditTimer = Xaml::DispatcherTimer();
	EditTimer.Interval(std::chrono::milliseconds(250));
	EditTimer.Tick(
		[this](auto&&, auto&&)
		{
			if (!Working && !WasGameRunning)
				FlushTextEdits(false);
		});
	EditTimer.Start();
	if (WasGameRunning)
		SetStatus(Tr("Game.ReadOnly"));
	Log("Main window ready");
	RestoreSession();
}
fire_and_forget EditorWindow::RequestClose()
{
	auto Lifetime = shared_from_this();
	if (AllowClose)
		co_return;
	ClosePromptOpen = true;
	try
	{
		PendingTextEdits.clear();
		VehicleData.DiscardPending();
		XmlDrafts.clear();
		SaveData.Close();
		Performance.Flush(Preferences.CacheFolder / L"Logs/ui-performance.yaml");
		DeactivateArmor();
		AllowClose = true;
		Window.Close();
	}
	catch (...)
	{
		ClosePromptOpen = false;
		Run([] { throw; });
	}
	co_return;
}
void EditorWindow::ApplyTheme()
{
	using Windows::UI::Color;
	bool Dark = Root.ActualTheme() == Xaml::ElementTheme::Dark;
	Root.Background(Xaml::Media::SolidColorBrush(Dark ? Color{255, 32, 32, 32} : Color{255, 250, 250, 250}));
	Color Accent{255, uint8_t(Preferences.Accent >> 16), uint8_t(Preferences.Accent >> 8),
				 uint8_t(Preferences.Accent)};
	Root.Resources().Insert(box_value(L"SystemAccentColor"), box_value(Accent));
	for (auto Name :
		 {L"AccentFillColorDefaultBrush", L"AccentFillColorSecondaryBrush", L"AccentFillColorTertiaryBrush",
		  L"SystemControlHighlightAccentBrush", L"SystemControlForegroundAccentBrush"})
		Root.Resources().Insert(box_value(Name), Xaml::Media::SolidColorBrush(Accent));
}

void EditorWindow::BuildShell()
{
	ResetPageCache();
	Root = Rows({Star(), Auto()});
	Root.Padding({8, 8, 12, 8});
	Root.Language(Preferences.Chinese ? L"zh-CN" : L"en-US");
	Root.RequestedTheme(Preferences.Theme == 1	 ? Xaml::ElementTheme::Light
						: Preferences.Theme == 2 ? Xaml::ElementTheme::Dark
												 : Xaml::ElementTheme::Default);
	ApplyTheme();
	Root.ActualThemeChanged([this](auto&&, auto&&) { ApplyTheme(); });
	Navigation = NavigationView();
	Navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact);
	Navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
	for (auto Key : {"Navigation.Vehicle", "Navigation.Save"})
	{
		NavigationViewItem Item;
		Item.Content(box_value(Tr(Key)));
		Item.Icon(SymbolIcon(Navigation.MenuItems().Size() == 0 ? Symbol::Edit : Symbol::Save));
		Navigation.MenuItems().Append(Item);
	}
	Navigation.Loaded(
		[this](auto&&, auto&&)
		{
			if (auto Item = Navigation.SettingsItem().try_as<NavigationViewItem>())
			{
				Item.Content(box_value(Tr("Navigation.Settings")));
				if (CurrentPage == 2)
					Navigation.SelectedItem(Item);
			}
		});
	Navigation.SelectionChanged(
		[this](auto&&, NavigationViewSelectionChangedEventArgs const& Event)
		{
			Run(
				[&]
				{
					if (Event.IsSettingsSelected())
						ShowPage(2);
					else
					{
						uint32_t Index{};
						if (Navigation.MenuItems().IndexOf(Event.SelectedItem(), Index))
							ShowPage(int(Index));
					}
				});
		});
	Root.Children().Append(Navigation);
	auto Footer = Columns({Pixels(20), Star(), Pixels(230)});
	Busy = ProgressRing();
	Busy.Width(18);
	Busy.Height(18);
	Busy.IsActive(Working);
	Footer.Children().Append(Busy);
	Status = Label(Tr("General.Ready"), 12);
	Grid::SetColumn(Status, 1);
	Footer.Children().Append(Status);
	WorkProgress = ProgressBar();
	WorkProgress.Minimum(0);
	WorkProgress.Maximum(100);
	WorkProgress.IsIndeterminate(Working);
	WorkProgress.VerticalAlignment(Xaml::VerticalAlignment::Center);
	WorkProgress.Visibility(Working ? Xaml::Visibility::Visible : Xaml::Visibility::Collapsed);
	Grid::SetColumn(WorkProgress, 2);
	Footer.Children().Append(WorkProgress);
	Navigation.IsEnabled(!Working);
	Grid::SetRow(Footer, 1);
	Root.Children().Append(Footer);
	Window.Content(Root);
	if (!NativeDefaults)
	{
		Navigation.IsPaneVisible(false);
		Navigation.Content(BuildBaselinePage());
		return;
	}
	if (CurrentPage < 2)
		Navigation.SelectedItem(Navigation.MenuItems().GetAt(CurrentPage));
	else
		Navigation.SelectedItem(Navigation.SettingsItem());
	if (!Navigation.Content())
		ShowPage(CurrentPage);
}
void EditorWindow::ShowPage(int Index)
{
	auto Timing = Performance.Measure("Navigation.ShowPage", std::to_string(Index));
	if (!NativeDefaults)
	{
		Navigation.Content(BuildBaselinePage());
		return;
	}
	Index = std::clamp(Index, 0, 2);
	if (CurrentPage == Index && PageCache[Index])
		return;
	RequireTextEdits();
	DeactivateArmor();
	if (!PageHost)
	{
		PageHost = Grid();
		Navigation.Content(PageHost);
	}
	if (PageCache[CurrentPage])
		PageCache[CurrentPage].Visibility(Xaml::Visibility::Collapsed);
	CurrentPage = Index;
	bool Reused = bool(PageCache[Index]);
	if (!PageCache[Index])
	{
		if (Index == 0)
			PageCache[Index] = BuildVehiclePage();
		else if (Index == 1)
			PageCache[Index] = BuildSavePage();
		else
			PageCache[Index] = BuildSettingsPage();
		PageHost.Children().Append(PageCache[Index]);
	}
	PageCache[Index].Visibility(Xaml::Visibility::Visible);
	if (Reused && Index == 0 && CurrentVehicleTab == 2)
		ActivateVehicleTab(2);
}
void EditorWindow::ResetPageCache()
{
	DeactivateArmor();
	++ParameterGeneration;
	ParameterBindings.clear();
	if (Navigation)
		Navigation.Content(nullptr);
	if (PageHost)
		PageHost.Children().Clear();
	PageHost = nullptr;
	PageCache.fill(nullptr);
	WriteControls.clear();
	SaveTabs = nullptr;
	SaveTabDirty.fill(true);
	SavePath = nullptr;
	GarageList = AddList = CrewList = SaveFieldList = nullptr;
	CurrencyInputs.clear();
	VehicleTabs = nullptr;
	ParameterItem = XmlItem = ArmorItem = nullptr;
	VehicleList = nullptr;
	SelectedXmlNode = nullptr;
	XmlTree = nullptr;
	XmlValue = nullptr;
	TreeAddresses.clear();
}
IAsyncAction EditorWindow::ScanGame()
{
	auto Lifetime = shared_from_this();
	if (Working)
		co_return;
	try
	{
		RequireTextEdits();
		if (!XmlDrafts.empty())
			throw std::runtime_error(String(Tr("Xml.ApplyBeforeReload")));
		DeactivateArmor();
		auto Path = FileSystem::path(GamePath.Text().c_str());
		if (!FileSystem::is_regular_file(Path / L"paths.xml"))
			throw std::runtime_error(String(Tr("Vehicle.InvalidGame")));
		SetWorking(true, "Vehicle.Loading");
		CancelWork = false;
		apartment_context UiThread;
		auto Loaded = std::make_unique<Catalog>();
		VehicleWorkspace LoadedData;
		CrewCatalog LoadedCrew;
		FileSystem::path Overrides;
		auto CacheFolder = Preferences.CacheFolder;
		auto Queue = Root.DispatcherQueue();
		auto LoadingCaption = Tr("Vehicle.Loading");
		std::exception_ptr Failure;
		co_await resume_background();
		try
		{
			auto LastReport = std::chrono::steady_clock::now();
			Loaded->Scan(Path,
						 [&](auto const& Detail)
						 {
							 if (CancelWork)
								 throw std::runtime_error("Loading cancelled");
							 auto Now = std::chrono::steady_clock::now();
							 if (Now - LastReport >= std::chrono::milliseconds(100))
							 {
								 LastReport = Now;
								 Queue.TryEnqueue(
									 [Lifetime, LoadingCaption, Detail]
									 { Lifetime->SetStatus(LoadingCaption + L" · " + HString(Detail)); });
							 }
						 });
			LoadedCrew.Scan(*Loaded);
			auto Paths = DataSection::Parse(ReadFile(Path / L"paths.xml"));
			if (auto Entries = Paths.Root.Child("Paths"))
				for (auto const& Entry : Entries->Children)
					if (Entry.Name == "Path" && Lower(Entry.Value).find("res_mod") != std::string::npos)
					{
						Overrides = FileSystem::absolute(Path / Wide(Entry.Value)).lexically_normal();
						break;
					}
			if (Overrides.empty())
				throw std::runtime_error("paths.xml contains no loose modification directory");
			auto Relative = Overrides.lexically_relative(FileSystem::absolute(Path).lexically_normal());
			if (Relative.empty() || Relative.is_absolute() || *Relative.begin() == L"..")
				throw std::runtime_error(
					"The override directory must be inside the selected game installation");
			auto RootName = Lower(PathUtf8(Loaded->Resources.GameRoot));
			auto Cache = CacheFolder /
						 Wide("workspace-" + std::to_string(Crc32(Bytes(RootName.begin(), RootName.end()))) +
							  ".ohcache");
			LoadedData.Open(*Loaded, NativeDefaults.get(), Cache, Overrides);
			if (CancelWork)
				throw std::runtime_error("Loading cancelled");
		}
		catch (...)
		{
			Failure = std::current_exception();
		}
		co_await UiThread;
		if (Failure)
			std::rethrow_exception(Failure);
		GameCatalog = std::move(*Loaded);
		VehicleData = std::move(LoadedData);
		VehicleData.RebindCatalog(GameCatalog);
		CrewNames = std::move(LoadedCrew);
		Preferences.GameFolder = GameCatalog.Resources.GameRoot;
		Preferences.OverrideFolder = Overrides;
		ActiveVehicle = SIZE_MAX;
		ActiveDocument.clear();
		ResetPageCache();
		if (!GameRunning())
			Preferences.Save();
		ShowPage(0);
		SetWorking(false);
		SetStatus(HString(std::to_string(GameCatalog.Vehicles.size())) + Tr("Vehicle.LoadedCount"));
		if (GameRunning())
			SetStatus(Tr("Game.ReadOnly"));
		if (CloseRequested)
			RequestClose();
	}
	catch (...)
	{
		SetWorking(false);
		if (CloseRequested)
			RequestClose();
		else
			Run([] { throw; });
	}
}
std::vector<size_t> EditorWindow::SelectedIndices(ListView const& List) const
{
	std::vector<size_t> Result;
	if (!List)
		return Result;
	for (auto const& Item : List.SelectedItems())
	{
		uint32_t Index{};
		if (List.Items().IndexOf(Item, Index))
			Result.push_back(Index);
	}
	return Result;
}
ScrollViewer EditorWindow::BuildSettingsPage()
{
	auto Page = Stack(false, 16);
	Page.Margin({20, 10, 20, 20});
	Page.Children().Append(Label(Tr("Navigation.Settings"), 26));
	auto Language = Combo({L"简体中文", L"English"}, Preferences.Chinese ? 0 : 1);
	Language.Header(box_value(Tr("Settings.Language")));
	Page.Children().Append(Language);
	auto Theme = Combo({Tr("Settings.System"), Tr("Settings.Light"), Tr("Settings.Dark")}, Preferences.Theme);
	Theme.Header(box_value(Tr("Settings.Theme")));
	Page.Children().Append(Theme);
	TextBox Accent;
	Accent.Header(box_value(Tr("Settings.Accent")));
	wchar_t Hex[9]{};
	swprintf_s(Hex, L"%06X", Preferences.Accent & 0xffffff);
	Accent.Text(Hex);
	Page.Children().Append(Accent);
	Page.Children().Append(Button("Settings.Save",
								  [this, Language, Theme, Accent]
								  {
									  auto Value = Trim(String(Accent.Text()));
									  if (Value.starts_with('#'))
										  Value.erase(0, 1);
									  if (Value.size() != 6 ||
										  Value.find_first_not_of("0123456789abcdefABCDEF") != Value.npos)
										  throw std::runtime_error("Enter a six-digit RRGGBB color");
									  auto Updated = Preferences;
									  Updated.Chinese = Language.SelectedIndex() == 0;
									  Updated.Theme = Theme.SelectedIndex();
									  Updated.Accent = 0xff000000 | uint32_t(std::stoul(Value, nullptr, 16));
									  Updated.Save();
									  Preferences = std::move(Updated);
									  BuildShell();
								  }));
	Page.Children().Append(
		Label(Tr("Settings.CacheLocation") + HString(PathUtf8(Preferences.CacheFolder)), 12));
	return Scroll(Page);
}
fire_and_forget EditorWindow::ApplyVehicleChanges()
{
	auto Lifetime = shared_from_this();
	co_await ChangeGameFiles(false);
}
fire_and_forget EditorWindow::ResetAllVehicles()
{
	auto Lifetime = shared_from_this();
	co_await ChangeGameFiles(true);
}
fire_and_forget EditorWindow::ResetCurrentVehicle()
{
	auto Lifetime = shared_from_this();
	try
	{
		RequireTextEdits();
		if (ActiveVehicle >= GameCatalog.Vehicles.size())
			co_return;
		if (!(co_await Confirm(Tr("Vehicle.ResetSelected"), Tr("Vehicle.ResetSelectedWarning"))))
			co_return;
		VehicleData.ResetVehicle(GameCatalog.Vehicles[ActiveVehicle]);
		ActivateVehicleTab(CurrentVehicleTab);
		SetStatus(Tr("Vehicle.Cached"));
	}
	catch (...)
	{
		Run([] { throw; });
	}
}
void EditorWindow::ExportChanges(bool Selected)
{
	RequireTextEdits();
	if (GameCatalog.Vehicles.empty() || (Selected && ActiveVehicle >= GameCatalog.Vehicles.size()))
		throw std::runtime_error(String(Tr("Vehicle.SelectFirst")));
	auto Path = PickFile(Window, true, L"OffHanger vehicle changes", L"*.ohvehicles",
						 Selected ? L"selected.ohvehicles" : L"modified-vehicles.ohvehicles");
	if (Path.empty())
		return;
	VehicleData.ExportChanges(Path, Selected ? &GameCatalog.Vehicles[ActiveVehicle] : nullptr);
	SetStatus(Tr("Vehicle.Exported"));
}
void EditorWindow::ImportChanges(bool Selected)
{
	RequireTextEdits();
	if (GameCatalog.Vehicles.empty() || (Selected && ActiveVehicle >= GameCatalog.Vehicles.size()))
		throw std::runtime_error(String(Tr("Vehicle.SelectFirst")));
	if (!XmlDrafts.empty())
		throw std::runtime_error(String(Tr("Xml.ApplyBeforeSave")));
	auto Path = PickFile(Window, false, L"OffHanger vehicle changes", L"*.ohvehicles");
	if (Path.empty())
		return;
	VehicleData.ImportChanges(Path, Selected ? &GameCatalog.Vehicles[ActiveVehicle] : nullptr);
	ActivateVehicleTab(CurrentVehicleTab);
	SetStatus(Tr("Vehicle.Imported"));
}
} // namespace OffHanger::Ui
