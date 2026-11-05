#pragma once
#include "Common.h"
namespace OffHangar
{
struct ZipEntry
{
    std::string Name;
    uint16_t Method{}, Flags{};
    uint32_t Crc{};
    uint64_t Size{}, Compressed{}, Offset{};
};
class Archive
{
    FileSystem::path SourcePath;

  public:
    std::vector<ZipEntry> Entries;
    explicit Archive(FileSystem::path Path, std::function<bool(std::string const&)> Filter = {});
    Bytes Read(ZipEntry const& Entry) const;
    FileSystem::path const& GetPath() const
    {
        return SourcePath;
    }
};
void WriteZip(FileSystem::path const& Path, std::vector<std::pair<std::string, Bytes>> const& Files);
} // namespace OffHangar
