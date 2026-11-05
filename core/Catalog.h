#pragma once
#include "Archive.h"
#include "DataSection.h"
namespace OffHangar
{
struct Resource
{
    std::string Path, Source;
    FileSystem::path File;
    std::shared_ptr<Archive> Archive;
    ZipEntry Entry;
    int Priority{};
    std::shared_ptr<Bytes const> MemoryData;
    Bytes Read() const;
};
class ResourceIndex
{
  public:
    FileSystem::path GameRoot;
    std::string Version;
    std::string LoadedSignature;
    std::map<std::string, Resource> Files;
    std::vector<std::string> Diagnostics;
    void Scan(FileSystem::path const& Game, std::function<void(std::string const&)> Progress = {});
    Resource const* Find(std::string Path) const;
    std::vector<Resource const*> Suffix(std::string const& Ending) const;
    void Add(Resource Resource);
    static std::string GameSignature(FileSystem::path const& Game);
};
struct Vehicle
{
    std::string Name, DisplayName, Nation, Model, Tags, ListPath, DefinitionPath, Source;
    int NationId = -1, Tier = 0;
    uint32_t Id{}, TypeCd{};
    bool Listed = false, Hidden = false, Extension = false, Enabled = true, Conflict = false;
    Bytes StockDescriptor, StockRecord;
    int Crew = 0;
    std::vector<std::string> CrewRoles;
    std::vector<uint32_t> ModuleCDs;
};
class Catalog
{
  public:
    ResourceIndex Resources;
    std::vector<Vehicle> Vehicles;
    std::vector<std::string> Diagnostics;
    void Scan(FileSystem::path const& Game, std::function<void(std::string const&)> Progress = {});
    void Rebuild(std::function<void(std::string const&)> Progress = {});
    Vehicle const* ByName(std::string const& Name) const;
    Vehicle const* ByCd(uint32_t Cd) const;
    std::vector<size_t> Search(std::string const& Query, int Origin = 0) const;
    std::vector<std::pair<std::string, std::string>> Documents(Vehicle const& Vehicle) const;
    DataSection LoadDefinition(Vehicle const& Vehicle) const;
    Bytes StockDescriptor(Vehicle const& Vehicle) const;
    std::string Localization(std::string Key) const;

  private:
    std::map<std::string, std::map<std::string, std::string>> Translations;
    void LoadTranslations();
};
uint32_t VehicleCd(Bytes const& Descriptor);
} // namespace OffHangar
