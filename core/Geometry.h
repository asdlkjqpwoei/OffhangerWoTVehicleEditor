#pragma once
#include "Catalog.h"
#include <array>
#include <cmath>
#include <limits>

namespace OffHanger
{
struct Vector2
{
	float X{}, Y{};
};
struct Vector3
{
	float X{}, Y{}, Z{};
	Vector3 operator+(Vector3 Other) const
	{
		return {X + Other.X, Y + Other.Y, Z + Other.Z};
	}
	Vector3 operator-(Vector3 Other) const
	{
		return {X - Other.X, Y - Other.Y, Z - Other.Z};
	}
	Vector3 operator*(float Scale) const
	{
		return {X * Scale, Y * Scale, Z * Scale};
	}
};
inline float Dot(Vector3 Left, Vector3 Right)
{
	return Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z;
}
inline Vector3 Cross(Vector3 Left, Vector3 Right)
{
	return {Left.Y * Right.Z - Left.Z * Right.Y, Left.Z * Right.X - Left.X * Right.Z,
			Left.X * Right.Y - Left.Y * Right.X};
}
inline Vector3 Normalized(Vector3 Value)
{
	auto Length = std::sqrt(Dot(Value, Value));
	return Length > 1e-10f ? Value * (1 / Length) : Vector3{};
}
struct Matrix4
{
	std::array<float, 16> Values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	static Matrix4 Translation(Vector3 Offset);
	Matrix4 operator*(Matrix4 const& Right) const;
	Vector3 TransformPoint(Vector3 Point) const;
};
struct MeshMaterial
{
	std::string Component;
	std::string Zone;
	std::string Texture;
	float Thickness{};
};
struct MeshTriangle
{
	std::array<Vector3, 3> Positions;
	std::array<Vector2, 3> TexCoords;
	uint32_t Material{};
	bool HasTextureCoordinates{};
};
struct Mesh
{
	std::vector<MeshTriangle> Triangles;
	std::vector<MeshMaterial> Materials;
	std::map<std::string, Matrix4> Nodes;
	std::vector<std::string> Diagnostics;
	Vector3 Minimum{}, Maximum{};
	void ComputeBounds();
	void Transform(Matrix4 const& Matrix);
	void Merge(Mesh Other);
};
struct ArmorHit
{
	size_t Triangle{};
	uint32_t Material{};
	float Distance{}, AngleDegrees{}, EffectiveThickness{};
	Vector3 Position;
};
std::vector<ArmorHit> IntersectArmor(Mesh const& Mesh, Vector3 Origin, Vector3 Direction,
									 float NormalizationDegrees = 0);
Vector3 ParseVector(std::string const& Value);
float Float32(Bytes const& Data, size_t Offset);
Mesh ReadVisualMesh(ResourceIndex const& Resources, std::string const& ModelPath, std::string Component);
Mesh ReadHavokMesh(Bytes const& Bytes, std::string Component);
Mesh ReadSdkMesh(Bytes const& Bytes);
Bytes WriteSdkMesh(Mesh const& Mesh);
// Uses explicit XML module choices. Empty module names select the stock entries.
struct AssemblySelection
{
	std::string Chassis, Turret, Gun;
	int TurretSlot{};
	bool Collision{};
};
Mesh BuildVehicleMesh(Catalog const& Catalog, Vehicle const& Vehicle, DataSection const& Definition,
					  AssemblySelection const& Selection = {});
void ApplyArmorThickness(Mesh& Mesh, DataSection const& Definition, AssemblySelection const& Selection);
} // namespace OffHanger
