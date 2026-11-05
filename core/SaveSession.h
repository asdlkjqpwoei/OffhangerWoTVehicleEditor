#pragma once
#include "SaveGame.h"

namespace OffHangar
{
enum class CleanupKind
{
    Duplicates,
    Invalid,
    Nonstandard,
    RentalDemo
};
struct SaveIssue
{
    int64_t InventoryId{};
    std::string VehicleName;
    std::string Reason;
};

class SaveSession
{
    Bytes Snapshot() const;
    void Restore(Bytes const& State);

  public:
    SaveGame Account;
    void Open(FileSystem::path const& Path);
    void Close();
    // A batch is atomic, including validation failures in its final item.
    void Execute(std::string Label, std::function<void(SaveGame&)> const& Command);
    std::vector<SaveIssue> PreviewCleanup(Catalog const& Catalog, CleanupKind Kind) const;
};
} // namespace OffHangar
