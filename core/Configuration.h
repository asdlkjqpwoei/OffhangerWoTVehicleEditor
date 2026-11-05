#pragma once
#include "Common.h"
#include "yaml-cpp/yaml.h"

namespace OffHangar
{
YAML::Node ReadYaml(FileSystem::path const& Path, size_t Maximum = 4 * 1024 * 1024);
void WriteYaml(FileSystem::path const& Path, YAML::Node const& Document);
std::string EmitYaml(YAML::Node const& Document);
} // namespace OffHangar
