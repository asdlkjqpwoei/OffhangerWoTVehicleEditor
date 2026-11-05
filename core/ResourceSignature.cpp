#include "Catalog.h"
#include <set>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
namespace OffHangar
{
std::string ResourceIndex::GameSignature(FileSystem::path const& Game)
{
    std::map<std::string, FileSystem::path> Sources;
    for (auto Name : {L"paths.xml", L"version.xml"})
        if (FileSystem::is_regular_file(Game / Name))
            Sources[Lower(Utf8(Name))] = Game / Name;
    for (auto Name : {L"res/packages", L"res/scripts", L"res/vehicles", L"res_mods", L"res_mod", L"mods"})
    {
        auto Root = Game / Name;
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
            auto Relative = PathUtf8(FileSystem::relative(Entry.path(), Game));
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
        Manifest += Relative + "|" + std::to_string(FileSystem::file_size(Path)) + "|" +
                    std::to_string(Seconds) + "\n";
    }
    std::ostringstream Signature;
    Signature << std::hex << std::setfill('0') << std::setw(8)
              << Crc32(Bytes(Manifest.begin(), Manifest.end()));
    return Signature.str();
}
} // namespace OffHangar
