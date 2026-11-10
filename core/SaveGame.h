#pragma once
#include "Catalog.h"
#include "Pickle.h"
namespace OffHanger
{
struct OwnedVehicle
{
	int64_t InventoryId{};
	uint32_t TypeCd{};
	std::string Name, Display;
	int Tier{};
	bool Known{};
	ValuePtr Record;
};
class SaveGame
{
  public:
	FileSystem::path Path;
	ValuePtr Root;
	uint32_t OriginalCrc{};
	bool Dirty = false;
	bool Json922 = false;
	void Open(FileSystem::path const& Path);
	std::vector<OwnedVehicle> Inventory(Catalog const& Catalog) const;
	std::vector<SaveProperty> Properties(std::string const& Search = {});
	void EditProperty(std::vector<size_t> const& Address, std::string const& Input);
	void SetCurrency(std::string const& Key, int64_t Value);
	int64_t Currency(std::string const& Key) const;
	void SetVehicleXp(uint32_t TypeCd, int64_t Xp);
	int64_t VehicleXp(uint32_t TypeCd) const;
	int64_t AddVehicle(Vehicle const& Vehicle, Catalog const& Catalog, bool WithCrew = true);
	void RemoveVehicle(int64_t InventoryId);
	int64_t CloneVehicle(int64_t InventoryId, bool CloneCrew = true);
	size_t DeduplicateVehicles(Catalog const& Catalog);
	FileSystem::path Save(FileSystem::path const& Target);
};
} // namespace OffHanger
