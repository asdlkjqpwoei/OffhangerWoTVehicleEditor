#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace OffHangar
{
namespace FileSystem = std::filesystem;
using Bytes = std::vector<uint8_t>;
Bytes ReadFile(FileSystem::path const& Path, uint64_t MaxSize = 128ull * 1024 * 1024);
void WriteFile(FileSystem::path const& Path, Bytes const& Bytes);
void WriteText(FileSystem::path const& Path, std::string const& Text);
std::string Utf8(std::wstring_view Text);
std::wstring Wide(std::string_view Text);
std::string Lower(std::string Text);
std::string Trim(std::string Text);
std::string Base64(Bytes const& Data);
Bytes Unbase64(std::string_view Text);
uint32_t Crc32(Bytes const& Data);
std::string Sha256File(FileSystem::path const& Path);
std::string Timestamp();
std::string PathUtf8(FileSystem::path const& Path);
std::string NormalizeResourcePath(std::string Path);
FileSystem::path AtomicWrite(FileSystem::path const& Path, Bytes const& Data, bool Backup = true,
                             std::optional<uint32_t> ExpectedCrc = {});
bool GameRunning();
void EnsureWritesAllowed();
FileSystem::path NativePath(FileSystem::path const& Path);
uint16_t U16(Bytes const& Data, size_t Pos);
uint32_t U32(Bytes const& Data, size_t Pos);
uint64_t U64(Bytes const& Data, size_t Pos);
void Put16(Bytes& Data, uint16_t Value);
void Put32(Bytes& Data, uint32_t Value);
void Put64(Bytes& Data, uint64_t Value);
void Append(Bytes& Data, Bytes const& Value);
void Append(Bytes& Data, std::string_view Value);
} // namespace OffHangar
