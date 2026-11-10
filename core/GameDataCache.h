#pragma once
#include "Common.h"

namespace OffHanger
{
enum class GameCacheMethod
{
	Unchanged,
	Invalidated
};
struct GameCacheResult
{
	GameCacheMethod Method = GameCacheMethod::Unchanged;
	std::string Detail;
};
// Retain the cache preimage until the enclosing XML transaction is committed.
// Invalidation touches only data.wgpdc and never starts the game or executes game scripts.
class GameDataCache
{
	FileSystem::path Path;
	std::optional<Bytes> Before;
	std::optional<std::string> ExpectedHash;
	bool Changed{};
	std::optional<std::string> Fingerprint() const;
	void CheckCurrent() const;

  public:
	explicit GameDataCache(FileSystem::path const& GameRoot);
	GameCacheResult Refresh(std::function<void(std::string const&)> const& Report = {});
	void Restore();
};
} // namespace OffHanger
