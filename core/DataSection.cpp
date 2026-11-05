#include "DataSection.h"
#include "pugixml.hpp"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace OffHangar
{
Section* Section::Child(std::string const& N)
{
    for (auto& C : Children)
        if (C.Name == N)
            return &C;
    return nullptr;
}
Section const* Section::Child(std::string const& N) const
{
    for (auto const& C : Children)
        if (C.Name == N)
            return &C;
    return nullptr;
}
std::string Section::Text(std::string const& N, std::string Fallback) const
{
    auto C = Child(N);
    return C ? C->Value : Fallback;
}
void Section::Set(std::string Text)
{
    if (ValueType == 6)
        throw std::runtime_error("Encrypted binary values are read-only");
    if (ValueType == 2)
    {
        size_t N;
        (void)std::stoll(Text, &N);
        if (N != Text.size())
            throw std::runtime_error("Expected an integer");
    }
    if (ValueType == 3)
    {
        std::istringstream In(Text);
        double V;
        size_t Count = 0;
        while (In >> V)
        {
            if (!std::isfinite(V) || std::abs(V) > std::numeric_limits<float>::max())
                throw std::runtime_error("Invalid floating-point value");
            ++Count;
        }
        if (!In.eof() || !Count || (Original.size() && Count != Original.size() / 4))
            throw std::runtime_error("Keep the original number of float/vector components");
    }
    if (ValueType == 4 && Text != "true" && Text != "false" && Text != "0" && Text != "1")
        throw std::runtime_error("Boolean value must be true or false");
    if (ValueType == 5)
        (void)Unbase64(Text);
    Value = std::move(Text);
    Edited = true;
}
static Section FromXml(pugi::xml_node N, unsigned Depth = 0)
{
    if (Depth > 128)
        throw std::runtime_error("XML nesting exceeds limit");
    Section S;
    S.Name = N.name();
    std::string Value;
    for (auto C : N.children())
    {
        if (C.type() == pugi::node_element)
            S.Children.push_back(FromXml(C, Depth + 1));
        else if (C.type() == pugi::node_pcdata || C.type() == pugi::node_cdata)
            Value += C.value();
    }
    S.Value = Trim(Value);
    for (auto A : N.attributes())
        S.Attributes.emplace_back(A.name(), A.value());
    return S;
}
static void ToXml(Section const& S, pugi::xml_node P)
{
    auto N = P.append_child(S.Name.c_str());
    for (auto const& [AttributeName, AttributeValue] : S.Attributes)
        N.append_attribute(AttributeName.c_str()).set_value(AttributeValue.c_str());
    if (!S.Value.empty())
        N.append_child(pugi::node_pcdata).set_value(S.Value.c_str());
    for (auto const& C : S.Children)
        ToXml(C, N);
}
class PackedReader
{
    Bytes const& Data;
    std::vector<std::string> Names;
    size_t Nodes = 0;
    Section Scalar(std::string Name, unsigned Type, size_t Begin, size_t End, unsigned Depth)
    {
        if (End < Begin || End > Data.size())
            throw std::runtime_error("Packed XML offset out of bounds");
        if (Type == 0)
            return ReadSection(std::move(Name), Begin, End, Depth + 1);
        Section S;
        S.Name = std::move(Name);
        S.ValueType = int(Type);
        S.Original.assign(Data.begin() + Begin, Data.begin() + End);
        auto Count = End - Begin;
        switch (Type)
        {
        case 1:
            S.Value.assign(reinterpret_cast<char const*>(Data.data() + Begin), Count);
            break;
        case 2:
        {
            if (Count != 0 && Count != 1 && Count != 2 && Count != 4 && Count != 8)
                throw std::runtime_error("Invalid packed integer");
            uint64_t V = 0;
            for (size_t I = 0; I < Count; ++I)
                V |= uint64_t(Data[Begin + I]) << (I * 8);
            if (Count && Count < 8 && (Data[End - 1] & 128))
                V |= (~0ull) << (Count * 8);
            S.Value = std::to_string(int64_t(V));
            break;
        }
        case 3:
        {
            if (Count % 4)
                throw std::runtime_error("Invalid packed float");
            std::ostringstream Out;
            Out << std::setprecision(std::numeric_limits<float>::max_digits10);
            for (size_t I = 0; I < Count; I += 4)
            {
                float F;
                auto Bits = U32(Data, Begin + I);
                std::memcpy(&F, &Bits, 4);
                if (I)
                    Out << ' ';
                Out << F;
            }
            S.Value = Out.str();
            break;
        }
        case 4:
            S.Value = Count ? "true" : "false";
            break;
        case 5:
        case 6:
            S.Value = Base64(S.Original);
            break;
        default:
            throw std::runtime_error("Unsupported packed XML value type: " + std::to_string(Type));
        }
        return S;
    }
    Section ReadSection(std::string Name, size_t Pos, size_t End, unsigned Depth)
    {
        if (Depth > 128 || ++Nodes > 1000000)
            throw std::runtime_error("Packed XML nesting exceeds limit");
        auto Count = U16(Data, Pos);
        auto Own = U32(Data, Pos + 2);
        size_t Payload = Pos + 6ull + Count * 6ull;
        if (Payload > End)
            throw std::runtime_error("Truncated packed XML section");
        auto OwnEnd = Own & 0x0fffffffu;
        if (OwnEnd > End - Payload)
            throw std::runtime_error("Invalid packed XML own-data length");
        auto S = Scalar(std::move(Name), Own >> 28, Payload, Payload + OwnEnd, Depth);
        uint32_t Previous = OwnEnd;
        for (unsigned I = 0; I < Count; ++I)
        {
            auto Key = U16(Data, Pos + 6 + I * 6);
            auto Desc = U32(Data, Pos + 8 + I * 6), Offset = Desc & 0x0fffffffu;
            if (Key >= Names.size() || Offset < Previous || Offset > End - Payload)
                throw std::runtime_error("Invalid packed XML child");
            S.Children.push_back(Scalar(Names[Key], Desc >> 28, Payload + Previous, Payload + Offset, Depth));
            Previous = Offset;
        }
        if (Payload + Previous != End)
            throw std::runtime_error("Unexpected trailing packed XML data");
        return S;
    }

  public:
    explicit PackedReader(Bytes const& D) : Data(D) {}
    Section Parse()
    {
        if (Data.size() < 6 || Data[4] != 0)
            throw std::runtime_error("Unsupported packed XML version");
        size_t Pos = 5;
        for (;;)
        {
            auto Begin = Pos;
            while (Pos < Data.size() && Data[Pos])
                ++Pos;
            if (Pos == Data.size())
                throw std::runtime_error("Truncated packed XML string table");
            if (Pos == Begin)
            {
                ++Pos;
                break;
            }
            Names.emplace_back(reinterpret_cast<char const*>(Data.data() + Begin), Pos - Begin);
            ++Pos;
            if (Names.size() > 65535)
                throw std::runtime_error("Packed XML string table exceeds limit");
        }
        return ReadSection("root", Pos, Data.size(), 0);
    }
};
static Bytes ScalarBytes(Section const& S)
{
    if (!S.Edited && S.ValueType != 1)
        return S.Original;
    Bytes B;
    switch (S.ValueType)
    {
    case 1:
        Append(B, S.Value);
        break;
    case 2:
    {
        int64_t V = std::stoll(S.Value);
        size_t Size = V == 0                             ? 0
                      : V >= -128 && V <= 127            ? 1
                      : V >= -32768 && V <= 32767        ? 2
                      : V >= INT32_MIN && V <= INT32_MAX ? 4
                                                         : 8;
        for (size_t I = 0; I < Size; ++I)
            B.push_back(uint8_t(uint64_t(V) >> (8 * I)));
        break;
    }
    case 3:
    {
        std::istringstream In(S.Value);
        float F;
        while (In >> F)
        {
            uint32_t V;
            std::memcpy(&V, &F, 4);
            Put32(B, V);
        }
        break;
    }
    case 4:
        if (S.Value == "true" || S.Value == "1")
            B.push_back(1);
        break;
    case 5:
    case 6:
        B = Unbase64(S.Value);
        break;
    default:
        throw std::runtime_error("Unsupported packed value type");
    }
    return B;
}
static Bytes SectionBytes(Section const& S, std::map<std::string, uint16_t> const& Ids)
{
    if (S.Children.size() > 65535)
        throw std::runtime_error("Too many XML children");
    Bytes Header, Payload = ScalarBytes(S);
    Put16(Header, uint16_t(S.Children.size()));
    Put32(Header, uint32_t(S.ValueType << 28) | uint32_t(Payload.size()));
    for (auto const& C : S.Children)
    {
        bool Nested = !C.Children.empty();
        auto Bytes = Nested ? SectionBytes(C, Ids) : ScalarBytes(C);
        Append(Payload, Bytes);
        if (Payload.size() > 0x0fffffffu)
            throw std::runtime_error("Packed section exceeds limit");
        Put16(Header, Ids.at(C.Name));
        Put32(Header, uint32_t((Nested ? 0 : C.ValueType) << 28) | uint32_t(Payload.size()));
    }
    Append(Header, Payload);
    return Header;
}
DataSection DataSection::Parse(Bytes const& B)
{
    DataSection D;
    if (B.size() >= 4 && U32(B, 0) == 0x62a14e45)
    {
        D.Packed = true;
        D.Root = PackedReader(B).Parse();
    }
    else
    {
        pugi::xml_document Doc;
        auto Result = Doc.load_buffer(B.data(), B.size(), pugi::parse_default | pugi::parse_ws_pcdata_single);
        if (!Result || !Doc.document_element())
            throw std::runtime_error(std::string("Cannot parse XML: ") + Result.description());
        D.Root = FromXml(Doc.document_element());
    }
    return D;
}
std::string DataSection::Xml() const
{
    pugi::xml_document Doc;
    ToXml(Root, Doc);
    std::ostringstream Out;
    Doc.save(Out, "  ", pugi::format_default, pugi::encoding_utf8);
    return Out.str();
}
Bytes DataSection::Serialize() const
{
    if (!Packed)
    {
        auto S = Xml();
        return Bytes(S.begin(), S.end());
    }
    std::map<std::string, uint16_t> Ids;
    std::vector<std::string> Names;
    std::function<void(Section const&)> Visit = [&](auto const& S)
    {
        for (auto const& C : S.Children)
        {
            if (!Ids.contains(C.Name))
            {
                if (Names.size() == 65535)
                    throw std::runtime_error("Too many XML names");
                Ids[C.Name] = uint16_t(Names.size());
                Names.push_back(C.Name);
            }
            Visit(C);
        }
    };
    Visit(Root);
    Bytes Out;
    Put32(Out, 0x62a14e45);
    Out.push_back(0);
    for (auto const& N : Names)
    {
        Append(Out, N);
        Out.push_back(0);
    }
    Out.push_back(0);
    Append(Out, SectionBytes(Root, Ids));
    return Out;
}
std::vector<Property> DataSection::Properties(std::string const& Search)
{
    std::vector<Property> Result;
    auto Query = Lower(Search);
    std::function<void(Section&, std::string)> Visit = [&](auto& S, auto Path)
    {
        if (!S.Value.empty() || S.Children.empty())
        {
            if (Query.empty() || Lower(Path + " " + S.Value).find(Query) != std::string::npos)
                Result.push_back({Path, S.Value, &S});
        }
        std::map<std::string, int> Totals, Seen;
        for (auto const& C : S.Children)
            ++Totals[C.Name];
        for (auto& C : S.Children)
        {
            auto N = C.Name;
            if (Totals[N] > 1)
                N += "[" + std::to_string(++Seen[C.Name]) + "]";
            Visit(C, Path.empty() ? N : Path + "/" + N);
        }
    };
    Visit(Root, "");
    return Result;
}
} // namespace OffHangar
