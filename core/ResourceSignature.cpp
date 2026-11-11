#include "Catalog.h"
#include <set>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
namespace OffHanger
{
std::string ResourceIndex::GameSignature(FileSystem::path const& Game)
{
	auto GameRoot = FileSystem::weakly_canonical(Game);
	std::map<std::string, FileSystem::path> Sources;
	for (auto Name : {L"paths.xml", L"version.xml"})
		if (FileSystem::is_regular_file(GameRoot / Name))
			Sources[Lower(Utf8(Name))] = GameRoot / Name;
	for (auto Name : {L"res/packages", L"res/scripts", L"res/vehicles", L"res_mods", L"res_mod", L"mods"})
	{
		auto Root = GameRoot / Name;
		if (!FileSystem::is_directory(Root))
			continue;
		for (auto const& Entry : FileSystem::recursive_directory_iterator(
				 Root, FileSystem::directory_options::skip_permission_denied))
		{
			if (!Entry.is_regular_file())
				continue;
			auto Extension = Lower(PathUtf8(Entry.path().extension()));
			static std::set<std::string> const TrackedExtensions = {".pkg",
																	".wotmod",
																	".xml",
																	".py",
																	".pyc",
																	".mo",
																	".model",
																	".visual",
																	".visual_processed",
																	".primitives",
																	".primitives_processed",
																	".dds",
																	".hkx",
																	".havok"};
			if (!TrackedExtensions.contains(Extension))
				continue;
			auto Relative = PathUtf8(Entry.path().lexically_relative(GameRoot));
			std::replace(Relative.begin(), Relative.end(), '\\', '/');
			Relative = Lower(Relative);
			if (Relative.starts_with("mods/configs/offhangar_studio/"))
				continue;
			Sources[Relative] = Entry.path();
		}
	}
	std::string Manifest;
	for (auto const& [Relative, Path] : Sources)
	{
		struct _stat64 Stat{};
		if (_wstat64(Path.c_str(), &Stat) != 0)
			throw std::runtime_error("Cannot stat game resource: " + Relative);
		auto Seconds = Stat.st_mtime;
		Manifest += Relative + "|" + std::to_string(Stat.st_size) + "|" + std::to_string(Seconds) + "\n";
	}
	std::ostringstream Signature;
	Signature << std::hex << std::setfill('0') << std::setw(8)
			  << Crc32(Bytes(Manifest.begin(), Manifest.end()));
	return Signature.str();
}
} // namespace OffHanger
