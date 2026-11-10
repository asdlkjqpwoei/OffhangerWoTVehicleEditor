#include "Geometry.h"
#include <regex>
#include <set>

namespace OffHanger
{
namespace
{
struct Chunk
{
	size_t Begin{}, End{};
};
struct TagItem
{
	std::string Type;
	uint32_t Offset{}, Count{};
};

// TAG0 references are item-table indices, not machine pointers. The reader only
// interprets the mesh-related records; unknown objects are never instantiated.
class TagReader
{
	Bytes const& Data;
	std::map<std::string, Chunk> Chunks;
	std::vector<std::string> Types;
	std::vector<TagItem> Items;
	size_t Payload{};

	uint32_t Big32(size_t Position) const
	{
		auto Value = U32(Data, Position);
		return (Value >> 24) | ((Value >> 8) & 0xff00) | ((Value << 8) & 0xff0000) | (Value << 24);
	}
	void Bounds(size_t Position, uint64_t Length) const
	{
		if (Position > Data.size() || Length > Data.size() - Position)
			throw std::runtime_error("Havok record exceeds file bounds");
	}
	void ReadChunks(size_t Begin, size_t End, unsigned Depth = 0)
	{
		if (Depth > 3)
			throw std::runtime_error("Havok chunk nesting exceeds limit");
		for (size_t Position = Begin; Position + 8 <= End;)
		{
			auto Size = Big32(Position) & 0x3fffffff;
			if (Size < 8 || Size > End - Position)
				throw std::runtime_error("Invalid Havok chunk length");
			std::string Tag(Data.begin() + Position + 4, Data.begin() + Position + 8);
			Chunks[Tag] = {Position + 8, Position + Size};
			if (Tag == "TYPE" || Tag == "INDX")
				ReadChunks(Position + 8, Position + Size, Depth + 1);
			Position += Size;
		}
	}
	uint32_t Packed(size_t& Position, size_t End) const
	{
		if (Position >= End)
			throw std::runtime_error("Truncated Havok packed integer");
		auto First = Data.at(Position++);
		if (First == 0xe8)
		{
			if (Position + 4 > End)
				throw std::runtime_error("Truncated Havok extended integer");
			auto Value = U32(Data, Position);
			Position += 4;
			return Value;
		}
		unsigned Width = !(First & 128) ? 0 : !(First & 64) ? 1 : !(First & 32) ? 2 : 3;
		if (Width > End - Position)
			throw std::runtime_error("Truncated Havok packed integer");
		uint32_t Value = First & (Width == 1 ? 0x3f : Width == 2 ? 0x1f : Width == 3 ? 7 : 0x7f);
		for (unsigned Index = 0; Index < Width; ++Index)
			Value = (Value << 8) | Data[Position++];
		return Value;
	}
	std::string ItemText(uint32_t Index) const
	{
		auto const& Item = Items.at(Index);
		if (Item.Type != "char")
			return {};
		auto Begin = Payload + Item.Offset;
		Bounds(Begin, Item.Count);
		auto End = Begin;
		while (End < Begin + Item.Count && Data[End])
			++End;
		return std::string(Data.begin() + Begin, Data.begin() + End);
	}
	TagItem const& Reference(uint32_t Index, std::initializer_list<char const*> Expected,
							 bool Optional = false) const
	{
		static TagItem const Empty;
		if (!Index && Optional)
			return Empty;
		if (!Index || Index >= Items.size())
			throw std::runtime_error("Invalid Havok array reference: " + std::to_string(Index));
		auto const& Item = Items[Index];
		for (auto Name : Expected)
			if (Item.Type == Name)
				return Item;
		throw std::runtime_error("Unexpected Havok array type: " + Item.Type);
	}
	size_t Array(TagItem const& Item, size_t Stride) const
	{
		if (Item.Count > 4000000)
			throw std::runtime_error("Havok array exceeds limits");
		auto Address = Payload + Item.Offset;
		Bounds(Address, uint64_t(Item.Count) * Stride);
		return Address;
	}
	Vector3 Vector(size_t Address) const
	{
		return {Float32(Data, Address), Float32(Data, Address + 4), Float32(Data, Address + 8)};
	}
	void DecodeMesh(uint32_t Index, uint32_t Material, Mesh& Result) const
	{
		auto const& MeshData = Reference(Index, {"hknpCompressedMeshShapeData"});
		auto Address = Array(MeshData, 88);
		auto DomainMin = Vector(Address + 16), DomainMax = Vector(Address + 32);
		auto const& Sections = Reference(U32(Data, Address + 64), {"hkcdStaticMeshTree::Section"});
		auto const& Primitives = Reference(U32(Data, Address + 68), {"hkcdStaticMeshTree::Primitive"});
		auto const& SharedIndices = Reference(U32(Data, Address + 72), {"hkUint16", "unsigned short"}, true);
		auto const& PackedVertices = Reference(U32(Data, Address + 76), {"hkUint32", "unsigned int"}, true);
		auto const& SharedVertices =
			Reference(U32(Data, Address + 80), {"hkUint64", "unsigned long long"}, true);
		auto SectionAddress = Array(Sections, 96), PrimitiveAddress = Array(Primitives, 4);
		auto SharedIndexAddress = Array(SharedIndices, 2), PackedAddress = Array(PackedVertices, 4);
		auto SharedAddress = Array(SharedVertices, 8);
		std::vector<Vector3> Shared;
		for (uint32_t Vertex = 0; Vertex < SharedVertices.Count; ++Vertex)
		{
			auto Bits = U64(Data, SharedAddress + size_t(Vertex) * 8);
			Shared.push_back(
				{DomainMin.X + float(Bits & 0x1fffff) * (DomainMax.X - DomainMin.X) / 2097151.f,
				 DomainMin.Y + float((Bits >> 21) & 0x1fffff) * (DomainMax.Y - DomainMin.Y) / 2097151.f,
				 DomainMin.Z + float((Bits >> 42) & 0x3fffff) * (DomainMax.Z - DomainMin.Z) / 4194303.f});
		}
		auto AppendTriangle = [&](Vector3 A, Vector3 B, Vector3 C)
		{
			auto Normal = Cross(B - A, C - A);
			if (Dot(Normal, Normal) <= 1e-14f)
				return;
			if (Result.Triangles.size() >= 3000000)
				throw std::runtime_error("Havok mesh exceeds triangle limit");
			Result.Triangles.push_back({{A, B, C}, {}, Material, false});
		};
		for (uint32_t Section = 0; Section < Sections.Count; ++Section)
		{
			auto Start = SectionAddress + size_t(Section) * 96;
			auto FirstPacked = U32(Data, Start + 72), FirstShared = U32(Data, Start + 76);
			auto FirstPrimitive = U32(Data, Start + 80);
			auto PackedCount = Data[Start + 88], PrimitiveCount = Data[Start + 89];
			if (!PrimitiveCount)
				continue;
			// Empty local-vertex domains can contain NaN quantization fields.
			// A section using only shared vertices does not read those fields.
			auto Offset = PackedCount ? Vector(Start + 48) : Vector3{};
			auto Scale = PackedCount ? Vector(Start + 60) : Vector3{};
			if (uint64_t(FirstPacked) + PackedCount > PackedVertices.Count ||
				uint64_t(FirstPrimitive) + PrimitiveCount > Primitives.Count)
				throw std::runtime_error("Havok section exceeds its arrays");
			size_t MaxIndex{};
			for (size_t Primitive = FirstPrimitive; Primitive < size_t(FirstPrimitive) + PrimitiveCount;
				 ++Primitive)
				for (size_t Corner = 0; Corner < 4; ++Corner)
					MaxIndex = std::max(MaxIndex, size_t(Data[PrimitiveAddress + Primitive * 4 + Corner]));
			std::vector<Vector3> Local;
			for (size_t Vertex = FirstPacked; Vertex < size_t(FirstPacked) + PackedCount; ++Vertex)
			{
				auto Bits = U32(Data, PackedAddress + Vertex * 4);
				Local.push_back({Offset.X + float(Bits & 2047) * Scale.X,
								 Offset.Y + float((Bits >> 11) & 2047) * Scale.Y,
								 Offset.Z + float((Bits >> 22) & 1023) * Scale.Z});
			}
			while (Local.size() <= MaxIndex)
			{
				auto IndexPosition = size_t(FirstShared) + Local.size() - PackedCount;
				// The fourth primitive byte may be a sentinel outside the
				// available vertices. Only real shared indices extend Local.
				if (IndexPosition >= SharedIndices.Count)
					break;
				auto GlobalIndex = U16(Data, SharedIndexAddress + IndexPosition * 2);
				if (GlobalIndex >= Shared.size())
					break;
				Local.push_back(Shared[GlobalIndex]);
			}
			for (size_t Primitive = FirstPrimitive; Primitive < size_t(FirstPrimitive) + PrimitiveCount;
				 ++Primitive)
			{
				auto Entry = PrimitiveAddress + Primitive * 4;
				auto A = Data[Entry], B = Data[Entry + 1], C = Data[Entry + 2], D = Data[Entry + 3];
				if (A == B || B == C || A == C)
					continue; // Degenerate strip connector.
				if (A >= Local.size() || B >= Local.size() || C >= Local.size())
					throw std::runtime_error(
						"Collision triangle references a missing vertex: mesh " + std::to_string(Index) +
						", section " + std::to_string(Section) + ", local " + std::to_string(Local.size()) +
						", indices " + std::to_string(A) + "," + std::to_string(B) + "," + std::to_string(C) +
						", shared index count " + std::to_string(SharedIndices.Count));
				AppendTriangle(Local.at(A), Local.at(B), Local.at(C));
				if (D < Local.size() && D != A && D != B && D != C)
					AppendTriangle(Local.at(A), Local.at(C), Local.at(D));
			}
		}
	}

