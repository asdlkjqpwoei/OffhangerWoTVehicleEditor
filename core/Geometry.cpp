#include "Geometry.h"
#include <bit>
#include <cstring>
#include <sstream>
#include <tuple>

namespace OffHanger
{
Matrix4 Matrix4::Translation(Vector3 Offset)
{
	Matrix4 Result;
	Result.Values[12] = Offset.X;
	Result.Values[13] = Offset.Y;
	Result.Values[14] = Offset.Z;
	return Result;
}
Matrix4 Matrix4::operator*(Matrix4 const& Right) const
{
	Matrix4 Result;
	Result.Values.fill(0);
	for (size_t Row = 0; Row < 4; ++Row)
		for (size_t Column = 0; Column < 4; ++Column)
			for (size_t Inner = 0; Inner < 4; ++Inner)
				Result.Values[Row * 4 + Column] += Values[Row * 4 + Inner] * Right.Values[Inner * 4 + Column];
	return Result;
}
Vector3 Matrix4::TransformPoint(Vector3 Point) const
{
	return {Point.X * Values[0] + Point.Y * Values[4] + Point.Z * Values[8] + Values[12],
			Point.X * Values[1] + Point.Y * Values[5] + Point.Z * Values[9] + Values[13],
			Point.X * Values[2] + Point.Y * Values[6] + Point.Z * Values[10] + Values[14]};
}
Vector3 ParseVector(std::string const& Value)
{
	std::istringstream Input(Value);
	Vector3 Result;
	if (!(Input >> Result.X >> Result.Y >> Result.Z))
		throw std::runtime_error("Expected a 3D vector");
	return Result;
}
float Float32(Bytes const& Data, size_t Offset)
{
	auto Result = std::bit_cast<float>(U32(Data, Offset));
	if (!std::isfinite(Result))
		throw std::runtime_error("Non-finite geometry coordinate at " + std::to_string(Offset));
	return Result;
}
void Mesh::ComputeBounds()
{
	if (Triangles.empty())
	{
		Minimum = Maximum = {};
		return;
	}
	Minimum = Maximum = Triangles[0].Positions[0];
	for (auto const& Triangle : Triangles)
		for (auto Point : Triangle.Positions)
		{
			Minimum = {std::min(Minimum.X, Point.X), std::min(Minimum.Y, Point.Y),
					   std::min(Minimum.Z, Point.Z)};
			Maximum = {std::max(Maximum.X, Point.X), std::max(Maximum.Y, Point.Y),
					   std::max(Maximum.Z, Point.Z)};
		}
}
void Mesh::Transform(Matrix4 const& Matrix)
{
	for (auto& Triangle : Triangles)
		for (auto& Point : Triangle.Positions)
			Point = Matrix.TransformPoint(Point);
	ComputeBounds();
}
void Mesh::Merge(Mesh Other)
{
	auto Offset = uint32_t(Materials.size());
	Materials.insert(Materials.end(), Other.Materials.begin(), Other.Materials.end());
	for (auto& Triangle : Other.Triangles)
	{
		Triangle.Material += Offset;
		Triangles.push_back(std::move(Triangle));
	}
	Diagnostics.insert(Diagnostics.end(), Other.Diagnostics.begin(), Other.Diagnostics.end());
	ComputeBounds();
}
std::vector<ArmorHit> IntersectArmor(Mesh const& Geometry, Vector3 Origin, Vector3 Direction,
									 float Normalization)
{
	Direction = Normalized(Direction);
	std::vector<ArmorHit> Result;
	for (size_t Index = 0; Index < Geometry.Triangles.size(); ++Index)
	{
		auto const& Triangle = Geometry.Triangles[Index];
		auto Edge1 = Triangle.Positions[1] - Triangle.Positions[0];
		auto Edge2 = Triangle.Positions[2] - Triangle.Positions[0];
		auto Perpendicular = Cross(Direction, Edge2);
		auto Determinant = Dot(Edge1, Perpendicular);
		if (std::abs(Determinant) < 1e-9f)
			continue;
		auto Relative = Origin - Triangle.Positions[0];
		auto U = Dot(Relative, Perpendicular) / Determinant;
		if (U < 0 || U > 1)
			continue;
		auto Q = Cross(Relative, Edge1);
		auto V = Dot(Direction, Q) / Determinant;
		if (V < 0 || U + V > 1)
			continue;
		auto Distance = Dot(Edge2, Q) / Determinant;
		if (Distance <= 1e-5f)
			continue;
		auto Cosine = std::clamp(std::abs(Dot(Normalized(Cross(Edge1, Edge2)), Direction)), 0.f, 1.f);
		auto Angle = std::acos(Cosine) * 180.f / 3.14159265358979323846f;
		auto AdjustedCosine =
			std::cos(std::max(0.f, Angle - Normalization) * 3.14159265358979323846f / 180.f);
		auto Nominal = Geometry.Materials.at(Triangle.Material).Thickness;
		Result.push_back(
			{Index, Triangle.Material, Distance, Angle,
			 AdjustedCosine > 1e-6f ? Nominal / AdjustedCosine : std::numeric_limits<float>::infinity(),
			 Origin + Direction * Distance});
	}
	std::sort(Result.begin(), Result.end(),
			  [](auto const& Left, auto const& Right) { return Left.Distance < Right.Distance; });
	// A ray through the common edge of two triangles hits one physical surface.
	Result.erase(
		std::unique(
			Result.begin(), Result.end(), [](auto const& Left, auto const& Right)
			{ return Left.Material == Right.Material && std::abs(Left.Distance - Right.Distance) < 1e-4f; }),
		Result.end());
	return Result;
}
static std::string FixedString(Bytes const& Data, size_t Offset, size_t Length)
{
	if (Offset > Data.size() || Length > Data.size() - Offset)
		throw std::runtime_error("Truncated mesh string");
	auto End = Offset;
	while (End < Offset + Length && Data[End])
		++End;
	return std::string(Data.begin() + Offset, Data.begin() + End);
}
static std::map<std::string, Bytes> PrimitiveSections(Bytes const& Data)
{
	if (Data.size() < 8)
		throw std::runtime_error("Truncated primitive resource");
	auto TableSize = U32(Data, Data.size() - 4);
	if (TableSize > Data.size() - 8)
		throw std::runtime_error("Invalid primitive section table");
	size_t TableStart = Data.size() - 4 - TableSize;
	size_t Position = TableStart, Payload = 4;
	std::map<std::string, Bytes> Result;
	while (Position + 24 <= Data.size() - 4)
	{
		auto Size = U32(Data, Position), NameSize = U32(Data, Position + 20);
		Position += 24;
		if (!NameSize || NameSize > Data.size() - 4 - Position || Size > TableStart - Payload)
			throw std::runtime_error("Primitive section exceeds its container");
		auto Name = FixedString(Data, Position, NameSize);
		Result.emplace(Name, Bytes(Data.begin() + Payload, Data.begin() + Payload + Size));
		Position += (size_t(NameSize) + 3) & ~size_t(3);
		Payload += (size_t(Size) + 3) & ~size_t(3);
	}
	return Result;
}
static Matrix4 NodeTransform(Section const& Node)
{
	Matrix4 Result;
	auto Transform = Node.Child("transform");
	if (!Transform)
		return Result;
	std::istringstream Input(Transform->Value);
	bool Parsed = true;
	for (size_t Row = 0; Row < 4; ++Row)
		for (size_t Column = 0; Column < 3; ++Column)
			if (!(Input >> Result.Values[Row * 4 + Column]))
				Parsed = false;
	if (!Parsed && !Transform->Children.empty())
	{
		Result = {};
		for (size_t Row = 0; Row < 4; ++Row)
		{
			auto Value = ParseVector(Transform->Text("row" + std::to_string(Row), Row == 0	 ? "1 0 0"
																				  : Row == 1 ? "0 1 0"
																				  : Row == 2 ? "0 0 1"
																							 : "0 0 0"));
			Result.Values[Row * 4] = Value.X;
			Result.Values[Row * 4 + 1] = Value.Y;
			Result.Values[Row * 4 + 2] = Value.Z;
		}
	}
	return Result;
}
static void CollectNodes(Section const& Node, Matrix4 Parent, std::map<std::string, Matrix4>& Result)
{
	auto World = NodeTransform(Node) * Parent;
	Result[Node.Text("identifier")] = World;
	for (auto const& Child : Node.Children)
		if (Child.Name == "node")
			CollectNodes(Child, World, Result);
}
static Resource const& Variant(ResourceIndex const& Resources, std::string Base,
							   std::initializer_list<char const*> Suffixes)
{
	for (auto Suffix : {".model", ".visual_processed", ".visual", ".primitives_processed", ".primitives"})
		if (Lower(Base).ends_with(Suffix))
		{
			Base.resize(Base.size() - std::strlen(Suffix));
			break;
		}
	for (auto Suffix : Suffixes)
		if (auto Found = Resources.Find(Base + Suffix))
			return *Found;
	throw std::runtime_error("Model resource not found: " + Base);
}
Mesh ReadVisualMesh(ResourceIndex const& Resources, std::string const& ModelPath, std::string Component)
{
	auto Source = Resources.Find(ModelPath);
	if (!Source)
		throw std::runtime_error("Model not found: " + ModelPath);
	auto Model = DataSection::Parse(Source->Read());
	std::string VisualBase;
	for (auto Name : {"nodefullVisual", "nodelessVisual", "visual", "billboardVisual"})
		if (auto Node = Model.Root.Child(Name))
		{
			VisualBase = Node->Value;
			break;
		}
	if (VisualBase.empty())
		VisualBase = ModelPath;
	auto Visual = DataSection::Parse(Variant(Resources, VisualBase, {".visual_processed", ".visual"}).Read());
	auto Primitives =
		PrimitiveSections(Variant(Resources, VisualBase, {".primitives_processed", ".primitives"}).Read());
	Mesh Result;
	for (auto const& Node : Visual.Root.Children)
		if (Node.Name == "node")
			CollectNodes(Node, {}, Result.Nodes);
	struct Vertex
	{
		Vector3 Position;
		Vector2 Uv;
	};
	static std::map<std::string, size_t> const Strides = {
		{"xyznuv", 24},		   {"xyznuvtb", 32},		{"xyznuviiiwwtb", 37},
		{"set3/xyznuvpc", 24}, {"set3/xyznuvtbpc", 32}, {"set3/xyznuviiiwwtbpc", 40}};
	for (auto const& RenderSet : Visual.Root.Children)
	{
		if (RenderSet.Name != "renderSet")
			continue;
		std::vector<std::string> Palette;
		for (auto const& Child : RenderSet.Children)
			if (Child.Name == "node")
				Palette.push_back(Child.Value);
		for (auto const& Geometry : RenderSet.Children)
		{
			if (Geometry.Name != "geometry")
				continue;
			auto const& VertexData = Primitives.at(Geometry.Text("vertices"));
			auto Format = FixedString(VertexData, 0, 64);
			size_t VertexStart = 68;
			uint32_t VertexCount = U32(VertexData, 64);
			if (Format.find("BPVT") != Format.npos)
			{
				Format = FixedString(VertexData, 68, 64);
				VertexCount = U32(VertexData, 132);
				VertexStart = 136;
			}
			auto FoundStride = Strides.find(Format);
			if (FoundStride == Strides.end())
				throw std::runtime_error("Unsupported vertex format: " + Format);
			auto Stride = FoundStride->second;
			if (VertexCount > 2000000 || VertexCount > (VertexData.size() - VertexStart) / Stride)
				throw std::runtime_error("Invalid vertex count");
			std::vector<Vertex> Vertices;
			Vertices.reserve(VertexCount);
			for (size_t Index = 0; Index < VertexCount; ++Index)
			{
				auto Offset = VertexStart + Index * Stride;
				Vector3 Position{Float32(VertexData, Offset), Float32(VertexData, Offset + 4),
								 Float32(VertexData, Offset + 8)};
				Vector2 Uv{Float32(VertexData, Offset + 16), Float32(VertexData, Offset + 20)};
				if (Stride == 37 || Stride == 40)
				{
					bool Packed = Stride == 40;
					std::array<size_t, 3> Bones{VertexData[Offset + 24 + (Packed ? 2 : 0)] / 3u,
												VertexData[Offset + 25] / 3u,
												VertexData[Offset + 24 + (Packed ? 0 : 2)] / 3u};
					auto WeightOffset = Offset + (Packed ? 30 : 27);
					std::array<float, 3> Weights{VertexData[WeightOffset] / 255.f,
												 VertexData[WeightOffset + 1] / 255.f, 0};
					Weights[2] = std::max(0.f, 1 - Weights[0] - Weights[1]);
					Vector3 Blended;
					float Total{};
					for (size_t Bone = 0; Bone < 3; ++Bone)
						if (Bones[Bone] < Palette.size() && Weights[Bone] > 0)
							if (auto Transform = Result.Nodes.find(Palette[Bones[Bone]]);
								Transform != Result.Nodes.end())
							{
								Blended =
									Blended + Transform->second.TransformPoint(Position) * Weights[Bone];
								Total += Weights[Bone];
							}
					if (Total > 1e-6f)
						Position = Blended * (1 / Total);
				}
				Vertices.push_back({Position, Uv});
			}
			auto const& IndexData = Primitives.at(Geometry.Text("primitive"));
			auto IndexFormat = Lower(FixedString(IndexData, 0, 64));
			size_t IndexWidth = IndexFormat == "list32" ? 4 : 2;
			auto IndexCount = U32(IndexData, 64), GroupCount = U32(IndexData, 68);
			if (IndexCount > 12000000 ||
				72ull + uint64_t(IndexCount) * IndexWidth + uint64_t(GroupCount) * 16 > IndexData.size())
				throw std::runtime_error("Invalid mesh index count");
			auto GroupStart = 72 + size_t(IndexCount) * IndexWidth;
			for (auto const& Group : Geometry.Children)
			{
				if (Group.Name != "primitiveGroup")
					continue;
				auto GroupIndex = size_t(std::stoul(Group.Value));
				if (GroupIndex >= GroupCount)
					throw std::runtime_error("Invalid primitive group index");
				auto Start = U32(IndexData, GroupStart + GroupIndex * 16);
				auto Count = U32(IndexData, GroupStart + GroupIndex * 16 + 4);
				auto BaseVertex = U32(IndexData, GroupStart + GroupIndex * 16 + 8);
				if (uint64_t(Start) + uint64_t(Count) * 3 > IndexCount)
					throw std::runtime_error("Primitive group exceeds indices");
				MeshMaterial Material;
				Material.Component = Component;
				if (auto Node = Group.Child("material"))
				{
					Material.Zone = Node->Text("identifier");
					if (!Material.Zone.starts_with("armor_"))
					{
						auto Kind = Node->Text("materialKind");
						if (!Kind.empty() && Kind != "0")
							Material.Zone = "armor_" + Kind;
					}
					for (auto const& Property : Node->Children)
						if (Property.Name == "property" &&
							(Lower(Property.Value) == "diffusemap" || Lower(Property.Value) == "albedomap"))
							Material.Texture = Property.Text("Texture");
				}
				auto MaterialIndex = uint32_t(Result.Materials.size());
				Result.Materials.push_back(std::move(Material));
				for (size_t TriangleIndex = 0; TriangleIndex < Count; ++TriangleIndex)
				{
					MeshTriangle Triangle;
					Triangle.Material = MaterialIndex;
					Triangle.HasTextureCoordinates = true;
					for (size_t Corner = 0; Corner < 3; ++Corner)
					{
						auto Address = 72 + (Start + TriangleIndex * 3 + Corner) * IndexWidth;
						auto VertexIndex =
							IndexWidth == 4 ? U32(IndexData, Address) : U16(IndexData, Address);
						if (VertexIndex >= Vertices.size() &&
							uint64_t(VertexIndex) + BaseVertex < Vertices.size())
							VertexIndex += BaseVertex;
						if (VertexIndex >= Vertices.size())
							throw std::runtime_error("Triangle index exceeds vertex array");
						Triangle.Positions[Corner] = Vertices[VertexIndex].Position;
						Triangle.TexCoords[Corner] = Vertices[VertexIndex].Uv;
					}
					Result.Triangles.push_back(std::move(Triangle));
				}
			}
		}
	}
	if (Result.Triangles.empty())
		throw std::runtime_error("Visual model has no supported render geometry");
	Result.ComputeBounds();
	return Result;
}
static Section const& SelectModule(Section const* Group, std::string const& Name)
{
	if (!Group || Group->Children.empty())
		throw std::runtime_error("Missing model module group");
	if (!Name.empty())
	{
		if (auto Result = Group->Child(Name))
			return *Result;
		throw std::runtime_error("Selected module is absent: " + Name);
	}
	return Group->Children.front();
}
static std::string ModelPath(Section const& Component, bool Collision)
{
	if (Collision)
		if (auto HitTester = Component.Child("hitTester"))
		{
			auto Path = HitTester->Text("collisionModelClient");
			if (Path.empty())
				Path = HitTester->Text("collisionModelServer");
			if (Lower(Path).ends_with(".model"))
				Path.resize(Path.size() - 6);
			return Path + ".havok";
		}
	if (auto Models = Component.Child("models"))
		return Models->Text("undamaged");
	return {};
}
void ApplyArmorThickness(Mesh& Geometry, DataSection const& Definition, AssemblySelection const& Selection)
{
	auto const& Chassis = SelectModule(Definition.Root.Child("chassis"), Selection.Chassis);
	auto const& Turret = SelectModule(Definition.Root.Child("turrets" + std::to_string(Selection.TurretSlot)),
									  Selection.Turret);
	auto const& Gun = SelectModule(Turret.Child("guns"), Selection.Gun);
	for (auto& Material : Geometry.Materials)
	{
		Material.Thickness = 0;
		Section const* Part = Material.Component == "Hull"		? Definition.Root.Child("hull")
							  : Material.Component == "Chassis" ? &Chassis
							  : Material.Component == "Turret"	? &Turret
							  : Material.Component == "Gun"		? &Gun
																: nullptr;
		auto Armor = Part ? Part->Child("armor") : nullptr;
		if (!Armor)
			continue;
		for (auto const& Zone : Armor->Children)
			if (Lower(Zone.Name) == Lower(Material.Zone))
			{
				Material.Zone = Zone.Name;
				Material.Thickness = std::stof(Zone.Value);
				break;
			}
	}
}
Mesh BuildVehicleMesh(Catalog const& Catalog, Vehicle const&, DataSection const& Definition,
					  AssemblySelection const& Selection)
{
	auto const& Root = Definition.Root;
	auto Hull = Root.Child("hull");
	if (!Hull)
		throw std::runtime_error("Vehicle has no hull");
	auto const& Chassis = SelectModule(Root.Child("chassis"), Selection.Chassis);
	auto const& Turret =
		SelectModule(Root.Child("turrets" + std::to_string(Selection.TurretSlot)), Selection.Turret);
	auto const& Gun = SelectModule(Turret.Child("guns"), Selection.Gun);
	auto ReadPart = [&](Section const& Part, std::string const& Component)
	{
		auto Path = ModelPath(Part, Selection.Collision);
		Mesh Geometry;
		if (Selection.Collision)
		{
			auto Source = Catalog.Resources.Find(Path);
			if (Source)
			{
				try
				{
					Geometry = ReadHavokMesh(Source->Read(), Component);
				}
				catch (std::exception const& Error)
				{
					throw std::runtime_error(Component + ": " + Error.what());
				}
			}
			else
			{
				auto LegacyPath =
					Path.ends_with(".havok") ? Path.substr(0, Path.size() - 6) + ".model" : Path;
				if (!Catalog.Resources.Find(LegacyPath))
					throw std::runtime_error("Collision resource not found: " + Path);
				Geometry = ReadVisualMesh(Catalog.Resources, LegacyPath, Component);
			}
		}
		else
			Geometry = ReadVisualMesh(Catalog.Resources, Path, Component);
		if (auto Armor = Part.Child("armor"))
			for (auto& Material : Geometry.Materials)
				for (auto const& Zone : Armor->Children)
					if (Lower(Zone.Name) == Lower(Material.Zone))
					{
						Material.Zone = Zone.Name;
						Material.Thickness = std::stof(Zone.Value);
						break;
					}
		return Geometry;
	};
	auto HullMesh = ReadPart(*Hull, "Hull");
	auto ChassisMesh = ReadPart(Chassis, "Chassis");
	auto TurretMesh = ReadPart(Turret, "Turret");
	auto GunMesh = ReadPart(Gun, "Gun");
	Matrix4 TurretTransform;
	if (auto Joint = HullMesh.Nodes.find("HP_turretJoint"); Joint != HullMesh.Nodes.end())
		TurretTransform = Joint->second;
	else if (auto Positions = Hull->Child("turretPositions");
			 Positions && size_t(Selection.TurretSlot) < Positions->Children.size())
		TurretTransform = Matrix4::Translation(ParseVector(Positions->Children[Selection.TurretSlot].Value));
	Matrix4 GunTransform;
	if (auto Joint = TurretMesh.Nodes.find("HP_gunJoint"); Joint != TurretMesh.Nodes.end())
		GunTransform = Joint->second;
	else if (auto Position = Turret.Child("gunPosition"))
		GunTransform = Matrix4::Translation(ParseVector(Position->Value));
	if (auto Anchor = ChassisMesh.Nodes.find("V"); Anchor != ChassisMesh.Nodes.end())
		ChassisMesh.Transform(Matrix4::Translation(
			{-Anchor->second.Values[12], -Anchor->second.Values[13], -Anchor->second.Values[14]}));
	else if (auto Position = Chassis.Child("hullPosition"))
		ChassisMesh.Transform(Matrix4::Translation(ParseVector(Position->Value) * -1));
	GunMesh.Transform(GunTransform * TurretTransform);
	TurretMesh.Transform(TurretTransform);
	HullMesh.Merge(std::move(ChassisMesh));
	HullMesh.Merge(std::move(TurretMesh));
	HullMesh.Merge(std::move(GunMesh));
	return HullMesh;
}
Mesh ReadSdkMesh(Bytes const& Data)
{
	if (FixedString(Data, 0, 8) != "WVSMESH2" || U16(Data, 8) != 2)
		throw std::runtime_error("Unsupported SDK mesh cache");
	auto Flags = U16(Data, 10);
	auto ZoneCount = U32(Data, 12), TextureCount = U32(Data, 16), TriangleCount = U32(Data, 20);
	if (ZoneCount > 100000 || TextureCount > 100000 || TriangleCount > 3000000)
		throw std::runtime_error("SDK mesh exceeds limits");
	size_t Position = 24 + ((Flags & 1) ? 24 : 0);
	if (Flags & ~1u || Position > Data.size())
		throw std::runtime_error("Invalid SDK mesh flags or bounds");
	std::vector<std::string> Zones, Textures;
	for (auto Table : {&Zones, &Textures})
		for (uint32_t Index = 0, Count = Table == &Zones ? ZoneCount : TextureCount; Index < Count; ++Index)
		{
			auto Length = U16(Data, Position);
			Position += 2;
			Table->push_back(FixedString(Data, Position, Length));
			Position += Length;
		}
	if (uint64_t(TriangleCount) * 69 > Data.size() - Position)
		throw std::runtime_error("Truncated SDK triangles");
	Mesh Result;
	std::map<std::pair<int, int>, uint32_t> Materials;
	for (uint32_t Index = 0; Index < TriangleCount; ++Index, Position += 69)
	{
		int Zone = int32_t(U32(Data, Position + 60)), Texture = int32_t(U32(Data, Position + 64));
		auto Key = std::pair(Zone, Texture);
		if (!Materials.contains(Key))
		{
			Materials[Key] = uint32_t(Result.Materials.size());
			auto ZoneName = Zone >= 0 && size_t(Zone) < Zones.size() ? Zones[Zone] : "";
			std::string Component = "SDK";
			auto Separator = ZoneName.find('/');
			if (Separator != ZoneName.npos)
			{
				auto Prefix = ZoneName.substr(0, Separator);
				if (Prefix == "Hull" || Prefix == "Chassis" || Prefix == "Turret" || Prefix == "Gun")
				{
					Component = std::move(Prefix);
					ZoneName.erase(0, Separator + 1);
				}
			}
			Result.Materials.push_back(
				{std::move(Component), std::move(ZoneName),
				 Texture >= 0 && size_t(Texture) < Textures.size() ? Textures[Texture] : "", 0});
		}
		MeshTriangle Triangle;
		Triangle.Material = Materials.at(Key);
		Triangle.HasTextureCoordinates = Data.at(Position + 68) != 0;
		for (size_t Corner = 0; Corner < 3; ++Corner)
		{
			auto Offset = Position + Corner * 12;
			Triangle.Positions[Corner] = {Float32(Data, Offset), Float32(Data, Offset + 4),
										  Float32(Data, Offset + 8)};
			Triangle.TexCoords[Corner] = {Float32(Data, Position + 36 + Corner * 8),
										  Float32(Data, Position + 40 + Corner * 8)};
		}
		Result.Triangles.push_back(Triangle);
	}
	Result.ComputeBounds();
	return Result;
}
Bytes WriteSdkMesh(Mesh const& Geometry)
{
	Bytes Result;
	Append(Result, "WVSMESH2");
	Put16(Result, 2);
	Put16(Result, 0);
	Put32(Result, uint32_t(Geometry.Materials.size()));
	Put32(Result, uint32_t(Geometry.Materials.size()));
	Put32(Result, uint32_t(Geometry.Triangles.size()));
	for (bool Texture : {false, true})
		for (auto const& Material : Geometry.Materials)
		{
			auto const& Text = Texture ? Material.Texture : Material.Component + "/" + Material.Zone;
			if (Text.size() > 65535)
				throw std::runtime_error("SDK material name exceeds limit");
			Put16(Result, uint16_t(Text.size()));
			Append(Result, Text);
		}
	auto PutFloat = [&](float Value) { Put32(Result, std::bit_cast<uint32_t>(Value)); };
	for (auto const& Triangle : Geometry.Triangles)
	{
		for (auto Position : Triangle.Positions)
		{
			PutFloat(Position.X);
			PutFloat(Position.Y);
			PutFloat(Position.Z);
		}
		for (auto Uv : Triangle.TexCoords)
		{
			PutFloat(Uv.X);
			PutFloat(Uv.Y);
		}
		Put32(Result, Triangle.Material);
		Put32(Result, Triangle.Material);
		Result.push_back(Triangle.HasTextureCoordinates ? 1 : 0);
	}
	return Result;
}
} // namespace OffHanger
