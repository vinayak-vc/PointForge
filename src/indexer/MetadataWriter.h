#pragma once
#include "common/OctreeFormat.h"
#include <string>
#include <vector>

namespace pf {

class PackageWriter;

bool writeMetaBin(const std::string& outDir, const FileMetadata& meta, PackageWriter* pkg = nullptr);
bool writeProjectMetadataBin(const std::string& outDir, const ProjectMetadata& meta, PackageWriter* pkg = nullptr);
bool writeMetadataJson(const std::string& outDir, const FileMetadata& meta, PackageWriter* pkg = nullptr);
bool writeHierarchy(const std::string& outDir, const std::vector<NodeRecord>& nodes, PackageWriter* pkg = nullptr);

} // namespace pf