  public:
	explicit TagReader(Bytes const& Input) : Data(Input)
	{
		Bounds(0, 8);
		if (std::string(Data.begin() + 4, Data.begin() + 8) != "TAG0")
			throw std::runtime_error("Unsupported collision format (expected Havok TAG0)");
		auto Size = Big32(0) & 0x3fffffff;
		Bounds(0, Size);
		ReadChunks(8, Size);
		Payload = Chunks.at("DATA").Begin;
		auto StringsChunk = Chunks.at(Chunks.contains("TST1") ? "TST1" : "TSTR");
		std::vector<std::string> Strings;
		for (size_t Position = StringsChunk.Begin; Position < StringsChunk.End;)
		{
			auto End = Position;
			while (End < StringsChunk.End && Data[End])
				++End;
			Strings.emplace_back(Data.begin() + Position, Data.begin() + End);
			Position = End + 1;
		}
		auto Names = Chunks.at(Chunks.contains("TNA1") ? "TNA1" : "TNAM");
		auto Position = Names.Begin;
		auto Count = Packed(Position, Names.End);
		if (Count > 100000)
			throw std::runtime_error("Havok type count exceeds limit");
		Types.resize(Count);
		for (size_t Type = 1; Type < Count; ++Type)
		{
			auto StringIndex = Packed(Position, Names.End), Parameters = Packed(Position, Names.End);
			Types[Type] = Strings.at(StringIndex);
			if (Parameters > 10000)
				throw std::runtime_error("Invalid Havok type parameters");
			for (size_t Parameter = 0; Parameter < Parameters; ++Parameter)
			{
				Packed(Position, Names.End);
				Packed(Position, Names.End);
			}
		}
		auto Table = Chunks.at("ITEM");
		for (Position = Table.Begin; Position + 12 <= Table.End; Position += 12)
		{
			auto TypeIndex = U32(Data, Position) & 0xffffff;
			Items.push_back({TypeIndex < Types.size() ? Types[TypeIndex] : "", U32(Data, Position + 4),
							 U32(Data, Position + 8)});
		}
	}
	Mesh Read(std::string const& Component) const
	{
		std::map<uint32_t, std::string> NamesByShape;
		for (auto const& Item : Items)
		{
			if (Item.Type != "hknpPhysicsSystemData::ExtendedBodyCinfo" || !Item.Count)
				continue;
			uint64_t Available = UINT64_MAX;
			for (auto const& Next : Items)
				if (Next.Offset > Item.Offset)
					Available = std::min(Available, uint64_t(Next.Offset - Item.Offset));
			if (Available == UINT64_MAX)
				Available = uint64_t(Item.Count) * 160;
			size_t Stride = size_t(Available / Item.Count);
			if (Stride < 24)
				Stride = 160;
			auto Begin = Array(Item, Stride);
			for (size_t Body = 0; Body < Item.Count; ++Body)
			{
				auto Shape = U32(Data, Begin + Body * Stride);
				auto Name = U32(Data, Begin + Body * Stride + 20);
				if (Shape >= Items.size() || Name >= Items.size())
					continue;
				auto Text = ItemText(Name);
				if (Lower(Text).starts_with("s_"))
					Text.erase(0, 2);
				NamesByShape[Shape] = Text;
			}
		}
		Mesh Result;
		for (uint32_t Shape = 1; Shape < Items.size(); ++Shape)
		{
			if (Items[Shape].Type != "hknpCompressedMeshShape")
				continue;
			auto FoundName = NamesByShape.find(Shape);
			// The file also holds detached construction shapes and effects.
			// Only shapes referenced by named armor bodies form vehicle armor.
			if (FoundName == NamesByShape.end())
				continue;
			auto Address = Array(Items[Shape], 32);
			auto MeshIndex = U32(Data, Address + 28);
			auto Material = uint32_t(Result.Materials.size());
			auto Name = FoundName->second;
			Result.Materials.push_back({Component, Name, "", 0});
			DecodeMesh(MeshIndex, Material, Result);
		}
		if (Result.Triangles.empty())
			throw std::runtime_error("No supported compressed collision meshes found");
		Result.ComputeBounds();
		return Result;
	}
};
} // namespace
Mesh ReadHavokMesh(Bytes const& Data, std::string Component)
{
	return TagReader(Data).Read(Component);
}
} // namespace OffHanger
