#include "Configuration.h"
namespace OffHangar
{
YAML::Node ReadYaml(FileSystem::path const& Path, size_t Maximum)
{
    auto Data = ReadFile(Path, Maximum);
    return YAML::Load(std::string(Data.begin(), Data.end()));
}
std::string EmitYaml(YAML::Node const& Document)
{
    YAML::Emitter Emitter;
    Emitter.SetIndent(2);
    Emitter << Document;
    if (!Emitter.good())
        throw std::runtime_error("Cannot serialize YAML configuration");
    return std::string(Emitter.c_str()) + "\n";
}
void WriteYaml(FileSystem::path const& Path, YAML::Node const& Document)
{
    auto Text = EmitYaml(Document);
    AtomicWrite(Path, Bytes(Text.begin(), Text.end()), false);
}
} // namespace OffHangar
