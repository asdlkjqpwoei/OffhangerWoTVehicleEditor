#pragma once
#include "Crew.h"

namespace OffHanger
{
struct OwnedCrew
{
	ValuePtr Key, LegacyVehicleKey;
	int64_t Id{}, VehicleId{};
	uint32_t TrainingCd{};
	CrewInfo Info;
	bool Valid{};
	int Class{};
	std::string Name, Theme, ThemeLabel, VehicleName, MountedVehicle, RecruitKey;
};
struct RecruitVariant
{
	int Nation{};
	Passport Passport;
	bool Female{}, Premium{};
	std::vector<int> Roles;
	std::vector<std::string> Vehicles;
};
struct RecruitCandidate
{
	std::string Key, Name, Theme, ThemeLabel;
	std::vector<RecruitVariant> Variants;
	bool Repeatable{};
	bool Allows(int Nation = -1, int Role = 0) const;
	bool AllowsVehicle(Vehicle const& Vehicle) const;
};
std::vector<OwnedCrew> BarracksRoster(SaveGame const& Save, Catalog const& Vehicles,
									  CrewCatalog const& Names);
std::vector<RecruitCandidate> RecruitCandidates(CrewCatalog const& Names);
std::vector<int> VehicleCrewRoles(Catalog const& Vehicles, Vehicle const& Vehicle);
void SetCrewExperience(SaveGame& Save, OwnedCrew const& Crew, uint32_t Experience);
void DismissCrew(SaveGame& Save, OwnedCrew const& Crew, bool FromAccount);
int64_t RecruitCrew(SaveGame& Save, Catalog const& Vehicles, CrewCatalog const& Names,
					RecruitCandidate const& Candidate, int Nation, int Class, uint32_t VehicleCd, int Role);
} // namespace OffHanger
