#pragma once
#include "VehicleWorkspace.h"
#include <array>
namespace OffHanger
{
enum class ParameterTransform
{
	Raw,
	ShotInterval,
	PitchAngles,
	TotalReload
};
struct ParameterInfo
{
	VehicleValue Field;
	std::string Category, Label, Help;
	ParameterTransform Transform = ParameterTransform::Raw;
	std::vector<size_t> Components;
	bool InvertAngleSign{};
};
struct VehicleCategory
{
	std::string Kind;
	bool HiddenByDefault{};
	std::string Availability = "unverified", Source;
	bool MatchesFilter(std::string_view Filter) const
	{
		return Filter.empty() ? !HiddenByDefault : Kind == Filter;
	}
};
struct VehicleFilter
{
	std::string Search, Kind;
	int Nation = -1, Tier = 0, Class = 0, Order = 0;
};
struct VehicleListing
{
	size_t Index{}, Family{};
	int Tier{}, Class{};
	std::string Tags, Search;
	VehicleCategory Category;
};
int VehicleClass(std::string const& Tags);
class ParameterDefinitions
{
	YAML::Node Definitions;
	struct AvailabilityRecord
	{
		std::string Status, Source;
	};
	struct TaxonomyRule
	{
		std::vector<std::string> Suffixes, Names, Tags, BaseTags, Prefixes;
	};
	std::unordered_map<std::string, AvailabilityRecord> Availability;
	std::unordered_map<std::string, YAML::Node> FieldRules, MechanicRules, ModifierRules;
	std::unordered_map<std::string, std::vector<YAML::Node>> FieldOverrides;
	TaxonomyRule RentalRule, OtherRule;

  public:
	explicit ParameterDefinitions(FileSystem::path const& Path);
	std::vector<ParameterInfo> Build(VehicleWorkspace& Workspace, Vehicle const& Vehicle, bool Chinese) const;
	std::string Category(VehicleValue const& Field) const;
	static std::string Mechanism(DataSection const& Document);
	VehicleCategory Classify(Vehicle const& Vehicle, Section const* Metadata = nullptr,
							 Catalog const* Catalogue = nullptr) const;
	static std::string DisplayValue(ParameterInfo const& Info, std::string const& Stored);
	static std::string StoredValue(ParameterInfo const& Info, std::string const& Input,
								   std::string const& Current);
};
class VehicleListIndex
{
	Catalog const* Source{};
	Vehicle const* Data{};
	uint64_t Revision = UINT64_MAX;
	std::vector<VehicleListing> Entries;
	std::array<std::vector<size_t>, 3> Orders;

  public:
	void Invalidate()
	{
		Source = nullptr;
	}
	void Refresh(Catalog const& Catalogue, VehicleWorkspace& Workspace, ParameterDefinitions const& Rules);
	VehicleListing const& At(size_t Index) const
	{
		return Entries.at(Index);
	}
	bool Matches(size_t Index, VehicleFilter const& Filter) const;
	std::vector<size_t> Select(VehicleFilter const& Filter) const;
};
} // namespace OffHanger
