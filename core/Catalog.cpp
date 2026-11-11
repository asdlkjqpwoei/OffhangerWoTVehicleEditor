#include "Catalog.h"
#include "pugixml.hpp"
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
namespace OffHanger
{
static std::string Normalize(std::string S)
{
	return NormalizeResourcePath(std::move(S));
}
static bool Relevant(std::string const& Name)
{
	auto S = Lower(Name);
	std::replace(S.begin(), S.end(), '\\', '/');
	return (S.ends_with(".xml") &&
			(S.find("scripts/item_defs/") != S.npos || S.ends_with("extension.xml"))) ||
		   S.ends_with(".mo") || S.ends_with(".model") || S.ends_with(".visual_processed") ||
		   S.ends_with(".visual") || S.ends_with(".primitives_processed") || S.ends_with(".primitives") ||
		   S.ends_with(".hkx") || S.ends_with(".havok");
}
Bytes Resource::Read() const
{
	if (MemoryData)
		return *MemoryData;
	return Archive ? Archive->Read(Entry) : ReadFile(File, 64ull * 1024 * 1024);
}
void ResourceIndex::Add(Resource R)
{
	std::replace(R.Path.begin(), R.Path.end(), '\\', '/');
	while (R.Path.starts_with("./"))
		R.Path.erase(0, 2);
	auto Key = Normalize(R.Path);
	auto It = Files.find(Key);
	if (It == Files.end() || It->second.Priority <= R.Priority)
		Files[Key] = std::move(R);
}
Resource const* ResourceIndex::Find(std::string Path) const
{
	if (Path.empty())
		return nullptr;
	auto It = Files.find(Normalize(std::move(Path)));
	return It == Files.end() ? nullptr : &It->second;
}
std::vector<Resource const*> ResourceIndex::Suffix(std::string const& Ending) const
{
	std::vector<Resource const*> Out;
	for (auto const& [EntryKey, ResourceEntry] : Files)
		if (EntryKey.ends_with(Ending))
			Out.push_back(&ResourceEntry);
	return Out;
}
void ResourceIndex::Scan(FileSystem::path const& Game, std::function<void(std::string const&)> Progress)
{
	Files.clear();
	Diagnostics.clear();
	GameRoot = FileSystem::weakly_canonical(Game);
	auto Paths = DataSection::Parse(ReadFile(GameRoot / L"paths.xml"));
	auto Definitions = Paths.Root.Child("Paths");
	if (!Definitions)
		throw std::runtime_error("paths.xml has no Paths section");
	Version.clear();
	if (FileSystem::exists(GameRoot / L"version.xml"))
	{
		try
		{
			auto Ver = DataSection::Parse(ReadFile(GameRoot / L"version.xml"));
			Version = Ver.Root.Text("version");
		}
		catch (...)
		{
		}
	}
	std::vector<std::pair<FileSystem::path, int>> Roots, ModRoots;
	int Layer = 0;
	for (auto const& Path : Definitions->Children)
		if (Path.Name == "Path")
		{
			auto Location = (GameRoot / FileSystem::path(Wide(Trim(Path.Value)))).lexically_normal();
			bool Mods = false;
			for (auto const& [AttributeName, AttributeValue] : Path.Attributes)
				if (AttributeName == "mask" && AttributeValue.find("wotmod") != AttributeValue.npos)
					Mods = true;
			if (Mods)
				ModRoots.emplace_back(Location, 1000000 - Layer * 100000);
			else
				Roots.emplace_back(Location, 1000000 - Layer * 100000);
			++Layer;
		}
	auto Packages = Definitions->Child("Packages");
	if (Packages)
	{
		int Priority = 0;
		for (auto It = Packages->Children.rbegin(); It != Packages->Children.rend(); ++It)
		{
			if (It->Name != "Package")
				continue;
			auto P = GameRoot / FileSystem::path(Wide(Trim(It->Value)));
			if (!FileSystem::is_regular_file(P))
				continue;
			try
			{
				auto Archive = std::make_shared<OffHanger::Archive>(P, Relevant);
				for (auto const& E : Archive->Entries)
					Add({E.Name, "Client / " + PathUtf8(P.filename()), {}, Archive, E, Priority});
			}
			catch (std::exception const& E)
			{
				Diagnostics.push_back(PathUtf8(P.filename()) + ": " + E.what());
			}
			++Priority;
		}
	}
	if (Progress)
		Progress("Indexing installed mod archives");
	for (auto const& [ModFolder, LayerPriority] : ModRoots)
	{
		if (!FileSystem::is_directory(ModFolder))
			continue;
		std::vector<FileSystem::path> Mods;
		for (auto const& E : FileSystem::recursive_directory_iterator(
				 ModFolder, FileSystem::directory_options::skip_permission_denied))
			if (E.is_regular_file() && Lower(PathUtf8(E.path().extension())) == ".wotmod")
				Mods.push_back(E.path());
		std::sort(Mods.begin(), Mods.end());
		auto Order = ModFolder / L"load_order.xml";
		if (FileSystem::is_regular_file(Order))
		{
			try
			{
				auto Doc = DataSection::Parse(ReadFile(Order));
				std::vector<FileSystem::path> ExplicitOrder;
				std::function<void(Section const&)> Walk = [&](auto const& S)
				{
					if (Lower(S.Value).ends_with(".wotmod"))
					{
						auto P = ModFolder / FileSystem::path(Wide(S.Value));
						auto I = std::find(Mods.begin(), Mods.end(), P);
						if (I != Mods.end())
						{
							ExplicitOrder.push_back(*I);
							Mods.erase(I);
						}
					}
					for (auto const& C : S.Children)
						Walk(C);
				};
				Walk(Doc.Root);
				Mods.insert(Mods.end(), ExplicitOrder.begin(), ExplicitOrder.end());
			}
			catch (std::exception const& E)
			{
				Diagnostics.push_back(E.what());
			}
		}
		int Offset = 0;
		for (auto const& P : Mods)
		{
			try
			{
				auto Archive = std::make_shared<OffHanger::Archive>(
					P, [](auto const& N) { return Lower(N).starts_with("res/") && Relevant(N); });
				for (auto const& E : Archive->Entries)
					Add({E.Name.substr(4),
						 "Mod / " + PathUtf8(P.filename()),
						 {},
						 Archive,
						 E,
						 LayerPriority + Offset});
			}
			catch (std::exception const& E)
			{
				Diagnostics.push_back(PathUtf8(P.filename()) + ": " + E.what());
			}
			++Offset;
		}
	}
	if (Progress)
		Progress("Indexing loose client and extension definitions");
	Roots.emplace_back(GameRoot / L"res", 10000);
	for (auto const& [ResourceRoot, LayerPriority] : Roots)
	{
		if (!FileSystem::is_directory(ResourceRoot))
			continue;
		std::vector<FileSystem::path> ScanRoots;
		if (FileSystem::is_directory(ResourceRoot / L"scripts/item_defs"))
			ScanRoots.push_back(ResourceRoot / L"scripts/item_defs");
		if (FileSystem::is_directory(ResourceRoot / L"vehicles"))
			ScanRoots.push_back(ResourceRoot / L"vehicles");
		if (FileSystem::is_directory(ResourceRoot / L"text"))
			ScanRoots.push_back(ResourceRoot / L"text");
		for (auto const& E : FileSystem::directory_iterator(
				 ResourceRoot, FileSystem::directory_options::skip_permission_denied))
			if (E.is_directory() && FileSystem::is_regular_file(E.path() / L"extension.xml"))
			{
				auto Rel = PathUtf8((E.path() / L"extension.xml").lexically_relative(ResourceRoot));
				Add({Rel,
					 "Extension / " + PathUtf8(E.path().filename()),
					 E.path() / L"extension.xml",
					 {},
					 {},
					 LayerPriority});
				if (FileSystem::is_directory(E.path() / L"scripts/item_defs"))
					ScanRoots.push_back(E.path() / L"scripts/item_defs");
				if (FileSystem::is_directory(E.path() / L"vehicles"))
					ScanRoots.push_back(E.path() / L"vehicles");
				if (FileSystem::is_directory(E.path() / L"text"))
					ScanRoots.push_back(E.path() / L"text");
			}
		for (auto const& Folder : ScanRoots)
			for (auto const& E : FileSystem::recursive_directory_iterator(
					 Folder, FileSystem::directory_options::skip_permission_denied))
				if (E.is_regular_file())
				{
					auto Rel = PathUtf8(E.path().lexically_relative(ResourceRoot));
					if (Relevant(Rel))
						Add({Rel,
							 "Loose override / " + PathUtf8(ResourceRoot.filename()),
							 E.path(),
							 {},
							 {},
							 LayerPriority});
				}
	}
	if (Files.empty())
		throw std::runtime_error("No vehicle definitions found in this game folder");
	LoadedSignature = GameSignature(GameRoot);
}
static int NationIndex(std::string const& N)
{
	static std::vector<std::string> const Names = {"ussr",	"germany", "usa",	 "china",  "france", "uk",
												   "japan", "czech",   "sweden", "poland", "italy"};
	auto I = std::find(Names.begin(), Names.end(), N);
	return I == Names.end() ? -1 : int(I - Names.begin());
}
static std::vector<std::string> Split(std::string const& S, char Delim)
{
	std::vector<std::string> V;
	std::istringstream In(S);
	std::string Part;
	while (std::getline(In, Part, Delim))
		V.push_back(Part);
	return V;
}
static bool HiddenTags(std::string const& Tags)
{
	auto S = " " + Tags + " ";
	return S.find(" secret ") != S.npos || S.find(" observer ") != S.npos || S.find(" testTank ") != S.npos ||
		   S.find(" event_battles ") != S.npos;
}
void Catalog::LoadTranslations()
{
	Translations.clear();
	for (auto const* R : Resources.Suffix(".mo"))
	{
		try
		{
			auto D = R->Read();
			if (D.size() < 28)
				continue;
			bool Swap = U32(D, 0) == 0xde120495u;
			auto Get = [&](size_t P)
			{
				auto N = U32(D, P);
				return Swap ? ((N >> 24) | ((N >> 8) & 0xff00) | ((N << 8) & 0xff0000) | (N << 24)) : N;
			};
			if (Get(0) != 0x950412deu)
				continue;
			auto Count = Get(8), Org = Get(12), Tr = Get(16);
			if (Count > 200000 || uint64_t(Org) + Count * 8ull > D.size() ||
				uint64_t(Tr) + Count * 8ull > D.size())
				throw std::runtime_error("Invalid MO table");
			auto Stem = PathUtf8(FileSystem::path(Wide(R->Path)).stem());
			auto& Table = Translations[Stem];
			for (uint32_t I = 0; I < Count; ++I)
			{
				auto A = Get(Org + I * 8), B = Get(Org + I * 8 + 4), C = Get(Tr + I * 8),
					 E = Get(Tr + I * 8 + 4);
				if (uint64_t(B) + A > D.size() || uint64_t(E) + C > D.size())
					throw std::runtime_error("Invalid MO string");
				std::string Key(reinterpret_cast<char const*>(D.data() + B), A),
					Text(reinterpret_cast<char const*>(D.data() + E), C);
				if (!Text.empty())
					Table[Key] = Text.substr(0, Text.find('\0'));
			}
		}
		catch (std::exception const& E)
		{
			Diagnostics.push_back(R->Path + ": " + E.what());
		}
	}
}
std::string Catalog::Localization(std::string Key) const
{
	if (!Key.starts_with('#'))
		return Key;
	auto Colon = Key.find(':');
	if (Colon == Key.npos)
		return Key;
	auto Table = Translations.find(Key.substr(1, Colon - 1));
	if (Table != Translations.end())
	{
		auto Value = Table->second.find(Key.substr(Colon + 1));
		if (Value != Table->second.end())
			return Value->second;
	}
	return Key;
}
void Catalog::Scan(FileSystem::path const& Game, std::function<void(std::string const&)> Progress)
{
	Resources.Scan(Game, Progress);
	Rebuild(Progress);
}
void Catalog::Rebuild(std::function<void(std::string const&)> Progress)
{
	Vehicles.clear();
	Diagnostics.clear();
	Diagnostics = Resources.Diagnostics;
	LoadTranslations();
	std::map<std::string, size_t> Byname;
	std::set<std::string> KnownDefinitions;
	for (auto const& [EntryKey, ResourceEntry] : Resources.Files)
	{
		if (!EntryKey.ends_with("/list.xml") || EntryKey.find("scripts/item_defs/vehicles/") == EntryKey.npos)
			continue;
		auto Prefix = EntryKey.substr(0, EntryKey.find("scripts/item_defs/vehicles/"));
		auto Parts = Split(EntryKey, '/');
		if (Parts.size() < 2)
			continue;
		auto Nation = Parts[Parts.size() - 2];
		auto Ni = NationIndex(Nation);
		if (Ni < 0)
			continue;
		bool Enabled = true;
		if (!Prefix.empty())
		{
			auto Ext = Resources.Find(Prefix + "extension.xml");
			if (Ext)
			{
				try
				{
					auto Flag = Lower(DataSection::Parse(Ext->Read()).Root.Text("IsEnabled", "true"));
					Enabled = Flag == "true" || Flag == "1";
				}
				catch (...)
				{
					Enabled = false;
				}
			}
			else
				Enabled = false;
		}
		try
		{
			auto List = DataSection::Parse(ResourceEntry.Read());
			for (auto const& Node : List.Root.Children)
			{
				auto IdText = Node.Text("id");
				if (IdText.empty())
					continue;
				Vehicle V;
				V.Nation = Nation;
				V.NationId = Ni;
				V.Model = Node.Name;
				V.Name = Nation + ":" + Node.Name;
				V.Id = uint32_t(std::stoul(IdText));
				if (V.Id > 65535)
					throw std::runtime_error("Vehicle id exceeds compact descriptor range");
				V.TypeCd = (V.Id << 8) | (uint32_t(Ni) << 4) | 1;
				V.Tier = std::stoi(Node.Text("level", "0"));
				V.Tags = Node.Text("tags");
				V.DisplayName = Localization(Node.Text("userString", Node.Name));
				if (V.DisplayName.starts_with('#'))
					V.DisplayName = Node.Name;
				V.Hidden = HiddenTags(V.Tags) || Lower(Node.Text("notInShop")) == "true";
				V.Extension = !Prefix.empty();
				V.Enabled = Enabled;
				V.Listed = true;
				V.ListPath = ResourceEntry.Path;
				V.DefinitionPath = EntryKey.substr(0, EntryKey.size() - 8) + Node.Name + ".xml";
				if (auto Definition = Resources.Find(V.DefinitionPath))
				{
					V.DefinitionPath = Definition->Path;
					V.Source = Definition->Source;
				}
				else
					V.Source = ResourceEntry.Source + " / No vehicle definition";
				KnownDefinitions.insert(Normalize(V.DefinitionPath));
				auto It = Byname.find(V.Name);
				if (It == Byname.end())
				{
					Byname[V.Name] = Vehicles.size();
					Vehicles.push_back(std::move(V));
				}
				else
				{
					Diagnostics.push_back("Duplicate registered name: " + V.Name);
					Vehicles[It->second].Conflict = true;
				}
			}
		}
		catch (std::exception const& E)
		{
			Diagnostics.push_back(ResourceEntry.Path + ": " + E.what());
		}
	}
	// Discover definitions omitted from list.xml without claiming they are registered.
	for (auto const& [EntryKey, ResourceEntry] : Resources.Files)
	{
		if (KnownDefinitions.contains(EntryKey) || !EntryKey.ends_with(".xml") ||
			EntryKey.find("scripts/item_defs/vehicles/") == EntryKey.npos ||
			EntryKey.find("/components/") != EntryKey.npos)
			continue;
		auto Parts = Split(ResourceEntry.Path, '/');
		if (Parts.size() < 2)
			continue;
		auto Ni = NationIndex(Parts[Parts.size() - 2]);
		if (Ni < 0 || Lower(Parts.back()) == "list.xml" || Lower(Parts.back()) == "customization.xml")
			continue;
		try
		{
			auto Doc = DataSection::Parse(ResourceEntry.Read());
			if (!Doc.Root.Child("hull") || !Doc.Root.Child("chassis"))
				continue;
			auto Model = Parts.back().substr(0, Parts.back().size() - 4), Nation = Parts[Parts.size() - 2];
			if (Byname.contains(Nation + ":" + Model))
				continue;
			Vehicle V;
			V.Nation = Nation;
			V.NationId = Ni;
			V.Model = Model;
			V.Name = Nation + ":" + Model;
			V.DisplayName = Model;
			V.DefinitionPath = ResourceEntry.Path;
			V.Source = ResourceEntry.Source;
			V.Hidden = true;
			V.Enabled = false;
			V.Extension = !EntryKey.starts_with("scripts/");
			Vehicles.push_back(std::move(V));
		}
		catch (std::exception const& E)
		{
			Diagnostics.push_back(ResourceEntry.Path + ": " + E.what());
		}
	}
	std::map<uint32_t, std::vector<size_t>> Ids;
	for (size_t I = 0; I < Vehicles.size(); ++I)
		if (Vehicles[I].TypeCd && Vehicles[I].Enabled)
			Ids[Vehicles[I].TypeCd].push_back(I);
	for (auto const& [TypeId, IdEntries] : Ids)
		if (IdEntries.size() > 1)
			for (auto I : IdEntries)
				Vehicles[I].Conflict = true;
	std::sort(Vehicles.begin(), Vehicles.end(), [](auto const& A, auto const& B) { return A.Name < B.Name; });
	if (Progress)
		Progress("Vehicle catalogue ready");
}
Vehicle const* Catalog::ByName(std::string const& N) const
{
	for (auto const& V : Vehicles)
		if (V.Name == N)
			return &V;
	return nullptr;
}
Vehicle const* Catalog::ByCd(uint32_t Cd) const
{
	if (!Cd || (Cd & 15) != 1)
		return nullptr;
	Vehicle const* Result = nullptr;
	for (auto const& V : Vehicles)
		if (V.TypeCd == Cd)
		{
			if (!Result || (!Result->Enabled && V.Enabled))
				Result = &V;
		}
	return Result;
}
std::vector<size_t> Catalog::Search(std::string const& Q, int Origin) const
{
	auto Query = Lower(Q);
	std::vector<size_t> Out;
	for (size_t I = 0; I < Vehicles.size(); ++I)
	{
		auto const& V = Vehicles[I];
		if (Origin == 1 && V.Extension)
			continue;
		if (Origin == 2 && !V.Extension)
			continue;
		if (Origin == 3 && !V.Hidden)
			continue;
		if (Origin == 4 && V.Listed)
			continue;
		if (!Query.empty() && Lower(V.Name + " " + V.DisplayName + " " + V.Tags + " " +
									std::to_string(V.Tier) + " " + std::to_string(V.TypeCd))
									  .find(Query) == std::string::npos)
			continue;
		Out.push_back(I);
	}
	return Out;
}
DataSection Catalog::LoadDefinition(Vehicle const& V) const
{
	auto R = Resources.Find(V.DefinitionPath);
	if (!R)
		throw std::runtime_error("This vehicle has no readable XML definition in the resource index");
	return DataSection::Parse(R->Read());
}
std::vector<std::pair<std::string, std::string>> Catalog::Documents(Vehicle const& V) const
{
	std::vector<std::pair<std::string, std::string>> Out;
	if (Resources.Find(V.DefinitionPath))
		Out.emplace_back("Vehicle definition", V.DefinitionPath);
	if (Resources.Find(V.ListPath))
		Out.emplace_back("Catalogue metadata / tier, price, tags", V.ListPath);
	auto Prefix = V.DefinitionPath.substr(0, V.DefinitionPath.find("scripts/item_defs/vehicles/"));
	for (auto const& Component : {"engines", "guns", "shells", "radios", "fuelTanks", "chassis", "turrets"})
	{
		auto Path = Prefix + "scripts/item_defs/vehicles/" + V.Nation + "/components/" + Component + ".xml";
		if (auto ExtensionResource = Resources.Find(Path))
			Out.emplace_back(std::string("Shared component / ") + Component, ExtensionResource->Path);
		else if (auto BaseResource = Resources.Find("scripts/item_defs/vehicles/" + V.Nation +
													"/components/" + Component + ".xml"))
			Out.emplace_back(std::string("Shared component / ") + Component, BaseResource->Path);
	}
	return Out;
}
Bytes Catalog::StockDescriptor(Vehicle const& V) const
{
	if (!V.StockDescriptor.empty())
		return V.StockDescriptor;
	if (!V.Listed || !V.Enabled || V.Conflict)
		throw std::runtime_error("Unregistered, disabled or conflicting vehicle: import a current runtime "
								 "registry before adding it");
	auto Doc = LoadDefinition(V);
	auto const& Root = Doc.Root;
	auto First = [&](std::string const& Name) -> Section const&
	{
		auto Section = Root.Child(Name);
		if (!Section || Section->Children.empty())
			throw std::runtime_error("Cannot determine stock module " + Name +
									 "; check the installed module definitions");
		return Section->Children.front();
	};
	auto ComponentId = [&](Section const& Local, std::string const& Group) -> uint16_t
	{
		auto Id = Local.Text("id");
		if (Id.empty() && Group == "chassis")
			Id = Local.Text("id");
		if (Id.empty())
		{
			auto Docs = Documents(V);
			for (auto const& [ComponentLabel, ComponentPath] : Docs)
				if (Lower(ComponentPath).ends_with("/" + Lower(Group) + ".xml"))
				{
					auto Data = DataSection::Parse(Resources.Find(ComponentPath)->Read());
					auto N = Data.Root.Child(Local.Name);
					if (N)
					{
						Id = N->Text("id");
						if (!Id.empty())
							break;
					}
				}
		}
		if (Id.empty())
			throw std::runtime_error("Missing id for " + Local.Name +
									 "; check the installed module definitions");
		auto Value = std::stoul(Id);
		if (Value > 65535)
			throw std::runtime_error("Module id exceeds descriptor range");
		return uint16_t(Value);
	};
	Bytes Result;
	uint8_t Header = uint8_t((V.NationId << 4) | 1);
	if (V.Id > 255)
		Header |= 2;
	Result.push_back(Header);
	Result.push_back(uint8_t(V.Id));
	if (V.Id > 255)
		Result.push_back(uint8_t(V.Id >> 8));
	Put16(Result, ComponentId(First("chassis"), "chassis"));
	Put16(Result, ComponentId(First("engines"), "engines"));
	Put16(Result, ComponentId(First("fuelTanks"), "fuelTanks"));
	Put16(Result, ComponentId(First("radios"), "radios"));
	for (int N = 0;; ++N)
	{
		auto Group = Root.Child("turrets" + std::to_string(N));
		if (!Group)
			break;
		if (Group->Children.empty())
			throw std::runtime_error("Empty turret group");
		auto const& Turret = Group->Children.front();
		Put16(Result, ComponentId(Turret, "turrets"));
		auto Guns = Turret.Child("guns");
		if (!Guns || Guns->Children.empty())
			throw std::runtime_error("No stock gun definition");
		Put16(Result, ComponentId(Guns->Children.front(), "guns"));
	}
	Result.push_back(0);
	return Result;
}
uint32_t VehicleCd(Bytes const& D)
{
	if (D.size() < 2)
		throw std::runtime_error("Truncated vehicle descriptor");
	if ((D[0] & 15) != 1 && (D[0] & 15) != 3)
		throw std::runtime_error("Not a vehicle descriptor");
	uint32_t Id = D[1];
	if (D[0] & 2)
	{
		if (D.size() < 3)
			throw std::runtime_error("Truncated extended vehicle descriptor");
		Id |= uint32_t(D[2]) << 8;
	}
	return (Id << 8) | (uint32_t(D[0] >> 4) << 4) | 1;
}
} // namespace OffHanger
