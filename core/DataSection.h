#pragma once
#include "Common.h"
namespace OffHanger
{
struct Section
{
	std::string Name, Value;
	std::vector<std::pair<std::string, std::string>> Attributes;
	std::vector<Section> Children;
	int ValueType = 1;
	Bytes Original;
	bool Edited = false;
	Section* Child(std::string const& Name);
	Section const* Child(std::string const& Name) const;
	std::string Text(std::string const& Name, std::string Fallback = {}) const;
	void Set(std::string Text);
};
struct Property
{
	std::string Path, Value;
	Section* Node{};
};
struct SectionStructure
{
	std::string Name;
	std::vector<SectionStructure> Children;
	int ValueType = 1;
	bool RealNumber = false;
};
class DataSection
{
  public:
	Section Root;
	bool Packed = false;
	static DataSection Parse(Bytes const& Bytes);
	// Preserve names and child order without decoding scalar values for structural indexes.
	static SectionStructure ParseStructure(Bytes const& Bytes);
	Bytes Serialize() const;
	std::string Xml() const;
	std::vector<Property> Properties(std::string const& Search = {});
};
} // namespace OffHanger
