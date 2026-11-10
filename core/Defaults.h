#pragma once
#include "Archive.h"
#include "DataSection.h"

namespace OffHanger
{
// Independent of the active resource index: applying edits or loading a working
// cache must never replace the original client defaults.
class DefaultResources
{
	std::shared_ptr<Archive const> Package;
	std::map<std::string, ZipEntry> Definitions;
	std::string Client;

  public:
	explicit DefaultResources(FileSystem::path const& Folder);
	static void Install(FileSystem::path const& Source, FileSystem::path const& Target);
	bool Contains(std::string const& ResourcePath) const;
	std::optional<DataSection> Load(std::string const& ResourcePath) const;
	FileSystem::path const& GetPath() const;
	std::string const& GetClient() const;
	size_t Count() const;
};
} // namespace OffHanger
