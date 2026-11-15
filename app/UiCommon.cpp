#include "UiCommon.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <psapi.h>

namespace OffHanger::Ui
{
using namespace winrt;
FileSystem::path ApplicationFolder()
{
	wchar_t Buffer[32768];
	auto Length = GetModuleFileNameW(nullptr, Buffer, DWORD(std::size(Buffer)));
	if (!Length || Length >= std::size(Buffer))
		throw std::runtime_error("Cannot locate the application folder");
	return FileSystem::path(std::wstring(Buffer, Length)).parent_path();
}
namespace
{
std::unordered_map<std::string, std::array<hstring, 2>> UiStrings;
}
void Settings::Load()
{
	DataFolder = ApplicationFolder();
	CacheFolder = DataFolder / L"Cache";
	UiStrings.clear();
	for (auto Entry : ReadYaml(DataFolder / L"Config/ui.yaml")["strings"])
		UiStrings.emplace(Entry.first.as<std::string>(),
						  std::array<hstring, 2>{HString(Entry.second["zh"].as<std::string>()),
												 HString(Entry.second["en"].as<std::string>())});
	Chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
	auto Path = DataFolder / L"settings.yaml";
	if (!FileSystem::is_regular_file(Path))
		return;
	auto Data = ReadYaml(Path);
	Chinese = Data["language"].as<std::string>(Chinese ? "zh-CN" : "en-US") == "zh-CN";
	Theme = std::clamp(Data["theme"].as<int>(0), 0, 2);
	ProfileUi = Data["profile_ui"].as<bool>(false);
	Accent = Data["accent"].as<uint32_t>(Accent);
	GameFolder = Wide(Data["game"].as<std::string>(""));
	LastSave = Wide(Data["last_save"].as<std::string>(""));
}
void Settings::Save() const
{
	EnsureWritesAllowed();
	YAML::Node Data;
	Data["schema"] = 2;
	Data["language"] = Chinese ? "zh-CN" : "en-US";
	Data["theme"] = Theme;
	Data["accent"] = Accent;
	Data["game"] = PathUtf8(GameFolder);
	Data["last_save"] = PathUtf8(LastSave);
	if (ProfileUi)
		Data["profile_ui"] = true;
	WriteYaml(DataFolder / L"settings.yaml", Data);
}
UiPerformance::Scope::Scope(UiPerformance& Trace, std::string InputName, std::string InputDetail)
	: Owner(Trace.Enabled ? &Trace : nullptr), Name(std::move(InputName)), Detail(std::move(InputDetail))
{
	if (Owner)
		Started = std::chrono::steady_clock::now();
}
UiPerformance::Scope::~Scope()
{
	if (!Owner)
		return;
	auto Elapsed =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
	PROCESS_MEMORY_COUNTERS_EX Memory{};
	K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&Memory),
							sizeof(Memory));
	try
	{
		if (Owner->Entries.size() < 4096)
			Owner->Entries.push_back({std::move(Name), std::move(Detail), Elapsed, Memory.PrivateUsage});
	}
	catch (...)
	{
	}
}
UiPerformance::Scope UiPerformance::Measure(std::string Name, std::string Detail)
{
	return Scope(*this, std::move(Name), std::move(Detail));
}
void UiPerformance::WatchRetired(Controls::Control const& Control)
{
	if (Enabled && RetiredControls.size() < 4096)
		RetiredControls.push_back(make_weak(Control));
}
void UiPerformance::Flush(FileSystem::path const& Target) const noexcept
{
	if (!Enabled || Entries.empty() || GameRunning())
		return;
	try
	{
		YAML::Node Report;
		Report["format"] = "OffHanger.UiPerformance";
		Report["schema"] = 1;
		Report["retired_controls"] = RetiredControls.size();
		Report["retired_controls_alive"] = std::count_if(RetiredControls.begin(), RetiredControls.end(),
														 [](auto const& Weak) { return bool(Weak.get()); });
		for (auto const& Entry : Entries)
		{
			YAML::Node Item;
			Item["name"] = Entry.Name;
			Item["detail"] = Entry.Detail;
			Item["milliseconds"] = Entry.Milliseconds;
			Item["private_bytes"] = Entry.PrivateBytes;
			Report["samples"].push_back(Item);
		}
		WriteYaml(Target, Report);
	}
	catch (...)
	{
	}
}
hstring Translate(bool Chinese, char const* Key)
{
	auto Item = UiStrings.find(Key);
	if (Item == UiStrings.end())
		throw std::runtime_error(std::string("Missing UI YAML key: ") + Key);
	return Item->second[Chinese ? 0 : 1];
}
hstring HString(std::string const& Utf8)
{
	return hstring(Wide(Utf8));
}
std::string String(hstring const& Text)
{
	return Utf8(Text.c_str());
}
Controls::TextBlock Label(hstring const& Text, double Size)
{
	Controls::TextBlock Result;
	Result.Text(Text);
	Result.FontSize(Size);
	Result.TextWrapping(Xaml::TextWrapping::Wrap);
	return Result;
}
Controls::ListViewItem ListRow(hstring const& Text)
{
	// Equal boxed strings can produce a cyclic WinUI automation sibling chain.
	// A container has reference identity even when two rows display equal text.
	Controls::ListViewItem Item;
	Item.Content(box_value(Text));
	return Item;
}
namespace
{
// Data items have reference identity even if their displayed text is equal.
// ListView creates and recycles only the containers near the viewport.
struct ListText : implements<ListText, Windows::Foundation::IStringable>
{
	hstring Text;
	explicit ListText(hstring Value) : Text(std::move(Value)) {}
	hstring ToString() const
	{
		return Text;
	}
};
} // namespace
void SetListRows(Controls::ListView const& List, std::vector<hstring> const& Rows)
{
	if (!List.ItemTemplate())
	{
		static auto Template =
			Xaml::Markup::XamlReader::Load(
				LR"(<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><TextBlock /></DataTemplate>)")
				.as<Xaml::DataTemplate>();
		List.ItemTemplate(Template);
		List.ContainerContentChanging(
			[](auto const&, Controls::ContainerContentChangingEventArgs const& Event)
			{
				if (Event.InRecycleQueue())
					return;
				auto Item = Event.Item().try_as<Windows::Foundation::IStringable>();
				if (!Item)
					return;
				auto Text = Item.ToString();
				auto Container = Event.ItemContainer();
				Container.ContentTemplateRoot().as<Controls::TextBlock>().Text(Text);
				Xaml::Automation::AutomationProperties::SetName(Container, Text);
				Event.Handled(true);
			});
	}
	std::vector<IInspectable> Items;
	Items.reserve(Rows.size());
	for (auto const& Text : Rows)
		Items.push_back(make<ListText>(Text));
	List.ItemsSource(single_threaded_observable_vector<IInspectable>(std::move(Items)));
}
Controls::Grid BuildVehicleFilters(bool Chinese, VehicleFilterControls& Filters,
								   std::function<void()> Changed, bool Sidebar)
{
	auto Tr = [Chinese](char const* Key) { return Translate(Chinese, Key); };
	auto Page = Rows(Sidebar ? std::initializer_list<Xaml::GridLength>{Auto(), Auto(), Auto(), Auto()}
							 : std::initializer_list<Xaml::GridLength>{Auto(), Auto()});
	Filters.Search = Controls::TextBox();
	Filters.Search.PlaceholderText(Tr("Vehicle.Search"));
	Page.Children().Append(Filters.Search);
	Filters.Nation = Combo({Tr("Filter.AllNations"), L"ussr", L"germany", L"usa", L"china", L"france", L"uk",
							L"japan", L"czech", L"sweden", L"poland", L"italy"});
	std::vector<winrt::hstring> Tiers{Tr("Filter.AllTiers")};
	for (int Tier = 1; Tier <= 11; ++Tier)
		Tiers.push_back(HString(std::to_string(Tier)));
	Filters.Tier = Combo(Tiers);
	Filters.Class = Combo({Tr("Filter.AllClasses"), Tr("Filter.Light"), Tr("Filter.Medium"),
						   Tr("Filter.Heavy"), Tr("Filter.Td"), Tr("Filter.Spg")});
	Filters.Kind = Combo({Tr("Filter.Regular"), Tr("Filter.Gold"), Tr("Filter.Silver"), Tr("Filter.Reward"),
						  Tr("Filter.Unavailable")});
	Filters.Sort = Combo({Tr("Filter.Name"), Tr("Filter.TierAscending"), Tr("Filter.TierDescending")});
	auto AddChoices = [&](std::initializer_list<Controls::ComboBox> Items, int Row)
	{
		auto Choices = Columns({});
		Choices.RowSpacing(10);
		for (auto const& Item : Items)
			Choices.Children().Append(Item);
		ArrangeGridColumns(Choices, int(Items.size()));
		Controls::Grid::SetRow(Choices, Row);
		Page.Children().Append(Choices);
		return Choices;
	};
	if (Sidebar)
	{
		AddChoices({Filters.Nation, Filters.Tier}, 1);
		AddChoices({Filters.Class, Filters.Kind}, 2);
		Controls::Grid::SetRow(Filters.Sort, 3);
		Page.Children().Append(Filters.Sort);
	}
	else
	{
		auto Choices =
			AddChoices({Filters.Nation, Filters.Tier, Filters.Class, Filters.Kind, Filters.Sort}, 1);
		Choices.SizeChanged(
			[](auto const& Sender, Xaml::SizeChangedEventArgs const& Event)
			{
				ArrangeGridColumns(Sender.template as<Controls::Grid>(),
								   Event.NewSize().Width >= 760 ? 5 : 3);
			});
	}
	Filters.Search.TextChanged([Changed](auto&&, auto&&) { Changed(); });
	for (auto const& Filter : {Filters.Nation, Filters.Tier, Filters.Class, Filters.Kind, Filters.Sort})
		Filter.SelectionChanged([Changed](auto&&, auto&&) { Changed(); });
	return Page;
}
VehicleFilter ReadVehicleFilter(VehicleFilterControls const& Filters)
{
	static char const* Kinds[] = {"", "gold", "silver", "reward", "unavailable"};
	return {Lower(String(Filters.Search.Text())),		Kinds[std::clamp(Filters.Kind.SelectedIndex(), 0, 4)],
			Filters.Nation.SelectedIndex() - 1,			std::max(0, Filters.Tier.SelectedIndex()),
			std::max(0, Filters.Class.SelectedIndex()), std::max(0, Filters.Sort.SelectedIndex())};
}
void ArrangeGridColumns(Controls::Grid const& Grid, int Count)
{
	if (Grid.ColumnDefinitions().Size() == uint32_t(Count))
		return;
	Grid.ColumnDefinitions().Clear();
	Grid.RowDefinitions().Clear();
	for (int Index = 0; Index < Count; ++Index)
	{
		Controls::ColumnDefinition Column;
		Column.Width(Star());
		Grid.ColumnDefinitions().Append(Column);
	}
	auto Size = Grid.Children().Size();
	for (uint32_t Index = 0; Index < (Size + Count - 1) / Count; ++Index)
	{
		Controls::RowDefinition Row;
		Row.Height(Auto());
		Grid.RowDefinitions().Append(Row);
	}
	for (uint32_t Index = 0; Index < Size; ++Index)
	{
		auto Child = Grid.Children().GetAt(Index).as<Xaml::FrameworkElement>();
		Controls::Grid::SetColumn(Child, int(Index % Count));
		Controls::Grid::SetRow(Child, int(Index / Count));
	}
}
Controls::StackPanel Stack(bool Horizontal, double Spacing)
{
	Controls::StackPanel Result;
	Result.Orientation(Horizontal ? Controls::Orientation::Horizontal : Controls::Orientation::Vertical);
	Result.Spacing(Spacing);
	return Result;
}
Controls::ScrollViewer Scroll(Xaml::UIElement const& Content)
{
	Controls::ScrollViewer Result;
	Result.Content(Content);
	Result.HorizontalContentAlignment(Xaml::HorizontalAlignment::Stretch);
	Result.VerticalScrollBarVisibility(Controls::ScrollBarVisibility::Auto);
	return Result;
}
Xaml::GridLength Auto()
{
	return {0, Xaml::GridUnitType::Auto};
}
Xaml::GridLength Star(double Weight)
{
	return {Weight, Xaml::GridUnitType::Star};
}
Xaml::GridLength Pixels(double Size)
{
	return {Size, Xaml::GridUnitType::Pixel};
}
Controls::Grid Rows(std::initializer_list<Xaml::GridLength> Heights)
{
	Controls::Grid Result;
	Result.RowSpacing(10);
	for (auto Height : Heights)
	{
		Controls::RowDefinition Definition;
		Definition.Height(Height);
		Result.RowDefinitions().Append(Definition);
	}
	return Result;
}
Controls::Grid Columns(std::initializer_list<Xaml::GridLength> Widths)
{
	Controls::Grid Result;
	Result.ColumnSpacing(12);
	for (auto Width : Widths)
	{
		Controls::ColumnDefinition Definition;
		Definition.Width(Width);
		Result.ColumnDefinitions().Append(Definition);
	}
	return Result;
}
Controls::ComboBox Combo(std::vector<hstring> const& Items, int Selected)
{
	Controls::ComboBox Result;
	Result.HorizontalAlignment(Xaml::HorizontalAlignment::Stretch);
	for (auto const& Item : Items)
		Result.Items().Append(box_value(Item));
	Result.SelectedIndex(Selected);
	return Result;
}
HWND NativeWindow(Xaml::Window const& Window)
{
	HWND Handle{};
	check_hresult(Window.as<IWindowNative>()->get_WindowHandle(&Handle));
	return Handle;
}
FileSystem::path PickFile(Xaml::Window const& Window, bool Save, wchar_t const* FilterName,
						  wchar_t const* Pattern, wchar_t const* DefaultName, bool Folder)
{
	com_ptr<IFileDialog> Dialog;
	if (Save)
		check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
									   IID_PPV_ARGS(Dialog.put())));
	else
		check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
									   IID_PPV_ARGS(Dialog.put())));
	DWORD Options{};
	Dialog->GetOptions(&Options);
	Dialog->SetOptions(Options | FOS_FORCEFILESYSTEM | (Folder ? FOS_PICKFOLDERS : 0));
	if (!Folder)
	{
		COMDLG_FILTERSPEC Filters[] = {{FilterName, Pattern}, {L"All files", L"*.*"}};
		Dialog->SetFileTypes(2, Filters);
		if (DefaultName && *DefaultName)
		{
			FileSystem::path Initial(DefaultName);
			if (Initial.has_parent_path() && FileSystem::is_directory(Initial.parent_path()))
			{
				com_ptr<IShellItem> InitialFolder;
				check_hresult(SHCreateItemFromParsingName(Initial.parent_path().c_str(), nullptr,
														  IID_PPV_ARGS(InitialFolder.put())));
				check_hresult(Dialog->SetDefaultFolder(InitialFolder.get()));
			}
			check_hresult(Dialog->SetFileName(Initial.filename().c_str()));
		}
	}
	auto Result = Dialog->Show(NativeWindow(Window));
	if (Result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		return {};
	check_hresult(Result);
	com_ptr<IShellItem> Item;
	check_hresult(Dialog->GetResult(Item.put()));
	PWSTR Name{};
	check_hresult(Item->GetDisplayName(SIGDN_FILESYSPATH, &Name));
	FileSystem::path Path(Name);
	CoTaskMemFree(Name);
	return Path;
}
int64_t NonnegativeInteger(hstring const& Text)
{
	auto Value = Trim(String(Text));
	size_t End{};
	auto Result = std::stoll(Value, &End);
	if (End != Value.size() || Result < 0)
		throw std::runtime_error("Enter a nonnegative integer");
	return Result;
}
FileSystem::path DefaultSavePath()
{
	PWSTR Folder{};
	check_hresult(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &Folder));
	FileSystem::path Result = FileSystem::path(Folder) / L"Wargaming.net/WorldOfTanks/offhangar2/account.dat";
	CoTaskMemFree(Folder);
	return Result;
}
FileSystem::path SavePathInFolder(FileSystem::path const& Folder)
{
	for (auto Name : {L"account.dat", L"garage_state.json"})
		if (FileSystem::is_regular_file(Folder / Name))
			return Folder / Name;
	throw std::runtime_error("The selected folder contains neither account.dat nor garage_state.json");
}
} // namespace OffHanger::Ui
