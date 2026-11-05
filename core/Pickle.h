#pragma once
#include "Common.h"
namespace OffHangar
{
enum class ValueKind
{
    None,
    Boolean,
    Integer,
    Long,
    Real,
    Bytes,
    Unicode,
    List,
    Tuple,
    Dictionary,
    Global,
    Reduce,
    NewObject
};
struct Value;
using ValuePtr = std::shared_ptr<Value>;
struct Value
{
    ValueKind Kind = ValueKind::None;
    int64_t Integer{};
    double Real{};
    std::string Text;
    Bytes LongBytes;
    std::vector<ValuePtr> Items;
    std::vector<std::pair<ValuePtr, ValuePtr>> Pairs;
    ValuePtr State;
    static ValuePtr Make(ValueKind Kind);
    static ValuePtr Number(int64_t Value);
    static ValuePtr FromBytes(Bytes const& Bytes);
    static ValuePtr FromBytes(std::string const& Text);
    static ValuePtr MakeList(size_t NoneCount = 0);
    ValuePtr Get(std::string const& Key) const;
    ValuePtr Get(int64_t Key) const;
    void Set(std::string const& Key, ValuePtr Value);
    void Set(int64_t Key, ValuePtr Value);
    int64_t AsInt(int64_t Fallback = 0) const;
    std::string Display() const;
    bool ScalarEditable() const;
    ValuePtr Edited(std::string const& Input) const;
};
ValuePtr ReadPickle(Bytes const& Data);
Bytes WritePickle(ValuePtr const& Root);
bool ValueEqual(ValuePtr const& A, ValuePtr const& B);
struct SaveProperty
{
    std::string Path;
    std::vector<size_t> Address;
    ValuePtr CurrentValue;
};
std::vector<SaveProperty> ValueProperties(ValuePtr const& Root, std::string const& Search = {});
} // namespace OffHangar
