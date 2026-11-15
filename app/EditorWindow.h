#pragma once
#include "ModelViewport.h"
#include "UiCommon.h"
#include "Barracks.h"
#include <atomic>
#include <array>
namespace OffHanger::Ui
{
class EditorWindow : public std::enable_shared_from_this<EditorWindow>
{
	Settings Preferences;
	UiPerformance Performance;
	Catalog GameCatalog;
	CrewCatalog CrewNames;
	SaveSession SaveData;
	VehicleWorkspace VehicleData;
	std::shared_ptr<DefaultResources const> NativeDefaults;
	std::unique_ptr<ParameterDefinitions> ParameterRules;
	std::atomic_bool CancelWork = false;
	bool Working{}, ClosePromptOpen{}, AllowClose{}, BuildingVehicle{}, CloseRequested{};
	std::string BaselineError;
	struct ParameterBinding
	{
		ParameterInfo Info;
		Controls::TextBox Input{nullptr};
	};
	std::vector<ParameterBinding> ParameterBindings;
	uint64_t ParameterGeneration{};
	Xaml::DispatcherTimer WriteStateTimer{nullptr};
	Xaml::DispatcherTimer EditTimer{nullptr};
	struct PendingTextEdit
	{
		winrt::weak_ref<Controls::TextBox> Input;
		std::function<bool(Controls::TextBox const&)> Commit;
		std::chrono::steady_clock::time_point Changed;
		bool Invalid{};
	};
	std::map<void*, PendingTextEdit> PendingTextEdits;
	bool CommittingText{}, RestoringSession{};
	std::vector<winrt::weak_ref<Controls::Control>> WriteControls;
	bool WasGameRunning{};
	std::array<Xaml::UIElement, 3> PageCache{nullptr, nullptr, nullptr};
	Controls::Grid PageHost{nullptr};
	int CurrentParameterCategory{}, CurrentSaveTab{};
	Controls::Pivot SaveTabs{nullptr};
	std::array<bool, 4> SaveTabDirty{true, true, true, true};
	int CurrentPage{}, CurrentVehicleTab{};
	size_t ActiveVehicle = SIZE_MAX;
	Xaml::Window Window{nullptr};
	Controls::NavigationView Navigation{nullptr};
	Controls::Grid Root{nullptr};
	Controls::TextBox GamePath{nullptr};
	Controls::TextBlock Status{nullptr};
	Controls::ProgressRing Busy{nullptr};
	Controls::ProgressBar WorkProgress{nullptr};
	Controls::Pivot VehicleTabs{nullptr};
	Controls::PivotItem ParameterItem{nullptr}, XmlItem{nullptr}, ArmorItem{nullptr};
	VehicleFilterControls VehicleFilters, GarageFilters, AddFilters;
	VehicleListIndex VehicleListings;
	Controls::ListView VehicleList{nullptr};
	Controls::TextBlock VehicleTitle{nullptr};
	std::vector<size_t> VehicleRows;
	std::vector<winrt::hstring> VehicleLabels;
	struct XmlPending
	{
		std::string Vehicle;
		VehicleValue Field;
		std::string Before;
		NamedAddress Names;
	};
	std::map<std::string, XmlPending> XmlDrafts;
	Controls::ComboBox DocumentList{nullptr};
	std::vector<std::string> VehicleDocuments;
	std::string ActiveDocument;
	Controls::TreeView XmlTree{nullptr};
	std::map<void*, NodeAddress> TreeAddresses;
	Controls::TextBlock XmlPath{nullptr}, XmlHint{nullptr};
	Controls::TextBox XmlValue{nullptr};
	Controls::TreeViewNode SelectedXmlNode{nullptr};
	NodeAddress SelectedXmlAddress;
	bool UpdatingXml{};
	std::shared_ptr<ModelViewport> ModelView;
	std::shared_ptr<Mesh> ActiveMesh;
	Controls::ListView ArmorMaterials{nullptr};
	Controls::TextBlock ArmorInfo{nullptr};
	Controls::TextBox ArmorThickness{nullptr};
	uint64_t ArmorGeneration{};
	std::string LoadedVehicle;
	std::map<uint32_t, VehicleValue> ArmorFields;
	bool UpdatingArmor{};
	Controls::TextBlock SavePath{nullptr};
	Controls::ListView GarageList{nullptr}, AddList{nullptr}, OwnedCrewList{nullptr}, RecruitList{nullptr},
		SaveFieldList{nullptr};
	Controls::TextBox SaveFieldSearch{nullptr}, CrewXp{nullptr};
	Controls::Pivot BarracksTabs{nullptr};
	Controls::ComboBox OwnedTheme{nullptr}, OwnedNation{nullptr}, OwnedClass{nullptr}, OwnedRole{nullptr},
		RecruitTheme{nullptr}, RecruitNation{nullptr}, RecruitRole{nullptr};
	std::vector<std::string> OwnedThemes, RecruitThemes;
	std::vector<OwnedCrew> Roster;
	std::vector<RecruitCandidate> Candidates;
	std::set<std::string> RecruitedKeys;
	std::vector<size_t> OwnedCrewRows, RecruitRows;
	std::vector<OwnedVehicle> GarageInventory;
	uint64_t InventoryRevision = UINT64_MAX, RosterRevision = UINT64_MAX;
	bool BuildingBarracks{};
	Controls::CheckBox AddWithCrew{nullptr};
	Controls::TextBox XpValue{nullptr}, SaveFieldValue{nullptr};
	std::map<std::string, Controls::TextBox> CurrencyInputs;
	std::vector<OwnedVehicle> GarageRows;
	std::vector<size_t> AddRows;
	std::vector<SaveProperty> SaveFields;
	winrt::hstring Tr(char const* Key) const;
	Controls::Button Button(char const* Key, std::function<void()> Action);
	void Run(std::function<void()> const& Action);
	void SetStatus(winrt::hstring const& Message);
	void SetWorking(bool Value, char const* StatusKey = nullptr);
	void BindDeferredEdit(Controls::TextBox const& Input,
						  std::function<bool(Controls::TextBox const&)> Commit);
	bool FlushTextEdits(bool Force = true, void* Only = nullptr);
	void RequireTextEdits();
	IAsyncAction RestoreSession();
	IAsyncAction OpenSavePath(FileSystem::path Path);
	void Log(std::string const& Message) noexcept;
	void ApplyTheme();
	winrt::fire_and_forget RequestClose();
	winrt::fire_and_forget CloseSave();
	winrt::fire_and_forget Message(winrt::hstring Title, winrt::hstring Body);
	IAsyncOperation<bool> Confirm(winrt::hstring Title, winrt::hstring Body);
	void BuildShell();
	void ShowPage(int Index);
	void ResetPageCache();
	Controls::Grid BuildBaselinePage();
	winrt::fire_and_forget ImportBaseline(bool FromGame);
	IAsyncAction ScanGame();
	Controls::Grid BuildVehiclePage();
	void RefreshVehicles();
	void SelectVehicle();
	void ActivateVehicleTab(int Index);
	Controls::Grid BuildParameterPage();
	Xaml::UIElement BuildParameterCategory(std::vector<ParameterInfo> const& Fields, Vehicle const& Vehicle,
										   char const* Category, uint64_t Generation);
	Controls::Grid BuildXmlPage();
	void RefreshXmlTree();
	void ExpandXml(Controls::TreeViewNode const& Parent, NodeAddress const& Address);
	Controls::TreeViewNode XmlNode(NodeAddress const& Address);
	void SelectXml(Controls::TreeViewNode const& Node);
	void ApplyXml();
	winrt::fire_and_forget ResetXml();
	Controls::Grid BuildModelPanel();
	winrt::fire_and_forget LoadModel();
	void DeactivateArmor();
	bool ArmorVisible() const;
	void RefreshModelArmor();
	void UpdateArmorThickness(uint32_t Index);
	winrt::fire_and_forget ResetCurrentVehicle();
	winrt::fire_and_forget ResetAllVehicles();
	winrt::fire_and_forget ApplyVehicleChanges();
	IAsyncAction ChangeGameFiles(bool Reset);
	void ExportChanges(bool Selected);
	void ImportChanges(bool Selected);
	Controls::ScrollViewer BuildSettingsPage();
	Controls::Grid BuildSavePage();
	Controls::Grid BuildInventoryTab();
	Controls::Grid BuildAddTab();
	Controls::Grid BuildCrewTab();
	Controls::Grid BuildSaveFieldsTab();
	void RefreshSave();
	void ActivateSaveTab(int Index);
	void RefreshGarage();
	void RefreshAdd();
	void RefreshCrew();
	void RefreshOwnedCrew();
	void RefreshRecruits();
	winrt::hstring CrewTheme(std::string const& Theme, std::string const& Label) const;
	winrt::hstring CrewRole(int Role) const;
	winrt::hstring CrewClass(int Class) const;
	winrt::fire_and_forget RemoveCrew(bool FromAccount);
	winrt::fire_and_forget RecruitSelectedCrew();
	void EnsureVehicleListings();
	void EnsureGarageInventory();
	winrt::hstring DeletionPreview(std::vector<SaveIssue> const& Plan) const;
	void RefreshSaveFields();
	std::set<int64_t> SelectedGarage() const;
	std::vector<size_t> SelectedIndices(Controls::ListView const& List) const;
	winrt::fire_and_forget LoadSave(bool Locate);
	void WriteSave(bool SaveAs);
	winrt::fire_and_forget DeleteSelected();
	winrt::fire_and_forget Cleanup(CleanupKind Kind);
	winrt::fire_and_forget AddSelected();

  public:
	explicit EditorWindow(Settings Input) : Preferences(std::move(Input)) {}
	void Initialize();
};
} // namespace OffHanger::Ui
