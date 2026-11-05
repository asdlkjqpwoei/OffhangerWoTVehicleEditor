#include "Pickle.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>
namespace OffHangar
{
ValuePtr Value::Make(ValueKind K)
{
    auto P = std::make_shared<Value>();
    P->Kind = K;
    return P;
}
ValuePtr Value::Number(int64_t V)
{
    auto P = Make(ValueKind::Integer);
    P->Integer = V;
    return P;
}
ValuePtr Value::FromBytes(Bytes const& B)
{
    return FromBytes(std::string(B.begin(), B.end()));
}
ValuePtr Value::FromBytes(std::string const& S)
{
    auto P = Make(ValueKind::Bytes);
    P->Text = S;
    return P;
}
ValuePtr Value::MakeList(size_t Count)
{
    auto P = Make(ValueKind::List);
    while (Count--)
        P->Items.push_back(Make(ValueKind::None));
    return P;
}
ValuePtr Value::Get(std::string const& Key) const
{
    for (auto const& [EntryKey, EntryValue] : Pairs)
        if ((EntryKey->Kind == ValueKind::Bytes || EntryKey->Kind == ValueKind::Unicode) &&
            EntryKey->Text == Key)
            return EntryValue;
    return {};
}
ValuePtr Value::Get(int64_t Key) const
{
    for (auto const& [EntryKey, EntryValue] : Pairs)
        if ((EntryKey->Kind == ValueKind::Integer || EntryKey->Kind == ValueKind::Long) &&
            EntryKey->AsInt(INT64_MIN) == Key)
            return EntryValue;
    return {};
}
void Value::Set(std::string const& Key, ValuePtr V)
{
    if (Kind != ValueKind::Dictionary)
        throw std::runtime_error("Expected a save dictionary");
    for (auto& [EntryKey, PreviousValue] : Pairs)
        if ((EntryKey->Kind == ValueKind::Bytes || EntryKey->Kind == ValueKind::Unicode) &&
            EntryKey->Text == Key)
        {
            PreviousValue = std::move(V);
            return;
        }
    Pairs.emplace_back(FromBytes(Key), std::move(V));
}
void Value::Set(int64_t Key, ValuePtr V)
{
    if (Kind != ValueKind::Dictionary)
        throw std::runtime_error("Expected a save dictionary");
    for (auto& [EntryKey, PreviousValue] : Pairs)
        if ((EntryKey->Kind == ValueKind::Integer || EntryKey->Kind == ValueKind::Long) &&
            EntryKey->AsInt(INT64_MIN) == Key)
        {
            PreviousValue = std::move(V);
            return;
        }
    Pairs.emplace_back(Number(Key), std::move(V));
}
int64_t Value::AsInt(int64_t Fallback) const
{
    if (Kind == ValueKind::Integer || Kind == ValueKind::Boolean)
        return Integer;
    if (Kind != ValueKind::Long || LongBytes.size() > 8)
        return Fallback;
    uint64_t V = 0;
    for (size_t I = 0; I < LongBytes.size(); ++I)
        V |= uint64_t(LongBytes[I]) << (I * 8);
    if (!LongBytes.empty() && LongBytes.size() < 8 && (LongBytes.back() & 128))
        V |= ~0ull << (LongBytes.size() * 8);
    return int64_t(V);
}
std::string Value::Display() const
{
    switch (Kind)
    {
    case ValueKind::None:
        return "None";
    case ValueKind::Boolean:
        return Integer ? "true" : "false";
    case ValueKind::Integer:
        return std::to_string(Integer);
    case ValueKind::Long:
        return LongBytes.size() <= 8 ? std::to_string(AsInt()) : "long(base64):" + Base64(LongBytes);
    case ValueKind::Real:
    {
        std::ostringstream Out;
        Out << std::setprecision(17) << Real;
        return Out.str();
    }
    case ValueKind::Bytes:
    {
        bool Printable = Text.size() < 2048 && Text.find_first_of(std::string("\0", 1)) == Text.npos;
        if (Printable)
        {
            try
            {
                Wide(Text);
            }
            catch (...)
            {
                Printable = false;
            }
        }
        if (Printable)
            for (unsigned char C : Text)
                if (C < 32 && C != '\t' && C != '\n' && C != '\r')
                    Printable = false;
        return Printable ? Text : "binary(" + std::to_string(Text.size()) + " bytes)";
    }
    case ValueKind::Unicode:
        return Text;
    case ValueKind::Dictionary:
        return "dict[" + std::to_string(Pairs.size()) + "]";
    case ValueKind::List:
        return "list[" + std::to_string(Items.size()) + "]";
    case ValueKind::Tuple:
        return "tuple[" + std::to_string(Items.size()) + "]";
    case ValueKind::Global:
        return Text;
    default:
        return "object (preserved)";
    }
}
bool Value::ScalarEditable() const
{
    return Kind == ValueKind::Boolean || Kind == ValueKind::Integer ||
           (Kind == ValueKind::Long && LongBytes.size() <= 8) || Kind == ValueKind::Real ||
           Kind == ValueKind::Unicode || (Kind == ValueKind::Bytes && Display() == Text);
}
ValuePtr Value::Edited(std::string const& Input) const
{
    if (!ScalarEditable())
        throw std::runtime_error("Binary and structured values cannot be edited as text");
    auto P = Make(Kind);
    size_t Used = 0;
    if (Kind == ValueKind::Boolean)
    {
        if (Input != "true" && Input != "false" && Input != "1" && Input != "0")
            throw std::runtime_error("Expected true or false");
        P->Integer = Input == "true" || Input == "1";
    }
    else if (Kind == ValueKind::Integer || Kind == ValueKind::Long)
    {
        auto V = std::stoll(Input, &Used);
        if (Used != Input.size())
            throw std::runtime_error("Expected an integer");
        if (Kind == ValueKind::Integer)
            P->Integer = V;
        else
        {
            for (int I = 0; I < 8; ++I)
                P->LongBytes.push_back(uint8_t(uint64_t(V) >> (I * 8)));
        }
    }
    else if (Kind == ValueKind::Real)
    {
        P->Real = std::stod(Input, &Used);
        if (Used != Input.size() || !std::isfinite(P->Real))
            throw std::runtime_error("Expected a finite number");
    }
    else
    {
        if (Kind == ValueKind::Unicode)
            Wide(Input);
        P->Text = Input;
    }
    return P;
}
class Reader
{
    Bytes const& Data;
    size_t Pos = 0, Count = 0;
    std::vector<ValuePtr> Stack;
    std::unordered_map<uint32_t, ValuePtr> Memo;
    uint8_t Byte()
    {
        if (Pos >= Data.size())
            throw std::runtime_error("Truncated pickle");
        return Data[Pos++];
    }
    std::string Raw(size_t N)
    {
        if (N > Data.size() - Pos || N > 64u * 1024u * 1024u)
            throw std::runtime_error("Pickle string exceeds bounds");
        std::string S(reinterpret_cast<char const*>(Data.data() + Pos), N);
        Pos += N;
        return S;
    }
    std::string Line()
    {
        auto Start = Pos;
        while (Pos < Data.size() && Data[Pos] != '\n')
            ++Pos;
        if (Pos == Data.size())
            throw std::runtime_error("Unterminated pickle line");
        auto S = RawAt(Start, Pos - Start);
        ++Pos;
        return S;
    }
    std::string RawAt(size_t P, size_t N)
    {
        return std::string(reinterpret_cast<char const*>(Data.data() + P), N);
    }
    uint32_t Word()
    {
        auto N = U32(Data, Pos);
        Pos += 4;
        return N;
    }
    ValuePtr Pop()
    {
        if (Stack.empty() || !Stack.back())
            throw std::runtime_error("Invalid pickle stack");
        auto P = Stack.back();
        Stack.pop_back();
        return P;
    }
    size_t Mark()
    {
        auto I = Stack.size();
        while (I && Stack[I - 1])
            --I;
        if (!I)
            throw std::runtime_error("Pickle MARK missing");
        return I - 1;
    }
    void Push(ValuePtr P)
    {
        if (++Count > 2000000 || Stack.size() > 1000000)
            throw std::runtime_error("Pickle object limit exceeded");
        Stack.push_back(std::move(P));
    }
    void Sequence(ValueKind Kind, size_t Start, bool HasMark)
    {
        auto P = Value::Make(Kind);
        P->Items.assign(Stack.begin() + Start + (HasMark ? 1 : 0), Stack.end());
        Stack.resize(Start);
        Push(P);
    }
    void DictionaryItems(ValuePtr const& P, size_t First)
    {
        if (P->Kind != ValueKind::Dictionary || (Stack.size() - First) % 2)
            throw std::runtime_error("Invalid pickle dictionary");
        for (size_t I = First; I < Stack.size(); I += 2)
        {
            bool Replaced = false;
            for (auto& Pair : P->Pairs)
                if (ValueEqual(Pair.first, Stack[I]))
                {
                    Pair.second = Stack[I + 1];
                    Replaced = true;
                    break;
                }
            if (!Replaced)
                P->Pairs.emplace_back(Stack[I], Stack[I + 1]);
        }
    }

