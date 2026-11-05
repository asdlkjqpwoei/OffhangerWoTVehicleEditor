#pragma once
#include "SaveGame.h"
#include <set>
namespace OffHangar
{
struct Passport
{
    uint16_t First{}, Last{}, Icon{};
    bool operator==(Passport const&) const = default;
};
struct CrewInfo
{
    int Nation{}, Role{};
    uint16_t Vehicle{};
    bool Female{}, Premium{};
    uint32_t EfficiencyXp{}, FreeXp{};
    Passport Passport;
    size_t PassportOffset{};
};
CrewInfo ParseCrew(Bytes const& Descriptor, bool Legacy922 = false);
Bytes BuildCrew(int Nation, uint16_t Vehicle, int Role, Passport Passport, bool Female = false,
                bool Premium = false, bool Legacy922 = false);
Bytes ChangeCrewPassport(Bytes const& Original, Passport Passport, bool Female, bool Premium,
                         bool Legacy922 = false);
Bytes ChangeCrewVehicle(Bytes const& Original, uint16_t Vehicle, bool Legacy922 = false);
struct CrewGroup
{
    std::string Nation, Key, Label, Tags;
    int NationId{};
    bool Female{}, Premium{}, Special{};
    std::vector<Passport> Members;
    std::vector<int> MemberRoles;
    std::vector<std::string> BoundVehicles;
    uint16_t RecruitmentVehicle{};
};
class CrewCatalog
{
  public:
    std::vector<CrewGroup> Groups;
    void Scan(Catalog const& Catalog);
    CrewGroup const* Ordinary(int Nation, bool Female) const;
    bool IsSpecial(CrewInfo const& Info) const;
};
struct CrewSlot
{
    int64_t InventoryId{}, VehicleId{};
    ValuePtr* Slot{};
    bool Base64Encoded{};
    Bytes Descriptor;
    CrewInfo Info;
};
std::vector<CrewSlot> CrewSlots(SaveGame& Save, std::set<int64_t> const& SelectedVehicles = {});
size_t AssignCrewGroup(SaveGame& Save, CrewCatalog const& Catalog, std::vector<size_t> const& GroupIndices,
                       std::set<int64_t> const& SelectedVehicles, bool Preview = false);
size_t RecruitCrewGroup(SaveGame& Save, CrewGroup const& Group, size_t Count);
} // namespace OffHangar
