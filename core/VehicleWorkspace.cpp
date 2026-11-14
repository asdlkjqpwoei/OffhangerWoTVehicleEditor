#include "VehicleWorkspace.h"
#include <cmath>
#include <sstream>
#include <windows.h>

namespace OffHanger
{
namespace
{
class CacheFileLock
{
	HANDLE Handle = INVALID_HANDLE_VALUE;

  public:
	explicit CacheFileLock(FileSystem::path Cache, bool Write = true)
	{
		if (Cache.empty())
			return;
		if (!Write && !FileSystem::exists(Cache))
			return;
		Cache += L".lock";
		if (!Write && !FileSystem::exists(Cache))
			return;
		if (Write)
			EnsureWritesAllowed();
		if (!Cache.parent_path().empty())
			if (Write)
				FileSystem::create_directories(NativePath(Cache.parent_path()));
		Handle = CreateFileW(NativePath(Cache).c_str(), Write ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
							 FILE_SHARE_READ, nullptr, Write ? OPEN_ALWAYS : OPEN_EXISTING,
							 FILE_ATTRIBUTE_NORMAL, nullptr);
		if (Handle == INVALID_HANDLE_VALUE)
			throw std::runtime_error(
				"The working cache is busy or not writable; close the other editor or check its folder");
	}
	CacheFileLock(CacheFileLock const&) = delete;
	CacheFileLock& operator=(CacheFileLock const&) = delete;
	~CacheFileLock()
	{
		if (Handle != INVALID_HANDLE_VALUE)
			CloseHandle(Handle);
	}
};
std::string Key(std::string const& Path)
{
	return NormalizeResourcePath(Path);
}
std::string ScopeKey(DocumentScope const& Scope)
{
	return Key(Scope.Path) + "/" + Scope.Component + "/" + Scope.Module;
}
bool Prefix(NodeAddress const& Root, NodeAddress const& Child)
{
	return Root.size() <= Child.size() && std::equal(Root.begin(), Root.end(), Child.begin());
}
std::optional<std::vector<double>> Numbers(std::string const& Text)
{
	std::istringstream Stream(Text);
	std::vector<double> Values;
	double Value;
	while (Stream >> Value)
		Values.push_back(Value);
	if (Values.empty() || !Stream.eof())
		return {};
	return Values;
}
std::string_view ComponentGroup(std::string_view Name)
{
	if (Name == "guns" || Name == "engines" || Name == "chassis" || Name == "radios" || Name == "fueltanks")
		return Name;
	if (Name == "shots")
		return "shells";
	if (Name == "turrets" ||
		(Name.starts_with("turrets") && Name.substr(7).find_first_not_of("0123456789") == Name.npos))
		return "turrets";
	return {};
}
std::string NumericKey(std::string const& Resource, std::vector<std::string_view> const& Names)
{
	std::string Group = "vehicle";
	size_t Begin{};
	if (Resource.ends_with("/list.xml"))
	{
		Group = "metadata";
		Begin = 1;
	}
	else if (Resource.find("/components/") != Resource.npos)
	{
		Group = Resource.substr(Resource.find_last_of('/') + 1);
		Group.resize(Group.size() - 4);
		Begin = !Names.empty() && Names.front() == "shared" ? 2 : 1;
	}
	// Inline and shared modules use the same parameter identity. Mechanic names and
	// nested paths stay distinct, so an unrelated field with the same leaf name is not evidence.
	for (size_t Index = Begin; Index + 1 < Names.size(); ++Index)
	{
		auto Name = Lower(std::string(Names[Index]));
		if (auto Component = ComponentGroup(Name); !Component.empty())
		{
			Group = Component;
			Begin = Index + 2;
			++Index;
		}
	}
	for (size_t Index = Begin; Index < Names.size(); ++Index)
		Group += "/" + Lower(std::string(Names[Index]));
	return Group;
}
std::string NumericKey(std::string const& Path, NamedAddress const& Names)
{
	std::vector<std::string_view> Views;
	Views.reserve(Names.size());
	for (auto const& Name : Names)
		Views.push_back(Name.Name);
	return NumericKey(Key(Path), Views);
}
bool IntegerLiteral(std::string const& Text)
{
	auto Number = Trim(Text);
	auto Begin = Number.starts_with('+') || Number.starts_with('-') ? 1u : 0u;
	return Number.size() > Begin && Number.find_first_not_of("0123456789", Begin) == Number.npos;
}
YAML::Node Encode(NamedAddress const& Names)
{
	YAML::Node Result(YAML::NodeType::Sequence);
	for (auto const& Step : Names)
	{
		YAML::Node Item;
		Item["name"] = Step.Name;
		Item["ordinal"] = Step.Ordinal;
		Result.push_back(Item);
	}
	return Result;
}
NamedAddress Decode(YAML::Node const& Input)
{
	if (!Input.IsSequence() || Input.size() > 128)
		throw std::runtime_error("Invalid value address");
	NamedAddress Result;
	for (auto Item : Input)
	{
		auto Name = Item["name"].as<std::string>();
		if (Name.empty() || Name.size() > 512)
			throw std::runtime_error("Invalid XML node name");
		Result.push_back({Name, Item["ordinal"].as<size_t>()});
	}
	return Result;
}
} // namespace
Section& VehicleWorkspace::Resolve(DataSection& Data, NodeAddress const& Address)
{
	auto Node = &Data.Root;
	for (auto Index : Address)
		Node = &Node->Children.at(Index);
	return *Node;
}
Section const& VehicleWorkspace::Resolve(DataSection const& Data, NodeAddress const& Address)
{
	auto Node = &Data.Root;
	for (auto Index : Address)
		Node = &Node->Children.at(Index);
	return *Node;
}
NamedAddress VehicleWorkspace::Names(DataSection const& Data, NodeAddress const& Address)
{
	NamedAddress Result;
	auto Node = &Data.Root;
	for (auto Index : Address)
	{
		auto const& Child = Node->Children.at(Index);
		size_t Ordinal{};
		for (size_t Before = 0; Before < Index; ++Before)
			Ordinal += Node->Children[Before].Name == Child.Name;
		Result.push_back({Child.Name, Ordinal});
		Node = &Child;
	}
	return Result;
}
NodeAddress VehicleWorkspace::Locate(DataSection const& Data, NamedAddress const& Names)
{
	NodeAddress Result;
	auto Node = &Data.Root;
	for (auto const& Step : Names)
	{
		size_t Occurrence{}, Index{};
		for (; Index < Node->Children.size(); ++Index)
			if (Node->Children[Index].Name == Step.Name && Occurrence++ == Step.Ordinal)
				break;
		if (Index == Node->Children.size())
			throw std::runtime_error("Missing XML field: " + Step.Name);
		Result.push_back(Index);
		Node = &Node->Children[Index];
	}
	return Result;
}
std::string VehicleWorkspace::Display(NamedAddress const& Names)
{
	std::string Result;
	for (auto const& Step : Names)
		Result += "/" + Step.Name + (Step.Ordinal ? "[" + std::to_string(Step.Ordinal + 1) + "]" : "");
	return Result;
}
bool VehicleWorkspace::IsReadOnly(Section const& Node)
{
	auto Name = Lower(Node.Name);
	auto Value = Lower(Node.Value);
	return Node.ValueType == 6 || Name == "id" || Name == "gold" || Name.starts_with("xmlns") ||
		   Node.Name.find(':') != std::string::npos || Lower(Trim(Node.Value)) == "shared" ||
		   Node.Value.empty() || Value.ends_with(".model") || Value.ends_with(".havok") ||
		   Name == "effects" || Name == "userstring" || Name == "shortuserstring" || Name == "icon";
}
bool VehicleWorkspace::AcceptsReal(std::string const& Path, Section const& Node,
								   NamedAddress const& Names) const
{
	auto Leaf = Lower(Node.Name);
	if (Leaf == "level" || Leaf == "price" || Leaf == "maxhealth" || Leaf == "maxregenhealth" ||
		Leaf == "maxammo" || Leaf == "count" || Node.Name.ends_with("Count"))
		return false;
	// Durations and firing rates remain continuous even in a client whose examples are all whole numbers.
	static std::set<std::string> const Continuous = {
		"reloadtime",		"reloadtimes",	  "autoreloadtimes",   "aimingtime",
		"repairtime",		"rate",			  "intraclipreload",   "intraburstreload",
		"chargetime",		"reloadlocktime", "ratetime",		   "aftershotdelay",
		"switchontime",		"switchofftime",  "stunduration",	   "minstunduration",
		"maxstunduration",	"duration",		  "dualgunchargetime", "dualgunreloadcooldown",
		"dualgunswitchtime"};
	return Continuous.contains(Leaf) || RealParameters.contains(NumericKey(Path, Names)) ||
		   Node.ValueType == 3 || (Node.Value.find_first_of(".eE") != Node.Value.npos && Numbers(Node.Value));
}
Section VehicleWorkspace::ValidateValue(std::string const& Path, Section const& Initial, Section const& Node,
										NamedAddress const& Names, std::string const& Value) const
{
	if (IsReadOnly(Node))
		throw std::runtime_error("This field is structural or read-only");
	if (Value.size() > 65536)
		throw std::runtime_error("XML value exceeds the editor limit");
	if ((Node.Value == "true" || Node.Value == "false") && Value != "true" && Value != "false" &&
		Value != "0" && Value != "1")
		throw std::runtime_error("Use true, false, zero or one for a boolean value");
	auto OriginalNumbers = Numbers(Node.Value), EditedNumbers = Numbers(Value);
	bool Real = AcceptsReal(Path, Initial, Names);
	if (OriginalNumbers)
	{
		if (!EditedNumbers || OriginalNumbers->size() != EditedNumbers->size())
			throw std::runtime_error("Keep the original number of numeric/vector components");
		for (auto Number : *EditedNumbers)
			if (!std::isfinite(Number) || std::abs(Number) > 1e12)
				throw std::runtime_error("A finite numeric value is required");
		if (!Real)
		{
			std::istringstream Components(Value);
			std::string Component;
			while (Components >> Component)
				if (!IntegerLiteral(Component))
					throw std::runtime_error("This parameter requires integer values");
		}
		auto Path = Lower(Display(Names));
		auto Leaf = Lower(Node.Name);
		bool Positive = Leaf == "reloadtime" || Leaf == "aimingtime" || Leaf == "maxhealth" ||
						Leaf == "power" || Leaf == "smplenginepower" || Leaf == "weight" ||
						Leaf == "terrainresistance" || Leaf == "terrainresistancefactors" || Leaf == "rate" ||
						Leaf == "reloadtimes" || Leaf == "shotdispersionradius";
		bool Nonnegative = Positive || Leaf == "level" || Leaf == "price" || Leaf == "forward" ||
						   Leaf == "backward" || Leaf == "terrainresistance" ||
						   Leaf == "shotdispersionradius" || Leaf == "count" || Leaf == "rate" ||
						   Leaf == "fireprobability";
		for (auto Number : *EditedNumbers)
			if ((Positive && Number <= 0) || (Nonnegative && Number < 0))
				throw std::runtime_error("Value is outside the safe range for this parameter");
		if (Leaf == "level" && (EditedNumbers->at(0) < 1 || EditedNumbers->at(0) > 11 ||
								std::floor(EditedNumbers->at(0)) != EditedNumbers->at(0)))
			throw std::runtime_error("Vehicle tier must be an integer from 1 to 11");
		if ((Leaf == "fireprobability" || Leaf == "firestartingchance" || Leaf == "firepenalty" ||
			 Leaf == "guaranteedstunduration" || Leaf == "damagedurationcoeff" ||
			 Leaf == "guaranteedstuneffect" || Leaf == "damageeffectcoeff" || Leaf == "boostfraction" ||
			 Leaf == "stunfactor") &&
			(EditedNumbers->at(0) < 0 || EditedNumbers->at(0) > 1))
			throw std::runtime_error("Probability must be between zero and one");
		if ((Leaf == "count" || Leaf.ends_with("shellcount")) &&
			(Path.find("clip") != Path.npos || Path.find("burst") != Path.npos ||
			 Path.find("double") != Path.npos))
			if (EditedNumbers->at(0) < 1 || EditedNumbers->at(0) > 120 ||
				std::floor(EditedNumbers->at(0)) != EditedNumbers->at(0))
				throw std::runtime_error("Native magazine count must be an integer from 1 to 120");
	}
	auto Checked = Node;
	Checked.ValueType = Initial.ValueType;
	if (Real && OriginalNumbers && Checked.ValueType == 2 && !IntegerLiteral(Value))
		Checked.ValueType = 1; // Packed XML also stores native decimal scalars as numeric text.
	Checked.Set(Value);
	return Checked;
}
void VehicleWorkspace::Open(Catalog const& Input, DefaultResources const* NativeDefaults,
							FileSystem::path Cache, FileSystem::path Overrides)
{
	Catalogue = &Input;
	Defaults = NativeDefaults;
	CacheFile = std::move(Cache);
	CacheCrc = !CacheFile.empty() && FileSystem::exists(CacheFile)
				   ? std::optional<uint32_t>(Crc32(ReadFile(CacheFile, 256ull * 1024 * 1024)))
				   : std::nullopt;
	OverrideRoot = FileSystem::absolute(Overrides).lexically_normal();
	ResourceSignature = Input.Resources.LoadedSignature;
	Documents.clear();
	EditPreimages.clear();
	Applied.clear();
	Owners.clear();
	ResetInProgress = false;
	ApplyInProgress = false;
	LegacyLedger = false;
	LastCache = {};
	Scopes.clear();
	Users.clear();
	RealParameters.clear();
	BuildScopes();
	if (!CacheFile.empty() && FileSystem::is_regular_file(CacheFile))
	{
		{
			CacheFileLock Lock(CacheFile, false);
			RestoreCache();
		}
		if (LegacyLedger && !GameRunning())
		{
			CacheFileLock Lock(CacheFile);
			Persist();
			LegacyLedger = false;
		}
	}
	++ChangeRevision;
}
VehicleDocument& VehicleWorkspace::Document(std::string const& Path)
{
	auto ResourceKey = Key(Path);
	if (auto Found = Documents.find(ResourceKey); Found != Documents.end())
	{
		if (InBatch && !EditPreimages.contains(ResourceKey))
			EditPreimages.emplace(ResourceKey, Found->second);
		return Found->second;
	}
	if (!Catalogue)
		throw std::runtime_error("Load game data first");
	auto Source = Catalogue->Resources.Find(Path);
	if (!Source)
		throw std::runtime_error("Missing resource: " + Path);
	auto Data = Source->Read();
	auto Parsed = DataSection::Parse(Data);
	auto& Result =
		Documents
			.emplace(ResourceKey, VehicleDocument{Source->Path, Parsed, Parsed, Parsed, Data, Crc32(Data)})
			.first->second;
	if (InBatch)
		EditPreimages.emplace(ResourceKey, Result);
	return Result;
}
VehicleDocument const& VehicleWorkspace::Get(std::string const& Path) const
{
	return Documents.at(Key(Path));
}
void VehicleWorkspace::BuildScopes()
{
	std::map<std::string, SectionStructure> Tables;
	auto Read = [&](std::string const& Path) -> SectionStructure const&
	{
		auto ResourceKey = Key(Path);
		if (!Tables.contains(ResourceKey))
		{
			auto Shape = DataSection::ParseStructure(Catalogue->Resources.Find(Path)->Read());
			bool ComponentTable = ResourceKey.find("/components/") != ResourceKey.npos;
			auto Group = NumericKey(ResourceKey, std::vector<std::string_view>{});
			// Reuse the canonical prefix down each branch instead of rebuilding it from the root
			// (and revalidating the same resource path) for every numeric leaf.
			std::function<void(SectionStructure const&, std::string const&, size_t, bool)> IndexNumbers =
				[&](SectionStructure const& Node, std::string const& Prefix, size_t Skip, bool TableRoot)
			{
				for (auto const& Child : Node.Children)
				{
					if (!Child.RealNumber && Child.Children.empty())
						continue;
					auto Name = Lower(Child.Name);
					auto ValueKey = Skip ? Prefix : Prefix + "/" + Name;
					if (Child.RealNumber)
						RealParameters.insert(ValueKey);
					if (!Skip)
						if (auto Component = ComponentGroup(Name); !Component.empty())
						{
							IndexNumbers(Child, std::string(Component), 1, false);
							continue;
						}
					auto NextSkip = Skip ? Skip - 1 : 0;
					if (TableRoot && Name == "shared")
						NextSkip = 1;
					IndexNumbers(Child, ValueKey, NextSkip, false);
				}
			};
			if (Shape.RealNumber)
				RealParameters.insert(Group);
			IndexNumbers(Shape, Group, ComponentTable || ResourceKey.ends_with("/list.xml") ? 1 : 0,
						 ComponentTable);
			Tables.emplace(ResourceKey, std::move(Shape));
		}
		return Tables.at(ResourceKey);
	};
	for (auto const& Vehicle : Catalogue->Vehicles)
	{
		auto& Entries = Scopes[Vehicle.Name];
		if (Vehicle.DefinitionPath.empty() || !Catalogue->Resources.Find(Vehicle.DefinitionPath))
			continue;
		auto const& Definition = Read(Vehicle.DefinitionPath);
		Entries.push_back({Vehicle.DefinitionPath, "vehicle", Vehicle.Model, {}});
		if (!Vehicle.ListPath.empty() && Catalogue->Resources.Find(Vehicle.ListPath))
		{
			auto const& List = Read(Vehicle.ListPath);
			for (size_t Index = 0; Index < List.Children.size(); ++Index)
				if (List.Children[Index].Name == Vehicle.Model)
					Entries.push_back({Vehicle.ListPath, "vehicle", Vehicle.Model, {Index}});
		}
		std::map<std::string, std::set<std::string>> References;
		std::function<void(SectionStructure const&)> Collect = [&](SectionStructure const& Node)
		{
			auto Group = Node.Name;
			if (Group.starts_with("turrets"))
				Group = "turrets";
			if (Group == "shots")
				Group = "shells";
			if (Group == "chassis" || Group == "engines" || Group == "guns" || Group == "turrets" ||
				Group == "shells" || Group == "radios" || Group == "fuelTanks")
				for (auto const& Child : Node.Children)
					References[Group].insert(Child.Name);
			for (auto const& Child : Node.Children)
				Collect(Child);
		};
		Collect(Definition);
		auto ComponentDocuments = Catalogue->Documents(Vehicle);
		std::map<std::string, std::set<NodeAddress>> ResolvedScopes;
		for (auto const& Scope : Entries)
			ResolvedScopes[Key(Scope.Path)].insert(Scope.Root);
		// Resolve only referenced component bodies; gun bodies can introduce shell references.
		for (int Pass = 0; Pass < 3; ++Pass)
			for (auto const& [Caption, Path] : ComponentDocuments)
			{
				if (Path.find("/components/") == Path.npos)
					continue;
				auto Group = Path.substr(Path.find_last_of('/') + 1);
				Group.resize(Group.size() - 4);
				std::vector<std::string> Paths{Path};
				auto BasePath =
					"scripts/item_defs/vehicles/" + Vehicle.Nation + "/components/" + Group + ".xml";
				if (Key(Path) != Key(BasePath) && Catalogue->Resources.Find(BasePath))
					Paths.push_back(BasePath);
				for (auto const& ComponentPath : Paths)
				{
					auto const& Table = Read(ComponentPath);
					auto& Resolved = ResolvedScopes[Key(ComponentPath)];
					auto const& Referenced = References[Group];
					auto Add = [&](SectionStructure const& Parent, NodeAddress PrefixAddress)
					{
						for (size_t Index = 0; Index < Parent.Children.size(); ++Index)
							if (Referenced.contains(Parent.Children[Index].Name))
							{
								auto Address = PrefixAddress;
								Address.push_back(Index);
								if (!Resolved.insert(Address).second)
									continue;
								Entries.push_back(
									{ComponentPath, Group, Parent.Children[Index].Name, Address});
								// Collecting an immutable body once already discovers all its references.
								Collect(Parent.Children[Index]);
							}
					};
					Add(Table, {});
					for (size_t Index = 0; Index < Table.Children.size(); ++Index)
						if (Table.Children[Index].Name == "shared")
							Add(Table.Children[Index], {Index});
				}
			}
		for (auto const& Scope : Entries)
			Users[ScopeKey(Scope)].insert(Vehicle.Name);
	}
}
std::vector<DocumentScope> const& VehicleWorkspace::VehicleScopes(Vehicle const& Vehicle) const
{
	return Scopes.at(Vehicle.Name);
}
std::vector<std::string> VehicleWorkspace::SharedUsers(DocumentScope const& Scope) const
{
	if (Scope.Path.find("/components/") == Scope.Path.npos)
		return {};
	auto Found = Users.find(ScopeKey(Scope));
	if (Found == Users.end())
		return {};
	return {Found->second.begin(), Found->second.end()};
}
std::vector<VehicleValue> VehicleWorkspace::Values(Vehicle const& Vehicle, ValueFilter const& Include)
{
	std::vector<VehicleValue> Result;
	for (auto const& Scope : VehicleScopes(Vehicle))
	{
		auto& Data = Document(Scope.Path).Current;
		auto Shared = SharedUsers(Scope);
		auto Address = Scope.Root;
		std::function<void(Section const&, std::string const&)> Walk =
			[&](Section const& Node, std::string const& Path)
		{
			if (Include && !Include(Node, Path, Scope))
				return;
			if (!Node.Value.empty() || Node.Children.empty())
				Result.push_back({Scope.Path, Path, Node.Value, Scope.Component, Scope.Module, Address,
								  IsReadOnly(Node), Shared});
			std::map<std::string_view, size_t> Ordinals;
			for (size_t Index = 0; Index < Node.Children.size(); ++Index)
			{
				auto const& Child = Node.Children[Index];
				auto Ordinal = Ordinals[Child.Name]++;
				auto ChildPath = Path + "/" + Child.Name;
				if (Ordinal)
					ChildPath += "[" + std::to_string(Ordinal + 1) + "]";
				Address.push_back(Index);
				Walk(Child, ChildPath);
				Address.pop_back();
			}
		};
		Walk(Resolve(Data, Scope.Root), Display(Names(Data, Scope.Root)));
	}
	return Result;
}
void VehicleWorkspace::Commit(std::function<void()> const& Command)
{
	if (InBatch)
	{
		Command();
		return;
	}
	EnsureWritesAllowed();
	EditPreimages.clear();
	auto PreviousOwners = Owners;
	InBatch = true;
	try
	{
		Command();
		// Only documents touched by this transaction need a preimage and validation.
		// Cached documents for previously viewed vehicles can be very large.
		for (auto const& [Path, Before] : EditPreimages)
		{
			auto const& Data = Documents.at(Path);
			ValidateStructure(Path, Data.Initial, Data.Current);
			DataSection::Parse(Data.Current.Serialize());
		}
		// Pending edits live only in memory and are discarded on exit.
		++ChangeRevision;
		InBatch = false;
		EditPreimages.clear();
	}
	catch (...)
	{
		for (auto& [Path, Before] : EditPreimages)
			Documents.at(Path) = std::move(Before);
		EditPreimages.clear();
		Owners = std::move(PreviousOwners);
		InBatch = false;
		throw;
	}
}
void VehicleWorkspace::ValidateStructure(std::string const& Path, DataSection const& Initial,
										 DataSection const& Current) const
{
	if (Initial.Packed != Current.Packed)
		throw std::runtime_error("Changing XML storage format is not allowed");
	NamedAddress Names;
	std::function<void(Section const&, Section const&)> Compare =
		[&](Section const& Before, Section const& After)
	{
		bool NumericText = Before.ValueType == 2 && After.ValueType == 1 &&
						   AcceptsReal(Path, Before, Names) && Numbers(Before.Value) && Numbers(After.Value);
		if (Before.Name != After.Name || Before.Attributes != After.Attributes ||
			(Before.ValueType != After.ValueType && !NumericText))
			throw std::runtime_error("XML identity, type or attributes changed");
		if (IsReadOnly(Before) && Before.Value != After.Value)
			throw std::runtime_error("A structural XML value changed");
		if (!IsReadOnly(Before) && (Before.Value != After.Value || NumericText))
			(void)ValidateValue(Path, Before, Before, Names, After.Value);
		std::vector<Section const*> BeforeChildren, AfterChildren;
		auto Collect = [](Section const& Parent, auto& Children)
		{
			size_t GoldMarkers{};
			for (auto const& Child : Parent.Children)
			{
				if (Parent.Name == "price" && Child.Name == "gold")
				{
					if (++GoldMarkers > 1 || !Child.Children.empty() || !Child.Attributes.empty() ||
						(!Child.Value.empty() && Child.Value != "true" && Child.Value != "false" &&
						 Child.Value != "1" && Child.Value != "0"))
						throw std::runtime_error("Only an empty gold price marker is permitted");
					continue;
				}
				Children.push_back(&Child);
			}
		};
		Collect(Before, BeforeChildren);
		Collect(After, AfterChildren);
		if (BeforeChildren.size() != AfterChildren.size())
			throw std::runtime_error("Adding or removing XML nodes is not allowed");
		std::map<std::string, size_t> Ordinals;
		for (size_t Index = 0; Index < BeforeChildren.size(); ++Index)
		{
			auto const& Name = BeforeChildren[Index]->Name;
			Names.push_back({Name, Ordinals[Name]++});
			Compare(*BeforeChildren[Index], *AfterChildren[Index]);
			Names.pop_back();
		}
		if (auto Health = After.Child("maxHealth"))
			if (auto Regenerated = After.Child("maxRegenHealth"))
			{
				auto Maximum = Numbers(Health->Value), Repaired = Numbers(Regenerated->Value);
				if (Maximum && Repaired && Repaired->front() > Maximum->front())
					throw std::runtime_error("Repaired module health exceeds maximum health");
			}
		auto Clip = After.Child("clip"), PreviousClip = Before.Child("clip");
		auto Automatic = After.Child("autoreload"), PreviousAutomatic = Before.Child("autoreload");
		if (Clip && PreviousClip && Automatic && PreviousAutomatic)
		{
			auto Times = Numbers(Automatic->Text("reloadTime"));
			auto OldTimes = Numbers(PreviousAutomatic->Text("reloadTime"));
			auto Count = Numbers(Clip->Text("count")), OldCount = Numbers(PreviousClip->Text("count"));
			if (Times && OldTimes && Count && OldCount && OldTimes->size() == size_t(OldCount->front()) &&
				Times->size() != size_t(Count->front()))
				throw std::runtime_error("Autoreload times must match the native magazine count");
		}
	};
	Compare(Initial.Root, Current.Root);
}
void VehicleWorkspace::Edit(Vehicle const& Vehicle, std::string const& Path, NodeAddress const& Address,
							std::string const& Value)
{
	bool Allowed = false;
	for (auto const& Scope : VehicleScopes(Vehicle))
		Allowed |= Key(Scope.Path) == Key(Path) && Prefix(Scope.Root, Address);
	if (!Allowed)
		throw std::runtime_error("This field does not belong to the selected vehicle");
	auto& Data = Document(Path);
	auto AddressNames = Names(Data.Current, Address);
	auto Changed = ValidateValue(Path, Resolve(Data.Initial, Locate(Data.Initial, AddressNames)),
								 Resolve(Data.Current, Address), AddressNames, Value);
	Commit(
		[&]
		{
			Resolve(Document(Path).Current, Address) = std::move(Changed);
			Owners[Key(Path)].insert(Vehicle.Name);
			for (auto const& Scope : VehicleScopes(Vehicle))
				if (Key(Scope.Path) == Key(Path) && Prefix(Scope.Root, Address))
					for (auto const& Name : SharedUsers(Scope))
						Owners[Key(Path)].insert(Name);
		});
}
void VehicleWorkspace::EditBatch(Vehicle const& Vehicle, std::vector<VehicleValue> const& Changes)
{
	Commit(
		[&]
		{
			for (auto const& Change : Changes)
				Edit(Vehicle, Change.Document, Change.Address, Change.Value);
		});
}
bool VehicleWorkspace::PriceCurrency(Vehicle const& Vehicle)
{
	auto Entry = Document(Vehicle.ListPath).Current.Root.Child(Vehicle.Model);
	auto Price = Entry ? Entry->Child("price") : nullptr;
	if (!Price)
		throw std::runtime_error("This vehicle has no registered price");
	return Price->Child("gold") != nullptr;
}
void VehicleWorkspace::SetPriceCurrency(Vehicle const& Vehicle, bool Gold)
{
	Document(Vehicle.ListPath);
	Commit(
		[&]
		{
			auto Entry = Document(Vehicle.ListPath).Current.Root.Child(Vehicle.Model);
			auto Price = Entry ? Entry->Child("price") : nullptr;
			if (!Price)
				throw std::runtime_error("This vehicle has no registered price");
			if (Gold && !Price->Child("gold"))
			{
				Section Marker;
				Marker.Name = "gold";
				Price->Children.push_back(std::move(Marker));
			}
			if (!Gold)
				std::erase_if(Price->Children, [](auto const& Node) { return Node.Name == "gold"; });
			Owners[Key(Vehicle.ListPath)].insert(Vehicle.Name);
		});
}
void VehicleWorkspace::ResetVehicle(Vehicle const& Vehicle)
{
	auto Fields = Values(Vehicle);
	std::map<std::string, std::optional<DataSection>> Native;
	Commit(
		[&]
		{
			for (auto const& Field : Fields)
			{
				if (Field.ReadOnly)
					continue;
				auto& Data = Document(Field.Document);
				auto AddressNames = Names(Data.Current, Field.Address);
				if (!Native.contains(Key(Field.Document)))
					Native[Key(Field.Document)] = Defaults ? Defaults->Load(Field.Document) : std::nullopt;
				auto Baseline = &Data.Initial;
				auto& Original = Native[Key(Field.Document)];
				NodeAddress SourceAddress;
				try
				{
					if (Original && (Numbers(Field.Value) || Field.Value == "true" || Field.Value == "false"))
					{
						SourceAddress = Locate(*Original, AddressNames);
						Baseline = &*Original;
					}
				}
				catch (std::exception const&)
				{
					Baseline = &Data.Initial;
				}
				if (Baseline == &Data.Initial)
					SourceAddress = Locate(Data.Initial, AddressNames);
				auto Value = Resolve(*Baseline, SourceAddress).Value;
				// A schema/version-dependent vector must keep the active client's shape.
				try
				{
					(void)ValidateValue(Field.Document,
										Resolve(Data.Initial, Locate(Data.Initial, AddressNames)),
										Resolve(Data.Current, Field.Address), AddressNames, Value);
				}
				catch (std::exception const&)
				{
					Value = Resolve(Data.Initial, Locate(Data.Initial, AddressNames)).Value;
				}
				Edit(Vehicle, Field.Document, Field.Address, Value);
			}
			if (!Vehicle.ListPath.empty())
			{
				auto& Metadata = Document(Vehicle.ListPath);
				auto Native = Defaults ? Defaults->Load(Vehicle.ListPath) : std::nullopt;
				auto Entry = (Native ? Native->Root : Metadata.Initial.Root).Child(Vehicle.Model);
				if (!Entry)
					Entry = Metadata.Initial.Root.Child(Vehicle.Model);
				if (auto Price = Entry ? Entry->Child("price") : nullptr)
					SetPriceCurrency(Vehicle, Price->Child("gold") != nullptr);
			}
		});
}
void VehicleWorkspace::ResetDocument(std::string const& Path)
{
	Document(Path);
	Commit([&] { Document(Path).Current = Document(Path).Initial; });
}
std::vector<std::pair<std::string, Bytes>> VehicleWorkspace::PendingFiles() const
{
	std::vector<std::pair<std::string, Bytes>> Result;
	for (auto const& [Path, Data] : Documents)
		if (Data.Current.Serialize() != Data.Loaded.Serialize())
			Result.emplace_back(Data.Path, Data.Current.Serialize());
	return Result;
}
bool VehicleWorkspace::HasPending() const
{
	return !PendingFiles().empty();
}
size_t VehicleWorkspace::ModifiedVehicleCount() const
{
	std::set<std::string> Names;
	for (auto const& [Path, Data] : Documents)
		if (Data.Current.Serialize() != Data.Initial.Serialize())
			if (auto Found = Owners.find(Path); Found != Owners.end())
				Names.insert(Found->second.begin(), Found->second.end());
	return Names.size();
}
void VehicleWorkspace::Persist() const
{
	if (CacheFile.empty())
		return;
	auto Existing = FileSystem::exists(CacheFile)
						? std::optional<uint32_t>(Crc32(ReadFile(CacheFile, 256ull * 1024 * 1024)))
						: std::nullopt;
	if (Existing != CacheCrc)
		throw std::runtime_error(
			"Another editor instance changed this working cache; reload its current data first");
	if (Applied.empty() && !ResetInProgress && !ApplyInProgress)
	{
		EnsureWritesAllowed();
		if (Existing)
			FileSystem::remove(NativePath(CacheFile));
		CacheCrc.reset();
		return;
	}
	YAML::Node Manifest;
	Manifest["format"] = "OffHanger.Workspace";
	Manifest["schema"] = 3;
	Manifest["reset_in_progress"] = ResetInProgress;
	Manifest["apply_in_progress"] = ApplyInProgress;
	Manifest["override_root"] = PathUtf8(OverrideRoot);
	std::vector<std::pair<std::string, Bytes>> Files;
	for (auto const& [Path, Data] : Documents)
	{
		if (!Applied.contains(Path))
			continue;
		YAML::Node Item;
		Item["path"] = Data.Path;
		Item["loaded_crc"] = Data.LoadedCrc;
		if (auto Found = Owners.find(Path); Found != Owners.end())
			for (auto const& Owner : Found->second)
				Item["vehicles"].push_back(Owner);
		Files.emplace_back("initial/" + Path, Data.InitialBytes);
		if (auto Found = Applied.find(Path); Found != Applied.end())
		{
			Item["applied"]["existed"] = Found->second.Existed;
			Item["applied"]["crc"] = Found->second.LastCrc;
			Item["applied"]["previous_crc"] = Found->second.PreviousCrc;
			if (Found->second.Existed)
				Files.emplace_back("preimage/" + Path, Found->second.Before);
		}
		Manifest["files"].push_back(Item);
	}
	auto Text = EmitYaml(Manifest);
	Files.emplace_back("workspace.yaml", Bytes(Text.begin(), Text.end()));
	WriteZip(CacheFile, Files);
	CacheCrc = Crc32(ReadFile(CacheFile, 256ull * 1024 * 1024));
}
void VehicleWorkspace::RestoreCache()
{
	Archive Cache(CacheFile);
	std::map<std::string, Bytes> Files;
	uint64_t Total{};
	for (auto const& Entry : Cache.Entries)
	{
		if ((Total += Entry.Size) > 256ull * 1024 * 1024)
			throw std::runtime_error("Working cache exceeds size limit");
		if (!Files.emplace(Key(Entry.Name), Cache.Read(Entry)).second)
			throw std::runtime_error("Duplicate cache entry");
	}
	auto const& Text = Files.at("workspace.yaml");
	auto Manifest = YAML::Load(std::string(Text.begin(), Text.end()));
	if (Manifest["format"].as<std::string>() != "OffHanger.Workspace" ||
		(Manifest["schema"].as<int>() != 2 && Manifest["schema"].as<int>() != 3) ||
		Lower(Manifest["override_root"].as<std::string>()) != Lower(PathUtf8(OverrideRoot)))
		throw std::runtime_error("Working cache belongs to a different game override directory");
	ResetInProgress = Manifest["reset_in_progress"].as<bool>(false);
	LegacyLedger = Manifest["schema"].as<int>() == 2;
	ApplyInProgress = Manifest["apply_in_progress"].as<bool>(false);
	for (auto Item : Manifest["files"])
	{
		if (!Item["applied"])
			continue; // Legacy pending drafts are deliberately not restored.
		auto Path = Item["path"].as<std::string>(), ResourceKey = Key(Path);
		auto& Data = Document(Path);
		Data.InitialBytes = Files.at("initial/" + ResourceKey);
		Data.Initial = DataSection::Parse(Data.InitialBytes);
		if (auto Record = Item["applied"])
		{
			AppliedFile File;
			File.Existed = Record["existed"].as<bool>();
			File.LastCrc = Record["crc"].as<uint32_t>();
			File.PreviousCrc = Record["previous_crc"].as<uint32_t>(0);
			if (File.Existed)
				File.Before = Files.at("preimage/" + ResourceKey);
			auto Target = OverrideRoot / Wide(Data.Path);
			auto Actual = FileSystem::exists(Target) ? Crc32(ReadFile(Target)) : 0;
			bool Restored =
				ResetInProgress && (File.Existed ? FileSystem::exists(Target) && Actual == Crc32(File.Before)
												 : !FileSystem::exists(Target));
			if (Actual != File.LastCrc && Actual != File.PreviousCrc && !Restored)
				throw std::runtime_error("An applied editor file was modified externally: " + Data.Path);
			Applied[ResourceKey] = std::move(File);
			if (FileSystem::is_regular_file(Target))
			{
				auto ActualBytes = ReadFile(Target);
				Data.Loaded = DataSection::Parse(ActualBytes);
				Data.LoadedCrc = Crc32(ActualBytes);
			}
		}
		Data.Current = Data.Loaded;
		ValidateStructure(Path, Data.Initial, Data.Current);
		for (auto Owner : Item["vehicles"])
			Owners[ResourceKey].insert(Owner.as<std::string>());
	}
}
std::vector<std::string> VehicleWorkspace::PreviewSave() const
{
	std::vector<std::string> Result;
	for (auto const& [Path, Data] : PendingFiles())
		Result.push_back(Path);
	return Result;
}
void VehicleWorkspace::SaveToGame(Progress const& Report)
{
	CacheFileLock Lock(CacheFile);
	if (!Catalogue)
		throw std::runtime_error("Load game data first");
	if (ResetInProgress || ApplyInProgress)
		throw std::runtime_error(
			"An interrupted reset must be completed with Reset to defaults before saving");
	if (GameRunning())
		throw std::runtime_error("Exit World of Tanks before saving vehicle data");
	auto Pending = PendingFiles();
	if (Pending.empty())
		return;
	auto Notify = [&](std::string const& Stage, size_t Completed)
	{
		if (Report)
			Report(Stage, Completed, Pending.size() + 4);
	};
	Notify("validate", 0);
	if (!Catalogue->Resources.GameRoot.empty() && !ResourceSignature.empty() &&
		ResourceIndex::GameSignature(Catalogue->Resources.GameRoot) != ResourceSignature)
		throw std::runtime_error("Game resources changed externally; reload before saving");
	auto PreviousApplied = Applied;
	auto PreviousDocuments = Documents;
	auto PreparedApplied = Applied;
	std::map<std::string, std::optional<Bytes>> Rollback;
	for (auto const& [Path, Data] : Pending)
	{
		auto const& DocumentData = Get(Path);
		ValidateStructure(Path, DocumentData.Initial, DocumentData.Current);
		if (auto Resource = Catalogue->Resources.Find(Path); Resource && !Applied.contains(Key(Path)))
			if (Crc32(Resource->Read()) != DocumentData.LoadedCrc)
				throw std::runtime_error("Loaded resource changed before saving: " + Path);
		auto Target = OverrideRoot / Wide(Path);
		Rollback[Path] = FileSystem::exists(Target) ? std::optional<Bytes>(ReadFile(Target)) : std::nullopt;
		auto ResourceKey = Key(Path);
		if (!Applied.contains(ResourceKey) && Rollback[Path] &&
			Crc32(*Rollback[Path]) != DocumentData.LoadedCrc)
			throw std::runtime_error("The target override differs from the loaded resource: " + Path);
		if (auto Written = Applied.find(ResourceKey); Written != Applied.end())
			if ((Rollback[Path] ? Crc32(*Rollback[Path]) : 0) != Written->second.LastCrc &&
				(Rollback[Path] ? Crc32(*Rollback[Path]) : 0) != Written->second.PreviousCrc)
				throw std::runtime_error("Applied resource changed before saving: " + Path);
		if (!PreparedApplied.contains(ResourceKey))
			PreparedApplied[ResourceKey] = {Rollback[Path].has_value(), Rollback[Path].value_or(Bytes{}), 0,
											0};
		PreparedApplied[ResourceKey].PreviousCrc = Rollback[Path] ? Crc32(*Rollback[Path]) : 0;
		PreparedApplied[ResourceKey].LastCrc = Crc32(Data);
		DataSection::Parse(Data);
	}
	GameDataCache GameCache(Catalogue->Resources.GameRoot);
	GameCacheResult CacheResult;
	try
	{
		Applied = std::move(PreparedApplied);
		ApplyInProgress = true;
		Notify("journal", 1);
		Persist(); // Durable preimages are written before the first game file.
		size_t Completed = 1;
		for (auto const& [Path, Data] : Pending)
		{
			AtomicWrite(OverrideRoot / Wide(Path), Data, false);
			Notify("write", ++Completed);
		}
		Notify("cache", ++Completed);
		CacheResult = GameCache.Refresh([&](auto const& Stage) { Notify(Stage, Completed); });
		for (auto const& [Path, Data] : Pending)
		{
			auto& DocumentData = Document(Path);
			DocumentData.Loaded = DocumentData.Current;
			DocumentData.LoadedCrc = Crc32(Data);
		}
		if (!Catalogue->Resources.GameRoot.empty())
			ResourceSignature = ResourceIndex::GameSignature(Catalogue->Resources.GameRoot);
		ApplyInProgress = false;
		Notify("finalize", ++Completed);
		Persist();
		Notify("done", Pending.size() + 4);
		LastCache = std::move(CacheResult);
	}
	catch (...)
	{
		auto Failure = std::current_exception();
		if (GameRunning())
			std::rethrow_exception(Failure); // Keep the durable recovery ledger; perform no writes.
		for (auto const& [Path, Data] : Rollback)
			if (Data)
				AtomicWrite(OverrideRoot / Wide(Path), *Data, false);
			else
				FileSystem::remove(NativePath(OverrideRoot / Wide(Path)));
		GameCache.Restore();
		Applied = std::move(PreviousApplied);
		Documents = std::move(PreviousDocuments);
		ApplyInProgress = false;
		if (!Catalogue->Resources.GameRoot.empty())
			ResourceSignature = ResourceIndex::GameSignature(Catalogue->Resources.GameRoot);
		Persist();
		std::rethrow_exception(Failure);
	}
	++ChangeRevision;
}
void VehicleWorkspace::ResetAll(Progress const& Report)
{
	CacheFileLock Lock(CacheFile);
	if (GameRunning())
		throw std::runtime_error("Exit World of Tanks before resetting applied data");
	auto Total = Applied.size() + 4;
	auto Notify = [&](std::string const& Stage, size_t Completed)
	{
		if (Report)
			Report(Stage, Completed, Total);
	};
	Notify("validate", 0);
	std::map<std::string, std::optional<Bytes>> Rollback;
	for (auto const& [Path, File] : Applied)
	{
		auto Target = OverrideRoot / Wide(Document(Path).Path);
		auto Exists = FileSystem::exists(Target);
		auto Data = Exists ? ReadFile(Target) : Bytes{};
		auto Actual = Exists ? Crc32(Data) : 0;
		bool Restored = ResetInProgress && (File.Existed ? Exists && Actual == Crc32(File.Before) : !Exists);
		if (Actual != File.LastCrc && Actual != File.PreviousCrc && !Restored)
			throw std::runtime_error("Reset refused because another tool changed: " + Path);
		Rollback[Path] = Exists ? std::optional<Bytes>(std::move(Data)) : std::nullopt;
	}
	auto Before = Documents;
	auto BeforeApplied = Applied;
	auto BeforeOwners = Owners;
	auto BeforeReset = ResetInProgress;
	auto BeforeApply = ApplyInProgress;
	GameDataCache GameCache(Catalogue && !Applied.empty() ? Catalogue->Resources.GameRoot
														  : FileSystem::path{});
	GameCacheResult CacheResult;
	try
	{
		// Journal reset intent before touching game files. A later launch can
		// recognize both applied bytes and already restored preimages.
		ResetInProgress = true;
		Notify("journal", 1);
		Persist();
		size_t Completed = 1;
		for (auto const& [Path, File] : Applied)
		{
			auto Target = OverrideRoot / Wide(Document(Path).Path);
			if (File.Existed)
				AtomicWrite(Target, File.Before, false);
			else
			{
				EnsureWritesAllowed();
				FileSystem::remove(NativePath(Target));
			}
			Notify("restore", ++Completed);
		}
		Notify("cache", ++Completed);
		if (Catalogue && !Applied.empty())
			CacheResult = GameCache.Refresh([&](auto const& Stage) { Notify(Stage, Completed); });
		for (auto& [Path, Data] : Documents)
		{
			Data.Current = Data.Loaded = Data.Initial;
			Data.LoadedCrc = Crc32(Data.InitialBytes);
		}
		Applied.clear();
		Owners.clear();
		ResetInProgress = false;
		ApplyInProgress = false;
		Notify("finalize", ++Completed);
		Persist();
		if (Catalogue && !Catalogue->Resources.GameRoot.empty())
			ResourceSignature = ResourceIndex::GameSignature(Catalogue->Resources.GameRoot);
		Notify("done", Total);
		LastCache = std::move(CacheResult);
	}
	catch (...)
	{
		auto Failure = std::current_exception();
		if (GameRunning())
			std::rethrow_exception(Failure);
		for (auto const& [Path, Data] : Rollback)
			if (Data)
				AtomicWrite(OverrideRoot / Wide(Before.at(Path).Path), *Data, false);
			else
				FileSystem::remove(NativePath(OverrideRoot / Wide(Before.at(Path).Path)));
		GameCache.Restore();
		Documents = std::move(Before);
		Applied = std::move(BeforeApplied);
		Owners = std::move(BeforeOwners);
		ResetInProgress = BeforeReset;
		ApplyInProgress = BeforeApply;
		if (Catalogue && !Catalogue->Resources.GameRoot.empty())
			ResourceSignature = ResourceIndex::GameSignature(Catalogue->Resources.GameRoot);
		Persist();
		std::rethrow_exception(Failure);
	}
	++ChangeRevision;
}
void VehicleWorkspace::DiscardPending()
{
	for (auto& [Path, Data] : Documents)
		Data.Current = Data.Loaded;
	++ChangeRevision;
}
void VehicleWorkspace::ExportChanges(FileSystem::path const& Target, Vehicle const* Selected)
{
	if (!Catalogue)
		throw std::runtime_error("Load game data first");
	YAML::Node Manifest;
	Manifest["format"] = "OffHanger.VehicleChanges";
	Manifest["schema"] = 2;
	for (auto const& Vehicle : Catalogue->Vehicles)
	{
		if (Selected && Selected->Name != Vehicle.Name)
			continue;
		if (!Selected && std::none_of(Owners.begin(), Owners.end(),
									  [&](auto const& Pair) { return Pair.second.contains(Vehicle.Name); }))
			continue;
		YAML::Node Record;
		Record["name"] = Vehicle.Name;
		Record["nation"] = Vehicle.Nation;
		Record["type_cd"] = Vehicle.TypeCd;
		if (!Vehicle.ListPath.empty())
		{
			auto& Metadata = Document(Vehicle.ListPath);
			auto Entry = Metadata.Initial.Root.Child(Vehicle.Model);
			auto Price = Entry ? Entry->Child("price") : nullptr;
			if (Price && bool(Price->Child("gold")) != PriceCurrency(Vehicle))
			{
				Record["currency"]["before"] = bool(Price->Child("gold"));
				Record["currency"]["after"] = PriceCurrency(Vehicle);
			}
		}
		std::set<std::string> Seen;
		for (auto const& Field : Values(Vehicle))
		{
			auto ResourceKey = Key(Field.Document);
			if (!Owners[ResourceKey].contains(Vehicle.Name) && !Selected)
				continue;
			auto const& Data = Document(Field.Document);
			auto AddressNames = Names(Data.Current, Field.Address);
			auto const& Initial = Resolve(Data.Initial, Locate(Data.Initial, AddressNames));
			if (Field.ReadOnly || Initial.Value == Field.Value)
				continue;
			auto Identity = ResourceKey + Field.DisplayPath;
			if (!Seen.insert(Identity).second)
				continue;
			YAML::Node Change;
			Change["document"] = Field.Document;
			Change["address"] = Encode(AddressNames);
			Change["before"] = Initial.Value;
			Change["after"] = Field.Value;
			Record["values"].push_back(Change);
		}
		if ((Record["values"] && Record["values"].size()) || Record["currency"])
			Manifest["vehicles"].push_back(Record);
	}
	if (!Manifest["vehicles"] || !Manifest["vehicles"].size())
		throw std::runtime_error("There are no modified vehicle values to export");
	auto Text = EmitYaml(Manifest);
	WriteZip(Target, {{"vehicle-changes.yaml", Bytes(Text.begin(), Text.end())}});
}
size_t VehicleWorkspace::ImportChanges(FileSystem::path const& Source, Vehicle const* Selected)
{
	Archive Package(Source);
	if (Package.Entries.size() != 1 || Package.Entries[0].Name != "vehicle-changes.yaml" ||
		Package.Entries[0].Size > 32ull * 1024 * 1024)
		throw std::runtime_error("Not a supported vehicle changes package");
	auto Data = Package.Read(Package.Entries[0]);
	auto Manifest = YAML::Load(std::string(Data.begin(), Data.end()));
	if (Manifest["format"].as<std::string>() != "OffHanger.VehicleChanges" ||
		Manifest["schema"].as<int>() != 2)
		throw std::runtime_error("Unsupported vehicle package schema");
	auto Records = Manifest["vehicles"];
	if (!Records.IsSequence() || Records.size() > 10000 || (Selected && Records.size() != 1))
		throw std::runtime_error("The selected-vehicle import requires exactly one matching vehicle");
	size_t Count{};
	Commit(
		[&]
		{
			std::set<std::string> UniqueVehicles;
			for (auto Record : Records)
			{
				auto Name = Record["name"].as<std::string>();
				auto Vehicle = Catalogue->ByName(Name);
				if (!Vehicle || !UniqueVehicles.insert(Name).second || (Selected && Selected->Name != Name) ||
					Record["nation"].as<std::string>() != Vehicle->Nation ||
					Record["type_cd"].as<uint32_t>() != Vehicle->TypeCd)
					throw std::runtime_error("Vehicle identity does not match this installation: " + Name);
				if (Record["currency"])
				{
					auto Before = Record["currency"]["before"].as<bool>();
					auto After = Record["currency"]["after"].as<bool>();
					if (PriceCurrency(*Vehicle) != Before && PriceCurrency(*Vehicle) != After)
						throw std::runtime_error("Target price currency changed");
					SetPriceCurrency(*Vehicle, After);
					++Count;
				}
				if ((Record["values"] && !Record["values"].IsSequence()) || Record["values"].size() > 100000)
					throw std::runtime_error("Invalid vehicle value count");
				for (auto Change : Record["values"])
				{
					auto Path = Change["document"].as<std::string>();
					Key(Path);
					auto& DocumentData = Document(Path);
					auto Address = Locate(DocumentData.Current, Decode(Change["address"]));
					auto const& Current = Resolve(DocumentData.Current, Address);
					auto Value = Change["after"].as<std::string>();
					if (Current.Value != Change["before"].as<std::string>() && Current.Value != Value)
						throw std::runtime_error("Conflicting target value: " + Path +
												 Display(Names(DocumentData.Current, Address)));
					Edit(*Vehicle, Path, Address, Value);
					++Count;
				}
			}
		});
	return Count;
}
} // namespace OffHanger