  public:
    explicit Reader(Bytes const& D) : Data(D) {}
    ValuePtr Parse()
    {
        if (Data.size() < 3 || Data[0] != 128 || Data[1] > 2)
            throw std::runtime_error("Expected an OffHangar Python pickle (protocol 2)");
        while (Pos < Data.size())
        {
            auto Op = Byte();
            switch (Op)
            {
            case 128:
                if (Byte() > 2)
                    throw std::runtime_error("Unsupported pickle protocol");
                break;
            case '.':
            {
                auto P = Pop();
                if (!Stack.empty() || Pos != Data.size())
                    throw std::runtime_error("Trailing pickle data");
                return P;
            }
            case '(':
                Stack.push_back({});
                break;
            case 'N':
                Push(Value::Make(ValueKind::None));
                break;
            case 136:
            case 137:
            {
                auto P = Value::Make(ValueKind::Boolean);
                P->Integer = Op == 136;
                Push(P);
                break;
            }
            case 'K':
                Push(Value::Number(Byte()));
                break;
            case 'M':
            {
                auto N = U16(Data, Pos);
                Pos += 2;
                Push(Value::Number(N));
                break;
            }
            case 'J':
                Push(Value::Number(int32_t(Word())));
                break;
            case 138:
            case 139:
            {
                size_t N = Op == 138 ? Byte() : Word();
                auto P = Value::Make(ValueKind::Long);
                auto S = Raw(N);
                P->LongBytes.assign(S.begin(), S.end());
                Push(P);
                break;
            }
            case 'G':
            {
                uint64_t Bits = 0;
                for (int I = 0; I < 8; ++I)
                    Bits = (Bits << 8) | Byte();
                auto P = Value::Make(ValueKind::Real);
                std::memcpy(&P->Real, &Bits, 8);
                Push(P);
                break;
            }
            case 'T':
            case 'U':
            case 'X':
            {
                size_t N = Op == 'U' ? Byte() : Word();
                auto P = Value::Make(Op == 'X' ? ValueKind::Unicode : ValueKind::Bytes);
                P->Text = Raw(N);
                if (Op == 'X')
                    Wide(P->Text);
                Push(P);
                break;
            }
            case ']':
                Push(Value::MakeList());
                break;
            case ')':
                Push(Value::Make(ValueKind::Tuple));
                break;
            case '}':
                Push(Value::Make(ValueKind::Dictionary));
                break;
            case 'l':
                Sequence(ValueKind::List, Mark(), true);
                break;
            case 't':
                Sequence(ValueKind::Tuple, Mark(), true);
                break;
            case 133:
            case 134:
            case 135:
            {
                size_t N = Op - 132;
                if (Stack.size() < N)
                    throw std::runtime_error("Invalid tuple");
                Sequence(ValueKind::Tuple, Stack.size() - N, false);
                break;
            }
            case 'd':
            {
                auto M = Mark();
                auto P = Value::Make(ValueKind::Dictionary);
                DictionaryItems(P, M + 1);
                Stack.resize(M);
                Push(P);
                break;
            }
            case 'a':
            {
                auto V = Pop();
                if (Stack.empty() || !Stack.back() || Stack.back()->Kind != ValueKind::List)
                    throw std::runtime_error("Expected pickle list");
                Stack.back()->Items.push_back(V);
                break;
            }
            case 'e':
            {
                auto M = Mark();
                if (!M || !Stack[M - 1] || Stack[M - 1]->Kind != ValueKind::List)
                    throw std::runtime_error("Expected pickle list");
                auto P = Stack[M - 1];
                P->Items.insert(P->Items.end(), Stack.begin() + M + 1, Stack.end());
                Stack.resize(M);
                break;
            }
            case 's':
            {
                auto V = Pop(), K = Pop();
                if (Stack.empty() || !Stack.back() || Stack.back()->Kind != ValueKind::Dictionary)
                    throw std::runtime_error("Expected pickle dictionary");
                auto P = Stack.back();
                bool Replaced = false;
                for (auto& Pair : P->Pairs)
                    if (ValueEqual(Pair.first, K))
                    {
                        Pair.second = V;
                        Replaced = true;
                        break;
                    }
                if (!Replaced)
                    P->Pairs.emplace_back(K, V);
                break;
            }
            case 'u':
            {
                auto M = Mark();
                if (!M || !Stack[M - 1])
                    throw std::runtime_error("Expected dictionary before MARK");
                DictionaryItems(Stack[M - 1], M + 1);
                Stack.resize(M);
                break;
            }
            case 'q':
            case 'r':
            {
                auto Id = Op == 'q' ? Byte() : Word();
                if (Stack.empty() || !Stack.back())
                    throw std::runtime_error("Invalid memo value");
                Memo[Id] = Stack.back();
                break;
            }
            case 'h':
            case 'j':
            {
                auto Id = Op == 'h' ? Byte() : Word();
                auto It = Memo.find(Id);
                if (It == Memo.end())
                    throw std::runtime_error("Unknown pickle memo reference");
                Push(It->second);
                break;
            }
            case 'c':
            {
                auto P = Value::Make(ValueKind::Global);
                auto Module = Line();
                P->Text = Module + '\n' + Line();
                Push(P);
                break;
            }
            case 'R':
            case 129:
            {
                auto Args = Pop(), Callable = Pop();
                auto P = Value::Make(Op == 'R' ? ValueKind::Reduce : ValueKind::NewObject);
                P->Items = {Callable, Args};
                Push(P);
                break;
            }
            case 'b':
            {
                auto State = Pop();
                if (Stack.empty() || !Stack.back())
                    throw std::runtime_error("Missing pickle object for BUILD");
                Stack.back()->State = State;
                break;
            }
            case '0':
                Pop();
                break;
            case '1':
                Stack.resize(Mark());
                break;
            case '2':
                if (Stack.empty() || !Stack.back())
                    throw std::runtime_error("Invalid DUP");
                Push(Stack.back());
                break;
            default:
                throw std::runtime_error("Unsupported pickle opcode " + std::to_string(Op) + " at " +
                                         std::to_string(Pos - 1));
            }
        }
        throw std::runtime_error("Pickle STOP missing");
    }
};
class Writer
{
    Bytes Out{128, 2};
    std::unordered_map<Value const*, uint32_t> Memo;
    void Memoize(ValuePtr const& P)
    {
        auto Id = uint32_t(Memo.size());
        Memo[P.get()] = Id;
        Out.push_back(Id < 256 ? 'q' : 'r');
        if (Id < 256)
            Out.push_back(uint8_t(Id));
        else
            Put32(Out, Id);
    }
    void Emit(ValuePtr const& P, unsigned Depth)
    {
        if (!P || Depth > 256)
            throw std::runtime_error("Save graph nesting exceeds limit");
        auto It = Memo.find(P.get());
        if (It != Memo.end())
        {
            auto Id = It->second;
            Out.push_back(Id < 256 ? 'h' : 'j');
            if (Id < 256)
                Out.push_back(uint8_t(Id));
            else
                Put32(Out, Id);
            return;
        }
        bool Saved = false;
        switch (P->Kind)
        {
        case ValueKind::None:
            Out.push_back('N');
            break;
        case ValueKind::Boolean:
            Out.push_back(P->Integer ? 136 : 137);
            break;
        case ValueKind::Integer:
        {
            auto V = P->Integer;
            if (V >= 0 && V <= 255)
            {
                Out.push_back('K');
                Out.push_back(uint8_t(V));
            }
            else if (V >= 0 && V <= 65535)
            {
                Out.push_back('M');
                Put16(Out, uint16_t(V));
            }
            else if (V >= INT32_MIN && V <= INT32_MAX)
            {
                Out.push_back('J');
                Put32(Out, uint32_t(V));
            }
            else
            {
                Out.push_back(138);
                Out.push_back(8);
                Put64(Out, uint64_t(V));
            }
            break;
        }
        case ValueKind::Long:
        {
            if (P->LongBytes.size() < 256)
            {
                Out.push_back(138);
                Out.push_back(uint8_t(P->LongBytes.size()));
            }
            else
            {
                Out.push_back(139);
                Put32(Out, uint32_t(P->LongBytes.size()));
            }
            Append(Out, P->LongBytes);
            break;
        }
        case ValueKind::Real:
        {
            Out.push_back('G');
            uint64_t V;
            std::memcpy(&V, &P->Real, 8);
            for (int I = 7; I >= 0; --I)
                Out.push_back(uint8_t(V >> (8 * I)));
            break;
        }
        case ValueKind::Bytes:
        case ValueKind::Unicode:
        {
            if (P->Kind == ValueKind::Bytes && P->Text.size() < 256)
            {
                Out.push_back('U');
                Out.push_back(uint8_t(P->Text.size()));
            }
            else
            {
                Out.push_back(P->Kind == ValueKind::Bytes ? 'T' : 'X');
                Put32(Out, uint32_t(P->Text.size()));
            }
            Append(Out, P->Text);
            break;
        }
        case ValueKind::List:
            Out.push_back(']');
            Memoize(P);
            Saved = true;
            if (!P->Items.empty())
            {
                Out.push_back('(');
                for (auto const& C : P->Items)
                    Emit(C, Depth + 1);
                Out.push_back('e');
            }
            break;
        case ValueKind::Tuple:
            Out.push_back('(');
            for (auto const& C : P->Items)
                Emit(C, Depth + 1);
            Out.push_back('t');
            break;
        case ValueKind::Dictionary:
            Out.push_back('}');
            Memoize(P);
            Saved = true;
            if (!P->Pairs.empty())
            {
                Out.push_back('(');
                for (auto const& [EntryKey, EntryValue] : P->Pairs)
                {
                    Emit(EntryKey, Depth + 1);
                    Emit(EntryValue, Depth + 1);
                }
                Out.push_back('u');
            }
            break;
        case ValueKind::Global:
            Out.push_back('c');
            Append(Out, P->Text);
            Out.push_back('\n');
            break;
        case ValueKind::Reduce:
        case ValueKind::NewObject:
            if (P->Items.size() != 2)
                throw std::runtime_error("Invalid reduced object");
            Emit(P->Items[0], Depth + 1);
            Emit(P->Items[1], Depth + 1);
            Out.push_back(P->Kind == ValueKind::Reduce ? 'R' : 129);
            break;
        }
        if (!Saved)
            Memoize(P);
        if (P->State)
        {
            Emit(P->State, Depth + 1);
            Out.push_back('b');
        }
        if (Out.size() > 128u * 1024u * 1024u)
            throw std::runtime_error("Serialized save exceeds limit");
    }

