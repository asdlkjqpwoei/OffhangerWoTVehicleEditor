#include "Archive.h"
#include "miniz_tinfl.h"
#include <fstream>
namespace OffHanger
{
static Bytes Range(FileSystem::path const& P, uint64_t Offset, size_t Length)
{
	auto Size = FileSystem::file_size(P);
	if (Offset > Size || Length > Size - Offset || Length > 128u * 1024u * 1024u)
		throw std::runtime_error("Invalid ZIP range");
	std::ifstream F(NativePath(P), std::ios::binary);
	F.seekg(Offset);
	Bytes Result(Length);
	if (Length && !F.read(reinterpret_cast<char*>(Result.data()), Length))
		throw std::runtime_error("Cannot read ZIP");
	return Result;
}
Archive::Archive(FileSystem::path P, std::function<bool(std::string const&)> Filter)
	: SourcePath(std::move(P))
{
	auto Size = FileSystem::file_size(SourcePath);
	if (Size < 22)
		throw std::runtime_error("Truncated ZIP archive");
	auto Length = size_t(std::min<uint64_t>(Size, 65557));
	auto Tail = Range(SourcePath, Size - Length, Length);
	size_t End = Tail.size() - 22;
	while (U32(Tail, End) != 0x06054b50u || End + 22 + U16(Tail, End + 20) != Tail.size())
	{
		if (!End)
			throw std::runtime_error("ZIP directory not found");
		--End;
	}
	uint64_t Count = U16(Tail, End + 10), Bytes = U32(Tail, End + 12), Offset = U32(Tail, End + 16);
	if (U16(Tail, End + 4) || U16(Tail, End + 6))
		throw std::runtime_error("Multi-volume ZIP is unsupported");
	if (Count == 65535 || Bytes == 0xffffffffu || Offset == 0xffffffffu)
	{
		auto Eocd = Size - Length + End;
		if (Eocd < 20)
			throw std::runtime_error("Invalid ZIP64 locator");
		auto Loc = Range(SourcePath, Eocd - 20, 20);
		if (U32(Loc, 0) != 0x07064b50u)
			throw std::runtime_error("ZIP64 locator missing");
		auto Z = Range(SourcePath, U64(Loc, 8), 56);
		if (U32(Z, 0) != 0x06064b50u)
			throw std::runtime_error("ZIP64 directory missing");
		Count = U64(Z, 32);
		Bytes = U64(Z, 40);
		Offset = U64(Z, 48);
	}
	if (Bytes > 128ull * 1024 * 1024 || Count > 2000000)
		throw std::runtime_error("ZIP directory exceeds limits");
	auto Directory = Range(SourcePath, Offset, size_t(Bytes));
	size_t Pos = 0;
	for (uint64_t N = 0; N < Count; ++N)
	{
		if (U32(Directory, Pos) != 0x02014b50u)
			throw std::runtime_error("Invalid ZIP entry");
		auto Names = U16(Directory, Pos + 28), Extra = U16(Directory, Pos + 30),
			 Comment = U16(Directory, Pos + 32);
		if (Pos + 46ull + Names + Extra + Comment > Directory.size())
			throw std::runtime_error("Truncated ZIP entry");
		ZipEntry Entry;
		Entry.Name.assign(reinterpret_cast<char const*>(Directory.data() + Pos + 46), Names);
		Entry.Flags = U16(Directory, Pos + 8);
		Entry.Method = U16(Directory, Pos + 10);
		Entry.Crc = U32(Directory, Pos + 16);
		Entry.Compressed = U32(Directory, Pos + 20);
		Entry.Size = U32(Directory, Pos + 24);
		Entry.Offset = U32(Directory, Pos + 42);
		size_t X = Pos + 46 + Names, Xend = X + Extra;
		while (X + 4 <= Xend)
		{
			auto Id = U16(Directory, X), Len = U16(Directory, X + 2);
			X += 4;
			if (X + Len > Xend)
				throw std::runtime_error("Invalid ZIP extra data");
			if (Id == 1)
			{
				auto Z = X;
				if (Entry.Size == 0xffffffffu)
				{
					Entry.Size = U64(Directory, Z);
					Z += 8;
				}
				if (Entry.Compressed == 0xffffffffu)
				{
					Entry.Compressed = U64(Directory, Z);
					Z += 8;
				}
				if (Entry.Offset == 0xffffffffu)
					Entry.Offset = U64(Directory, Z);
			}
			X += Len;
		}
		if (!Filter || Filter(Entry.Name))
			Entries.push_back(std::move(Entry));
		Pos += 46ull + Names + Extra + Comment;
	}
}
Bytes Archive::Read(ZipEntry const& Entry) const
{
	if (Entry.Flags & 1)
		throw std::runtime_error("Encrypted ZIP entry is unsupported");
	if (Entry.Size > 64ull * 1024 * 1024 || Entry.Compressed > 64ull * 1024 * 1024)
		throw std::runtime_error("ZIP item exceeds limit");
	auto Header = Range(SourcePath, Entry.Offset, 30);
	if (U32(Header, 0) != 0x04034b50u)
		throw std::runtime_error("ZIP local header missing");
	auto Compressed =
		Range(SourcePath, Entry.Offset + 30 + U16(Header, 26) + U16(Header, 28), size_t(Entry.Compressed));
	Bytes Data;
	if (Entry.Method == 0)
		Data = std::move(Compressed);
	else if (Entry.Method == 8)
	{
		Data.resize(size_t(Entry.Size));
		auto N =
			tinfl_decompress_mem_to_mem(Data.data(), Data.size(), Compressed.data(), Compressed.size(), 0);
		if (N != Data.size())
			throw std::runtime_error("ZIP decompression failed: " + Entry.Name);
	}
	else
		throw std::runtime_error("Unsupported ZIP compression method");
	if (Data.size() != Entry.Size || Crc32(Data) != Entry.Crc)
		throw std::runtime_error("ZIP CRC mismatch: " + Entry.Name);
	return Data;
}
void WriteZip(FileSystem::path const& P, std::vector<std::pair<std::string, Bytes>> const& Files)
{
	if (Files.size() > 65535)
		throw std::runtime_error("Too many output files");
	Bytes Out, Central;
	for (auto const& [EntryName, EntryData] : Files)
	{
		if (EntryName.empty() || EntryName.front() == '/' || EntryName.find("..") != EntryName.npos ||
			EntryName.find('\\') != EntryName.npos || EntryName.size() > 65535 ||
			EntryData.size() > 0xffffffffu)
			throw std::runtime_error("Invalid output archive path");
		auto Offset = uint32_t(Out.size()), Crc = Crc32(EntryData);
		Put32(Out, 0x04034b50);
		Put16(Out, 20);
		Put16(Out, 0x800);
		Put16(Out, 0);
		Put32(Out, 0);
		Put32(Out, Crc);
		Put32(Out, uint32_t(EntryData.size()));
		Put32(Out, uint32_t(EntryData.size()));
		Put16(Out, uint16_t(EntryName.size()));
		Put16(Out, 0);
		Append(Out, EntryName);
		Append(Out, EntryData);
		Put32(Central, 0x02014b50);
		Put16(Central, 20);
		Put16(Central, 20);
		Put16(Central, 0x800);
		Put16(Central, 0);
		Put32(Central, 0);
		Put32(Central, Crc);
		Put32(Central, uint32_t(EntryData.size()));
		Put32(Central, uint32_t(EntryData.size()));
		Put16(Central, uint16_t(EntryName.size()));
		Put16(Central, 0);
		Put16(Central, 0);
		Put16(Central, 0);
		Put16(Central, 0);
		Put32(Central, 0);
		Put32(Central, Offset);
		Append(Central, EntryName);
	}
	auto Offset = uint32_t(Out.size());
	Append(Out, Central);
	Put32(Out, 0x06054b50);
	Put16(Out, 0);
	Put16(Out, 0);
	Put16(Out, uint16_t(Files.size()));
	Put16(Out, uint16_t(Files.size()));
	Put32(Out, uint32_t(Central.size()));
	Put32(Out, Offset);
	Put16(Out, 0);
	AtomicWrite(P, Out, false);
}
} // namespace OffHanger
