#include "ModelCache.h"
#include "Configuration.h"
namespace OffHangar
{
namespace
{
std::string SelectionKey(Vehicle const& Vehicle, AssemblySelection const& Selection)
{
    return Vehicle.Name + "|" + Selection.Chassis + "|" + std::to_string(Selection.TurretSlot) + "|" +
           Selection.Turret + "|" + Selection.Gun + "|collision";
}
uint32_t GeometryFingerprint(DataSection Definition)
{
    std::function<void(Section&)> RemoveArmor = [&](Section& Node)
    {
        std::erase_if(Node.Children, [](Section const& Child) { return Child.Name == "armor"; });
        for (auto& Child : Node.Children)
            RemoveArmor(Child);
    };
    RemoveArmor(Definition.Root);
    auto Data = Definition.Serialize();
    return Crc32(Data);
}
} // namespace
FileSystem::path ModelCachePath(FileSystem::path const& Folder, Vehicle const& Vehicle,
                                AssemblySelection const& Selection)
{
    auto Key = SelectionKey(Vehicle, Selection);
    return Folder / Wide("armor-" + std::to_string(Crc32(Bytes(Key.begin(), Key.end()))) + ".meshbin");
}
std::optional<Mesh> LoadModelCache(FileSystem::path const& Folder, std::string const& Signature,
                                   Vehicle const& Vehicle, DataSection const& Definition,
                                   AssemblySelection const& Selection)
{
    auto Path = ModelCachePath(Folder, Vehicle, Selection);
    auto MetadataPath = Path;
    MetadataPath.replace_extension(L"yaml");
    if (!FileSystem::is_regular_file(Path) || !FileSystem::is_regular_file(MetadataPath))
        return {};
    auto Metadata = ReadYaml(MetadataPath);
    if (Metadata["format"].as<std::string>() != "OffHanger.ArmorCache" ||
        Metadata["selection"].as<std::string>() != SelectionKey(Vehicle, Selection) ||
        Metadata["signature"].as<std::string>() != Signature ||
        Metadata["definition_crc"].as<uint32_t>() != GeometryFingerprint(Definition))
        return {};
    auto Data = ReadFile(Path);
    if (Crc32(Data) != Metadata["crc"].as<uint32_t>())
        return {};
    auto Result = ReadSdkMesh(Data);
    ApplyArmorThickness(Result, Definition, Selection);
    return Result;
}
void SaveModelCache(FileSystem::path const& Folder, std::string const& Signature, Vehicle const& Vehicle,
                    DataSection const& Definition, AssemblySelection const& Selection, Mesh const& Geometry)
{
    if (GameRunning())
        return;
    auto Path = ModelCachePath(Folder, Vehicle, Selection);
    auto Data = WriteSdkMesh(Geometry);
    YAML::Node Metadata;
    Metadata["format"] = "OffHanger.ArmorCache";
    Metadata["selection"] = SelectionKey(Vehicle, Selection);
    Metadata["signature"] = Signature;
    Metadata["definition_crc"] = GeometryFingerprint(Definition);
    Metadata["crc"] = Crc32(Data);
    AtomicWrite(Path, Data, false);
    Path.replace_extension(L"yaml");
    WriteYaml(Path, Metadata);
}
} // namespace OffHangar
