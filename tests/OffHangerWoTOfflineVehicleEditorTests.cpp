#include "VehicleWorkspace.h"
#include "VehicleParameters.h"
#include "VehicleFactory.h"
#include "SaveSession.h"
#include "Geometry.h"
#include "Crew.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <windows.h>
using namespace OffHanger;
namespace
{
size_t Assertions{};
FileSystem::path Output;
void Check(bool Value, char const* Message)
{
	++Assertions;
	if (!Value)
		throw std::runtime_error(Message);
}
template <class Action> void Reject(Action const& Function, char const* Message)
{
	bool Failed{};
	try
	{
		Function();
	}
	catch (std::exception const&)
	{
		Failed = true;
	}
	Check(Failed, Message);
}
void Add(Catalog& Catalogue, std::string const& Path, std::string const& Xml)
{
	Resource Source;
	Source.Path = Path;
	Source.MemoryData = std::make_shared<Bytes const>(Xml.begin(), Xml.end());
	Catalogue.Resources.Add(std::move(Source));
}
Catalog Fixture()
{
	Catalog Catalogue;
	for (auto Model : {"A", "B"})
	{
		Vehicle Vehicle;
		Vehicle.Model = Model;
		Vehicle.Nation = "ussr";
		Vehicle.Name = "ussr:" + Vehicle.Model;
		Vehicle.NationId = 0;
		Vehicle.Id = Model[0];
		Vehicle.TypeCd = (Vehicle.Id << 8) | 1;
		Vehicle.Tier = 8;
		Vehicle.Listed = true;
		Vehicle.Enabled = true;
		Vehicle.DefinitionPath = "scripts/item_defs/vehicles/ussr/" + Vehicle.Model + ".xml";
		Vehicle.ListPath = "scripts/item_defs/vehicles/ussr/list.xml";
		Add(Catalogue, Vehicle.DefinitionPath,
			"<root><speedLimits><forward>40</forward><backward>15</backward></speedLimits>"
			"<offlinePatch><required>1</required></offlinePatch><hull><maxHealth>1200</maxHealth>"
			"<armor><armor_1>100</armor_1></armor></hull><engines><engineA>shared</engineA></engines>"
			"<chassis><track><terrainResistance>1 1.5 2</terrainResistance></track></chassis>"
			"<turrets0><turret><guns><gun><reloadTime>10</reloadTime></gun></guns></turret></turrets0></"
			"root>");
		Catalogue.Vehicles.push_back(Vehicle);
	}
	Add(Catalogue, "scripts/item_defs/vehicles/ussr/list.xml",
		"<root><A><id>65</id><level>8</level><price>1000<gold/></price><tags>mediumTank "
		"premium</tags></A><B><id>66</id><level>8</level><price>2000</price><tags>mediumTank</tags></B></"
		"root>");
	Add(Catalogue, "scripts/item_defs/vehicles/ussr/components/engines.xml",
		"<root><shared><engineA><power>600</power><fireProbability>0.2</fireProbability></"
		"engineA><unrelated><power>900</power></unrelated></shared></root>");
	return Catalogue;
}
void TestFormats()
{
	auto Ui = ReadYaml(FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) / "assets/Config/ui.yaml");
	for (auto Entry : Ui["strings"])
		Check(Entry.second["zh"].IsScalar() && Entry.second["en"].IsScalar(),
			  "A bilingual UI entry is invalid");
	auto Root = Value::Make(ValueKind::Dictionary);
	auto Shared = Value::MakeList();
	Shared->Items = {Value::Number(INT64_MIN), Value::Number(INT64_MAX)};
	Root->Set("a", Shared);
	Root->Set("b", Shared);
	Root->Set("binary", Value::FromBytes(Bytes{0, 255, 128}));
	auto Restored = ReadPickle(WritePickle(Root));
	Check(ValueEqual(Root, Restored) && Restored->Get("a") == Restored->Get("b"),
		  "Pickle values or aliases changed");
	std::string Text = "<root><n>100</n><vectors>1 2 3</vectors></root>";
	auto Definition = DataSection::Parse(Bytes(Text.begin(), Text.end()));
	Definition.Packed = true;
	Check(DataSection::Parse(Definition.Serialize()).Xml() == Definition.Xml(),
		  "Packed XML roundtrip failed");
	auto Path = Output / "hash.txt";
	WriteText(Path, "abc");
	Check(Sha256File(Path) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		  "SHA256 mismatch");
	Reject([&] { NormalizeResourcePath("../escape.xml"); }, "Resource traversal accepted");
	SaveSession Save;
	Save.Account.Root = Value::Make(ValueKind::Dictionary);
	Save.Account.Root->Set("credits", Value::Number(100));
	Reject(
		[&]
		{
			Save.Execute("invalid",
						 [](SaveGame& Account)
						 {
							 Account.SetCurrency("credits", 300);
							 Account.SetCurrency("gold", -1);
						 });
		},
		"Invalid save batch accepted");
	Check(Save.Account.Currency("credits") == 100, "Failed save batch did not roll back");
}
void TestDefinitionStructure()
{
	std::string Xml = "<root flag='kept'><values><integer>-129</integer>"
					  "<vector>1.25 -2 0.03125</vector><boolean>true</boolean>"
					  "<binary>AP+A</binary></values><same>one</same><same>two</same><empty/></root>";
	auto Document = DataSection::Parse(Bytes(Xml.begin(), Xml.end()));
	auto& Values = *Document.Root.Child("values");
	for (auto const& [Name, Type] : std::vector<std::pair<std::string, int>>{
			 {"integer", 2}, {"vector", 3}, {"boolean", 4}, {"binary", 5}})
	{
		auto& Node = *Values.Child(Name);
		Node.ValueType = Type;
		Node.Edited = true;
	}
	std::function<void(Section const&, SectionStructure const&)> Compare =
		[&](auto const& Full, auto const& Shape)
	{
		Check(Full.Name == Shape.Name && Full.ValueType == Shape.ValueType &&
				  Full.Children.size() == Shape.Children.size(),
			  "Structural parsing changed node identities or addresses");
		for (size_t Index = 0; Index < Full.Children.size(); ++Index)
			Compare(Full.Children[Index], Shape.Children[Index]);
	};
	for (bool Packed : {false, true})
	{
		Document.Packed = Packed;
		auto Data = Document.Serialize();
		Compare(DataSection::Parse(Data).Root, DataSection::ParseStructure(Data));
		Data.resize(Data.size() / 2);
		Reject([&] { DataSection::ParseStructure(Data); }, "Truncated structural XML was accepted");
	}
	for (auto const& [Type, Length] : std::vector<std::pair<uint32_t, uint32_t>>{{2, 3}, {3, 2}, {7, 0}})
	{
		Bytes Invalid;
		Put32(Invalid, 0x62a14e45);
		Invalid.insert(Invalid.end(), {0, 0});
		Put16(Invalid, 0);
		Put32(Invalid, (Type << 28) | Length);
		Invalid.resize(Invalid.size() + Length);
		Reject([&] { DataSection::Parse(Invalid); }, "Invalid packed scalar was accepted");
		Reject([&] { DataSection::ParseStructure(Invalid); },
			   "Structural parsing bypassed packed scalar validation");
	}
}
void TestResourceIndexPaths()
{
	auto Root = Output / "resource-index";
	auto ResourcePath = std::string("scripts/item_defs/vehicles/ussr/A.xml");
	auto Override = Root / "res_mods/current" / Wide(ResourcePath);
	std::string Current = "<root><hull><maxHealth>100</maxHealth></hull></root>";
	WriteText(Root / "paths.xml", "<root><Paths><Path>./res_mods/current/../current</Path></Paths></root>");
	WriteText(Root / "res" / Wide(ResourcePath), "<root>original</root>");
	WriteText(Override, Current);
	WriteText(Root / "res_mods/current/Extra/extension.xml", "<root><IsEnabled>true</IsEnabled></root>");
	WriteText(Root / "res_mods/current/Extra" / Wide(ResourcePath), Current);
	ResourceIndex Index;
	Index.Scan(Root);
	Check(Index.Find(ResourcePath) &&
			  Index.Find(ResourcePath)->Read() == Bytes(Current.begin(), Current.end()),
		  "Lexical resource paths changed override precedence");
	Check(Index.Find("Extra/extension.xml") && Index.Find("Extra/" + ResourcePath),
		  "An extension's virtual resource paths were changed");
	Check(Index.LoadedSignature == ResourceIndex::GameSignature(FileSystem::relative(Root)),
		  "Relative and absolute game roots produced different resource signatures");
	auto Previous = Index.LoadedSignature;
	Current += "<!-- changed -->";
	WriteText(Override, Current);
	Check(ResourceIndex::GameSignature(Root) != Previous,
		  "Resource signature stopped detecting changed files");
	Index.Scan(Root);
	Check(Index.Find(ResourcePath)->Read() == Bytes(Current.begin(), Current.end()),
		  "Reloading a resource index retained stale override content");
}
void TestWorkspace()
{
	auto Catalogue = Fixture();
	auto const& Vehicle = Catalogue.Vehicles[0];
	auto Root = Output / "machine-one", OtherRoot = Output / "machine-two";
	auto Target = Root / "game-overrides" / Wide(Vehicle.DefinitionPath);
	auto SourceBytes = Catalogue.Resources.Find(Vehicle.DefinitionPath)->Read();
	auto Before = std::string("<!--offline patch stays intact-->\n") +
				  std::string(SourceBytes.begin(), SourceBytes.end());
	WriteText(Target, Before);
	Add(Catalogue, Vehicle.DefinitionPath, Before);
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, Root / "Cache/workspace.ohcache", Root / "game-overrides");
	auto Fields = Workspace.Values(Vehicle);
	auto Find = [&](std::string const& Suffix) -> VehicleValue
	{
		for (auto const& Field : Workspace.Values(Vehicle))
			if (Field.DisplayPath.ends_with(Suffix))
				return Field;
		throw std::runtime_error("Fixture field not found: " + Suffix);
	};
	auto Speed = Find("/speedLimits/forward"), Power = Find("/engineA/power");
	Check(Power.SharedVehicles.size() == 2, "Shared module users were not resolved");
	Check(std::none_of(Fields.begin(), Fields.end(), [](auto const& Field)
					   { return Field.DisplayPath.find("unrelated") != std::string::npos; }),
		  "Unrelated shared module is visible");
	Workspace.Edit(Vehicle, Speed.Document, Speed.Address, "55");
	Workspace.Edit(Vehicle, Power.Document, Power.Address, "750");
	Check(!FileSystem::exists(Root / "Cache/workspace.ohcache"), "Pending edits were persisted to disk");
	Reject([&] { Workspace.Edit(Vehicle, Power.Document, Power.Address, "nan"); },
		   "Invalid numeric value accepted");
	Reject([&] { Workspace.Edit(Vehicle, Vehicle.ListPath, {1, 1}, "11"); },
		   "Another vehicle's metadata was editable");
	Workspace.ExportChanges(Output / "all.ohvehicles");
	Workspace.ExportChanges(Output / "one.ohvehicles", &Vehicle);
	VehicleWorkspace Imported;
	Imported.Open(Catalogue, nullptr, OtherRoot / "Cache/workspace.ohcache", OtherRoot / "game-overrides");
	Check(Imported.ImportChanges(Output / "one.ohvehicles", &Vehicle) == 2,
		  "Cross-machine import changed field count");
	Check(Imported.Get(Speed.Document).Current.Root.Child("speedLimits")->Text("forward") == "55",
		  "Imported speed differs");
	Reject([&] { Imported.ImportChanges(Output / "one.ohvehicles", &Catalogue.Vehicles[1]); },
		   "Wrong-vehicle import accepted");
	Imported.Edit(Vehicle, Speed.Document, Speed.Address, "99");
	Reject([&] { Imported.ImportChanges(Output / "one.ohvehicles", &Vehicle); },
		   "Conflicting target value accepted");
	Check(Imported.Get(Power.Document).Current.Root.Child("shared")->Child("engineA")->Text("power") == "750",
		  "Rejected import partly altered shared data");
	Workspace.SaveToGame();
	Check(!Workspace.HasPending() && Workspace.AppliedCount() == 2,
		  "Applied changes disappeared from tracking");
	Workspace.ExportChanges(Output / "after-apply.ohvehicles", &Vehicle);
	VehicleWorkspace Restarted;
	Restarted.Open(Catalogue, nullptr, Root / "Cache/workspace.ohcache", Root / "game-overrides");
	Check(!Restarted.HasPending() && Restarted.AppliedCount() == 2, "Applied ledger did not survive restart");
	auto Saved = ReadFile(Target);
	WriteText(Target, "external change");
	Reject([&] { Restarted.ResetAll(); }, "Reset overwrote an external change");
	WriteFile(Target, Saved);
	Restarted.ResetAll();
	Check(ReadFile(Target) == Bytes(Before.begin(), Before.end()),
		  "Global reset did not restore exact patch bytes");
	Check(!FileSystem::exists(Root / "game-overrides" / Wide(Power.Document)),
		  "Global reset did not remove an editor-created override");
	Restarted.SetPriceCurrency(Vehicle, false);
	Check(!Restarted.PriceCurrency(Vehicle), "Controlled price currency edit failed");
	Restarted.ExportChanges(Output / "currency.ohvehicles", &Vehicle);
	ParameterDefinitions Definitions(FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) /
									 "assets/Config/parameters.yaml");
	auto Class =
		Definitions.Classify(Vehicle, Restarted.Document(Vehicle.ListPath).Current.Root.Child(Vehicle.Model));
	Check(Class.Kind == "reward", "Premium reward classification is incorrect");
	auto Parameters = Definitions.Build(Restarted, Vehicle, true);
	Check(std::any_of(Parameters.begin(), Parameters.end(),
					  [](auto const& Item) { return Item.Category == "engine"; }),
		  "Engine form fields missing");
	Workspace.Edit(Vehicle, Speed.Document, Speed.Address, "77");
	Reject([&] { Workspace.SaveToGame(); }, "An older editor instance overwrote a newer cache");
	auto LockPath = Root / "Cache/workspace.ohcache.lock";
	auto Lock = CreateFileW(LockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
							FILE_ATTRIBUTE_NORMAL, nullptr);
	Check(Lock != INVALID_HANDLE_VALUE, "Cannot acquire the fixture cache lock");
	Reject([&] { Restarted.SaveToGame(); }, "Two writers entered the same cache transaction");
	CloseHandle(Lock);
	Restarted.SaveToGame();
	Reject(
		[&]
		{
			VehicleWorkspace WrongDirectory;
			WrongDirectory.Open(Catalogue, nullptr, Root / "Cache/workspace.ohcache",
								OtherRoot / "game-overrides");
		},
		"A local cache was restored into another game directory");
}
void TestSharedComponentDependencies()
{
	auto Catalogue = Fixture();
	for (auto const& Vehicle : Catalogue.Vehicles)
		Add(Catalogue, Vehicle.DefinitionPath,
			"<root><hull><maxHealth>1200</maxHealth></hull>"
			"<engines><engineA>shared</engineA></engines>"
			"<turrets0><turret><guns><start>shared</start></guns></turret></turrets0></root>");
	auto Guns = std::string("scripts/item_defs/vehicles/ussr/components/guns.xml");
	Add(Catalogue, Guns,
		"<root><shared>"
		"<early><guns><late>shared</late></guns><shots><shellEarly>shared</shellEarly></shots></early>"
		"<start><reloadTime>11</reloadTime><guns><early>shared</early><late>shared</late></guns></start>"
		"<late><guns><deep>shared</deep></guns><shots><shellLate>shared</shellLate></shots></late>"
		"<deep><guns><start>shared</start></guns></deep>"
		"<unrelated><reloadTime>99</reloadTime></unrelated></shared>"
		"<start><reloadTime>22</reloadTime></start></root>");
	Add(Catalogue, "scripts/item_defs/vehicles/ussr/components/shells.xml",
		"<root><shared><shellEarly><damage>100</damage></shellEarly>"
		"<shellLate><damage>200</damage></shellLate>"
		"<unrelated><damage>999</damage></unrelated></shared></root>");
	for (auto& [Key, Resource] : Catalogue.Resources.Files)
	{
		auto Document = DataSection::Parse(Resource.Read());
		Document.Packed = true;
		Resource.MemoryData = std::make_shared<Bytes const>(Document.Serialize());
	}
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, {}, Output / "component-dependencies");
	for (auto const& Vehicle : Catalogue.Vehicles)
	{
		auto const& Scopes = Workspace.VehicleScopes(Vehicle);
		Check(Scopes.size() == 10, "Transitive component dependencies are incomplete or duplicated");
		std::set<std::pair<std::string, NodeAddress>> Unique;
		std::set<NodeAddress> StartAddresses;
		for (auto const& Scope : Scopes)
		{
			Check(Unique.emplace(Scope.Path, Scope.Root).second, "A cyclic reference duplicated a scope");
			Check(Scope.Module != "unrelated", "An unreferenced shared component was included");
			if (Scope.Path == Guns && Scope.Module == "start")
				StartAddresses.insert(Scope.Root);
			if (Scope.Component == "guns" || Scope.Component == "shells")
				Check(Workspace.SharedUsers(Scope).size() == 2,
					  "Transitive shared-component ownership is incomplete");
		}
		Check(StartAddresses == std::set<NodeAddress>{{1}, {0, 1}},
			  "Same-named root and shared modules were incorrectly merged");
		auto Fields = Workspace.Values(Vehicle);
		for (auto const& [Path, Value] :
			 std::vector<std::pair<std::string, std::string>>{{"/start/reloadTime", "22"},
															  {"/shared/start/reloadTime", "11"},
															  {"/shared/shellEarly/damage", "100"},
															  {"/shared/shellLate/damage", "200"}})
			Check(std::any_of(Fields.begin(), Fields.end(), [&](auto const& Field)
							  { return Field.DisplayPath == Path && Field.Value == Value; }),
				  "An indirectly referenced component field is missing");
	}
}
YAML::Node ReadManifest(FileSystem::path const& Path, std::string const& Name)
{
	Archive Package(Path);
	for (auto const& Entry : Package.Entries)
		if (Entry.Name == Name)
		{
			auto Data = Package.Read(Entry);
			return YAML::Load(std::string(Data.begin(), Data.end()));
		}
	throw std::runtime_error("Missing test manifest");
}
void RewriteManifest(FileSystem::path const& Path, std::string const& Name, YAML::Node const& Manifest)
{
	Archive Package(Path);
	std::vector<std::pair<std::string, Bytes>> Entries;
	for (auto const& Entry : Package.Entries)
		if (Entry.Name != Name)
			Entries.emplace_back(Entry.Name, Package.Read(Entry));
	auto Text = EmitYaml(Manifest);
	Entries.emplace_back(Name, Bytes(Text.begin(), Text.end()));
	WriteZip(Path, Entries);
}
void TestInvalidPackages()
{
	auto Catalogue = Fixture();
	auto const& Vehicle = Catalogue.Vehicles.front();
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, {}, Output / "invalid-import");
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "65");
	auto Package = Output / "validate.ohvehicles";
	Workspace.ExportChanges(Package, &Vehicle);
	auto Original = ReadManifest(Package, "vehicle-changes.yaml");
	VehicleWorkspace Target;
	Target.Open(Catalogue, nullptr, {}, Output / "import-validation-target");
	auto Initial = Target.Document(Vehicle.DefinitionPath).Current.Serialize();
	auto Invalid = [&](std::function<void(YAML::Node)> const& Mutate)
	{
		auto Manifest = YAML::Clone(Original);
		Mutate(Manifest);
		RewriteManifest(Package, "vehicle-changes.yaml", Manifest);
		Reject([&] { Target.ImportChanges(Package, &Vehicle); }, "Invalid package accepted");
		Check(Target.Get(Vehicle.DefinitionPath).Current.Serialize() == Initial,
			  "Invalid package changed working data");
	};
	Invalid([](auto Manifest) { Manifest["schema"] = 999; });
	Invalid([](auto Manifest) { Manifest["vehicles"][0]["type_cd"] = 1; });
	Invalid([](auto Manifest) { Manifest["vehicles"][0]["nation"] = "germany"; });
	Invalid([](auto Manifest) { Manifest["vehicles"][0]["values"][0]["document"] = "../escape.xml"; });
	Invalid([](auto Manifest) { Manifest["vehicles"][0]["values"][0]["address"][0]["name"] = "clip"; });
	Invalid([](auto Manifest) { Manifest["vehicles"][0]["values"][0]["after"] = "-8"; });
	Invalid([](auto Manifest) { Manifest["vehicles"][0]["values"][0]["after"] = "nan"; });
	Invalid([](auto Manifest) { Manifest["vehicles"].push_back(YAML::Clone(Manifest["vehicles"][0])); });
	WriteText(Package, "invalid ZIP");
	Reject([&] { Target.ImportChanges(Package, &Vehicle); }, "A non-ZIP package was accepted");
	auto Copy = Vehicle;
	Copy.Model += "_R";
	ParameterDefinitions Definitions(FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) /
									 "assets/Config/parameters.yaml");
	Check(Definitions.Classify(Copy).Kind == "rental" && Definitions.Classify(Copy).HiddenByDefault,
		  "Rental copies are not hidden by default");
	Copy.Model = "A_bob";
	Check(Definitions.Classify(Copy).Kind == "other" && Definitions.Classify(Copy).HiddenByDefault,
		  "Bob copies are not hidden by default");
	Copy.Model = "A_test";
	Check(Definitions.Classify(Copy).HiddenByDefault, "Test copies are not hidden by default");
}
void TestInterruptedWrites()
{
	auto Catalogue = Fixture();
	auto const& Vehicle = Catalogue.Vehicles.front();
	auto Root = Output / "interrupted", Cache = Root / "Cache/working.ohcache";
	auto Overrides = Root / "game";
	auto Before = Catalogue.Resources.Find(Vehicle.DefinitionPath)->Read();
	WriteFile(Overrides / Wide(Vehicle.DefinitionPath), Before);
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, Cache, Overrides);
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "60");
	auto Fields = Workspace.Values(Vehicle);
	auto Power = *std::find_if(Fields.begin(), Fields.end(), [](auto const& Field)
							   { return Field.DisplayPath.ends_with("/engineA/power"); });
	Workspace.Edit(Vehicle, Power.Document, Power.Address, "700");
	Workspace.SaveToGame();
	// Simulate termination after journaling all writes but before one replacement.
	auto SaveManifest = ReadManifest(Cache, "workspace.yaml");
	SaveManifest["apply_in_progress"] = true;
	RewriteManifest(Cache, "workspace.yaml", SaveManifest);
	FileSystem::remove(Overrides / Wide(Power.Document));
	VehicleWorkspace ResumeSave;
	ResumeSave.Open(Catalogue, nullptr, Cache, Overrides);
	Check(!ResumeSave.HasPending(), "An interrupted application restored unapplied drafts");
	Reject([&] { ResumeSave.SaveToGame(); }, "An incomplete application allowed another write");
	ResumeSave.ResetAll();
	ResumeSave.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "60");
	ResumeSave.Edit(Vehicle, Power.Document, Power.Address, "700");
	ResumeSave.SaveToGame();
	ResumeSave.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "80");
	ResumeSave.SaveToGame();
	// Simulate termination after restoring just one file during global reset.
	auto Manifest = ReadManifest(Cache, "workspace.yaml");
	Manifest["reset_in_progress"] = true;
	RewriteManifest(Cache, "workspace.yaml", Manifest);
	WriteFile(Overrides / Wide(Vehicle.DefinitionPath), Before);
	VehicleWorkspace ResumeReset;
	ResumeReset.Open(Catalogue, nullptr, Cache, Overrides);
	Reject([&] { ResumeReset.SaveToGame(); }, "Saving during an interrupted reset was accepted");
	ResumeReset.ResetAll();
	Check(ReadFile(Overrides / Wide(Vehicle.DefinitionPath)) == Before &&
			  !FileSystem::exists(Overrides / Wide(Power.Document)),
		  "Interrupted reset could not complete");
}
Bytes GameCacheFixture(std::string const& Marker = "vehicle data")
{
	auto Root = Value::Make(ValueKind::Dictionary);
	auto Version = Value::Make(ValueKind::Tuple);
	Version->Items.push_back(Value::FromBytes("fixture-client"));
	Root->Set("version", Version);
	auto Entries = Value::MakeList();
	for (auto Name : {"vehicles_list", "vehicles_cache", "gui_items_params_cache", "unrelated"})
	{
		auto Entry = Value::Make(ValueKind::Tuple), Payload = Value::Make(ValueKind::Tuple);
		auto Serializer = Value::Make(ValueKind::Global);
		Serializer->Text = "persistent_data_cache_common.serializers\nWGPickleSerializer";
		Payload->Items = {Value::FromBytes(std::string(Name) == "unrelated" ? "keep this payload" : Marker),
						  Serializer};
		Entry->Items = {Value::FromBytes(Name), Payload};
		Entries->Items.push_back(Entry);
	}
	Root->Set("data", Entries);
	return WritePickle(Root);
}
void TestGameCacheRefresh()
{
	auto Catalogue = Fixture();
	auto Root = Output / "game-cache-refresh";
	// A cache refresh must work without a runnable client or any game scripts.
	WriteText(Root / "win64/WorldOfTanks.exe", "This fixture must never be executed");
	auto Overrides = Root / "res_mods/current";
	auto Cache = Root / "editor/workspace.ohcache";
	Catalogue.Resources.GameRoot = Root;
	auto const& Vehicle = Catalogue.Vehicles.front();
	auto Target = Overrides / Wide(Vehicle.DefinitionPath);
	auto Before = Catalogue.Resources.Find(Vehicle.DefinitionPath)->Read();
	WriteFile(Target, Before);
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, Cache, Overrides);
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "65");
	WriteFile(Root / "data.wgpdc", GameCacheFixture());
	auto Locked = CreateFileW((Root / "data.wgpdc").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
							  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	Check(Locked != INVALID_HANDLE_VALUE, "Cannot lock the fixture game cache");
	try
	{
		Reject([&] { Workspace.SaveToGame(); }, "An unrefreshable game cache was reported as applied");
		Check(ReadFile(Target) == Before && Workspace.HasPending() && !Workspace.AppliedCount(),
			  "Cache refresh failure did not roll back game files and preserve pending edits");
	}
	catch (...)
	{
		CloseHandle(Locked);
		throw;
	}
	CloseHandle(Locked);
	std::vector<std::string> Stages;
	size_t LastCompleted{};
	auto Progress = [&](std::string const& Stage, size_t Completed, size_t Total)
	{
		Check(Completed >= LastCompleted && Completed <= Total, "Apply progress moved backwards");
		LastCompleted = Completed;
		Stages.push_back(Stage);
	};
	Workspace.SaveToGame(Progress);
	Check(!FileSystem::exists(Root / "data.wgpdc"), "Application retained the stale game cache");
	Check(Workspace.LastCacheRefresh().Method == GameCacheMethod::Invalidated,
		  "Vehicle application did not invalidate the game cache");
	Check(std::find(Stages.begin(), Stages.end(), "cache.invalidate") != Stages.end(),
		  "Cache invalidation did not report progress");
	Check(!Workspace.HasPending() && Workspace.AppliedCount() == 1 && Stages.back() == "done",
		  "Successful application did not finish its ledger and progress");
	WriteFile(Root / "data.wgpdc", GameCacheFixture("cache rebuilt by game after applying"));
	Locked = CreateFileW((Root / "data.wgpdc").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
						 FILE_ATTRIBUTE_NORMAL, nullptr);
	auto AppliedBytes = ReadFile(Target);
	try
	{
		Reject([&] { Workspace.ResetAll(); }, "Reset accepted a stale locked game cache");
		Check(ReadFile(Target) == AppliedBytes && Workspace.AppliedCount() == 1,
			  "Failed reset lost its applied files or rollback ledger");
	}
	catch (...)
	{
		CloseHandle(Locked);
		throw;
	}
	CloseHandle(Locked);
	VehicleWorkspace Restarted;
	Restarted.Open(Catalogue, nullptr, Cache, Overrides);
	LastCompleted = 0;
	Restarted.ResetAll(Progress);
	Check(ReadFile(Target) == Before && !FileSystem::exists(Root / "data.wgpdc") &&
			  !FileSystem::exists(Cache) && Stages.back() == "done",
		  "Reset after restart did not restore XML and invalidate the regenerated cache");
	WriteFile(Root / "data.wgpdc", GameCacheFixture("cache after reset"));
	auto UntouchedCache = ReadFile(Root / "data.wgpdc");
	Restarted.SaveToGame();
	Check(ReadFile(Root / "data.wgpdc") == UntouchedCache,
		  "Applying no modifications unnecessarily invalidated the cache");
	Restarted.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "75");
	Restarted.SaveToGame();
	Check(!FileSystem::exists(Root / "data.wgpdc"), "Repeated application kept a stale game cache");
	Restarted.ResetAll();
}
void TestCacheInvalidation()
{
	for (auto const& Initial : {GameCacheFixture("previous cache"), Bytes{'b', 'a', 'd'}, Bytes{}})
	{
		auto Root = Output / ("cache-invalidation-" + std::to_string(Initial.size()));
		WriteFile(Root / "data.wgpdc", Initial);
		WriteText(Root / "unrelated.bin", "preserved data");
		GameDataCache Cache(Root);
		std::vector<std::string> Progress;
		Check(Cache.Refresh([&](auto const& Stage) { Progress.push_back(Stage); }).Method ==
				  GameCacheMethod::Invalidated,
			  "Offline cache invalidation was not used");
		Check(!FileSystem::exists(Root / "data.wgpdc"), "Stale cache was not removed");
		Check(Progress == std::vector<std::string>{"cache.invalidate"},
			  "Cache invalidation did not report its stage");
		Check(!FileSystem::exists(Root / "win64") && !FileSystem::exists(Root / "editor"),
			  "Cache invalidation required or created a game process profile");
		auto Unrelated = ReadFile(Root / "unrelated.bin");
		Check(std::string(Unrelated.begin(), Unrelated.end()) == "preserved data",
			  "Cache invalidation changed an unrelated file");
		Cache.Restore();
		Check(ReadFile(Root / "data.wgpdc") == Initial, "Cache rollback did not restore original bytes");
		Cache.Restore();
		Check(ReadFile(Root / "data.wgpdc") == Initial, "Repeated rollback changed the original cache");
	}
	auto MissingRoot = Output / "cache-already-absent";
	GameDataCache Missing(MissingRoot);
	Check(Missing.Refresh().Method == GameCacheMethod::Invalidated,
		  "An already absent cache was not acknowledged");
	Check(Missing.Refresh().Method == GameCacheMethod::Invalidated, "Repeated cache invalidation failed");
	Missing.Restore();
	Check(!FileSystem::exists(MissingRoot), "Handling an absent cache created files or directories");
	GameDataCache NoGame(FileSystem::path{});
	Check(NoGame.Refresh().Method == GameCacheMethod::Unchanged,
		  "An empty game root affected the current directory");
	auto Root = Output / "cache-conflicts";
	auto Path = Root / "data.wgpdc";
	auto Initial = GameCacheFixture("initial cache");
	auto External = GameCacheFixture("external cache");
	WriteFile(Path, Initial);
	GameDataCache Cache(Root);
	Reject([&] { Cache.Refresh([](auto const&) { throw std::runtime_error("Injected failure"); }); },
		   "Progress failure was ignored");
	Check(ReadFile(Path) == Initial, "A failure before invalidation changed the cache");
	Reject([&] { Cache.Refresh([&](auto const&) { WriteFile(Path, External); }); },
		   "An external change made during progress reporting was deleted");
	Check(ReadFile(Path) == External, "An external cache change was lost");
	WriteFile(Path, Initial);
	auto Locked = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
							  FILE_ATTRIBUTE_NORMAL, nullptr);
	Check(Locked != INVALID_HANDLE_VALUE, "Cannot lock cache after taking its preimage");
	try
	{
		Reject([&] { Cache.Refresh(); }, "A lock acquired after the snapshot was ignored");
		Check(ReadFile(Path) == Initial, "An unsuccessful deletion damaged the cache");
	}
	catch (...)
	{
		CloseHandle(Locked);
		throw;
	}
	CloseHandle(Locked);
	Cache.Refresh();
	WriteFile(Path, External);
	Reject([&] { Cache.Restore(); }, "Rollback overwrote a newly created external cache");
	Check(ReadFile(Path) == External, "Rollback lost external cache bytes");
	Check(SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_READONLY), "Cannot mark cache read-only");
	try
	{
		Reject([&] { GameDataCache ReadOnly(Root); }, "A read-only cache passed preflight");
	}
	catch (...)
	{
		SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_NORMAL);
		throw;
	}
	SetFileAttributesW(Path.c_str(), FILE_ATTRIBUTE_NORMAL);
	auto DirectoryRoot = Output / "cache-is-directory";
	FileSystem::create_directories(DirectoryRoot / "data.wgpdc");
	Reject([&] { GameDataCache Directory(DirectoryRoot); }, "A directory was accepted as the cache");
	Check(FileSystem::is_directory(DirectoryRoot / "data.wgpdc"), "The cache directory was removed");
	auto Data = WritePickle(Value::FromBytes("bounded string"));
	Reject([&] { ReadPickle(Data, 2); }, "Pickle parser ignored its configured string bound");
	Check(ReadPickle(Data, 32)->Text == "bounded string",
		  "Configured pickle string bound rejected valid data");
}
void TestCacheRollbackAfterFinalizationFailure()
{
	auto Catalogue = Fixture();
	auto Root = Output / "cache-finalization-rollback";
	Catalogue.Resources.GameRoot = Root;
	auto const& Vehicle = Catalogue.Vehicles.front();
	auto Target = Root / "res_mods/current" / Wide(Vehicle.DefinitionPath);
	auto InitialXml = Catalogue.Resources.Find(Vehicle.DefinitionPath)->Read();
	WriteFile(Target, InitialXml);
	auto InitialCache = GameCacheFixture("cache before apply");
	WriteFile(Root / "data.wgpdc", InitialCache);
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, Root / "editor/workspace.ohcache", Root / "res_mods/current");
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "65");
	auto FailAfterCache = [](std::string const& Stage, size_t, size_t)
	{
		if (Stage == "finalize")
			throw std::runtime_error("Injected finalization failure after cache refresh");
	};
	Reject([&] { Workspace.SaveToGame(FailAfterCache); }, "Finalization failure was ignored");
	Check(ReadFile(Target) == InitialXml && ReadFile(Root / "data.wgpdc") == InitialCache &&
			  Workspace.HasPending() && !Workspace.AppliedCount(),
		  "Application failure restored XML but lost its matching cache preimage");
	Workspace.SaveToGame();
	auto AppliedXml = ReadFile(Target);
	auto RebuiltCache = GameCacheFixture("cache matching applied XML");
	WriteFile(Root / "data.wgpdc", RebuiltCache);
	Reject([&] { Workspace.ResetAll(FailAfterCache); }, "Reset finalization failure was ignored");
	Check(ReadFile(Target) == AppliedXml && ReadFile(Root / "data.wgpdc") == RebuiltCache &&
			  Workspace.AppliedCount() == 1,
		  "Reset failure did not retain matching applied XML, cache and rollback ledger");
	Workspace.ResetAll();
	Check(ReadFile(Target) == InitialXml, "Reset could not complete after a finalization failure");
	GameDataCache Concurrent(Root);
	auto External = GameCacheFixture("external writer");
	WriteFile(Root / "data.wgpdc", External);
	Reject([&] { Concurrent.Refresh(); }, "An external cache change was overwritten");
	Check(ReadFile(Root / "data.wgpdc") == External, "External cache bytes changed");
}
void TestSevenVersusSevenRentals()
{
	ParameterDefinitions Definitions(FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) /
									 "assets/Config/parameters.yaml");
	auto Catalogue = Fixture();
	auto Vehicle = Catalogue.Vehicles.front();
	for (auto Model : {"A_7x7", "7X7_A_copy", "A_7x7_extra"})
	{
		Vehicle.Model = Model;
		Vehicle.Name = "ussr:" + Vehicle.Model;
		auto Category = Definitions.Classify(Vehicle);
		Check(Category.Kind == "rental" && Category.HiddenByDefault && Category.Availability == "rental",
			  "A 7v7 rental required a base model or a catalogue to be recognized");
	}
	Vehicle.Name = "sweden:S28_UDES_15_16_7x7";
	Vehicle.Model = "S28_UDES_15_16_7x7";
	Check(Definitions.Classify(Vehicle).HiddenByDefault,
		  "Public catalogue evidence exposed a 7v7 rental copy");
}
void TestNativeMechanismValues()
{
	auto Catalogue = Fixture();
	auto const& Vehicle = Catalogue.Vehicles.front();
	auto Data = DataSection::Parse(Catalogue.Resources.Find(Vehicle.DefinitionPath)->Read());
	auto Gun = Data.Root.Child("turrets0")->Child("turret")->Child("guns")->Child("gun");
	Section Clip;
	Clip.Name = "clip";
	Clip.Children = {Section{"count", "3"}, Section{"rate", "30"}};
	Gun->Children.push_back(Clip);
	Add(Catalogue, Vehicle.DefinitionPath, Data.Xml());
	VehicleWorkspace Workspace;
	auto Overrides = Output / "native-mechanism";
	Workspace.Open(Catalogue, nullptr, {}, Overrides);
	auto Values = Workspace.Values(Vehicle);
	auto Rate = *std::find_if(Values.begin(), Values.end(),
							  [](auto const& Value) { return Value.DisplayPath.ends_with("/clip/rate"); });
	Reject([&] { Workspace.Edit(Vehicle, Rate.Document, Rate.Address, "0"); },
		   "Zero native firing rate accepted");
	Workspace.Edit(Vehicle, Rate.Document, Rate.Address, "60");
	Check(ParameterDefinitions::Mechanism(Workspace.Get(Vehicle.DefinitionPath).Current) == "magazine",
		  "Editing a native firing rate changed its mechanism");
	auto Target = Overrides / Wide(Vehicle.DefinitionPath);
	std::string External = "unrecognized external override";
	WriteText(Target, External);
	Reject([&] { Workspace.SaveToGame(); }, "An unrecognized target override was overwritten");
	Check(ReadFile(Target) == Bytes(External.begin(), External.end()),
		  "Rejected save changed the external target");
}
void TestNativeDefaults()
{
	auto Catalogue = Fixture();
	auto const Vehicle = Catalogue.Vehicles.front();
	auto Folder = Output / "default-fixture";
	FileSystem::create_directories(Folder);
	auto Bytes = Catalogue.Resources.Find(Vehicle.DefinitionPath)->Read();
	auto Original = DataSection::Parse(Bytes);
	Original.Root.Child("speedLimits")->Child("forward")->Set("25");
	std::erase_if(Original.Root.Children, [](auto const& Node) { return Node.Name == "offlinePatch"; });
	Original.Packed = true;
	WriteZip(Folder / "scripts.pkg", {{Vehicle.DefinitionPath, Original.Serialize()}});
	YAML::Node Manifest;
	Manifest["format"] = "OffHangarStudio.Defaults";
	Manifest["schema"] = 1;
	Manifest["archive"] = "scripts.pkg";
	Manifest["bytes"] = FileSystem::file_size(Folder / "scripts.pkg");
	Manifest["sha256"] = Sha256File(Folder / "scripts.pkg");
	Manifest["client"] = "fixture";
	WriteYaml(Folder / "manifest.yaml", Manifest);
	DefaultResources Defaults(Folder);
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, &Defaults, {}, Output / "default-overrides");
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "80");
	Workspace.ResetVehicle(Vehicle);
	auto const& Restored = Workspace.Get(Vehicle.DefinitionPath).Current;
	Check(Restored.Root.Child("speedLimits")->Text("forward") == "25", "Native scalar default was not used");
	Check(Restored.Root.Child("offlinePatch")->Text("required") == "1",
		  "Native default reset removed offline patch structure");
	Check(!Restored.Packed, "Native reset replaced the active XML format");
	Check(!Restored.Root.Child("clip"), "Default reset introduced a loading mechanism");
	auto Before = Restored.Serialize();
	Reject([&] { Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "-5"); },
		   "Negative forward speed accepted");
	Check(Workspace.Get(Vehicle.DefinitionPath).Current.Serialize() == Before,
		  "Rejected edit changed cached data");
	auto Other = Workspace.Values(Vehicle);
	auto Terrain = std::find_if(Other.begin(), Other.end(), [](auto const& Field)
								{ return Field.DisplayPath.ends_with("terrainResistance"); });
	Reject([&] { Workspace.Edit(Vehicle, Terrain->Document, Terrain->Address, "1 2"); },
		   "A vector changed component count");
	Reject([&] { Workspace.Edit(Vehicle, Terrain->Document, Terrain->Address, "0 1 2"); },
		   "Zero terrain resistance accepted");
	Workspace.SetPriceCurrency(Vehicle, false);
	Workspace.ExportChanges(Output / "currency-only.ohvehicles", &Vehicle);
	VehicleWorkspace Target;
	Target.Open(Catalogue, &Defaults, {}, Output / "currency-target");
	Target.ImportChanges(Output / "currency-only.ohvehicles", &Vehicle);
	Check(!Target.PriceCurrency(Vehicle), "Currency marker did not import");
	Workspace.ResetDocument(Vehicle.DefinitionPath);
	Check(Workspace.Get(Vehicle.DefinitionPath).Current.Root.Child("speedLimits")->Text("forward") == "40",
		  "XML file reset did not use the patch-aware initial document");
}
void TestParameterPresentation()
{
	ParameterInfo Interval;
	Interval.Transform = ParameterTransform::ShotInterval;
	Check(ParameterDefinitions::DisplayValue(Interval, "30") == "2",
		  "Native rate is not displayed as seconds");
	Check(ParameterDefinitions::StoredValue(Interval, "0.5", "30") == "120",
		  "Short reload did not convert to the native rate");
	Reject([&] { ParameterDefinitions::StoredValue(Interval, "0", "30"); },
		   "A zero firing interval was accepted");
	ParameterInfo Depression;
	Depression.Transform = ParameterTransform::PitchAngles;
	Depression.Components = {1, 5};
	Check(ParameterDefinitions::StoredValue(Depression, "7", "0 5 0.5 1 1 5") == "0 7 0.5 1 1 7",
		  "Depression changed coordinates or other angle groups");
	ParameterInfo Elevation = Depression;
	Elevation.InvertAngleSign = true;
	Elevation.Components = {1, 3};
	Check(ParameterDefinitions::StoredValue(Elevation, "20", "0 -15 1 -15") == "0 -20 1 -20",
		  "Elevation did not preserve its native sign");
	auto Restricted = Depression;
	Restricted.Components = {1, 3};
	Check(ParameterDefinitions::DisplayValue(Restricted, "0 -3 1 -3") == "-3",
		  "A restricted negative depression was displayed with the opposite meaning");
	Check(ParameterDefinitions::StoredValue(Restricted, "-5", "0 -3 1 -3") == "0 -5 1 -5",
		  "Restricted negative depression could not be edited without reversing its sign");
	ParameterInfo Automatic;
	Automatic.Transform = ParameterTransform::TotalReload;
	Check(ParameterDefinitions::DisplayValue(Automatic, "7 8 10") == "25",
		  "Total autoreload duration is incorrect");
	Check(ParameterDefinitions::StoredValue(Automatic, "50", "7 8 10") == "14 16 20",
		  "Total reload did not preserve per-round ratios");
	auto Catalogue = Fixture();
	auto const& Vehicle = Catalogue.Vehicles.front();
	Add(Catalogue, Vehicle.DefinitionPath,
		"<root><mechanics><stanceDance><passiveTurboEnginePowerBonus>0.25</passiveTurboEnginePowerBonus>"
		"<activeFightReloadSpdBonus>0.2</activeFightReloadSpdBonus>"
		"<activeTurboRotationSpeedDebuff>0.15</activeTurboRotationSpeedDebuff></stanceDance>"
		"<concentrationMode><modifiers><mul><name>dynAttrs/engine/power</name><value>1.1</value></mul>"
		"<mul><name>dynAttrs/gun/aimingTime</name><value>0.3</value></mul></modifiers></concentrationMode>"
		"</mechanics></root>");
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, {}, Output / "mechanic-categories");
	auto Config = FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) / "assets/Config/parameters.yaml";
	ParameterDefinitions Definitions(Config);
	auto Fields = Definitions.Build(Workspace, Vehicle, false);
	auto Has = [&](std::string const& Suffix, std::string const& Category, std::string const& Value)
	{
		return std::any_of(Fields.begin(), Fields.end(),
						   [&](auto const& Info)
						   {
							   return Info.Field.DisplayPath.ends_with(Suffix) && Info.Category == Category &&
									  Info.Field.Value == Value;
						   });
	};
	Check(Has("/passiveTurboEnginePowerBonus", "engine", "0.25") &&
			  Has("/activeFightReloadSpdBonus", "gun", "0.2") &&
			  Has("/activeTurboRotationSpeedDebuff", "chassis", "0.15"),
		  "A multi-component mechanic was grouped entirely under its root category");
	Check(Has("/value", "engine", "1.1") && Has("/value", "gun", "0.3"),
		  "Modifier targets did not determine their component categories");
	auto StandaloneConfig = Output / "no-evidence/parameters.yaml";
	WriteFile(StandaloneConfig, ReadFile(Config));
	ParameterDefinitions NoEvidence(StandaloneConfig);
	Check(!NoEvidence.Classify(Vehicle).HiddenByDefault,
		  "Missing optional publication metadata hid a normal vehicle");
}
void TestPendingLifetime()
{
	auto Catalogue = Fixture();
	auto const& Vehicle = Catalogue.Vehicles.front();
	auto Cache = Output / "pending-lifetime/cache.ohcache";
	auto Overrides = Output / "pending-lifetime/game";
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, nullptr, Cache, Overrides);
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "55");
	Check(!FileSystem::exists(Cache), "An unapplied edit created a persistent draft");
	Workspace.DiscardPending();
	Check(!Workspace.HasPending(), "Discarding pending edits left changes behind");
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "55");
	Workspace.SaveToGame();
	auto Ledger = ReadFile(Cache);
	Workspace.Edit(Vehicle, Vehicle.DefinitionPath, {0, 0}, "75");
	Check(ReadFile(Cache) == Ledger, "Unapplied edits altered the rollback ledger");
	VehicleWorkspace Reopened;
	Reopened.Open(Catalogue, nullptr, Cache, Overrides);
	Check(Reopened.Document(Vehicle.DefinitionPath).Current.Root.Child("speedLimits")->Text("forward") ==
				  "55" &&
			  !Reopened.HasPending(),
		  "A restart restored unapplied cached values");
	Workspace.DiscardPending();
	Check(Workspace.Document(Vehicle.DefinitionPath).Current.Root.Child("speedLimits")->Text("forward") ==
			  "55",
		  "Closing discarded already applied values");
	// Older releases persisted pending drafts. Opening them must retain only
	// physical rollback records and must never reapply the pending values.
	auto Manifest = ReadManifest(Cache, "workspace.yaml");
	Manifest["schema"] = 2;
	std::vector<std::pair<std::string, Bytes>> LegacyFiles;
	Archive OldLedger(Cache);
	for (auto const& Entry : OldLedger.Entries)
		if (Entry.Name != "workspace.yaml")
			LegacyFiles.emplace_back(Entry.Name, OldLedger.Read(Entry));
	auto Draft = Workspace.Document(Vehicle.DefinitionPath).Current;
	Draft.Root.Child("speedLimits")->Child("forward")->Set("75");
	LegacyFiles.emplace_back("draft/" + Vehicle.DefinitionPath, Draft.Serialize());
	auto Text = EmitYaml(Manifest);
	LegacyFiles.emplace_back("workspace.yaml", Bytes(Text.begin(), Text.end()));
	WriteZip(Cache, LegacyFiles);
	VehicleWorkspace Migrated;
	Migrated.Open(Catalogue, nullptr, Cache, Overrides);
	Check(!Migrated.HasPending() && Migrated.AppliedCount() == 1,
		  "A legacy draft was restored instead of discarded");
	Check(ReadManifest(Cache, "workspace.yaml")["schema"].as<int>() == 3,
		  "The legacy pending cache was not migrated");
	Archive NewLedger(Cache);
	Check(std::none_of(NewLedger.Entries.begin(), NewLedger.Entries.end(),
					   [](auto const& Entry) { return Entry.Name.starts_with("draft/"); }),
		  "A legacy draft remained in the rollback ledger");
	Manifest["files"][0].remove("applied");
	Text = EmitYaml(Manifest);
	LegacyFiles.back().second = Bytes(Text.begin(), Text.end());
	auto PendingOnly = Output / "pending-lifetime/legacy-pending.ohcache";
	WriteZip(PendingOnly, LegacyFiles);
	VehicleWorkspace PendingLegacy;
	PendingLegacy.Open(Catalogue, nullptr, PendingOnly, Overrides);
	Check(!FileSystem::exists(PendingOnly) && !PendingLegacy.HasPending(),
		  "An unapplied legacy-only cache survived migration");
}
void TestBaselineImport()
{
	auto Root = Output / "baseline-import";
	std::string Xml = "<root><value>1</value></root>";
	Bytes Data(Xml.begin(), Xml.end());
	std::vector<std::pair<std::string, Bytes>> Entries{{"scripts/item_defs/vehicles/ussr/list.xml", Data},
													   {"scripts/item_defs/vehicles/usa/list.xml", Data}};
	for (int Index = 0; Index < 55; ++Index)
		Entries.emplace_back("scripts/item_defs/vehicles/ussr/fixture" + std::to_string(Index) + ".xml",
							 Data);
	auto Source = Root / "input/scripts.pkg";
	auto Target = Root / "editor/script.pkg";
	WriteZip(Source, Entries);
	DefaultResources::Install(Source, Target);
	Check(ReadFile(Source) == ReadFile(Target), "Baseline import did not preserve archive bytes");
	DefaultResources Installed(Target);
	Check(Installed.Count() == Entries.size(), "Imported defaults cannot be reopened");
	WriteText(Target, "tampered");
	Reject([&] { DefaultResources Changed(Target); }, "Tampered user defaults passed integrity validation");
	auto Mod = Root / "mod.pkg";
	WriteZip(Mod, {{"scripts/item_defs/vehicles/ussr/fixture.xml", Data}});
	Reject([&] { DefaultResources::Install(Mod, Root / "wrong/script.pkg"); },
		   "A vehicle Mod was accepted as a complete native baseline");
}
void TestRunningGameGuard()
{
	auto Root = Output / "write-guard";
	auto Target = Root / "target.txt";
	std::string Original = "unchanged";
	WriteText(Target, Original);
	wchar_t Self[32768]{};
	GetModuleFileNameW(nullptr, Self, DWORD(std::size(Self)));
	auto FakeGame = Root / "WorldOfTanks.exe";
	Check(CopyFileW(Self, NativePath(FakeGame).c_str(), false) != 0,
		  "Cannot prepare the process-detection fixture");
	auto EventName = L"Local\\OffHangerWriteGuard-" + std::to_wstring(GetCurrentProcessId());
	auto Event = CreateEventW(nullptr, true, false, EventName.c_str());
	auto Command = L"\"" + FakeGame.wstring() + L"\" --hold-game-guard " + EventName;
	STARTUPINFOW Startup{sizeof(Startup)};
	PROCESS_INFORMATION Process{};
	Check(CreateProcessW(NativePath(FakeGame).c_str(), Command.data(), nullptr, nullptr, false,
						 CREATE_NO_WINDOW, nullptr, nullptr, &Startup, &Process) != 0,
		  "Cannot start the process-detection fixture");
	try
	{
		Check(GameRunning(), "The running game process was not detected");
		Reject([&] { WriteText(Target, "changed"); },
			   "Direct writes were allowed while the game was running");
		Reject([&] { AtomicWrite(Target, Bytes{1, 2, 3}); },
			   "Atomic writes were allowed while the game was running");
		SaveGame Account;
		Account.Root = Value::Make(ValueKind::Dictionary);
		Reject([&] { Account.Save(Root / "save-as.dat"); }, "Save-as was allowed while the game was running");
		Check(ReadFile(Target) == Bytes(Original.begin(), Original.end()),
			  "The game guard altered the original file");
	}
	catch (...)
	{
		SetEvent(Event);
		WaitForSingleObject(Process.hProcess, 10000);
		CloseHandle(Process.hThread);
		CloseHandle(Process.hProcess);
		CloseHandle(Event);
		throw;
	}
	SetEvent(Event);
	WaitForSingleObject(Process.hProcess, 10000);
	CloseHandle(Process.hThread);
	CloseHandle(Process.hProcess);
	CloseHandle(Event);
}
void TestInstallation(FileSystem::path const& Game, FileSystem::path const& SavePath)
{
	Catalog Catalogue;
	Catalogue.Scan(Game);
	Check(Catalogue.Vehicles.size() > 1000, "Installed catalogue is unexpectedly small");
	auto Bundled = FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) / "assets/Defaults/scripts.pkg";
	DefaultResources Defaults(FileSystem::is_regular_file(Bundled) ? Bundled
																   : Game / L"res/packages/scripts.pkg");
	ParameterDefinitions Definitions(FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) /
									 "assets/Config/parameters.yaml");
	VehicleWorkspace Workspace;
	Workspace.Open(Catalogue, &Defaults, {}, Output / "integration-overrides");
	YAML::Node Audit;
	for (auto const& Vehicle : Catalogue.Vehicles)
	{
		Section const* Metadata =
			Vehicle.ListPath.empty() ? nullptr
									 : Workspace.Document(Vehicle.ListPath).Current.Root.Child(Vehicle.Model);
		auto Category = Definitions.Classify(Vehicle, Metadata, &Catalogue);
		auto Record = Audit[Vehicle.Name];
		Record["display"] = Vehicle.DisplayName;
		Record["availability"] = Category.Availability;
		Record["hidden"] = Category.HiddenByDefault;
		Record["kind"] = Category.Kind;
		if (!Category.Source.empty())
			Record["source"] = Category.Source;
	}
	WriteYaml(Output / "vehicle-availability-audit.yaml", Audit);
	std::string MechanicReport = "vehicle\tcategory\tfield\tlabel\n";
	for (auto const& Vehicle : Catalogue.Vehicles)
		if (Vehicle.Tier >= 11 && !Vehicle.DefinitionPath.empty() &&
			Workspace.Document(Vehicle.DefinitionPath).Current.Root.Child("mechanics"))
			for (auto const& Info : Definitions.Build(Workspace, Vehicle, true))
				if (Info.Field.DisplayPath.starts_with("/mechanics/"))
					MechanicReport += Vehicle.Name + "\t" + Info.Category + "\t" + Info.Field.DisplayPath +
									  "\t" + Info.Label + "\n";
	WriteText(Output / "mechanic-coverage.tsv", MechanicReport);
	Check(!Audit["usa:A176_M103_110_CFE_R"]["hidden"].as<bool>(), "Rambo was hidden as a rental copy");
	Check(!Audit["czech:Cz21_Vz_60S"]["hidden"].as<bool>(), "The Vz. 60S base vehicle was hidden");
	Check(Audit["uk:GB12_Conqueror_FL"]["availability"].as<std::string>() == "rental",
		  "Conqueror FL was not classified as a rental");
	VehicleFactory Factory(Catalogue);
	size_t StockCount{}, FailureCount{};
	std::string Failures, ParameterReport = "vehicle\tcategory\tmodule\tfield\tlabel\n";
	for (auto const& Vehicle : Catalogue.Vehicles)
	{
		if (!Vehicle.Enabled || !Vehicle.Listed || Vehicle.Conflict || Vehicle.DefinitionPath.empty())
			continue;
		try
		{
			auto Stock = Factory.Stock(Vehicle);
			Check(VehicleCd(Stock.Descriptor) == Vehicle.TypeCd, "Stock descriptor identity changed");
			Check(Stock.Record()->Get("eqs")->Items.size() == Stock.EquipmentSlots,
				  "Equipment slot count mismatch");
			++StockCount;
		}
		catch (std::exception const& Error)
		{
			++FailureCount;
			Failures += Vehicle.Name + "\t" + Error.what() + "\n";
		}
	}
	WriteText(Output / "stock-failures.tsv", Failures);
	Check(StockCount > 1000 && FailureCount == 0, "Installed vehicle stock data did not validate");
	auto Type59 = Catalogue.ByName("china:Ch01_Type59");
	Check(Type59 != nullptr, "Type 59 is missing");
	auto Fields = Definitions.Build(Workspace, *Type59, true);
	size_t NormalPower{}, PhysicsPower{}, Terrain{};
	std::set<std::string> Categories;
	for (auto const& Info : Fields)
	{
		Categories.insert(Info.Category);
		NormalPower += Info.Field.DisplayPath.ends_with("/power");
		PhysicsPower += Info.Field.DisplayPath.ends_with("/smplEnginePower");
		Terrain += Info.Field.DisplayPath.ends_with("/terrainResistance");
		ParameterReport += Type59->Name + "\t" + Info.Category + "\t" + Info.Field.Module + "\t" +
						   Info.Field.DisplayPath + "\t" + Info.Label + "\n";
		Check(!Info.Help.empty(), "A parameter has no help text");
	}
	auto HasField = [&](std::vector<ParameterInfo> const& Values, std::string const& Ending)
	{
		return std::any_of(Values.begin(), Values.end(),
						   [&](auto const& Info) { return Info.Field.DisplayPath.ends_with(Ending); });
	};
	for (auto Needle : {"R170_IS_2_II", "F10_AMX_50B", "Progetto_M35", "A38_T92", "GB152_AT_FV230_Breaker",
						"Ch19_121", "G185_Leopard_120_Verbessert"})
	{
		auto Sample =
			std::find_if(Catalogue.Vehicles.begin(), Catalogue.Vehicles.end(),
						 [&](auto const& Vehicle) { return Vehicle.Name.find(Needle) != std::string::npos; });
		Check(Sample != Catalogue.Vehicles.end(), "A representative native mechanism vehicle is missing");
		auto SampleFields = Definitions.Build(Workspace, *Sample, true);
		for (auto const& Info : SampleFields)
			ParameterReport += Sample->Name + "\t" + Info.Category + "\t" + Info.Field.Module + "\t" +
							   Info.Field.DisplayPath + "\t" + Info.Label + "\n";
		for (auto const& Scope : Workspace.VehicleScopes(*Sample))
			if (Scope.Component == "vehicle" || Scope.Component == "guns" || Scope.Component == "shells")
			{
				auto const& Data = Workspace.Document(Scope.Path).Current;
				WriteText(Output / "definitions" / Wide(Scope.Path), Data.Xml());
			}
		if (std::string(Needle) == "R170_IS_2_II")
			Check(HasField(SampleFields, "/dualGun/chargeTime") &&
					  HasField(SampleFields, "/dualGun/reloadTimes"),
				  "Native double-gun form values are missing");
		if (std::string(Needle) == "F10_AMX_50B")
			Check(HasField(SampleFields, "/clip/count") && HasField(SampleFields, "/clip/rate"),
				  "Native magazine form values are missing");
		if (std::string(Needle) == "A38_T92")
			Check(std::any_of(SampleFields.begin(), SampleFields.end(), [](auto const& Info)
							  { return Lower(Info.Field.DisplayPath).find("stun") != std::string::npos; }),
				  "Artillery stun form values are missing");
		if (std::string(Needle) == "GB152_AT_FV230_Breaker")
		{
			auto PowerMode = std::find_if(SampleFields.begin(), SampleFields.end(),
										  [](auto const& Info)
										  {
											  return Info.Field.DisplayPath.ends_with(
												  "/mechanics/powerMode/vehicleParams/enginePower");
										  });
			Check(PowerMode != SampleFields.end() && PowerMode->Category == "engine",
				  "Breaker power mode is missing or classified as a gun mechanic");
		}
		if (std::string(Needle) == "Ch19_121")
		{
			// An installed vehicle may already contain applied edits. Exercise its largest
			// depression group without assuming the original five-degree value is still present.
			ParameterInfo const* Depression{};
			for (auto const& Info : SampleFields)
				if (Info.Transform == ParameterTransform::PitchAngles && !Info.InvertAngleSign &&
					Info.Field.Document == Sample->DefinitionPath &&
					Info.Field.DisplayPath.ends_with("/pitchLimits/maxPitch") && !Info.Components.empty() &&
					(!Depression || Info.Components.size() > Depression->Components.size()))
					Depression = &Info;
			Check(Depression != nullptr, "121 depression profile was not recognized");
			auto OriginalAngle =
				std::stod(ParameterDefinitions::DisplayValue(*Depression, Depression->Field.Value));
			auto ChangedAngle = OriginalAngle < 85 ? OriginalAngle + 2 : OriginalAngle - 2;
			auto Stored = ParameterDefinitions::StoredValue(*Depression, std::to_string(ChangedAngle),
															Depression->Field.Value);
			std::istringstream Before(Depression->Field.Value), After(Stored);
			std::vector<double> OldValues, NewValues;
			double Number;
			while (Before >> Number)
				OldValues.push_back(Number);
			while (After >> Number)
				NewValues.push_back(Number);
			Check(OldValues.size() == NewValues.size(), "121 pitch profile changed its shape");
			for (size_t Index = 0; Index < OldValues.size(); ++Index)
				Check(NewValues[Index] == (Index % 2 == 1 && OldValues[Index] == OriginalAngle
											   ? ChangedAngle
											   : OldValues[Index]),
					  "121 depression editing changed coordinates or restricted-angle regions");
		}
		if (std::string(Needle) == "G185_Leopard_120_Verbessert")
			Check(std::any_of(SampleFields.begin(), SampleFields.end(),
							  [](auto const& Info)
							  {
								  return Info.Transform == ParameterTransform::PitchAngles &&
										 !Info.InvertAngleSign &&
										 ParameterDefinitions::DisplayValue(Info, Info.Field.Value) == "-3";
							  }),
				  "Leopard 120 rear depression restriction lost its negative sign");
	}
	WriteText(Output / "parameter-coverage.tsv", ParameterReport);
	Check(NormalPower && PhysicsPower && Terrain,
		  "Main/physics engine or terrain copies are missing from forms");
	Check(Categories.contains("gun") && Categories.contains("shell") && Categories.contains("turret"),
		  "A requested parameter category is empty");
	auto Initial = Workspace.Document(Type59->DefinitionPath).Current;
	Workspace.ResetVehicle(*Type59);
	Check(Workspace.Get(Type59->DefinitionPath).Current.Root.Child("hull") != nullptr,
		  "Reset destroyed the hull definition");
	Check(Workspace.Get(Type59->DefinitionPath).Current.Packed == Initial.Packed,
		  "Reset changed installed XML storage type");
	AssemblySelection Collision;
	Collision.Collision = true;
	auto Mesh =
		BuildVehicleMesh(Catalogue, *Type59, Workspace.Get(Type59->DefinitionPath).Current, Collision);
	Check(Mesh.Triangles.size() > 100, "Reset vehicle collision geometry could not be read");
	std::cout << "Catalogue " << Catalogue.Vehicles.size() << "; stock " << StockCount << "; form fields "
			  << Fields.size() << "; collision triangles " << Mesh.Triangles.size() << '\n';
	if (!SavePath.empty())
	{
		SaveGame Account;
		Account.Open(SavePath);
		auto Original = WritePickle(Account.Root);
		Check(ValueEqual(Account.Root, ReadPickle(Original)), "Live-save read-only roundtrip mismatch");
		size_t Known{};
		for (auto const& Vehicle : Account.Inventory(Catalogue))
			Known += Vehicle.Known;
		Check(Known == Account.Root->Get("vehicles")->Pairs.size(), "Unresolved save vehicle identity");
		WriteFile(Output / "account-roundtrip.dat", Original);
		SaveGame Copy;
		Copy.Open(Output / "account-roundtrip.dat");
		auto OriginalCount = Copy.Inventory(Catalogue).size();
		auto Credits = Copy.Currency("credits");
		Copy.SetCurrency("credits", Credits + 123);
		auto SourceWithCrew = Copy.AddVehicle(*Type59, Catalogue, true);
		++OriginalCount;
		Check(!CrewSlots(Copy, {SourceWithCrew}).empty(), "Adding a vehicle did not create its crew");
		auto Clone = Copy.CloneVehicle(SourceWithCrew, true);
		Check(Copy.Inventory(Catalogue).size() == OriginalCount + 1, "Cloned vehicle is missing");
		auto Crew = CrewSlots(Copy, {Clone});
		Check(!Crew.empty(), "Cloned crew is missing");
		std::set<int64_t> CloneCrewIds;
		for (auto const& Slot : Crew)
			Check(CloneCrewIds.insert(Slot.InventoryId).second, "Cloned crew IDs are not unique");
		Copy.SetVehicleXp(Type59->TypeCd, 12345);
		auto SavedCopy = Output / "edited-account.dat";
		Copy.Save(SavedCopy);
		SaveGame Reloaded;
		Reloaded.Open(SavedCopy);
		Check(Reloaded.Currency("credits") == Credits + 123 && Reloaded.VehicleXp(Type59->TypeCd) == 12345,
			  "Account resource/vehicle XP edits did not survive saving");
		Reloaded.RemoveVehicle(Clone);
		Check(Reloaded.Inventory(Catalogue).size() == OriginalCount, "Removing the cloned vehicle failed");
		std::cout << "Read-only account identities: " << Known << '\n';
	}
}
} // namespace
int wmain(int ArgumentCount, wchar_t** Arguments)
{
	if (ArgumentCount == 3 && std::wstring_view(Arguments[1]) == L"--hold-game-guard")
	{
		auto Event = OpenEventW(SYNCHRONIZE, false, Arguments[2]);
		if (Event)
		{
			WaitForSingleObject(Event, 10000);
			CloseHandle(Event);
		}
		return 0;
	}
	try
	{
		Output = FileSystem::path(OHEDITOR_SOURCE_DIRECTORY) / "build/verification" / Wide(Timestamp());
		FileSystem::create_directories(Output);
		if (ArgumentCount == 3 && std::wstring_view(Arguments[1]) == L"--verify-cache-copy")
		{
			FileSystem::path Source = Arguments[2];
			auto Original = ReadFile(Source, 512ull * 1024 * 1024);
			auto OriginalHash = Sha256File(Source);
			auto OriginalTime = FileSystem::last_write_time(Source);
			auto FixtureRoot = Output / "cache-file-copy";
			auto Copy = FixtureRoot / "data.wgpdc";
			WriteFile(Copy, Original);
			GameDataCache Cache(FixtureRoot);
			Check(Cache.Refresh().Method == GameCacheMethod::Invalidated,
				  "A real cache copy could not be invalidated without the client");
			Check(!FileSystem::exists(Copy), "A real cache copy remained after invalidation");
			Check(!FileSystem::exists(FixtureRoot / "win64") && !FileSystem::exists(FixtureRoot / "PDC"),
				  "Invalidation created client runtime directories");
			Cache.Restore();
			Check(ReadFile(Copy, 512ull * 1024 * 1024) == Original,
				  "Rollback did not restore every byte of a real cache copy");
			Check(Sha256File(Copy) == OriginalHash, "Restored real cache copy has a different SHA-256");
			Check(Sha256File(Source) == OriginalHash && FileSystem::last_write_time(Source) == OriginalTime,
				  "Copy-only verification changed the original cache");
			std::cout << "Real cache copy invalidation and rollback verified without the client; "
					  << Assertions << " assertions\n";
			return 0;
		}
		if (ArgumentCount == 3 && std::wstring_view(Arguments[1]) == L"--verify-cache")
		{
			auto Original = ReadFile(Arguments[2], 512ull * 1024 * 1024);
			auto Root = ReadPickle(Original, 512ull * 1024 * 1024);
			Check(Root->Get("version") && Root->Get("version")->Kind == ValueKind::Tuple,
				  "Real native cache has no client version");
			auto Entries = Root->Get("data");
			Check(Entries && Entries->Kind == ValueKind::List, "Real native cache has no providers");
			for (auto Name : {"vehicles_list", "vehicles_cache", "gui_items_params_cache"})
				Check(std::any_of(Entries->Items.begin(), Entries->Items.end(),
								  [&](auto const& Entry)
								  {
									  return Entry->Items[0]->Text == Name &&
											 !Entry->Items[1]->Items[0]->Text.empty();
								  }),
					  "Real native output has a missing or empty vehicle provider");
			auto Roundtrip = WritePickle(Root);
			Check(ValueEqual(Root, ReadPickle(Roundtrip, 512ull * 1024 * 1024)),
				  "Native cache provider payloads changed during non-executing inspection");
			std::cout << "Real native cache verified read-only; " << Assertions << " assertions\n";
			return 0;
		}
		if (ArgumentCount > 2 && std::wstring_view(Arguments[1]) == L"--inspect")
		{
			Catalog Catalogue;
			Catalogue.Scan(Arguments[2]);
			for (auto const& Vehicle : Catalogue.Vehicles)
				for (int Index = 3; Index < ArgumentCount; ++Index)
					if (Lower(Vehicle.Name + " " + Vehicle.DisplayName).find(Lower(Utf8(Arguments[Index]))) !=
						std::string::npos)
					{
						WriteText(Output / Wide(Vehicle.Model + ".xml"),
								  Catalogue.LoadDefinition(Vehicle).Xml());
						std::cout << Vehicle.Name << "\t" << Vehicle.DisplayName << '\n';
					}
			std::cout << "Inspection output: " << PathUtf8(Output) << '\n';
			return 0;
		}
		TestFormats();
		TestDefinitionStructure();
		TestResourceIndexPaths();
		TestWorkspace();
		TestSharedComponentDependencies();
		TestNativeDefaults();
		TestInvalidPackages();
		TestInterruptedWrites();
		TestGameCacheRefresh();
		TestCacheInvalidation();
		TestCacheRollbackAfterFinalizationFailure();
		TestSevenVersusSevenRentals();
		TestNativeMechanismValues();
		TestParameterPresentation();
		TestPendingLifetime();
		TestBaselineImport();
		TestRunningGameGuard();
		if (ArgumentCount > 1)
			TestInstallation(Arguments[1], ArgumentCount > 2 ? Arguments[2] : L"");
		std::cout << "Verification output: " << PathUtf8(Output) << "\n";
		std::cout << Assertions << " assertions passed\n";
		return 0;
	}
	catch (std::exception const& Error)
	{
		std::cerr << "FAILED after " << Assertions << ": " << Error.what() << '\n';
		return 1;
	}
}
