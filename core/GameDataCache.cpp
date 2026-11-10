#include "GameDataCache.h"
#include <windows.h>

namespace OffHanger
{
namespace
{
constexpr uint64_t MaximumCacheSize = 512ull * 1024 * 1024;
struct Handle
{
	HANDLE Value{};
	explicit Handle(HANDLE Input) : Value(Input) {}
	Handle(Handle const&) = delete;
	Handle& operator=(Handle const&) = delete;
	~Handle()
	{
		if (Value && Value != INVALID_HANDLE_VALUE)
			CloseHandle(Value);
	}
};
} // namespace
std::optional<std::string> GameDataCache::Fingerprint() const
{
	if (Path.empty() || !FileSystem::exists(Path))
		return {};
	if (!FileSystem::is_regular_file(Path))
		throw std::runtime_error("The game cache path is not a regular file: " + PathUtf8(Path));
	return Sha256File(Path);
}
void GameDataCache::CheckCurrent() const
{
	if (Fingerprint() != ExpectedHash)
		throw std::runtime_error("The game data cache changed externally; reload before applying");
}
GameDataCache::GameDataCache(FileSystem::path const& GameRoot)
{
	if (GameRoot.empty())
		return;
	Path = FileSystem::absolute(GameRoot).lexically_normal() / L"data.wgpdc";
	if (FileSystem::exists(Path))
	{
		if (!FileSystem::is_regular_file(Path))
			throw std::runtime_error("The game cache path is not a regular file: " + PathUtf8(Path));
		// Fail before changing any XML if the cache is locked or cannot be removed.
		{
			Handle Probe(CreateFileW(NativePath(Path).c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ,
									 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (Probe.Value == INVALID_HANDLE_VALUE ||
				(GetFileAttributesW(NativePath(Path).c_str()) & FILE_ATTRIBUTE_READONLY))
				throw std::runtime_error("The game cache is locked or cannot be removed: " + PathUtf8(Path));
		}
		auto PreviousWriteTime = FileSystem::last_write_time(Path);
		auto PreviousHash = Fingerprint();
		Before = ReadFile(Path, MaximumCacheSize);
		ExpectedHash = Fingerprint();
		if (ExpectedHash != PreviousHash || FileSystem::file_size(Path) != Before->size() ||
			FileSystem::last_write_time(Path) != PreviousWriteTime)
			throw std::runtime_error("The game cache changed while its preimage was being read");
	}
}
GameCacheResult GameDataCache::Refresh(std::function<void(std::string const&)> const& Report)
{
	if (Path.empty())
		return {};
	if (Report)
		Report("cache.invalidate");
	EnsureWritesAllowed();
	CheckCurrent();
	if (ExpectedHash)
	{
		// The verified generator requires the full game engine. Invalidate only this
		// derived file; the next normal game startup rebuilds it from current resources.
		if (!DeleteFileW(NativePath(Path).c_str()))
			throw std::runtime_error("Cannot remove the stale game cache (Windows error " +
									 std::to_string(GetLastError()) + "): " + PathUtf8(Path));
		ExpectedHash.reset();
		Changed = true;
	}
	return {
		GameCacheMethod::Invalidated,
		"data.wgpdc is absent; the next normal game startup will rebuild it. No game process was started."};
}
void GameDataCache::Restore()
{
	if (!Changed)
		return;
	EnsureWritesAllowed();
	CheckCurrent();
	AtomicWrite(Path, *Before, false);
	ExpectedHash = Fingerprint();
	Changed = false;
}
} // namespace OffHanger