  public:
    Bytes Write(ValuePtr const& P)
    {
        Emit(P, 0);
        Out.push_back('.');
        return Out;
    }
};
ValuePtr ReadPickle(Bytes const& D)
{
    return Reader(D).Parse();
}
Bytes WritePickle(ValuePtr const& P)
{
    return Writer().Write(P);
}
static bool EqualGraph(ValuePtr const& A, ValuePtr const& B,
                       std::set<std::pair<Value const*, Value const*>>& Visited)
{
    if (A == B)
        return true;
    if (!A || !B)
        return false;
    if (A->Kind != B->Kind)
    {
        // Python 2 treats ASCII str/unicode dictionary keys as equal. JSON
        // always reconstructs strings as unicode; arbitrary binary bytes stay distinct.
        bool TextKinds = (A->Kind == ValueKind::Bytes && B->Kind == ValueKind::Unicode) ||
                         (B->Kind == ValueKind::Bytes && A->Kind == ValueKind::Unicode);
        if (TextKinds && A->Text == B->Text &&
            std::all_of(A->Text.begin(), A->Text.end(), [](unsigned char Byte) { return Byte < 128; }))
            return true;
        auto Integral = [](ValuePtr const& Value)
        {
            return Value->Kind == ValueKind::Integer || Value->Kind == ValueKind::Boolean ||
                   (Value->Kind == ValueKind::Long && Value->LongBytes.size() <= 8);
        };
        return Integral(A) && Integral(B) && A->AsInt() == B->AsInt();
    }
    if (!Visited.emplace(A.get(), B.get()).second)
        return true;
    if (A->Kind == ValueKind::Integer || A->Kind == ValueKind::Boolean)
        return A->Integer == B->Integer;
    if (A->Kind == ValueKind::Long)
        return A->LongBytes == B->LongBytes;
    if (A->Kind == ValueKind::Real)
        return A->Real == B->Real;
    if (A->Kind == ValueKind::Bytes || A->Kind == ValueKind::Unicode || A->Kind == ValueKind::Global)
        return A->Text == B->Text;
    if (A->Kind == ValueKind::None)
        return true;
    if (A->Kind == ValueKind::Dictionary)
    {
        if (A->Pairs.size() != B->Pairs.size())
            return false;
        for (size_t Index = 0; Index < A->Pairs.size(); ++Index)
            if (!EqualGraph(A->Pairs[Index].first, B->Pairs[Index].first, Visited) ||
                !EqualGraph(A->Pairs[Index].second, B->Pairs[Index].second, Visited))
                return false;
        return true;
    }
    if (A->Kind == ValueKind::Tuple || A->Kind == ValueKind::List || A->Kind == ValueKind::Reduce ||
        A->Kind == ValueKind::NewObject)
    {
        if (A->Items.size() != B->Items.size())
            return false;
        for (size_t I = 0; I < A->Items.size(); ++I)
            if (!EqualGraph(A->Items[I], B->Items[I], Visited))
                return false;
        return EqualGraph(A->State, B->State, Visited);
    }
    return false;
}
bool ValueEqual(ValuePtr const& A, ValuePtr const& B)
{
    std::set<std::pair<Value const*, Value const*>> Visited;
    return EqualGraph(A, B, Visited);
}
std::vector<SaveProperty> ValueProperties(ValuePtr const& Root, std::string const& Search)
{
    std::vector<SaveProperty> Result;
    std::set<Value const*> Ancestry;
    auto Query = Lower(Search);
    std::function<void(ValuePtr const&, std::string, std::vector<size_t>, unsigned)> Visit =
        [&](ValuePtr const& Node, std::string Path, std::vector<size_t> Address, unsigned Depth)
    {
        if (!Node || Depth > 32 || Ancestry.contains(Node.get()) || Result.size() > 50000)
            return;
        if (Node->ScalarEditable() || Node->Kind == ValueKind::Bytes || Node->Kind == ValueKind::Long ||
            Node->Kind == ValueKind::None)
        {
            if (Query.empty() || Lower(Path + ' ' + Node->Display()).find(Query) != std::string::npos)
                Result.push_back({std::move(Path), std::move(Address), Node});
            return;
        }
        Ancestry.insert(Node.get());
        auto VisitChild = [&](ValuePtr const& Child, std::string ChildPath, size_t Index)
        {
            auto ChildAddress = Address;
            ChildAddress.push_back(Index);
            Visit(Child, std::move(ChildPath), std::move(ChildAddress), Depth + 1);
        };
        if (Node->Kind == ValueKind::Dictionary)
            for (size_t Index = 0; Index < Node->Pairs.size(); ++Index)
            {
                auto const& [EntryKey, EntryValue] = Node->Pairs[Index];
                VisitChild(EntryValue, Path.empty() ? EntryKey->Display() : Path + '/' + EntryKey->Display(),
                           Index);
            }
        else if (Node->Kind == ValueKind::List || Node->Kind == ValueKind::Tuple)
            for (size_t Index = 0; Index < Node->Items.size(); ++Index)
                VisitChild(Node->Items[Index], Path + '[' + std::to_string(Index) + ']', Index);
        Ancestry.erase(Node.get());
    };
    Visit(Root, "", {}, 0);
    return Result;
}
} // namespace OffHangar
