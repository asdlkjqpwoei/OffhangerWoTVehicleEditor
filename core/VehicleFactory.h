#pragma once
#include "Catalog.h"
#include "Pickle.h"
namespace OffHangar
{
struct StockVehicle
{
    Bytes Descriptor;
    std::vector<std::string> Roles;
    std::vector<uint32_t> ModuleCDs;
    std::vector<std::pair<uint32_t, int>> Ammunition;
    std::vector<std::pair<uint32_t, uint32_t>> TurretGunCDs;
    size_t EquipmentSlots = 3, BoosterSlots = 1, DeviceSlots = 3;
    std::vector<uint32_t> BuiltinEquipment;
    ValuePtr Record() const;
};
class VehicleFactory
{
    Catalog const& SourceCatalog;
    std::map<std::string, DataSection> Tables;
    DataSection const& Document(std::string const& Path);
    std::vector<Section const*> ComponentTables(Vehicle const& Vehicle, std::string const& Group);
    uint16_t ComponentId(Vehicle const& Vehicle, std::string const& Group, Section const& Local);
    Section EffectiveComponent(Vehicle const& Vehicle, std::string const& Group, Section const& Local);

  public:
    explicit VehicleFactory(Catalog const& InputCatalog) : SourceCatalog(InputCatalog) {}
    StockVehicle Stock(Vehicle const& Vehicle);
};
} // namespace OffHangar
