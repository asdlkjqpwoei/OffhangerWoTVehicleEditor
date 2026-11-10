#pragma once
#include "VehicleWorkspace.h"
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
};
class ParameterDefinitions
{
	YAML::Node Definitions;
	YAML::Node Availability;

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
} // namespace OffHanger
