#include "SaveSession.h"
#include "Crew.h"
#include "Json.h"
#include <set>

namespace OffHanger
{
Bytes SaveSession::Snapshot() const
{
	return Account.Json922 ? WriteJson(Account.Root) : WritePickle(Account.Root);
}
void SaveSession::Restore(Bytes const& State)
{
	Account.Root = Account.Json922 ? ReadJson(State) : ReadPickle(State);
	Account.Dirty = true;
}
void SaveSession::Open(FileSystem::path const& Path)
{
	SaveGame Loaded;
	Loaded.Open(Path);
	Account = std::move(Loaded);
}
void SaveSession::Close()
{
	Account = {};
}
void SaveSession::Execute(std::string /*Label*/, std::function<void(SaveGame&)> const& Command)
{
	EnsureWritesAllowed();
	if (!Account.Root)
		throw std::runtime_error("Open an account save first");
	auto Before = Snapshot();
	bool WasDirty = Account.Dirty;
	try
	{
		Command(Account);
		if (Snapshot() == Before)
			return;
		Account.Dirty = true;
	}
	catch (...)
	{
		Restore(Before);
		Account.Dirty = WasDirty;
		throw;
	}
}
std::vector<SaveIssue> SaveSession::PreviewCleanup(Catalog const& Catalog, CleanupKind Kind) const
{
	std::vector<SaveIssue> Result;
	std::set<uint32_t> Seen;
	for (auto const& Owned : Account.Inventory(Catalog))
	{
		auto Definition = Catalog.ByCd(Owned.TypeCd);
		std::string Reason;
		if (Kind == CleanupKind::Duplicates && Owned.TypeCd && !Seen.insert(Owned.TypeCd).second)
			Reason = "duplicate vehicle type";
		if (Kind == CleanupKind::Invalid && (!Owned.TypeCd || !Definition))
			Reason = Owned.TypeCd ? "type absent from the current catalogue" : "invalid descriptor";
		if (Kind == CleanupKind::Nonstandard && Definition)
		{
			auto Tags = Lower(Definition->Tags);
			bool NormalClass = false;
			for (auto Class : {"lighttank", "mediumtank", "heavytank", "at-spg", "spg"})
				NormalClass |= Tags.find(Class) != Tags.npos;
			if (!NormalClass || Definition->Tier < 1 || Definition->Tier > 11)
				Reason = "special mode vehicle outside the standard class/tier range";
		}
		if (Kind == CleanupKind::RentalDemo)
		{
			auto Name = Lower(Owned.Name);
			auto Tags = Definition ? Lower(Definition->Tags) : "";
			if (Name.find("rental") != Name.npos || Name.find("_demo") != Name.npos ||
				Name.find("7x7") != Name.npos || Tags.find("rental") != Tags.npos ||
				Tags.find("demo") != Tags.npos)
				Reason = "rental/demo variant";
		}
		if (!Reason.empty())
			Result.push_back({Owned.InventoryId, Owned.Name, Reason});
	}
	return Result;
}
} // namespace OffHanger
