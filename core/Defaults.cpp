#include "Defaults.h"
#include "Configuration.h"

namespace OffHangar
{
DefaultResources::DefaultResources(FileSystem::path const& Folder)
{
    bool LegacyFolder = FileSystem::is_directory(Folder);
    auto Path = LegacyFolder ? Folder / L"scripts.pkg" : Folder;
    auto Metadata = LegacyFolder ? Folder / L"manifest.yaml" : Folder.parent_path() / L"defaults.yaml";
    Client = "User-provided original client";
    if (FileSystem::is_regular_file(Metadata))
    {
        auto Manifest = ReadYaml(Metadata);
        auto Format = Manifest["format"].as<std::string>();
        if ((Format != "OffHangarStudio.Defaults" && Format != "OffHanger.Defaults") ||
            (Manifest["schema"].as<int>() != 1 && Manifest["schema"].as<int>() != 2) ||
            Manifest["archive"].as<std::string>() != PathUtf8(Path.filename()))
            throw std::runtime_error("Unsupported original-client defaults manifest");
        if (FileSystem::file_size(Path) != Manifest["bytes"].as<uint64_t>() ||
            Sha256File(Path) != Manifest["sha256"].as<std::string>())
            throw std::runtime_error("Original-client script.pkg does not match its integrity record");
        Client = Manifest["client"].as<std::string>();
    }
    Package =
        std::make_shared<Archive>(Path,
                                  [](std::string const& Name)
                                  {
                                      if (!Lower(Name).ends_with(".xml"))
                                          return false;
                                      auto Key = NormalizeResourcePath(Name);
                                      return Key.starts_with("scripts/item_defs/") && Key.ends_with(".xml");
                                  });
    for (auto const& Entry : Package->Entries)
        if (!Definitions.emplace(NormalizeResourcePath(Entry.Name), Entry).second)
            throw std::runtime_error("Duplicate original-client definition: " + Entry.Name);
    if (Definitions.empty())
        throw std::runtime_error("The original-client defaults archive contains no item definitions");
    if (!LegacyFolder && Path.filename() == L"script.pkg" &&
        (Count() < 50 || !Contains("scripts/item_defs/vehicles/ussr/list.xml") ||
         !Contains("scripts/item_defs/vehicles/usa/list.xml")))
        throw std::runtime_error("The baseline must be a complete original client scripts archive");
}
void DefaultResources::Install(FileSystem::path const& Source, FileSystem::path const& Target)
{
    EnsureWritesAllowed();
    DefaultResources Candidate(Source);
    if (Candidate.Count() < 50 || !Candidate.Contains("scripts/item_defs/vehicles/ussr/list.xml") ||
        !Candidate.Contains("scripts/item_defs/vehicles/usa/list.xml"))
        throw std::runtime_error(
            "Select a complete original client scripts.pkg, not a vehicle or Mod package");
    for (auto const& [Path, Entry] : Candidate.Definitions)
        DataSection::Parse(Candidate.Package->Read(Entry));
    auto Data = ReadFile(Source, 2ull * 1024 * 1024 * 1024);
    auto Hash = Sha256File(Source);
    AtomicWrite(Target, Data, false);
    YAML::Node Manifest;
    Manifest["format"] = "OffHanger.Defaults";
    Manifest["schema"] = 2;
    Manifest["archive"] = PathUtf8(Target.filename());
    Manifest["bytes"] = Data.size();
    Manifest["sha256"] = Hash;
    Manifest["client"] = "User-provided original client";
    WriteYaml(Target.parent_path() / L"defaults.yaml", Manifest);
}
bool DefaultResources::Contains(std::string const& ResourcePath) const
{
    return !ResourcePath.empty() && Definitions.contains(NormalizeResourcePath(ResourcePath));
}
std::optional<DataSection> DefaultResources::Load(std::string const& ResourcePath) const
{
    if (ResourcePath.empty())
        return {};
    auto Found = Definitions.find(NormalizeResourcePath(ResourcePath));
    if (Found == Definitions.end())
        return {};
    return DataSection::Parse(Package->Read(Found->second));
}
FileSystem::path const& DefaultResources::GetPath() const
{
    return Package->GetPath();
}
std::string const& DefaultResources::GetClient() const
{
    return Client;
}
size_t DefaultResources::Count() const
{
    return Definitions.size();
}
} // namespace OffHangar
