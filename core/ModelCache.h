#pragma once
#include "Geometry.h"

namespace OffHangar
{
// Cached meshes contain assembled geometry. Armor values always come from the
// current working XML so editing a thickness never requires rebuilding geometry.
FileSystem::path ModelCachePath(FileSystem::path const& Folder, Vehicle const& Vehicle,
                                AssemblySelection const& Selection);
std::optional<Mesh> LoadModelCache(FileSystem::path const& Folder, std::string const& Signature,
                                   Vehicle const& Vehicle, DataSection const& Definition,
                                   AssemblySelection const& Selection);
void SaveModelCache(FileSystem::path const& Folder, std::string const& Signature, Vehicle const& Vehicle,
                    DataSection const& Definition, AssemblySelection const& Selection, Mesh const& Mesh);
} // namespace OffHangar
