#include "SaveGame.h"
#include "Crew.h"
#include "Json.h"
#include "VehicleFactory.h"
#include <set>
namespace OffHangar
{
void SaveGame::Open(FileSystem::path const& P)
{
    auto Bytes = ReadFile(P);
    auto First = std::find_if(Bytes.begin(), Bytes.end(),
                              [](uint8_t C) { return C != ' ' && C != '\t' && C != '\r' && C != '\n'; });
    Json922 = First != Bytes.end() && *First == '{';
    auto Parsed = Json922 ? ReadJson(Bytes) : ReadPickle(Bytes);
    if (Parsed->Kind != ValueKind::Dictionary || !Parsed->Get("vehicles") ||
        Parsed->Get("vehicles")->Kind != ValueKind::Dictionary)
        throw std::runtime_error("This file is not an OffHangar account save");
    Root = Parsed;
    Path = FileSystem::weakly_canonical(P);
    OriginalCrc = Crc32(Bytes);
    Dirty = false;
}
std::vector<OwnedVehicle> SaveGame::Inventory(Catalog const& C) const
{
    std::vector<OwnedVehicle> Out;
    if (!Root)
        return Out;
    for (auto const& [EntryKey, EntryValue] : Root->Get("vehicles")->Pairs)
    {
        OwnedVehicle Owned;
        try
        {
            Owned.InventoryId = Json922 ? std::stoll(EntryKey->Text) : EntryKey->AsInt(-1);
        }
        catch (...)
        {
            Owned.InventoryId = -1;
        }
        Owned.Record = EntryValue;
        auto Desc = EntryValue->Get("compDescr");
        if (Desc && (Desc->Kind == ValueKind::Bytes || Desc->Kind == ValueKind::Unicode))
        {
            try
            {
                Owned.TypeCd =
                    VehicleCd(Json922 ? Unbase64(Desc->Text) : Bytes(Desc->Text.begin(), Desc->Text.end()));
            }
            catch (...)
            {
            }
        }
        if (auto Vehicle = C.ByCd(Owned.TypeCd))
        {
            Owned.Known = true;
            Owned.Name = Vehicle->Name;
            Owned.Display = Vehicle->DisplayName;
            Owned.Tier = Vehicle->Tier;
        }
        else
        {
            Owned.Name = "unknown:" + std::to_string(Owned.TypeCd);
            Owned.Display = Owned.Name;
        }
        Out.push_back(std::move(Owned));
    }
    std::sort(Out.begin(), Out.end(),
              [](auto const& A, auto const& B) { return A.InventoryId < B.InventoryId; });
    return Out;
}
std::vector<SaveProperty> SaveGame::Properties(std::string const& Search)
{
    if (!Root)
        return {};
    return ValueProperties(Root, Search);
}
void SaveGame::EditProperty(std::vector<size_t> const& Address, std::string const& Input)
{
    if (!Root)
        throw std::runtime_error("Open an account save first");
    // Failed batches replace Root; resolve addresses against the current graph.
    // Positional addresses also distinguish display paths containing '/' or '[n]'.
    auto Slot = &Root;
    for (auto Index : Address)
    {
        if (!*Slot)
            throw std::runtime_error("The selected save field no longer exists");
        if ((*Slot)->Kind == ValueKind::Dictionary)
            Slot = &(*Slot)->Pairs.at(Index).second;
        else if ((*Slot)->Kind == ValueKind::List || (*Slot)->Kind == ValueKind::Tuple)
            Slot = &(*Slot)->Items.at(Index);
        else
            throw std::runtime_error("The selected save field changed; select it again");
    }
    if (!*Slot)
        throw std::runtime_error("The selected save field no longer exists");
    *Slot = (*Slot)->Edited(Input);
    Dirty = true;
}
static ValuePtr DictPath(ValuePtr Root, std::string const& Key)
{
    auto P = Root->Get(Key);
    if (!P)
    {
        P = Value::Make(ValueKind::Dictionary);
        Root->Set(Key, P);
    }
    if (P->Kind != ValueKind::Dictionary)
        throw std::runtime_error("Save field is not a dictionary: " + Key);
    return P;
}
void SaveGame::SetCurrency(std::string const& Key, int64_t Value)
{
    static std::set<std::string> const Keys = {"credits",   "gold",   "crystal", "freeXP",
                                               "equipCoin", "bpcoin", "slots"};
    if (!Root || !Keys.contains(Key) || Value < 0)
        throw std::runtime_error("Invalid account value");
    auto Target = Root;
    if (Json922)
    {
        Target = DictPath(Root, "ledger");
        if (Key != "slots")
            Target = DictPath(Target, "wallet");
    }
    Target->Set(Key, Value::Number(Value));
    Dirty = true;
}
int64_t SaveGame::Currency(std::string const& Key) const
{
    if (!Root)
        return 0;
    auto Target = Root;
    if (Json922)
    {
        Target = Target->Get("ledger");
        if (!Target)
            return 0;
        if (Key != "slots")
            Target = Target->Get("wallet");
        if (!Target)
            return 0;
    }
    auto Value = Target->Get(Key);
    return Value ? Value->AsInt() : 0;
}
void SaveGame::SetVehicleXp(uint32_t Cd, int64_t Xp)
{
    if (!Root || Xp < 0)
        throw std::runtime_error("Invalid vehicle XP");
    auto Table = Json922 ? DictPath(DictPath(Root, "ledger"), "vehicleXP") : DictPath(Root, "vehTypeXP");
    if (Json922)
        Table->Set(std::to_string(Cd), Value::Number(Xp));
    else
        Table->Set(Cd, Value::Number(Xp));
    Dirty = true;
}
int64_t SaveGame::VehicleXp(uint32_t Cd) const
{
    if (!Root)
        return 0;
    auto Owner = Json922 ? Root->Get("ledger") : Root;
    auto Table = Owner ? Owner->Get(Json922 ? "vehicleXP" : "vehTypeXP") : nullptr;
    auto Value = Table ? (Json922 ? Table->Get(std::to_string(Cd)) : Table->Get(Cd)) : nullptr;
    return Value ? Value->AsInt() : 0;
}
static void SetAdd(ValuePtr const& Set, int64_t Value)
{
    if (!Set || Set->Kind != ValueKind::Reduce || Set->Items.size() != 2 ||
        Set->Items[0]->Kind != ValueKind::Global || Set->Items[0]->Text != "__builtin__\nset")
        return;
    auto Args = Set->Items[1];
    if (Args->Items.empty())
        Args->Items.push_back(Value::MakeList());
    auto List = Args->Items[0];
    for (auto const& Item : List->Items)
        if (Item->AsInt(INT64_MIN) == Value)
            return;
    List->Items.push_back(Value::Number(Value));
}
int64_t SaveGame::AddVehicle(Vehicle const& Vehicle, Catalog const& Catalog, bool WithCrew)
{
    if (!Root)
        throw std::runtime_error("Open an account save first");
    if (Vehicle.Conflict || !Vehicle.Enabled || !Vehicle.Listed)
        throw std::runtime_error("Vehicle registration is unresolved; refresh the runtime catalogue first");
    for (auto const& Owned : Inventory(Catalog))
        if (Owned.TypeCd == Vehicle.TypeCd)
            throw std::runtime_error("This vehicle is already in the garage");
    if (Json922 && Vehicle.Id > 255)
        throw std::runtime_error("0.9.22 saves cannot represent vehicle ids above 255");
    ValuePtr Record;
    StockVehicle Stock;
    bool Built = false;
    if (!Vehicle.DefinitionPath.empty())
    {
        try
        {
            Stock = VehicleFactory(Catalog).Stock(Vehicle);
            Built = true;
        }
        catch (...)
        {
            if (Vehicle.StockRecord.empty())
                throw;
        }
    }
    if (Built)
        Record = Stock.Record();
    else if (!Vehicle.StockRecord.empty())
        Record = ReadPickle(Vehicle.StockRecord);
    else
        throw std::runtime_error(
            "A current runtime stock configuration is required for this script-registered vehicle");
    if (Record->Kind != ValueKind::Dictionary || !Record->Get("compDescr") || !Record->Get("crew"))
        throw std::runtime_error("Invalid stock vehicle record");
    auto Descriptor = Record->Get("compDescr");
    if (Descriptor->Kind != ValueKind::Bytes ||
        VehicleCd(Bytes(Descriptor->Text.begin(), Descriptor->Text.end())) != Vehicle.TypeCd)
        throw std::runtime_error("Stock vehicle type mismatch");
    auto Table = Root->Get("vehicles");
    auto Next = Root->Get("nextInvID");
    int64_t Id = Next ? Next->AsInt(1) : 1;
    for (auto const& Pair : Table->Pairs)
        Id = std::max(Id, Pair.first->AsInt() + 1);
    if (Id < 1 || Id >= INT32_MAX)
        throw std::runtime_error("No free inventory id");
    auto Roles = Built ? Stock.Roles : Vehicle.CrewRoles;
    auto Crew = Value::MakeList(Roles.size());
    std::vector<std::pair<int64_t, ValuePtr>> NewCrew;
    auto NextTman = Root->Get("nextTmanInvID");
    int64_t Tid = NextTman ? NextTman->AsInt(1) : 1;
    auto Tankmen = Root->Get("tankmen");
    if (Tankmen)
        for (auto const& Pair : Tankmen->Pairs)
            Tid = std::max(Tid, Pair.first->AsInt() + 1);
    if (WithCrew)
    {
        if (Roles.empty() && Vehicle.Crew)
            throw std::runtime_error(
                "Runtime snapshot has no crew roles; refresh it or add this vehicle without crew");
        CrewCatalog Names;
        Names.Scan(Catalog);
        auto Group = Names.Ordinary(Vehicle.NationId, false);
        if (!Roles.empty() && (!Group || Group->Members.empty()))
            throw std::runtime_error("No valid native crew passport pool for this nation");
        static std::map<std::string, int> const RoleIds = {
            {"commander", 1}, {"radioman", 2}, {"driver", 3}, {"gunner", 4}, {"loader", 5}};
        for (size_t I = 0; I < Roles.size(); ++I)
        {
            auto Role = RoleIds.find(Roles[I]);
            if (Role == RoleIds.end())
                throw std::runtime_error("Unsupported custom crew role: " + Roles[I]);
            auto Bytes = BuildCrew(Vehicle.NationId, uint16_t(Vehicle.Id), Role->second,
                                   Group->Members[I % Group->Members.size()], false, false, Json922);
            if (Json922)
            {
                auto Value = Value::Make(ValueKind::Unicode);
                Value->Text = Base64(Bytes);
                auto Rec = Record->Get("crew");
                if (!Rec || Rec->Kind != ValueKind::Dictionary)
                {
                    Rec = Value::Make(ValueKind::Dictionary);
                    Record->Set("crew", Rec);
                }
                Rec->Set(std::to_string(I), Value);
            }
            else
            {
                auto Member = Value::Make(ValueKind::Dictionary);
                Member->Set("compDescr", Value::FromBytes(Bytes));
                Member->Set("vehicle", Value::Number(Id));
                NewCrew.emplace_back(Tid, Member);
                Crew->Items[I] = Value::Number(Tid++);
            }
        }
    }
    if (Json922)
    {
        if (!WithCrew)
            Record->Set("crew", Value::Make(ValueKind::Dictionary));
        auto Value = Value::Make(ValueKind::Unicode);
        Value->Text = Base64(Bytes(Descriptor->Text.begin(), Descriptor->Text.end()));
        Record->Set("compDescr", Value);
        auto LegacyRecord = Value::Make(ValueKind::Dictionary);
        LegacyRecord->Set("compDescr", Value);
        auto LegacyCrew = Record->Get("crew");
        LegacyRecord->Set("crew", LegacyCrew && LegacyCrew->Kind == ValueKind::Dictionary
                                      ? LegacyCrew
                                      : Value::Make(ValueKind::Dictionary));
        LegacyRecord->Set("eqs", Value::MakeList());
        LegacyRecord->Set("eqsLayout", Value::MakeList());
        auto Name = Value::Make(ValueKind::Unicode);
        Name->Text = Vehicle.Name;
        LegacyRecord->Set("name", Name);
        auto Repair = Value::MakeList();
        Repair->Items = {Value::Number(0), Value::Number(1)};
        LegacyRecord->Set("repair", Repair);
        LegacyRecord->Set("settings", Value::Number(14));
        auto Shells = Record->Get("shells");
        if (!Shells)
            Shells = Value::MakeList();
        LegacyRecord->Set("shells", Shells);
        LegacyRecord->Set("shellsLayout", ReadPickle(WritePickle(Shells)));
        auto LayoutIndex = Value::MakeList();
        if (Built && !Stock.TurretGunCDs.empty())
            LayoutIndex->Items = {Value::Number(Stock.TurretGunCDs[0].first),
                                  Value::Number(Stock.TurretGunCDs[0].second)};
        LegacyRecord->Set("shellsLayoutIdx", LayoutIndex);
        Record = std::move(LegacyRecord);
        Record = FromJson(ToJson(Record));
        Table->Set(std::to_string(Vehicle.TypeCd), Record);
        auto Ledger = DictPath(Root, "ledger");
        // 0.9.22 uses a list of compact descriptors, not a dictionary.
        auto UnlockList = Ledger->Get("unlocks");
        if (!UnlockList || UnlockList->Kind != ValueKind::List)
        {
            UnlockList = Value::MakeList();
            Ledger->Set("unlocks", UnlockList);
        }
        auto Unlock = [&](uint32_t Cd)
        {
            for (auto const& Existing : UnlockList->Items)
                if (Existing->AsInt(-1) == Cd)
                    return;
            UnlockList->Items.push_back(Value::Number(Cd));
        };
        Unlock(Vehicle.TypeCd);
        if (Built)
            for (auto Cd : Stock.ModuleCDs)
                Unlock(Cd);
        auto Owned = DictPath(Root, "owned");
        auto Grant = [&](uint32_t Cd, int64_t Count)
        {
            auto Bucket = DictPath(Owned, std::to_string(Cd & 15));
            if (!Bucket->Get(std::to_string(Cd)))
                Bucket->Set(std::to_string(Cd), Value::Number(Count));
        };
        for (auto Cd : Built ? Stock.ModuleCDs : Vehicle.ModuleCDs)
            Grant(Cd, 1);
        if (Built)
            for (auto const& [Cd, Count] : Stock.Ammunition)
                Grant(Cd, Count);
        if (!DictPath(Ledger, "vehicleXP")->Get(std::to_string(Vehicle.TypeCd)))
            DictPath(Ledger, "vehicleXP")->Set(std::to_string(Vehicle.TypeCd), Value::Number(0));
    }
    else
    {
        Record->Set("crew", Crew);
        Table->Set(Id, Record);
        if (!Tankmen)
        {
            Tankmen = Value::Make(ValueKind::Dictionary);
            Root->Set("tankmen", Tankmen);
        }
        for (auto const& Pair : NewCrew)
            Tankmen->Set(Pair.first, Pair.second);
        Root->Set("nextTmanInvID", Value::Number(Tid));
        Root->Set("nextInvID", Value::Number(Id + 1));
        SetAdd(Root->Get("unlocks"), Vehicle.TypeCd);
        for (auto Cd : Built ? Stock.ModuleCDs : Vehicle.ModuleCDs)
            SetAdd(Root->Get("unlocks"), Cd);
    }
    if (Currency("slots") < int64_t(Table->Pairs.size()))
        SetCurrency("slots", int64_t(Table->Pairs.size()));
    Dirty = true;
    return Json922 ? Vehicle.TypeCd : Id;
}
void SaveGame::RemoveVehicle(int64_t Id)
{
    if (!Root)
        throw std::runtime_error("Open a save first");
    auto Table = Root->Get("vehicles");
    for (auto It = Table->Pairs.begin(); It != Table->Pairs.end(); ++It)
        if ((Json922 ? It->first->Text == std::to_string(Id) : It->first->AsInt(-1) == Id))
        {
            auto Crew = It->second->Get("crew");
            auto Tankmen = Root->Get("tankmen");
            if (Crew && Tankmen)
                for (auto const& Member : Crew->Items)
                    if (Member->Kind != ValueKind::None)
                    {
                        auto Record = Tankmen->Get(Member->AsInt(-1));
                        if (Record && Record->Kind == ValueKind::Dictionary)
                        {
                            Record->Set("vehicle", Value::Number(0));
                            if (Record->Get("vehicleID"))
                                Record->Set("vehicleID", Value::Number(0));
                            if (Record->Get("vehicleInvID"))
                                Record->Set("vehicleInvID", Value::Number(0));
                        }
                    }
            Table->Pairs.erase(It);
            Dirty = true;
            return;
        }
    throw std::runtime_error("Garage inventory entry not found");
}
int64_t SaveGame::CloneVehicle(int64_t Id, bool CloneCrew)
{
    if (Json922)
        throw std::runtime_error(
            "0.9.22 stores one entry per vehicle type and cannot represent a duplicate of the same type");
    auto Table = Root->Get("vehicles");
    auto Source = Table->Get(Id);
    if (!Source)
        throw std::runtime_error("Vehicle not found");
    auto Copy = ReadPickle(WritePickle(Source));
    auto Next = Root->Get("nextInvID");
    int64_t Allocated = Next ? Next->AsInt(1) : 1;
    for (auto const& Pair : Table->Pairs)
        Allocated = std::max(Allocated, Pair.first->AsInt() + 1);
    auto Crew = Copy->Get("crew"), Tankmen = Root->Get("tankmen");
    if (Crew && Crew->Kind == ValueKind::List)
    {
        for (auto& Member : Crew->Items)
        {
            if (Member->Kind == ValueKind::None)
                continue;
            auto Original = Tankmen ? Tankmen->Get(Member->AsInt(-1)) : nullptr;
            if (!CloneCrew || !Original)
            {
                Member = Value::Make(ValueKind::None);
                continue;
            }
            auto Tnext = Root->Get("nextTmanInvID");
            int64_t Tid = Tnext ? Tnext->AsInt(1) : 1;
            for (auto const& Pair : Tankmen->Pairs)
                Tid = std::max(Tid, Pair.first->AsInt() + 1);
            auto Record = ReadPickle(WritePickle(Original));
            if (Record->Kind == ValueKind::Dictionary)
                Record->Set("vehicle", Value::Number(Allocated));
            Tankmen->Set(Tid, Record);
            Root->Set("nextTmanInvID", Value::Number(Tid + 1));
            Member = Value::Number(Tid);
        }
    }
    Table->Set(Allocated, Copy);
    Root->Set("nextInvID", Value::Number(Allocated + 1));
    SetCurrency("slots", std::max(Currency("slots"), int64_t(Table->Pairs.size())));
    Dirty = true;
    return Allocated;
}
size_t SaveGame::DeduplicateVehicles(Catalog const& C)
{
    if (Json922)
        return 0;
    std::set<uint32_t> Seen;
    std::vector<int64_t> Ids;
    for (auto const& V : Inventory(C))
        if (V.TypeCd && !Seen.insert(V.TypeCd).second)
            Ids.push_back(V.InventoryId);
    for (auto Id : Ids)
        RemoveVehicle(Id);
    return Ids.size();
}
FileSystem::path SaveGame::Save(FileSystem::path const& Target)
{
    EnsureWritesAllowed();
    if (!Root)
        throw std::runtime_error("Open a save first");
    bool Same = FileSystem::weakly_canonical(Target) == Path;
    auto Bytes = Json922 ? WriteJson(Root) : WritePickle(Root);
    auto Verified = Json922 ? ReadJson(Bytes) : ReadPickle(Bytes);
    if (Verified->Kind != ValueKind::Dictionary)
        throw std::runtime_error("Save verification failed");
    auto Backup =
        AtomicWrite(Target, Bytes, true, Same ? std::optional<uint32_t>(OriginalCrc) : std::nullopt);
    if (Same)
        OriginalCrc = Crc32(Bytes);
    else
    {
        Path = FileSystem::weakly_canonical(Target);
        OriginalCrc = Crc32(Bytes);
    }
    // Retain the ten newest backups created by this application only.
    std::vector<FileSystem::path> Backups;
    auto Prefix = PathUtf8(Target.filename()) + ".before-ohstudio-";
    auto Folder = Target.parent_path().empty() ? FileSystem::current_path() : Target.parent_path();
    for (auto const& Entry : FileSystem::directory_iterator(Folder))
        if (Entry.is_regular_file() && PathUtf8(Entry.path().filename()).starts_with(Prefix) &&
            Entry.path().extension() == L".bak")
            Backups.push_back(Entry.path());
    std::sort(Backups.begin(), Backups.end());
    while (Backups.size() > 10)
    {
        EnsureWritesAllowed();
        FileSystem::remove(NativePath(Backups.front()));
        Backups.erase(Backups.begin());
    }
    Dirty = false;
    return Backup;
}
} // namespace OffHangar
