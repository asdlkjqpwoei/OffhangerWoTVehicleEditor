#pragma once
#include "SaveGame.h"

namespace OffHanger
{
class ParameterDefinitions;
enum class CleanupKind
{
	Invalid,
	Unavailable
};
struct SaveIssue
{
	int64_t InventoryId{};
	std::string VehicleName;
	std::string Reason;
	uint32_t TypeCd{};
	ValuePtr InventoryKey;
	std::vector<std::string> DataFields;
	std::vector<int64_t> CrewIds;
};

class SaveSession
{
	Bytes Snapshot() const;
	void Restore(Bytes const& State);
	uint64_t ChangeRevision{};

  public:
	SaveGame Account;
	void Open(FileSystem::path const& Path);
	void Close();
	// A batch is atomic, including validation failures in its final item.
	void Execute(std::string Label, std::function<void(SaveGame&)> const& Command);
	uint64_t Revision() const
	{
		return ChangeRevision;
	}
	static std::string InvalidVehicleReason(OwnedVehicle const& Vehicle, Catalog const& Catalogue);
	static SaveIssue DescribeDeletion(OwnedVehicle const& Vehicle, std::string Reason = {});
	std::vector<SaveIssue> PreviewCleanup(Catalog const& Catalog, CleanupKind Kind,
										  ParameterDefinitions const& Rules) const;
};
} // namespace OffHanger
