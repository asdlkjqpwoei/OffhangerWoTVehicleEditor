#pragma once
#include "Catalog.h"
#include "Configuration.h"
#include "Defaults.h"
#include <set>

namespace OffHangar
{
using NodeAddress = std::vector<size_t>;
struct NodeName
{
    std::string Name;
    size_t Ordinal{};
};
using NamedAddress = std::vector<NodeName>;
struct VehicleDocument
{
    std::string Path;
    DataSection Initial, Loaded, Current;
    Bytes InitialBytes;
    uint32_t LoadedCrc{};
};
struct DocumentScope
{
    std::string Path, Component, Module;
    NodeAddress Root;
};
struct VehicleValue
{
    std::string Document, DisplayPath, Value, Component, Module;
    NodeAddress Address;
    bool ReadOnly{};
    std::vector<std::string> SharedVehicles;
};
struct AppliedFile
{
    bool Existed{};
    Bytes Before;
    uint32_t LastCrc{};
    uint32_t PreviousCrc{};
};

// Scalar editing never replaces a vehicle's XML schema or native mechanism.
// Applied-file preimages remain independent of the in-memory loaded baseline.
class VehicleWorkspace
{
    Catalog const* Catalogue{};
    DefaultResources const* Defaults{};
    FileSystem::path CacheFile, OverrideRoot;
    std::string ResourceSignature;
    mutable std::optional<uint32_t> CacheCrc;
    std::map<std::string, VehicleDocument> Documents;
    std::map<std::string, AppliedFile> Applied;
    std::map<std::string, std::vector<DocumentScope>> Scopes;
    std::map<std::string, std::set<std::string>> Users;
    std::map<std::string, std::set<std::string>> Owners;
    uint64_t ChangeRevision{};
    bool InBatch{}, ResetInProgress{}, ApplyInProgress{}, LegacyLedger{};
    void BuildScopes();
    void Persist() const;
    void RestoreCache();
    void ValidateValue(Section const& Node, NamedAddress const& Names, std::string const& Value) const;
    static void ValidateStructure(DataSection const& Initial, DataSection const& Current);
    void Commit(std::function<void()> const& Command);
    std::vector<std::pair<std::string, Bytes>> PendingFiles() const;

  public:
    void Open(Catalog const& Input, DefaultResources const* NativeDefaults, FileSystem::path Cache,
              FileSystem::path Overrides);
    VehicleDocument& Document(std::string const& Path);
    VehicleDocument const& Get(std::string const& Path) const;
    std::vector<DocumentScope> const& VehicleScopes(Vehicle const& Vehicle) const;
    std::vector<VehicleValue> Values(Vehicle const& Vehicle);
    std::vector<std::string> SharedUsers(DocumentScope const& Scope) const;
    static Section& Resolve(DataSection& Document, NodeAddress const& Address);
    static Section const& Resolve(DataSection const& Document, NodeAddress const& Address);
    static NamedAddress Names(DataSection const& Document, NodeAddress const& Address);
    static NodeAddress Locate(DataSection const& Document, NamedAddress const& Names);
    static std::string Display(NamedAddress const& Names);
    static bool IsReadOnly(Section const& Node);
    void Edit(Vehicle const& Vehicle, std::string const& Path, NodeAddress const& Address,
              std::string const& Value);
    void EditBatch(Vehicle const& Vehicle, std::vector<VehicleValue> const& Changes);
    void SetPriceCurrency(Vehicle const& Vehicle, bool Gold);
    bool PriceCurrency(Vehicle const& Vehicle);
    void ResetVehicle(Vehicle const& Vehicle);
    void ResetDocument(std::string const& Path);
    bool HasPending() const;
    size_t AppliedCount() const
    {
        return Applied.size();
    }
    uint64_t Revision() const
    {
        return ChangeRevision;
    }
    size_t ModifiedVehicleCount() const;
    std::vector<std::string> PreviewSave() const;
    void SaveToGame();
    void ResetAll();
    void DiscardPending();
    void ExportChanges(FileSystem::path const& Target, Vehicle const* Selected = nullptr);
    size_t ImportChanges(FileSystem::path const& Source, Vehicle const* Selected = nullptr);
};
} // namespace OffHangar
