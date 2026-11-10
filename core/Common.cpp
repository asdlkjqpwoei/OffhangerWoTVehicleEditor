#include "Common.h"
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>

namespace OffHanger
{
FileSystem::path NativePath(FileSystem::path const& Path)
{
	auto Text = FileSystem::absolute(Path).lexically_normal().wstring();
	if (Text.starts_with(L"\\\\?\\"))
		return Text;
	if (Text.starts_with(L"\\\\"))
		return L"\\\\?\\UNC\\" + Text.substr(2);
	return L"\\\\?\\" + Text;
}
void EnsureWritesAllowed()
{
	if (GameRunning())
		throw std::runtime_error("World of Tanks is running. All writes are disabled until the game exits.");
}
Bytes ReadFile(FileSystem::path const& Path, uint64_t Maximum)
{
	std::ifstream Input(NativePath(Path), std::ios::binary | std::ios::ate);
	if (!Input)
		throw std::runtime_error("Cannot open: " + PathUtf8(Path));
	auto Length = Input.tellg();
	if (Length < 0 || uint64_t(Length) > Maximum)
		throw std::runtime_error("File size exceeds limit: " + PathUtf8(Path));
	Bytes Data(static_cast<size_t>(Length));
	Input.seekg(0);
	if (!Data.empty() && !Input.read(reinterpret_cast<char*>(Data.data()), Data.size()))
		throw std::runtime_error("Incomplete file read");
	return Data;
}
void WriteFile(FileSystem::path const& Path, Bytes const& Data)
{
	EnsureWritesAllowed();
	if (!Path.parent_path().empty())
		FileSystem::create_directories(NativePath(Path.parent_path()));
	EnsureWritesAllowed();
	std::ofstream Output(NativePath(Path), std::ios::binary | std::ios::trunc);
	if (!Output || (!Data.empty() && !Output.write(reinterpret_cast<char const*>(Data.data()), Data.size())))
		throw std::runtime_error("Cannot write: " + PathUtf8(Path));
	Output.flush();
	if (!Output)
		throw std::runtime_error("Cannot flush output file");
}
void WriteText(FileSystem::path const& Path, std::string const& Text)
{
	WriteFile(Path, Bytes(Text.begin(), Text.end()));
}
std::string Utf8(std::wstring_view Text)
{
	if (Text.empty())
		return {};
	int Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Text.data(), int(Text.size()), nullptr, 0,
									 nullptr, nullptr);
	if (!Length)
		throw std::runtime_error("Invalid Unicode string");
	std::string Result(Length, 0);
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Text.data(), int(Text.size()), Result.data(), Length,
						nullptr, nullptr);
	return Result;
}
std::wstring Wide(std::string_view Text)
{
	if (Text.empty())
		return {};
	int Length =
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), int(Text.size()), nullptr, 0);
	if (!Length)
		throw std::runtime_error("Invalid UTF-8 string");
	std::wstring Result(Length, 0);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text.data(), int(Text.size()), Result.data(), Length);
	return Result;
}
std::string PathUtf8(FileSystem::path const& Path)
{
	return Utf8(Path.wstring());
}
std::string NormalizeResourcePath(std::string Path)
{
	std::replace(Path.begin(), Path.end(), '\\', '/');
	while (Path.starts_with("./"))
		Path.erase(0, 2);
	if (Path.empty() || Path.front() == '/' || Path.find(':') != Path.npos || Path.find('\0') != Path.npos)
		throw std::runtime_error("Invalid virtual resource path");
	for (size_t Begin = 0; Begin <= Path.size();)
	{
		auto End = Path.find('/', Begin);
		auto Component = Path.substr(Begin, End == Path.npos ? Path.npos : End - Begin);
		if (Component.empty() || Component == "." || Component == "..")
			throw std::runtime_error("Invalid virtual resource path component");
		if (End == Path.npos)
			break;
		Begin = End + 1;
	}
	return Lower(std::move(Path));
}
std::string Lower(std::string Text)
{
	for (auto& Character : Text)
		if (static_cast<unsigned char>(Character) < 128)
			Character = char(std::tolower(static_cast<unsigned char>(Character)));
	return Text;
}
std::string Trim(std::string Text)
{
	auto First = Text.find_first_not_of(" \r\n\t");
	return First == Text.npos ? "" : Text.substr(First, Text.find_last_not_of(" \r\n\t") - First + 1);
}
std::string Base64(Bytes const& Data)
{
	static char const Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string Result;
	for (size_t Index = 0; Index < Data.size(); Index += 3)
	{
		uint32_t Group = uint32_t(Data[Index]) << 16;
		if (Index + 1 < Data.size())
			Group |= uint32_t(Data[Index + 1]) << 8;
		if (Index + 2 < Data.size())
			Group |= Data[Index + 2];
		Result += Alphabet[(Group >> 18) & 63];
		Result += Alphabet[(Group >> 12) & 63];
		Result += Index + 1 < Data.size() ? Alphabet[(Group >> 6) & 63] : '=';
		Result += Index + 2 < Data.size() ? Alphabet[Group & 63] : '=';
	}
	return Result;
}
Bytes Unbase64(std::string_view Text)
{
	std::string Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	Bytes Result;
	uint32_t Accumulator = 0;
	int Bits = 0;
	bool Ended = false;
	for (unsigned char Character : Text)
	{
		if (std::isspace(Character))
			continue;
		if (Character == '=')
		{
			Ended = true;
			continue;
		}
		auto Position = Alphabet.find(Character);
		if (Position == Alphabet.npos || Ended)
			throw std::runtime_error("Invalid base64");
		Accumulator = (Accumulator << 6) | uint32_t(Position);
		Bits += 6;
		if (Bits >= 8)
		{
			Bits -= 8;
			Result.push_back(uint8_t(Accumulator >> Bits));
		}
	}
	return Result;
}
uint32_t Crc32(Bytes const& Data)
{
	static auto const Table = []
	{
		std::array<uint32_t, 256> Values{};
		for (uint32_t Index = 0; Index < Values.size(); ++Index)
		{
			auto Remainder = Index;
			for (int Bit = 0; Bit < 8; ++Bit)
				Remainder = (Remainder >> 1) ^ (0xedb88320u & (0u - (Remainder & 1u)));
			Values[Index] = Remainder;
		}
		return Values;
	}();
	uint32_t Checksum = ~0u;
	for (auto Byte : Data)
		Checksum = Table[(Checksum ^ Byte) & 255] ^ (Checksum >> 8);
	return ~Checksum;
}
std::string Sha256File(FileSystem::path const& Path)
{
	std::ifstream Input(NativePath(Path), std::ios::binary);
	if (!Input)
		throw std::runtime_error("Cannot open: " + PathUtf8(Path));
	struct HashOwner
	{
		BCRYPT_HASH_HANDLE Handle{};
		~HashOwner()
		{
			if (Handle)
				BCryptDestroyHash(Handle);
		}
	} Hash;
	if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &Hash.Handle, nullptr, 0, nullptr, 0, 0) < 0)
		throw std::runtime_error("Cannot initialize SHA-256");
	std::array<uint8_t, 64 * 1024> Buffer;
	while (Input)
	{
		Input.read(reinterpret_cast<char*>(Buffer.data()), Buffer.size());
		if (auto Count = Input.gcount();
			Count > 0 && BCryptHashData(Hash.Handle, Buffer.data(), static_cast<ULONG>(Count), 0) < 0)
			throw std::runtime_error("Cannot hash: " + PathUtf8(Path));
	}
	if (!Input.eof())
		throw std::runtime_error("Cannot read: " + PathUtf8(Path));
	std::array<uint8_t, 32> Digest;
	if (BCryptFinishHash(Hash.Handle, Digest.data(), static_cast<ULONG>(Digest.size()), 0) < 0)
		throw std::runtime_error("Cannot finish SHA-256");
	std::ostringstream Result;
	Result << std::hex << std::setfill('0');
	for (auto Byte : Digest)
		Result << std::setw(2) << static_cast<unsigned>(Byte);
	return Result.str();
}
std::string Timestamp()
{
	SYSTEMTIME Now;
	GetLocalTime(&Now);
	char Buffer[64];
	sprintf_s(Buffer, "%04u%02u%02u-%02u%02u%02u-%03u", Now.wYear, Now.wMonth, Now.wDay, Now.wHour,
			  Now.wMinute, Now.wSecond, Now.wMilliseconds);
	return Buffer;
}
FileSystem::path AtomicWrite(FileSystem::path const& Path, Bytes const& Data, bool Backup,
							 std::optional<uint32_t> Expected)
{
	EnsureWritesAllowed();
	static std::atomic_uint64_t Sequence{};
	std::wostringstream Suffix;
	Suffix << Wide(Timestamp()) << L'-' << GetCurrentProcessId() << L'-' << std::setfill(L'0')
		   << std::setw(16) << Sequence.fetch_add(1);
	if (Expected && (!FileSystem::exists(Path) || Crc32(ReadFile(Path)) != *Expected))
		throw std::runtime_error("The original file changed after opening; reload it before saving.");
	FileSystem::path Saved;
	if (Backup && FileSystem::exists(Path))
	{
		Saved = Path;
		Saved += L".before-OffHangerWoTOfflineVehicleEditor-" + Suffix.str() + L".bak";
		FileSystem::copy_file(NativePath(Path), NativePath(Saved), FileSystem::copy_options::none);
	}
	auto Temp = Path;
	Temp += L".OffHangerWoTOfflineVehicleEditor-" + Suffix.str() + L".tmp";
	WriteFile(Temp, Data);
	HANDLE Handle = CreateFileW(NativePath(Temp).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
								OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (Handle == INVALID_HANDLE_VALUE)
		throw std::runtime_error("Cannot flush temporary file");
	bool Flushed = FlushFileBuffers(Handle) != 0;
	CloseHandle(Handle);
	EnsureWritesAllowed();
	if (!Flushed || !MoveFileExW(NativePath(Temp).c_str(), NativePath(Path).c_str(),
								 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		FileSystem::remove(NativePath(Temp));
		throw std::runtime_error("Atomic save failed; original file retained");
	}
	return Saved;
}
bool GameRunning()
{
	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (Snapshot == INVALID_HANDLE_VALUE)
		return true;
	PROCESSENTRY32W Entry{sizeof(Entry)};
	bool Found = false;
	if (!Process32FirstW(Snapshot, &Entry))
	{
		CloseHandle(Snapshot);
		return true;
	}
	do
	{
		if (_wcsicmp(Entry.szExeFile, L"WorldOfTanks.exe") == 0 ||
			_wcsicmp(Entry.szExeFile, L"WorldOfTanks64.exe") == 0)
		{
			Found = true;
			break;
		}
	} while (Process32NextW(Snapshot, &Entry));
	if (!Found && GetLastError() != ERROR_NO_MORE_FILES)
		Found = true;
	CloseHandle(Snapshot);
	return Found;
}
uint16_t U16(Bytes const& Data, size_t Offset)
{
	if (Offset > Data.size() || Data.size() - Offset < 2)
		throw std::runtime_error("Truncated binary data");
	return Data[Offset] | uint16_t(Data[Offset + 1]) << 8;
}
uint32_t U32(Bytes const& Data, size_t Offset)
{
	return U16(Data, Offset) | uint32_t(U16(Data, Offset + 2)) << 16;
}
uint64_t U64(Bytes const& Data, size_t Offset)
{
	return U32(Data, Offset) | uint64_t(U32(Data, Offset + 4)) << 32;
}
void Put16(Bytes& Data, uint16_t Value)
{
	Data.push_back(uint8_t(Value));
	Data.push_back(uint8_t(Value >> 8));
}
void Put32(Bytes& Data, uint32_t Value)
{
	Put16(Data, uint16_t(Value));
	Put16(Data, uint16_t(Value >> 16));
}
void Put64(Bytes& Data, uint64_t Value)
{
	Put32(Data, uint32_t(Value));
	Put32(Data, uint32_t(Value >> 32));
}
void Append(Bytes& Destination, Bytes const& Source)
{
	Destination.insert(Destination.end(), Source.begin(), Source.end());
}
void Append(Bytes& Destination, std::string_view Source)
{
	Destination.insert(Destination.end(), Source.begin(), Source.end());
}
} // namespace OffHanger
