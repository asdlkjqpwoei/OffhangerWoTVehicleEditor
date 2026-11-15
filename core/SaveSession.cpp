#include "SaveSession.h"
#include "Crew.h"
#include "Json.h"
#include "VehicleParameters.h"
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
	++ChangeRevision;
}
void SaveSession::Close()
{
	Account = {};
	++ChangeRevision;
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
		++ChangeRevision;
	}
	catch (...)
	{
		Restore(Before);
		Account.Dirty = WasDirty;
		++ChangeRevision;
		throw;
	}
}
std::string SaveSession::InvalidVehicleReason(OwnedVehicle const& Owned, Catalog const& Catalogue)
{
	if (Owned.InventoryId < 1)
		return "invalid_inventory_key";
	if (!Owned.TypeCd)
		return "invalid_descriptor";
	auto Definition = Catalogue.ByCd(Owned.TypeCd);
	if (!Definition)
		return "missing_vehicle";
	if (!Definition->Listed || !Definition->Enabled || Definition->Conflict)
		return "missing_registration";
	if (!Catalogue.Resources.Find(Definition->DefinitionPath) && Definition->StockDescriptor.empty())
		return "missing_definition";
	return {};
}
SaveIssue SaveSession::DescribeDeletion(OwnedVehicle const& Owned, std::string Reason)
{
	SaveIssue Issue{Owned.InventoryId, Owned.Known ? Owned.Display + " · " + Owned.Name : Owned.Name,
					std::move(Reason), Owned.TypeCd, Owned.InventoryKey};
	if (Owned.Record && Owned.Record->Kind == ValueKind::Dictionary)
		for (auto const& [Key, Value] : Owned.Record->Pairs)
		{
			auto Preview = Value ? Value->Display() : "None";
			if (Preview.size() > 240)
				Preview = "text (" + std::to_string(Preview.size()) + " bytes)";
			Issue.DataFields.push_back(Key->Display() + " = " + Preview);
		}
	else
		Issue.DataFields.push_back(Owned.Record ? Owned.Record->Display() : "None");
	if (auto Crew = Owned.Record ? Owned.Record->Get("crew") : nullptr)
		for (auto const& Member : Crew->Items)
			if (Member && Member->Kind != ValueKind::None)
				Issue.CrewIds.push_back(Member->AsInt(-1));
	return Issue;
}
std::vector<SaveIssue> SaveSession::PreviewCleanup(Catalog const& Catalog, CleanupKind Kind,
												   ParameterDefinitions const& Rules) const
{
	std::vector<SaveIssue> Result;
	std::map<std::string, DataSection> Metadata;
	for (auto const& Owned : Account.Inventory(Catalog))
	{
		auto Definition = Catalog.ByCd(Owned.TypeCd);
		auto Invalid = InvalidVehicleReason(Owned, Catalog);
		std::string Reason = Kind == CleanupKind::Invalid ? Invalid : "";
		if (Kind == CleanupKind::Unavailable && Invalid.empty() && Definition)
		{
			Section const* Entry = nullptr;
			if (auto Resource = Catalog.Resources.Find(Definition->ListPath))
			{
				if (!Metadata.contains(Definition->ListPath))
					Metadata.emplace(Definition->ListPath, DataSection::Parse(Resource->Read()));
				Entry = Metadata.at(Definition->ListPath).Root.Child(Definition->Model);
			}
			if (Rules.Classify(*Definition, Entry, &Catalog).HiddenByDefault)
				Reason = "unavailable";
		}
		if (!Reason.empty())
			Result.push_back(DescribeDeletion(Owned, Reason));
	}
	return Result;
}
} // namespace OffHanger
