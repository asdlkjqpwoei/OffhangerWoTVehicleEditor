#include "VehicleFactory.h"
#include <cmath>
#include <sstream>
namespace OffHanger
{
DataSection const& VehicleFactory::Document(std::string const& Path)
{
	auto Key = Lower(Path);
	auto It = Tables.find(Key);
	if (It != Tables.end())
		return It->second;
	auto R = SourceCatalog.Resources.Find(Path);
	if (!R)
		throw std::runtime_error("Missing definition: " + Path);
	return Tables.emplace(Key, DataSection::Parse(R->Read())).first->second;
}
std::vector<Section const*> VehicleFactory::ComponentTables(Vehicle const& V, std::string const& Group)
{
	auto Marker = V.DefinitionPath.find("scripts/item_defs/vehicles/");
	auto Prefix = Marker == V.DefinitionPath.npos ? "" : V.DefinitionPath.substr(0, Marker);
	std::vector<Section const*> Result;
	std::vector<std::string> Paths;
	if (!Prefix.empty())
		Paths.push_back(Prefix + "scripts/item_defs/vehicles/" + V.Nation + "/components/" + Group + ".xml");
	Paths.push_back("scripts/item_defs/vehicles/" + V.Nation + "/components/" + Group + ".xml");
	for (auto const& Path : Paths)
		if (SourceCatalog.Resources.Find(Path))
			Result.push_back(&Document(Path).Root);
	return Result;
}
uint16_t VehicleFactory::ComponentId(Vehicle const& V, std::string const& Group, Section const& Local)
{
	auto Id = Local.Text("id");
	if (Id.empty())
		for (auto Table : ComponentTables(V, Group))
		{
			auto Ids = Table->Child("ids");
			if (Ids)
				if (auto Item = Ids->Child(Local.Name))
					Id = Item->Value;
			if (Id.empty())
				if (auto Item = Table->Child(Local.Name))
					Id = Item->Text("id");
			if (!Id.empty())
				break;
		}
	if (Id.empty())
		throw std::runtime_error("No declared module id: " + Group + "/" + Local.Name);
	auto N = std::stoul(Id);
	if (N > 65535)
		throw std::runtime_error("Module id exceeds supported range");
	bool Body = !Local.Children.empty();
	for (auto Table : ComponentTables(V, Group))
	{
		if (auto Item = Table->Child(Local.Name))
			Body |= !Item->Children.empty();
		if (auto Shared = Table->Child("shared"))
			if (auto Item = Shared->Child(Local.Name))
				Body |= !Item->Children.empty();
	}
	if (!Body)
		throw std::runtime_error("Module has an id but no usable definition: " + Local.Name);
	return uint16_t(N);
}
static Section MergeComponent(Section Base, Section const& Local)
{
	if (!Local.Value.empty() && Lower(Local.Value) != "shared")
		Base.Value = Local.Value;
	for (auto const& C : Local.Children)
	{
		auto It = std::find_if(Base.Children.begin(), Base.Children.end(),
							   [&](auto const& Old) { return Old.Name == C.Name; });
		if (It == Base.Children.end())
			Base.Children.push_back(C);
		else
			*It = MergeComponent(*It, C);
	}
	Base.Name = Local.Name;
	return Base;
}
Section VehicleFactory::EffectiveComponent(Vehicle const& V, std::string const& Group, Section const& Local)
{
	Section Result;
	Result.Name = Local.Name;
	auto Sources = ComponentTables(V, Group);
	for (auto It = Sources.rbegin(); It != Sources.rend(); ++It)
	{
		if (auto Node = (*It)->Child(Local.Name))
			Result = MergeComponent(std::move(Result), *Node);
		if (auto Shared = (*It)->Child("shared"))
			if (auto Node = Shared->Child(Local.Name))
				Result = MergeComponent(std::move(Result), *Node);
	}
	return MergeComponent(std::move(Result), Local);
}
StockVehicle VehicleFactory::Stock(Vehicle const& V)
{
	if (V.NationId < 0 || V.NationId > 15 || !V.TypeCd || !V.Enabled || V.Conflict)
		throw std::runtime_error("Vehicle registration or nation id is unresolved");
	auto const& Root = Document(V.DefinitionPath).Root;
	StockVehicle Stock;
	auto SlotPath = std::string("scripts/item_defs/vehicles/common/supply_slot_types.xml");
	if (auto Supply = Root.Child("supplySlots"); Supply && SourceCatalog.Resources.Find(SlotPath))
	{
		auto Slots = Document(SlotPath).Root.Child("slots");
		if (!Slots)
			throw std::runtime_error("Supply slot type table has no slots");
		Stock.EquipmentSlots = Stock.BoosterSlots = Stock.DeviceSlots = 0;
		std::istringstream Input(Supply->Value);
		std::string Id;
		while (Input >> Id)
		{
			bool Found = false;
			for (auto const& Slot : Slots->Children)
				if (Slot.Text("id") == Id)
				{
					Found = true;
					auto Type = Slot.Text("type");
					if (Type == "optionalDevice")
						++Stock.DeviceSlots;
					if (Type == "equipment")
						if (auto Meta = Slot.Child("meta"))
						{
							if (Meta->Text("equipmentType") == "regular")
								++Stock.EquipmentSlots;
							if (Meta->Text("equipmentType") == "battleBoosters")
								++Stock.BoosterSlots;
						}
					break;
				}
			if (!Found)
				throw std::runtime_error("Unknown supply slot id: " + Id);
		}
		if (Stock.EquipmentSlots > 64 || Stock.BoosterSlots > 64 || Stock.DeviceSlots > 64)
			throw std::runtime_error("Vehicle supply slot count exceeds limits");
	}
	if (auto Builtins = Root.Child("builtins"); Builtins && !Trim(Builtins->Value).empty())
	{
		auto EquipmentPath = std::string("scripts/item_defs/equipments.xml");
		if (SourceCatalog.Resources.Find(EquipmentPath))
		{
			auto const& Equipment = Document(EquipmentPath).Root;
			std::istringstream Input(Builtins->Value);
			std::string Name;
			std::vector<std::pair<int, uint32_t>> Required;
			while (Input >> Name)
				if (auto Definition = Equipment.Child(Name))
				{
					auto Id = Definition->Text("id");
					int Priority = INT_MAX;
					std::istringstream Tags(Definition->Text("tags"));
					std::string Tag;
					while (Tags >> Tag)
						if (Tag.starts_with("builtin_priority_"))
							Priority = std::stoi(Tag.substr(17));
					if (!Id.empty())
						Required.emplace_back(Priority, (uint32_t(std::stoul(Id)) << 8) | 11);
				}
				else
					throw std::runtime_error("Required built-in equipment is absent: " + Name);
			std::sort(Required.begin(), Required.end());
			for (auto const& [Priority, Cd] : Required)
				Stock.BuiltinEquipment.push_back(Cd);
		}
		else
			throw std::runtime_error("Missing equipment definitions for this vehicle's built-in equipment");
	}
	auto Crew = Root.Child("crew");
	if (Crew)
		for (auto const& Role : Crew->Children)
			Stock.Roles.push_back(Role.Name);
	auto First = [&](std::string const& Group) -> Section const&
	{
		auto Section = Root.Child(Group);
		if (!Section || Section->Children.empty())
			throw std::runtime_error("Missing stock module group: " + Group);
		return Section->Children.front();
	};
	Bytes Components;
	auto Add = [&](Section const& Local, std::string const& Group, int Type)
	{
		auto Id = ComponentId(V, Group, Local);
		Put16(Components, Id);
		auto Cd = (uint32_t(Id) << 8) | (uint32_t(V.NationId) << 4) | Type;
		Stock.ModuleCDs.push_back(Cd);
		return Cd;
	};
	Add(First("chassis"), "chassis", 2);
	Add(First("engines"), "engines", 5);
	Add(First("fuelTanks"), "fuelTanks", 6);
	Add(First("radios"), "radios", 7);
	for (int Slot = 0;; ++Slot)
	{
		auto TurretGroup = Root.Child("turrets" + std::to_string(Slot));
		if (!TurretGroup)
			break;
		if (TurretGroup->Children.empty())
			throw std::runtime_error("Empty stock turret group");
		auto const& Turret = TurretGroup->Children.front();
		auto Tcd = Add(Turret, "turrets", 3);
		auto Guns = Turret.Child("guns");
		if (!Guns || Guns->Children.empty())
			throw std::runtime_error("Stock turret has no gun");
		auto const& GunLocal = Guns->Children.front();
		auto Gcd = Add(GunLocal, "guns", 4);
		Stock.TurretGunCDs.emplace_back(Tcd, Gcd);
		if (Slot == 0)
		{
			auto Gun = EffectiveComponent(V, "guns", GunLocal);
			int Capacity = std::stoi(Gun.Text("maxAmmo", "0"));
			auto Shots = Gun.Child("shots");
			if (Shots && !Shots->Children.empty())
			{
				std::vector<double> Portions;
				for (auto const& Shot : Shots->Children)
				{
					auto ShellName = Trim(Shot.Text("shell", Shot.Name));
					Section Shell;
					Shell.Name = ShellName;
					auto Id = ComponentId(V, "shells", Shell);
					Stock.Ammunition.emplace_back((uint32_t(Id) << 8) | (uint32_t(V.NationId) << 4) | 10, 0);
					Portions.push_back(std::stod(Shot.Text("defaultPortion", "1")));
				}
				int ClipSize = 1;
				if (auto Clip = Gun.Child("clip"))
					ClipSize = std::max(1, std::stoi(Clip->Text("count", "1")));
				if (ClipSize == Capacity)
					ClipSize = 1;
				if (Capacity < 0 || Capacity > 100000)
					throw std::runtime_error("Invalid ammunition capacity");
				int Remaining = Capacity;
				for (size_t I = 0; I < Portions.size(); ++I)
				{
					if (!std::isfinite(Portions[I]) || Portions[I] < 0 || Portions[I] > 100)
						throw std::runtime_error("Invalid default ammunition portion");
					auto Count = int(std::floor(Portions[I] * Capacity / ClipSize + .5)) * ClipSize;
					Count = std::clamp(Count, 0, Remaining);
					Stock.Ammunition[I].second = Count;
					Remaining -= Count;
				}
			}
		}
	}
	if (Stock.TurretGunCDs.empty())
		throw std::runtime_error("Vehicle has no supported turret/gun slot");
	uint8_t Header = uint8_t((V.NationId << 4) | 1 | (V.Id > 255 ? 2 : 0));
	Stock.Descriptor = {Header, uint8_t(V.Id)};
	if (V.Id > 255)
		Stock.Descriptor.push_back(uint8_t(V.Id >> 8));
	Append(Stock.Descriptor, Components);
	Stock.Descriptor.push_back(0);
	return Stock;
}
ValuePtr StockVehicle::Record() const
{
	auto Result = Value::Make(ValueKind::Dictionary);
	Result->Set("compDescr", Value::FromBytes(Descriptor));
	Result->Set("crew", Value::MakeList(Roles.size()));
	auto Shells = Value::MakeList();
	for (auto [ShellCd, ShellCount] : Ammunition)
	{
		Shells->Items.push_back(Value::Number(ShellCd));
		Shells->Items.push_back(Value::Number(ShellCount));
	}
	Result->Set("shells", Shells);
	auto Zeros = [](size_t Count)
	{
		auto List = Value::MakeList();
		for (size_t Index = 0; Index < Count; ++Index)
			List->Items.push_back(Value::Number(0));
		return List;
	};
	auto Equipment = Zeros(EquipmentSlots);
	if (BuiltinEquipment.size() > EquipmentSlots)
		throw std::runtime_error("Built-in equipment exceeds available supply slots");
	for (size_t Index = 0; Index < BuiltinEquipment.size(); ++Index)
		Equipment->Items[Index] = Value::Number(BuiltinEquipment[Index]);
	Result->Set("eqs", Equipment);
	auto Eqs = Value::MakeList();
	Eqs->Items.push_back(ReadPickle(WritePickle(Equipment)));
	Result->Set("eqsLayout", Eqs);
	Result->Set("boosters", Zeros(BoosterSlots));
	auto Boost = Value::MakeList();
	Boost->Items.push_back(Zeros(BoosterSlots));
	Result->Set("boostersLayout", Boost);
	auto Devices = Value::MakeList();
	Devices->Items.push_back(Zeros(DeviceSlots));
	Result->Set("devicesLayout", Devices);
	auto Layouts = Value::Make(ValueKind::Dictionary);
	if (!TurretGunCDs.empty())
	{
		auto Key = Value::Make(ValueKind::Tuple);
		Key->Items = {Value::Number(TurretGunCDs[0].first), Value::Number(TurretGunCDs[0].second)};
		auto Array = Value::MakeList();
		Array->Items.push_back(ReadPickle(WritePickle(Shells)));
		Array->Items.push_back(ReadPickle(WritePickle(Shells)));
		Layouts->Pairs.emplace_back(Key, Array);
		Result->Set("_shellsLayoutKey", Key);
	}
	Result->Set("shellsLayout", Layouts);
	Result->Set("settings", Value::Number(0));
	auto Indexes = Value::Make(ValueKind::Dictionary);
	Indexes->Set(1, Value::Number(0));
	Indexes->Set(2, Value::Number(0));
	Result->Set("layoutIndexes", Indexes);
	return Result;
}
} // namespace OffHanger
