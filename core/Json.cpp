#include "Json.h"
#include <set>
namespace OffHangar
{
ValuePtr FromJson(Json const& J)
{
    if (J.is_null())
        return Value::Make(ValueKind::None);
    if (J.is_boolean())
    {
        auto P = Value::Make(ValueKind::Boolean);
        P->Integer = J.get<bool>();
        return P;
    }
    if (J.is_number_unsigned() && J.get<uint64_t>() > uint64_t(INT64_MAX))
    {
        auto P = Value::Make(ValueKind::Long);
        Put64(P->LongBytes, J.get<uint64_t>());
        P->LongBytes.push_back(0);
        return P;
    }
    if (J.is_number_integer())
        return Value::Number(J.get<int64_t>());
    if (J.is_number_float())
    {
        auto P = Value::Make(ValueKind::Real);
        P->Real = J.get<double>();
        return P;
    }
    if (J.is_string())
    {
        auto P = Value::Make(ValueKind::Unicode);
        P->Text = J.get<std::string>();
        return P;
    }
    if (J.is_array())
    {
        auto P = Value::MakeList();
        for (auto const& V : J)
            P->Items.push_back(FromJson(V));
        return P;
    }
    auto P = Value::Make(ValueKind::Dictionary);
    for (auto It = J.begin(); It != J.end(); ++It)
    {
        auto K = Value::Make(ValueKind::Unicode);
        K->Text = It.key();
        P->Pairs.emplace_back(K, FromJson(It.value()));
    }
    return P;
}
static Json JsonValue(ValuePtr const& P, std::set<Value const*>& Seen, unsigned Depth)
{
    if (!P || Depth > 128 || Seen.contains(P.get()))
        throw std::runtime_error("JSON cannot represent this cyclic save graph");
    switch (P->Kind)
    {
    case ValueKind::None:
        return nullptr;
    case ValueKind::Boolean:
        return bool(P->Integer);
    case ValueKind::Integer:
        return P->Integer;
    case ValueKind::Real:
        return P->Real;
    case ValueKind::Long:
        if (P->LongBytes.size() <= 8)
            return P->AsInt();
        if (P->LongBytes.size() == 9 && !P->LongBytes.back())
            return U64(P->LongBytes, 0);
        throw std::runtime_error("Integer exceeds JSON 64-bit range");
    case ValueKind::Unicode:
    case ValueKind::Bytes:
        return P->Text;
    case ValueKind::List:
    case ValueKind::Tuple:
    {
        Seen.insert(P.get());
        auto A = Json::array();
        for (auto const& C : P->Items)
            A.push_back(JsonValue(C, Seen, Depth + 1));
        Seen.erase(P.get());
        return A;
    }
    case ValueKind::Dictionary:
    {
        Seen.insert(P.get());
        auto O = Json::object();
        for (auto const& [EntryKey, EntryValue] : P->Pairs)
            O[EntryKey->Display()] = JsonValue(EntryValue, Seen, Depth + 1);
        Seen.erase(P.get());
        return O;
    }
    default:
        throw std::runtime_error("Cannot convert a Python object into a JSON save");
    }
}
Json ToJson(ValuePtr const& P)
{
    std::set<Value const*> Seen;
    return JsonValue(P, Seen, 0);
}
ValuePtr ReadJson(Bytes const& Bytes)
{
    return FromJson(Json::parse(Bytes));
}
Bytes WriteJson(ValuePtr const& P)
{
    auto S = ToJson(P).dump(2);
    S += '\n';
    return Bytes(S.begin(), S.end());
}
} // namespace OffHangar
