#pragma once
#include "Common.h"
namespace OffHangar
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
class DataSection
{
  public:
    Section Root;
    bool Packed = false;
    static DataSection Parse(Bytes const& Bytes);
    Bytes Serialize() const;
    std::string Xml() const;
    std::vector<Property> Properties(std::string const& Search = {});
};
} // namespace OffHangar
