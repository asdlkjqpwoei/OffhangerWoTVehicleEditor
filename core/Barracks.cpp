#include "Barracks.h"
#include "VehicleParameters.h"
#include <tuple>

namespace OffHanger
{
namespace
{
ValuePtr* FindEntry(ValuePtr const& Table, ValuePtr const& Key)
{
	if (Table && Table->Kind == ValueKind::Dictionary)
		for (auto& Entry : Table->Pairs)
			if (ValueEqual(Entry.first, Key))
				return &Entry.second;
	return nullptr;
}
ValuePtr* Descriptor(SaveGame& Save, OwnedCrew const& Crew)
{
	ValuePtr* Entry = nullptr;
	if (Save.Json922)
	{
		auto Vehicle = FindEntry(Save.Root->Get("vehicles"), Crew.LegacyVehicleKey);
		Entry = Vehicle && *Vehicle ? FindEntry((*Vehicle)->Get("crew"), Crew.Key) : nullptr;
	}
	else
		Entry = FindEntry(Save.Root->Get("tankmen"), Crew.Key);
	if (Entry && *Entry && (*Entry)->Kind == ValueKind::Dictionary)
		for (auto& Pair : (*Entry)->Pairs)
			if (Pair.first->Text == "compDescr")
				return &Pair.second;
	return Entry;
}
Bytes ReadDescriptor(ValuePtr const& Value, bool Legacy)
{
	if (!Value || (Value->Kind != ValueKind::Bytes && Value->Kind != ValueKind::Unicode))
		throw std::runtime_error("Missing crew descriptor");
	return Legacy ? Unbase64(Value->Text) : Bytes(Value->Text.begin(), Value->Text.end());
}
void Detach(SaveGame& Save, OwnedCrew const& Crew)
{
	if (auto Vehicles = Save.Root->Get("vehicles"))
		for (auto const& [Key, Record] : Vehicles->Pairs)
			if (auto Seats = Record ? Record->Get("crew") : nullptr)
				for (auto& Seat : Seats->Items)
					if (Crew.Id > 0 && Seat && Seat->Kind != ValueKind::None && Seat->AsInt(-1) == Crew.Id)
						Seat = Value::Make(ValueKind::None);
	if (auto Entry = FindEntry(Save.Root->Get("tankmen"), Crew.Key);
		Entry && *Entry && (*Entry)->Kind == ValueKind::Dictionary)
		for (auto Field : {"vehicle", "vehicleID", "vehicleInvID"})
			if (std::string_view(Field) == "vehicle" || (*Entry)->Get(Field))
				(*Entry)->Set(Field, Value::Number(0));
}
} // namespace
std::vector<OwnedCrew> BarracksRoster(SaveGame const& Save, Catalog const& Vehicles, CrewCatalog const& Names)
{
	std::vector<OwnedCrew> Result;
	if (!Save.Root)
		return Result;
	std::unordered_map<int64_t, OwnedVehicle> Garage;
	std::unordered_map<int64_t, int64_t> Owners;
	for (auto const& Vehicle : Save.Inventory(Vehicles))
	{
		Garage.emplace(Vehicle.InventoryId, Vehicle);
		if (!Save.Json922)
			if (auto Seats = Vehicle.Record ? Vehicle.Record->Get("crew") : nullptr)
				for (auto const& Seat : Seats->Items)
					if (Seat && Seat->Kind != ValueKind::None && Seat->AsInt(-1) > 0)
						Owners.try_emplace(Seat->AsInt(), Vehicle.InventoryId);
	}
	auto Append =
		[&](ValuePtr const& Key, ValuePtr const& Record, ValuePtr const& VehicleKey, int64_t VehicleId)
	{
		OwnedCrew Row;
		Row.Key = Key;
		Row.LegacyVehicleKey = VehicleKey;
		Row.Id = Save.Json922 ? 0 : Key->AsInt(-1);
		Row.VehicleId = VehicleId;
		if (!Save.Json922 && !VehicleId && Record)
			for (auto Field : {"vehicle", "vehicleID", "vehicleInvID"})
				if (auto Assigned = Record->Get(Field); Assigned && Garage.contains(Assigned->AsInt()))
				{
					Row.VehicleId = Assigned->AsInt();
					break;
				}
		try
		{
			auto Data = Record && Record->Kind == ValueKind::Dictionary ? Record->Get("compDescr") : Record;
			Row.Info = ParseCrew(ReadDescriptor(Data, Save.Json922), Save.Json922);
			Row.Valid =
				Row.Info.Nation >= 0 && Row.Info.Nation < 11 && Row.Info.Role >= 1 && Row.Info.Role <= 5;
			Row.TrainingCd = (uint32_t(Row.Info.Vehicle) << 8) | (uint32_t(Row.Info.Nation) << 4) | 1;
			Row.Name = Names.NameFor(Row.Info);
			if (auto Group = Names.GroupFor(Row.Info))
			{
				Row.Theme = Group->Theme;
				Row.ThemeLabel = Group->ThemeLabel;
				if (Group->Special)
					if (auto Member =
							std::find(Group->Members.begin(), Group->Members.end(), Row.Info.Passport);
						Member != Group->Members.end())
						Row.RecruitKey = Group->Key + ":" + std::to_string(Member - Group->Members.begin());
			}
			if (auto Vehicle = Vehicles.ByCd(Row.TrainingCd))
			{
				Row.VehicleName = Vehicle->DisplayName;
				Row.Class = VehicleClass(Vehicle->Tags);
			}
		}
		catch (std::exception const&)
		{
		}
		if (Row.Theme.empty())
		{
			Row.Theme = "unknown";
			Row.ThemeLabel = "unknown";
		}
		if (auto Found = Garage.find(Row.VehicleId); Found != Garage.end())
			Row.MountedVehicle = Found->second.Display;
		Result.push_back(std::move(Row));
	};
	if (Save.Json922)
	{
		for (auto const& Vehicle : Save.Inventory(Vehicles))
			if (auto Seats = Vehicle.Record ? Vehicle.Record->Get("crew") : nullptr)
				for (auto const& [Key, Record] : Seats->Pairs)
					Append(Key, Record, Vehicle.InventoryKey, Vehicle.InventoryId);
	}
	else if (auto Tankmen = Save.Root->Get("tankmen"))
		for (auto const& [Key, Record] : Tankmen->Pairs)
			Append(Key, Record, nullptr, Owners.contains(Key->AsInt(-1)) ? Owners.at(Key->AsInt(-1)) : 0);
	std::sort(
		Result.begin(), Result.end(),
		[](auto const& A, auto const& B)
		{
			return std::tuple(A.VehicleId != 0, A.VehicleId ? uint64_t(A.VehicleId) : uint64_t(A.TrainingCd),
							  CrewRoleOrder(A.Info.Role), A.Id, A.Key->Display()) <
				   std::tuple(B.VehicleId != 0, B.VehicleId ? uint64_t(B.VehicleId) : uint64_t(B.TrainingCd),
							  CrewRoleOrder(B.Info.Role), B.Id, B.Key->Display());
		});
	return Result;
}
std::vector<RecruitCandidate> RecruitCandidates(CrewCatalog const& Names)
{
	std::vector<RecruitCandidate> Result;
	std::unordered_map<std::string, size_t> ByKey;
	for (auto const& Group : Names.Groups)
		for (size_t Member = 0,
					Count = Group.Special ? Group.Members.size() : std::min(size_t(1), Group.Members.size());
			 Member < Count; ++Member)
		{
			auto Key = Group.Key + ":" + std::to_string(Member);
			if (!ByKey.contains(Key))
			{
				ByKey[Key] = Result.size();
				auto Name = Member < Group.MemberNames.size() ? Group.MemberNames[Member] : "";
				Result.push_back({Key, Name.empty() ? Group.Label : Name, Group.Theme, Group.ThemeLabel});
				Result.back().Repeatable = !Group.Special;
			}
			Result[ByKey.at(Key)].Variants.push_back({Group.NationId, Group.Members[Member], Group.Female,
													  Group.Premium, Group.AllowedRoles,
													  Group.BoundVehicles});
		}
	std::sort(Result.begin(), Result.end(), [](auto const& A, auto const& B)
			  { return std::tie(A.Theme, A.Name, A.Key) < std::tie(B.Theme, B.Name, B.Key); });
	return Result;
}
bool RecruitCandidate::Allows(int Nation, int Role) const
{
	for (auto const& Variant : Variants)
		if ((Nation < 0 || Variant.Nation == Nation) &&
			(!Role || std::find(Variant.Roles.begin(), Variant.Roles.end(), Role) != Variant.Roles.end()))
			return true;
	return false;
}
bool RecruitCandidate::AllowsVehicle(Vehicle const& Vehicle) const
{
	for (auto const& Variant : Variants)
		if (Variant.Nation == Vehicle.NationId &&
			(Variant.Vehicles.empty() || std::find(Variant.Vehicles.begin(), Variant.Vehicles.end(),
												   Vehicle.Name) != Variant.Vehicles.end()))
			return true;
	return false;
}
std::vector<int> VehicleCrewRoles(Catalog const& Vehicles, Vehicle const& Vehicle)
{
	std::vector<int> Roles;
	if (auto Source = Vehicles.Resources.Find(Vehicle.DefinitionPath))
	{
		auto Document = DataSection::Parse(Source->Read());
		if (auto Crew = Document.Root.Child("crew"))
			for (auto const& Seat : Crew->Children)
				if (auto Role = CrewRoleId(Seat.Name);
					Role && std::find(Roles.begin(), Roles.end(), Role) == Roles.end())
					Roles.push_back(Role);
	}
	std::sort(Roles.begin(), Roles.end(), [](int A, int B) { return CrewRoleOrder(A) < CrewRoleOrder(B); });
	return Roles;
}
void SetCrewExperience(SaveGame& Save, OwnedCrew const& Crew, uint32_t Experience)
{
	if (!Save.Root)
		throw std::runtime_error("Open an account save first");
	if (Experience > 4000000000u)
		throw std::runtime_error("Crew experience must be between 0 and 4000000000");
	auto Target = Descriptor(Save, Crew);
	if (!Target)
		throw std::runtime_error("The selected crew member no longer exists");
	auto Data = ReadDescriptor(*Target, Save.Json922);
	auto Info = ParseCrew(Data, Save.Json922);
	auto Offset = Info.PassportOffset + (Save.Json922 ? 8 : 6);
	if (Offset + 4 > Data.size())
		throw std::runtime_error("Truncated crew experience");
	for (size_t I = 0; I < 4; ++I)
		Data[Offset + I] = uint8_t(Experience >> (8 * (Save.Json922 ? I : 3 - I)));
	if (Save.Json922)
	{
		auto Encoded = Value::Make(ValueKind::Unicode);
		Encoded->Text = Base64(Data);
		*Target = Encoded;
	}
	else
		*Target = Value::FromBytes(Data);
	Save.Dirty = true;
}
void DismissCrew(SaveGame& Save, OwnedCrew const& Crew, bool FromAccount)
{
	if (!Save.Root)
		throw std::runtime_error("Open an account save first");
	if (Save.Json922 && !FromAccount)
		throw std::runtime_error("0.9.22 has no independent barracks inventory");
	auto Table = Save.Root->Get("tankmen");
	if (Save.Json922)
	{
		auto Vehicle = FindEntry(Save.Root->Get("vehicles"), Crew.LegacyVehicleKey);
		Table = Vehicle && *Vehicle ? (*Vehicle)->Get("crew") : nullptr;
	}
	if (!FindEntry(Table, Crew.Key))
		throw std::runtime_error("The selected crew member no longer exists");
	if (!Save.Json922)
		Detach(Save, Crew);
	if (FromAccount)
		std::erase_if(Table->Pairs, [&](auto const& Pair) { return ValueEqual(Pair.first, Crew.Key); });
	Save.Dirty = true;
}
int64_t RecruitCrew(SaveGame& Save, Catalog const& Vehicles, CrewCatalog const& Names,
					RecruitCandidate const& Candidate, int Nation, int Class, uint32_t Cd, int Role)
{
	if (!Save.Root || Save.Json922)
		throw std::runtime_error("This save does not support independent barracks recruitment");
	auto Vehicle = Vehicles.ByCd(Cd);
	if (!Vehicle || !Vehicle->Enabled || !Vehicle->Listed || Vehicle->Conflict || !Vehicle->Id ||
		Vehicle->Id > 65535 || Vehicle->NationId != Nation || Class < 1 || Class > 5 ||
		VehicleClass(Vehicle->Tags) != Class)
		throw std::runtime_error("Select a valid nation, vehicle class and vehicle");
	auto Roles = VehicleCrewRoles(Vehicles, *Vehicle);
	if (std::find(Roles.begin(), Roles.end(), Role) == Roles.end())
		throw std::runtime_error("This vehicle has no seat for the selected primary role");
	RecruitVariant const* Chosen = nullptr;
	for (auto const& Variant : Candidate.Variants)
		if (Variant.Nation == Nation &&
			std::find(Variant.Roles.begin(), Variant.Roles.end(), Role) != Variant.Roles.end() &&
			(Variant.Vehicles.empty() || std::find(Variant.Vehicles.begin(), Variant.Vehicles.end(),
												   Vehicle->Name) != Variant.Vehicles.end()))
		{
			Chosen = &Variant;
			break;
		}
	if (!Chosen || !Names.BrotherhoodSkill)
		throw std::runtime_error(
			"The selected recruitment violates native crew restrictions or lacks Brothers in Arms data");
	if (!Candidate.Repeatable)
		for (auto const& Member : CrewSlots(Save))
			for (auto const& Variant : Candidate.Variants)
				if (Member.Info.Nation == Variant.Nation && Member.Info.Passport == Variant.Passport)
					throw std::runtime_error("This special crew member is already owned");
	auto Data =
		BuildCrew(Nation, uint16_t(Vehicle->Id), Role, Chosen->Passport, Chosen->Female, Chosen->Premium);
	auto SkillsOffset = 2 + ((Data[1] & 128) ? 2 : 1) + 3;
	Data.erase(Data.begin() + SkillsOffset, Data.begin() + SkillsOffset + 2);
	Data.insert(Data.begin() + SkillsOffset, {1, Names.BrotherhoodSkill, 100, 1});
	Data[1] |= 16; // A fully trained, gifted (zero-XP) Brothers in Arms skill.
	auto Table = Save.Root->Get("tankmen");
	if (!Table)
	{
		Table = Value::Make(ValueKind::Dictionary);
		Save.Root->Set("tankmen", Table);
	}
	if (Table->Kind != ValueKind::Dictionary)
		throw std::runtime_error("Invalid tankmen inventory");
	auto Next = Save.Root->Get("nextTmanInvID");
	int64_t Id = Next ? std::max(int64_t(1), Next->AsInt(1)) : 1;
	for (auto const& Pair : Table->Pairs)
	{
		auto Existing = Pair.first->AsInt(-1);
		if (Existing == INT64_MAX)
			throw std::runtime_error("Crew inventory id limit reached");
		Id = std::max(Id, Existing + 1);
	}
	if (Id == INT64_MAX)
		throw std::runtime_error("Crew inventory id limit reached");
	auto Record = Value::Make(ValueKind::Dictionary);
	Record->Set("compDescr", Value::FromBytes(Data));
	Record->Set("vehicle", Value::Number(0));
	Table->Set(Id, Record);
	Save.Root->Set("nextTmanInvID", Value::Number(Id + 1));
	Save.Dirty = true;
	return Id;
}
} // namespace OffHanger
