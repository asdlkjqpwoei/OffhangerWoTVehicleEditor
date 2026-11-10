#include "Crew.h"
#include <regex>
#include <sstream>
#include <tuple>
namespace OffHanger
{
static uint16_t Be16(Bytes const& D, size_t P)
{
	auto N = U16(D, P);
	return uint16_t(N >> 8 | N << 8);
}
static uint32_t Be32(Bytes const& D, size_t P)
{
	auto N = U32(D, P);
	return (N >> 24) | ((N >> 8) & 0xff00) | ((N << 8) & 0xff0000) | (N << 24);
}
static void PutBE16(Bytes& D, uint16_t V)
{
	D.push_back(uint8_t(V >> 8));
	D.push_back(uint8_t(V));
}
static void Set16(Bytes& D, size_t P, uint16_t V, bool Little)
{
	if (P + 2 > D.size())
		throw std::runtime_error("Truncated crew passport");
	D[P] = uint8_t(Little ? V : V >> 8);
	D[P + 1] = uint8_t(Little ? V >> 8 : V);
}
CrewInfo ParseCrew(Bytes const& D, bool Old)
{
	if (D.size() < 8)
		throw std::runtime_error("Truncated crew descriptor");
	CrewInfo R;
	R.Nation = D[0] >> 4;
	if ((D[0] & 15) != 8)
		throw std::runtime_error("Not a tankman descriptor");
	if (Old)
	{
		auto Count = D[5];
		size_t Flag = 6ull + Count;
		if (Flag + 13 > D.size())
			throw std::runtime_error("Truncated 0.9.22 crew descriptor");
		auto Flags = D[Flag];
		R.Vehicle = D[1];
		R.Role = D[2];
		R.EfficiencyXp = D[3];
		R.Female = (Flags & 32) != 0;
		R.Premium = (Flags & 64) != 0;
		R.PassportOffset = Flag + 1;
		R.Passport = {U16(D, Flag + 1), U16(D, Flag + 3), U16(D, Flag + 5)};
		R.FreeXp = U32(D, Flag + 9);
		return R;
	}
	auto Flags = D[1];
	size_t Pos = 2;
	if (Flags & 128)
	{
		R.Vehicle = Be16(D, Pos);
		Pos += 2;
	}
	else
		R.Vehicle = D[Pos++];
	if (Pos + 4 > D.size())
		throw std::runtime_error("Truncated crew skills");
	R.Role = D[Pos] >> 1;
	R.EfficiencyXp = (uint32_t(D[Pos] & 1) << 16) | (uint32_t(D[Pos + 1]) << 8) | D[Pos + 2];
	Pos += 3;
	auto Skills = D[Pos++];
	Pos += Skills + 1;
	if (Flags & 16)
		++Pos;
	if (Flags & 8)
	{
		if (Pos >= D.size())
			throw std::runtime_error("Truncated crew bonus skills");
		Pos += size_t(D[Pos]) + 1;
	}
	if (Pos + 10 > D.size())
		throw std::runtime_error("Truncated crew passport");
	R.PassportOffset = Pos;
	R.Passport = {Be16(D, Pos), Be16(D, Pos + 2), Be16(D, Pos + 4)};
	R.FreeXp = Be32(D, Pos + 6);
	R.Female = (Flags & 32) != 0;
	R.Premium = (Flags & 64) != 0;
	return R;
}
Bytes BuildCrew(int Nation, uint16_t Vehicle, int Role, Passport P, bool Female, bool Premium, bool Old)
{
	if (Nation < 0 || Nation > 15 || Role < 1 || Role > 5)
		throw std::runtime_error("Invalid crew nationality or role");
	uint8_t Flags = (Female ? 32 : 0) | (Premium ? 64 : 0);
	Bytes D;
	if (Old)
	{
		if (Vehicle > 255)
			throw std::runtime_error("0.9.22 cannot represent extended vehicle ids");
		D = {uint8_t((Nation << 4) | 8), uint8_t(Vehicle), uint8_t(Role), 100, 0, 0, Flags};
		Put16(D, P.First);
		Put16(D, P.Last);
		Put16(D, P.Icon);
		Put16(D, 1 | (50 << 5)); // Initial rank and skill levels per rank in the 9.x format.
		Put32(D, 0);
		return D;
	}
	if (Vehicle > 255)
		Flags |= 128;
	D = {uint8_t((Nation << 4) | 8), Flags};
	if (Flags & 128)
		PutBE16(D, Vehicle);
	else
		D.push_back(uint8_t(Vehicle));
	uint32_t Xp = 100000;
	D.push_back(uint8_t((Role << 1) | (Xp >> 16 & 1)));
	D.push_back(uint8_t(Xp >> 8));
	D.push_back(uint8_t(Xp));
	D.push_back(0);
	D.push_back(0);
	PutBE16(D, P.First);
	PutBE16(D, P.Last);
	PutBE16(D, P.Icon);
	Put32(D, 0);
	return D;
}
Bytes ChangeCrewPassport(Bytes const& Original, Passport Passport, bool Female, bool Premium, bool Old)
{
	auto Info = ParseCrew(Original, Old);
	Bytes D = Original;
	auto Flag = Old ? Info.PassportOffset - 1 : 1;
	D[Flag] = uint8_t((D[Flag] & ~96) | (Female ? 32 : 0) | (Premium ? 64 : 0));
	Set16(D, Info.PassportOffset, Passport.First, Old);
	Set16(D, Info.PassportOffset + 2, Passport.Last, Old);
	Set16(D, Info.PassportOffset + 4, Passport.Icon, Old);
	return D;
}
Bytes ChangeCrewVehicle(Bytes const& Original, uint16_t Vehicle, bool Old)
{
	auto Info = ParseCrew(Original, Old);
	Bytes D = Original;
	if (Old)
	{
		if (Vehicle > 255)
			throw std::runtime_error("0.9.22 cannot represent extended vehicle ids");
		D[1] = uint8_t(Vehicle);
		return D;
	}
	size_t OldWidth = (D[1] & 128) ? 2 : 1;
	D.erase(D.begin() + 2, D.begin() + 2 + OldWidth);
	Bytes Id;
	if (Vehicle > 255)
	{
		PutBE16(Id, Vehicle);
		D[1] |= 128;
	}
	else
	{
		Id.push_back(uint8_t(Vehicle));
		D[1] &= 127;
	}
	D.insert(D.begin() + 2, Id.begin(), Id.end());
	return D;
}
static std::vector<uint16_t> Ids(Section const* Section)
{
	std::vector<uint16_t> Out;
	if (!Section)
		return Out;
	std::regex Digits("[0-9]+");
	for (std::sregex_iterator I(Section->Value.begin(), Section->Value.end(), Digits), End; I != End; ++I)
	{
		auto Id = std::stoul(I->str());
		if (Id <= 65535)
			Out.push_back(uint16_t(Id));
	}
	if (Out.empty())
		for (auto const& C : Section->Children)
		{
			auto S = C.Name;
			auto N = S.find_first_of("0123456789");
			if (N != S.npos)
			{
				auto Id = std::stoul(S.substr(N));
				if (Id <= 65535)
					Out.push_back(uint16_t(Id));
			}
		}
	return Out;
}
void CrewCatalog::Scan(Catalog const& Catalog)
{
	Groups.clear();
	static std::vector<std::string> const Nations = {"ussr",  "germany", "usa",	   "china",	 "france", "uk",
													 "japan", "czech",	 "sweden", "poland", "italy"};
	for (size_t Ni = 0; Ni < Nations.size(); ++Ni)
	{
		auto R = Catalog.Resources.Find("scripts/item_defs/tankmen/" + Nations[Ni] + ".xml");
		if (!R)
			continue;
		auto Doc = DataSection::Parse(R->Read());
		for (auto const& Container : {"normalGroups", "premiumGroups"})
		{
			auto Root = Doc.Root.Child(Container);
			if (!Root)
				continue;
			for (auto const& Group : Root->Children)
			{
				auto First = Ids(Group.Child("firstNames")), Last = Ids(Group.Child("lastNames")),
					 Icons = Ids(Group.Child("icons"));
				if (First.empty() || Last.empty() || Icons.empty())
					continue;
				CrewGroup Result;
				Result.Nation = Nations[Ni];
				Result.NationId = int(Ni);
				Result.Key = Group.Name;
				Result.Tags = Group.Text("tags");
				Result.Label = Catalog.Localization(Group.Text("userString", Group.Name));
				if (Result.Label.empty() || Result.Label.starts_with('#'))
					Result.Label = Group.Name;
				auto ThemeLabel =
					Catalog.Localization("#tooltips:notrecruitedtankman/" + Group.Name + "/label");
				if (!ThemeLabel.empty() && !ThemeLabel.starts_with('#'))
					Result.Label = std::move(ThemeLabel);
				Result.Premium = std::string(Container) == "premiumGroups";
				Result.Female =
					Lower(Group.Text("sex")) == "female" || Lower(Group.Name).starts_with("women");
				Result.Special = !std::regex_match(Group.Name, std::regex("(?:men|women)[0-9]+"));
				static std::regex const VehicleTagPattern("[a-z]+:[A-Za-z0-9_]+");
				for (std::sregex_iterator Match(Result.Tags.begin(), Result.Tags.end(), VehicleTagPattern),
					 End;
					 Match != End; ++Match)
					if (auto Vehicle = Catalog.ByName(Match->str()))
						if (Vehicle->NationId == int(Ni))
							Result.BoundVehicles.push_back(Vehicle->Name);
				for (auto const& Vehicle : Catalog.Vehicles)
					if (Vehicle.NationId == int(Ni) && Vehicle.Enabled && !Vehicle.Conflict && Vehicle.Id)
					{
						Result.RecruitmentVehicle = uint16_t(Vehicle.Id);
						break;
					}
				if (!Result.BoundVehicles.empty())
					Result.RecruitmentVehicle = uint16_t(Catalog.ByName(Result.BoundVehicles.front())->Id);
				auto Count = std::max({First.size(), Last.size(), Icons.size()});
				for (size_t N = 0; N < Count; ++N)
					Result.Members.push_back(
						{First[N % First.size()], Last[N % Last.size()], Icons[N % Icons.size()]});
				for (auto const& Member : Result.Members)
				{
					std::string IconName;
					if (auto IconTable = Doc.Root.Child("icons"))
						for (auto const& Icon : IconTable->Children)
						{
							auto Digits = Icon.Name.find_first_of("0123456789");
							if (Digits != Icon.Name.npos &&
								std::stoul(Icon.Name.substr(Digits)) == Member.Icon)
							{
								IconName = Lower(Icon.Value);
								break;
							}
						}
					int Role = 0;
					for (auto const& [Name, Id] : std::vector<std::pair<std::string, int>>{{"commander", 1},
																						   {"gunner", 2},
																						   {"driver", 3},
																						   {"radioman", 4},
																						   {"radio", 4},
																						   {"operator", 4},
																						   {"loader", 5}})
						if (IconName.find(Name) != IconName.npos)
						{
							Role = Id;
							break;
						}
					Result.MemberRoles.push_back(Role);
				}
				Groups.push_back(std::move(Result));
			}
		}
	}
}
CrewGroup const* CrewCatalog::Ordinary(int Nation, bool Female) const
{
	auto Name = Female ? "women1" : "men1";
	for (auto const& G : Groups)
		if (G.NationId == Nation && G.Key == Name)
			return &G;
	return nullptr;
}
bool CrewCatalog::IsSpecial(CrewInfo const& I) const
{
	for (auto const& G : Groups)
		if (G.Special && G.NationId == I.Nation)
			for (auto const& P : G.Members)
				if (P == I.Passport)
					return true;
	return false;
}
static ValuePtr* Slot(ValuePtr& P, std::string const& Key)
{
	if (!P || P->Kind != ValueKind::Dictionary)
		return nullptr;
	for (auto& [EntryKey, EntryValue] : P->Pairs)
		if ((EntryKey->Kind == ValueKind::Bytes || EntryKey->Kind == ValueKind::Unicode) &&
			EntryKey->Text == Key)
			return &EntryValue;
	return nullptr;
}
std::vector<CrewSlot> CrewSlots(SaveGame& Save, std::set<int64_t> const& Selected)
{
	std::vector<CrewSlot> Out;
	if (!Save.Root)
		return Out;
	if (Save.Json922)
	{
		for (auto& [VehicleKey, VehicleRecord] : Save.Root->Get("vehicles")->Pairs)
		{
			int64_t Vid;
			try
			{
				Vid = std::stoll(VehicleKey->Text);
			}
			catch (...)
			{
				continue;
			}
			if (!Selected.empty() && !Selected.contains(Vid))
				continue;
			auto Crew = VehicleRecord->Get("crew");
			if (!Crew)
				continue;
			for (auto& [EntryKey, EntryValue] : Crew->Pairs)
				try
				{
					auto Bytes = Unbase64(EntryValue->Text);
					Out.push_back({0, Vid, &EntryValue, true, Bytes, ParseCrew(Bytes, true)});
				}
				catch (...)
				{
				}
		}
	}
	else
	{
		std::map<int64_t, int64_t> Owners;
		for (auto const& [EntryKey, EntryValue] : Save.Root->Get("vehicles")->Pairs)
		{
			auto Crew = EntryValue->Get("crew");
			if (!Crew)
				continue;
			for (auto const& Member : Crew->Items)
				if (Member->Kind != ValueKind::None)
					Owners[Member->AsInt(-1)] = EntryKey->AsInt();
		}
		auto Tankmen = Save.Root->Get("tankmen");
		if (!Tankmen)
			return Out;
		for (auto& [EntryKey, EntryValue] : Tankmen->Pairs)
		{
			auto Id = EntryKey->AsInt(-1), Vid = Owners.contains(Id) ? Owners[Id] : 0;
			if (!Selected.empty() && !Selected.contains(Vid))
				continue;
			ValuePtr* Target = &EntryValue;
			if (EntryValue->Kind == ValueKind::Dictionary)
				Target = Slot(EntryValue, "compDescr");
			if (!Target || !*Target || (*Target)->Kind != ValueKind::Bytes)
				continue;
			try
			{
				auto& Text = (*Target)->Text;
				Bytes Bytes(Text.begin(), Text.end());
				Out.push_back({Id, Vid, Target, false, Bytes, ParseCrew(Bytes)});
			}
			catch (...)
			{
			}
		}
	}
	return Out;
}
static void Replace(CrewSlot const& S, Bytes const& D)
{
	if (S.Base64Encoded)
	{
		auto P = Value::Make(ValueKind::Unicode);
		P->Text = Base64(D);
		*S.Slot = P;
	}
	else
		*S.Slot = Value::FromBytes(D);
}
size_t AssignCrewGroup(SaveGame& Save, CrewCatalog const& C, std::vector<size_t> const& Groups,
					   std::set<int64_t> const& Selected, bool Preview)
{
	if (!Save.Root)
		throw std::runtime_error("Open an account save first");
	if (Groups.empty())
		throw std::runtime_error("Select one or more crew groups");
	size_t Changed = 0;
	auto Slots = CrewSlots(Save, Selected);
	std::map<std::pair<int64_t, int>, std::vector<CrewSlot const*>> Mounted;
	for (auto const& Slot : Slots)
		if (Slot.VehicleId)
			Mounted[{Slot.VehicleId, Slot.Info.Nation}].push_back(&Slot);
	struct Candidate
	{
		CrewGroup const* Group;
		size_t Member;
		int Role;
	};
	// Reserve role-specific portraits across the entire combined theme before
	// filling unmatched seats. Free barracks crew are deliberately excluded.
	for (auto const& [Owner, Members] : Mounted)
	{
		std::vector<Candidate> Candidates;
		for (auto Index : Groups)
		{
			auto const& Group = C.Groups.at(Index);
			if (Group.NationId != Owner.second)
				continue;
			for (size_t Member = 0; Member < Group.Members.size(); ++Member)
				Candidates.push_back(
					{&Group, Member, Member < Group.MemberRoles.size() ? Group.MemberRoles[Member] : 0});
		}
		if (Candidates.empty())
			continue;
		std::vector<size_t> Assignments(Members.size(), SIZE_MAX);
		std::vector<bool> Used(Candidates.size());
		for (size_t Seat = 0; Seat < Members.size(); ++Seat)
			for (size_t Index = 0; Index < Candidates.size(); ++Index)
				if (!Used[Index] && Candidates[Index].Role &&
					Candidates[Index].Role == Members[Seat]->Info.Role)
				{
					Assignments[Seat] = Index;
					Used[Index] = true;
					break;
				}
		size_t Repeat = 0;
		for (size_t Seat = 0; Seat < Members.size(); ++Seat)
		{
			if (Assignments[Seat] == SIZE_MAX)
			{
				auto Free = std::find(Used.begin(), Used.end(), false);
				Assignments[Seat] =
					Free == Used.end() ? Repeat++ % Candidates.size() : size_t(Free - Used.begin());
				Used[Assignments[Seat]] = true;
			}
			auto const& Choice = Candidates[Assignments[Seat]];
			auto const& Slot = *Members[Seat];
			auto Descriptor = ChangeCrewPassport(Slot.Descriptor, Choice.Group->Members[Choice.Member],
												 Choice.Group->Female, Choice.Group->Premium, Save.Json922);
			if (Descriptor != Slot.Descriptor)
			{
				++Changed;
				if (!Preview)
					Replace(Slot, Descriptor);
			}
		}
	}
	if (Changed && !Preview)
		Save.Dirty = true;
	return Changed;
}
size_t RecruitCrewGroup(SaveGame& Save, CrewGroup const& Group, size_t Count)
{
	if (Save.Json922)
		throw std::runtime_error(
			"0.9.22 stores crew within vehicle records and has no independent barracks inventory");
	if (!Save.Root || Group.Members.empty() || !Group.RecruitmentVehicle || Count > 1000)
		throw std::runtime_error("Invalid crew recruitment request");
	auto Table = Save.Root->Get("tankmen");
	if (!Table)
	{
		Table = Value::Make(ValueKind::Dictionary);
		Save.Root->Set("tankmen", Table);
	}
	auto Next = Save.Root->Get("nextTmanInvID");
	int64_t Id = Next ? Next->AsInt(1) : 1;
	for (auto const& Pair : Table->Pairs)
		Id = std::max(Id, Pair.first->AsInt() + 1);
	std::set<std::tuple<int, uint16_t, uint16_t, uint16_t>> Existing;
	for (auto const& Member : CrewSlots(Save))
		Existing.emplace(Member.Info.Nation, Member.Info.Passport.First, Member.Info.Passport.Last,
						 Member.Info.Passport.Icon);
	size_t Added = 0;
	for (size_t I = 0; I < Count; ++I)
	{
		auto const& Passport = Group.Members[I % Group.Members.size()];
		if (!Existing.emplace(Group.NationId, Passport.First, Passport.Last, Passport.Icon).second)
			continue;
		auto Record = Value::Make(ValueKind::Dictionary);
		auto Role = I % Group.Members.size() < Group.MemberRoles.size()
						? Group.MemberRoles[I % Group.Members.size()]
						: 0;
		Record->Set("compDescr",
					Value::FromBytes(BuildCrew(Group.NationId, Group.RecruitmentVehicle, Role ? Role : 1,
											   Passport, Group.Female, Group.Premium)));
		Record->Set("vehicle", Value::Number(0));
		Table->Set(Id++, Record);
		++Added;
	}
	Save.Root->Set("nextTmanInvID", Value::Number(Id));
	Save.Dirty |= Added != 0;
	return Added;
}
} // namespace OffHanger
