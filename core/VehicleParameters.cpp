#include "VehicleParameters.h"
#include <cctype>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <numeric>
namespace OffHanger
{
namespace
{
std::vector<double> ParseNumbers(std::string const& Text)
{
	std::istringstream Stream(Text);
	std::vector<double> Values;
	double Number;
	while (Stream >> Number)
	{
		if (!std::isfinite(Number))
			throw std::runtime_error("Use finite numbers");
		Values.push_back(Number);
	}
	if (!Stream.eof() || Values.empty())
		throw std::runtime_error("A numeric value is required");
	return Values;
}
std::string FormatNumber(double Number)
{
	std::ostringstream Result;
	Result << std::setprecision(9) << Number;
	return Result.str();
}
std::string FormatNumbers(std::vector<double> const& Numbers)
{
	std::string Result;
	for (auto Number : Numbers)
		Result += (Result.empty() ? "" : " ") + FormatNumber(Number);
	return Result;
}
} // namespace
ParameterDefinitions::ParameterDefinitions(FileSystem::path const& Path) : Definitions(ReadYaml(Path))
{
	if (Definitions["schema"].as<int>() != 1 || !Definitions["fields"].IsMap() ||
		!Definitions["taxonomy"].IsMap())
		throw std::runtime_error("Unsupported parameter definitions YAML");
	auto Evidence = Path.parent_path() / L"vehicles.yaml";
	if (FileSystem::is_regular_file(Evidence))
	{
		auto Data = ReadYaml(Evidence, 2 * 1024 * 1024);
		std::unordered_map<std::string, std::string> Sources;
		if (auto Entries = Data["sources"]; Entries.IsMap())
			for (auto Entry : Entries)
				Sources.emplace(Entry.first.as<std::string>(), Entry.second.as<std::string>());
		if (auto Entries = Data["vehicles"]; Entries.IsMap())
			for (auto Entry : Entries)
			{
				auto Source = Sources.find(Entry.second["source"].as<std::string>(""));
				Availability.emplace(Entry.first.as<std::string>(),
									 AvailabilityRecord{Entry.second["status"].as<std::string>("unverified"),
														Source != Sources.end() ? Source->second : ""});
			}
	}
	auto Index = [](YAML::Node const& Source, auto& Target)
	{
		if (Source.IsMap())
			for (auto Entry : Source)
				Target.emplace(Entry.first.template as<std::string>(), Entry.second);
	};
	Index(Definitions["fields"], FieldRules);
	Index(Definitions["mechanic_fields"], MechanicRules);
	for (auto Entry : Definitions["mechanic_modifiers"])
		ModifierRules.emplace(Lower(Entry.first.as<std::string>()), Entry.second);
	for (auto Rule : Definitions["field_overrides"])
		FieldOverrides[Rule["field"].as<std::string>()].push_back(Rule);
	auto CompileTaxonomy = [&](char const* Name, TaxonomyRule& Rule)
	{
		auto Node = Definitions["taxonomy"][Name];
		auto Copy = [&](char const* Key, std::vector<std::string>& Values)
		{
			for (auto Value : Node[Key])
				Values.push_back(Value.as<std::string>());
		};
		Copy("name_suffixes", Rule.Suffixes);
		Copy("name_contains", Rule.Names);
		Copy("tag_contains", Rule.Tags);
		Copy("display_prefixes", Rule.Prefixes);
	};
	CompileTaxonomy("rental", RentalRule);
	CompileTaxonomy("other", OtherRule);
}
std::string ParameterDefinitions::Category(VehicleValue const& Field) const
{
	auto Path = Lower(Field.DisplayPath);
	if (Path == "/hull/maxhealth")
		return "vehicle";
	if (Field.Component == "shells" || Path.find("/shots/") != Path.npos)
		return "shell";
	if (Path.find("/mechanics/") != Path.npos)
	{
		auto Leaf = Path.substr(Path.find_last_of('/') + 1);
		for (auto Rule : Definitions["mechanic_field_categories"])
			if (Leaf == Rule.first.as<std::string>())
				return Rule.second.as<std::string>();
		for (auto Rule : Definitions["mechanic_categories"])
			if (Path.find("/" + Lower(Rule.first.as<std::string>()) + "/") != Path.npos)
				return Rule.second.as<std::string>();
		return "vehicle";
	}
	if (Field.Component == "guns" || Path.find("/guns/") != Path.npos)
		return "gun";
	if (Field.Component == "engines" || Path.find("/engines/") != Path.npos)
		return "engine";
	if (Field.Component == "chassis" || Path.find("/chassis/") != Path.npos || Path.starts_with("/hull/"))
		return "chassis";
	if (Field.Component == "turrets" || Path.find("/turrets") != Path.npos)
		return "turret";
	return "vehicle";
}
std::string ParameterDefinitions::Mechanism(DataSection const& Document)
{
	bool Clip{}, Burst{}, Automatic{}, Double{};
	std::function<void(Section const&, bool)> Visit = [&](Section const& Node, bool InGun)
	{
		auto Name = Lower(Node.Name);
		InGun |= Name == "guns";
		if (InGun)
		{
			Clip |= Name == "clip" || Name == "clipshellcount";
			Burst |= Name == "burst" || Name == "burstshellcount";
			Automatic |= Name.starts_with("autoreload");
			Double |= Name.find("doublegun") != Name.npos || Name.find("dualgun") != Name.npos;
		}
		for (auto const& Child : Node.Children)
			Visit(Child, InGun);
	};
	Visit(Document.Root, false);
	if (Double)
		return "double";
	if (Automatic)
		return "autoreload";
	if (Burst)
		return "burst";
	if (Clip)
		return "magazine";
	return "single";
}
std::vector<ParameterInfo> ParameterDefinitions::Build(VehicleWorkspace& Workspace, Vehicle const& Vehicle,
													   bool Chinese) const
{
	std::vector<ParameterInfo> Result;
	auto Include = [&](Section const& Node, std::string const& DisplayPath, DocumentScope const& Scope)
	{
		if (Scope.Component == "radios" || Scope.Component == "fuelTanks")
			return false;
		auto Path = Lower(DisplayPath);
		for (auto Token : {"/models", "/hittester", "/customization", "/traces", "/spline", "/exhaust",
						   "/swinging", "/emblem", "/armor/", "/turretpositions", "/physicaltracks",
						   "/tracknodes", "/tracks/", "/wheels", "/groundnodes", "/aodecals", "/camouflage/"})
			if (Path.find(Token) != Path.npos)
				return false;
		return !Node.Children.empty() || FieldRules.contains(Node.Name) ||
			   Path.find("/mechanics/") != Path.npos;
	};
	for (auto const& Field : Workspace.Values(Vehicle, Include))
	{
		if (Field.ReadOnly)
			continue;
		auto Path = Lower(Field.DisplayPath);
		auto const& Node =
			VehicleWorkspace::Resolve(Workspace.Document(Field.Document).Current, Field.Address);
		YAML::Node Rule(YAML::NodeType::Undefined);
		if (auto Found = FieldRules.find(Node.Name); Found != FieldRules.end())
			Rule.reset(Found->second);
		if (Path.find("/mechanics/") != Path.npos)
			if (auto Found = MechanicRules.find(Node.Name); Found != MechanicRules.end())
				Rule.reset(Found->second);
		if (!Rule)
		{
			if (Path.find("/mechanics/") == Path.npos)
				continue;
			if (Field.Value != "true" && Field.Value != "false")
				try
				{
					(void)ParseNumbers(Field.Value);
				}
				catch (std::exception const&)
				{
					continue;
				}
		}
		if (Field.Value.empty())
			continue;
		auto CategoryName = Category(Field);
		YAML::Node Modifier;
		std::string Operation, ModifierAttribute;
		if (Path.find("/mechanics/") != Path.npos && Node.Name == "value")
		{
			auto ParentAddress = Field.Address;
			ParentAddress.pop_back();
			auto const& Parent =
				VehicleWorkspace::Resolve(Workspace.Document(Field.Document).Current, ParentAddress);
			ModifierAttribute = Parent.Text("name");
			auto Attribute = Lower(ModifierAttribute);
			Operation = Parent.Name;
			if (auto Found = ModifierRules.find(Attribute); Found != ModifierRules.end())
			{
				Modifier.reset(Found->second);
				CategoryName = Modifier["category"].as<std::string>();
			}
		}
		auto Overrides = FieldOverrides.find(Node.Name);
		if (Overrides != FieldOverrides.end())
			for (auto Override : Overrides->second)
			{
				if (Override["category"] && Override["category"].as<std::string>() != CategoryName)
					continue;
				if (Override["suffix"] && !Field.DisplayPath.ends_with(Override["suffix"].as<std::string>()))
					continue;
				if (Override["contains"] &&
					Field.DisplayPath.find(Override["contains"].as<std::string>()) == std::string::npos)
					continue;
				Rule.reset(Override);
				break;
			}
		auto Language = Chinese ? "zh" : "en";
		auto Label = Rule && Rule[Language] ? Rule[Language].as<std::string>() : Node.Name;
		auto Help = Rule && Rule[Chinese ? "help_zh" : "help_en"]
						? Rule[Chinese ? "help_zh" : "help_en"].as<std::string>()
						: Definitions[Chinese ? "fallback_zh" : "fallback_en"].as<std::string>();
		if (Modifier.IsMap())
		{
			Label = Modifier[Language].as<std::string>();
			auto Presentation =
				Definitions["presentation"][Operation == "mul" ? "modifier_multiplier" : "modifier_addition"];
			Label += Presentation[Language].as<std::string>();
			Help = Presentation[Chinese ? "help_zh" : "help_en"].as<std::string>();
			Help += "\n" + ModifierAttribute;
		}
		Help += "\n" + Field.Document + Field.DisplayPath;
		auto DisplayField = Field;
		auto Names = VehicleWorkspace::Names(Workspace.Document(Field.Document).Current, Field.Address);
		for (size_t Index = 0; Index + 1 < Names.size(); ++Index)
		{
			if (Names[Index].Name == "engines" || Names[Index].Name == "chassis" ||
				Names[Index].Name == "guns" || Names[Index].Name == "shots" ||
				Names[Index].Name.starts_with("turrets"))
				DisplayField.Module = Names[Index + 1].Name;
			if (Names[Index].Name == "mechanics")
			{
				auto Group = Definitions["mechanic_names"][Names[Index + 1].Name];
				DisplayField.Module = Group ? Group[Language].as<std::string>() : Names[Index + 1].Name;
			}
		}
		ParameterInfo Info{std::move(DisplayField), CategoryName, Label, Help};
		if (Node.Name == "rate" && (Path.find("/clip/") != Path.npos || Path.find("/burst/") != Path.npos))
		{
			Info.Transform = ParameterTransform::ShotInterval;
			Info.Label = Definitions["presentation"]["shot_interval"][Language].as<std::string>();
			Info.Help = Definitions["presentation"]["shot_interval"][Chinese ? "help_zh" : "help_en"]
							.as<std::string>() +
						"\n" + Field.DisplayPath;
		}
		if (Node.Name == "reloadTime")
		{
			auto Parent = Field.Address;
			Parent.pop_back();
			auto const& Gun = VehicleWorkspace::Resolve(Workspace.Document(Field.Document).Current, Parent);
			bool Magazine = Gun.Child("clip") != nullptr;
			bool Automatic = Gun.Child("autoreload") != nullptr;
			for (auto const& Scope : Workspace.VehicleScopes(Vehicle))
				if (Scope.Component == "guns" && Scope.Module == Info.Field.Module)
				{
					auto const& Shared =
						VehicleWorkspace::Resolve(Workspace.Document(Scope.Path).Current, Scope.Root);
					Magazine |= Shared.Child("clip") != nullptr;
					Automatic |= Shared.Child("autoreload") != nullptr;
				}
			if (Magazine && !Automatic && Path.find("/autoreload/") == Path.npos)
				Info.Label = Definitions["presentation"]["magazine_reload"][Language].as<std::string>();
			if (Path.find("/autoreload/") != Path.npos)
			{
				auto Total = Info;
				Total.Transform = ParameterTransform::TotalReload;
				Total.Label = Definitions["presentation"]["total_autoreload"][Language].as<std::string>();
				Total.Help = Definitions["presentation"]["total_autoreload"][Chinese ? "help_zh" : "help_en"]
								 .as<std::string>() +
							 "\n" + Field.DisplayPath;
				Result.push_back(std::move(Total));
			}
		}
		if (Node.Name == "minPitch" || Node.Name == "maxPitch")
		{
			auto Numbers = ParseNumbers(Field.Value);
			bool Profile = Numbers.size() >= 4 && Numbers.size() % 2 == 0 &&
						   std::abs(Numbers.front()) < 1e-6 &&
						   std::abs(Numbers[Numbers.size() - 2] - 1) < 1e-6;
			for (size_t Index = 2; Profile && Index < Numbers.size(); Index += 2)
				Profile &= Numbers[Index] >= Numbers[Index - 2] && Numbers[Index] <= 1;
			if (Profile || Numbers.size() == 1)
			{
				std::map<double, std::vector<size_t>> Groups;
				for (size_t Index = Profile ? 1 : 0; Index < Numbers.size(); Index += 2)
					Groups[Numbers[Index]].push_back(Index);
				for (auto const& [Angle, Indices] : Groups)
				{
					auto AngleInfo = Info;
					AngleInfo.Transform = ParameterTransform::PitchAngles;
					AngleInfo.Components = Indices;
					AngleInfo.InvertAngleSign = Node.Name == "minPitch";
					auto Key = AngleInfo.InvertAngleSign ? "elevation" : "depression";
					AngleInfo.Label = Definitions["presentation"][Key][Language].as<std::string>();
					if (Groups.size() > 1)
						AngleInfo.Label +=
							" · " + std::to_string(Indices.size()) +
							Definitions["presentation"]["angle_nodes"][Language].as<std::string>();
					AngleInfo.Help =
						Definitions["presentation"][Key][Chinese ? "help_zh" : "help_en"].as<std::string>() +
						"\n" + Field.DisplayPath;
					Result.push_back(std::move(AngleInfo));
				}
				continue;
			}
		}
		Result.push_back(std::move(Info));
	}
	return Result;
}
VehicleCategory ParameterDefinitions::Classify(Vehicle const& Vehicle, Section const* Metadata,
											   Catalog const* Catalogue) const
{
	if (auto Base = Catalogue ? Catalogue->ModeBase(Vehicle) : nullptr)
	{
		if (Metadata && Metadata->Name == Base->Model)
			return Classify(*Base, Metadata, Catalogue);
		if (auto Resource = Catalogue->Resources.Find(Base->ListPath))
		{
			auto List = DataSection::Parse(Resource->Read());
			return Classify(*Base, List.Root.Child(Base->Model), Catalogue);
		}
		return Classify(*Base, nullptr, Catalogue);
	}
	auto Name = Lower(Vehicle.Model),
		 Tags = Lower(Metadata ? Metadata->Text("tags", Vehicle.Tags) : Vehicle.Tags);
	auto DisplayName = Lower(Vehicle.DisplayName);
	if (Lower(Vehicle.Name).find("7x7") != std::string::npos || Name.find("7x7") != Name.npos ||
		DisplayName.find("7x7") != DisplayName.npos)
		return {"unavailable", true, "rental", "7v7"};
	if (!Vehicle.Listed || !Vehicle.Enabled || Vehicle.Conflict || Name.ends_with("_siege_mode"))
		return {"unavailable", true, "copy", ""};
	auto Matches = [&](TaxonomyRule const& Rule)
	{
		for (auto const& Token : Rule.Suffixes)
			if (Name.ends_with(Token))
				return true;
		for (auto const& Token : Rule.Names)
			if (Name.find(Token) != Name.npos)
				return true;
		for (auto const& Token : Rule.Tags)
			if (Tags.find(Token) != Tags.npos)
				return true;
		for (auto const& Token : Rule.Prefixes)
			if (DisplayName.starts_with(Token))
				return true;
		return false;
	};
	auto Evidence = Availability.find(Vehicle.Name);
	auto Status = Evidence != Availability.end() ? Evidence->second.Status : "unverified";
	auto Source = Evidence != Availability.end() ? Evidence->second.Source : "";
	bool Rental = Matches(RentalRule), Other = Matches(OtherRule);
	if (Catalogue && Name.ends_with("_fl"))
	{
		auto Base = Vehicle.Name.substr(0, Vehicle.Name.size() - 3);
		Rental |= Catalogue->ByName(Base) != nullptr;
	}
	if (Status == "available" || Status == "base_variant")
		Rental = Other = false;
	if (Status == "rental" || (Status == "unverified" && Rental))
		return {"unavailable", true, "rental", Source};
	if (Status == "test" || Status == "copy" || (Status == "unverified" && Other))
		return {"unavailable", true, Status == "test" ? "test" : "copy", Source};
	bool Premium = Tags.find("premium") != Tags.npos;
	bool Reward = Tags.find("special") != Tags.npos || Tags.find("reward") != Tags.npos;
	auto Price = Metadata ? Metadata->Child("price") : nullptr;
	if (Reward || (Premium && Price && !Price->Child("gold")))
		return {"reward", false, Status, Source};
	if (Price && Price->Child("gold"))
		return {"gold", false, Status, Source};
	if (Premium)
		return {"reward", false, Status, Source};
	return {"silver", false, Status, Source};
}
std::string ParameterDefinitions::DisplayValue(ParameterInfo const& Info, std::string const& Stored)
{
	if (Info.Transform == ParameterTransform::Raw)
		return Stored;
	auto Values = ParseNumbers(Stored);
	if (Info.Transform == ParameterTransform::ShotInterval)
		return FormatNumber(60.0 / Values.at(0));
	if (Info.Transform == ParameterTransform::TotalReload)
		return FormatNumber(std::accumulate(Values.begin(), Values.end(), 0.0));
	auto Angle = Values.at(Info.Components.at(0));
	return FormatNumber(Info.InvertAngleSign ? -Angle : Angle);
}
std::string ParameterDefinitions::StoredValue(ParameterInfo const& Info, std::string const& Input,
											  std::string const& Current)
{
	if (Info.Transform == ParameterTransform::Raw)
		return Input;
	auto InputValues = ParseNumbers(Input);
	if (InputValues.size() != 1)
		throw std::runtime_error("Enter one numeric value");
	auto Number = InputValues[0];
	if (Info.Transform == ParameterTransform::ShotInterval)
	{
		if (Number <= 0)
			throw std::runtime_error("The interval must be greater than zero");
		return FormatNumber(60.0 / Number);
	}
	auto Values = ParseNumbers(Current);
	if (Info.Transform == ParameterTransform::TotalReload)
	{
		auto Total = std::accumulate(Values.begin(), Values.end(), 0.0);
		if (Number <= 0 || Total <= 0)
			throw std::runtime_error("Reload duration must be positive");
		for (auto& Value : Values)
			Value *= Number / Total;
	}
	else
	{
		if (std::abs(Number) > 90)
			throw std::runtime_error("The pitch angle must be between -90 and 90 degrees");
		for (auto Index : Info.Components)
			Values.at(Index) = Info.InvertAngleSign ? -Number : Number;
	}
	return FormatNumbers(Values);
}
} // namespace OffHanger
