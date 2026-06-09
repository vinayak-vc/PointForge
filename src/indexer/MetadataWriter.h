#pragma once
#include "common/OctreeFormat.h"
#include <string>
#include <vector>

namespace pf {

bool writeMetaBin(const std::string& outDir, const FileMetadata& meta);
bool writeMetadataJson(const std::string& outDir, const FileMetadata& meta);
bool writeHierarchy(const std::string& outDir, const std::vector<NodeRecord>& nodes);

} // namespace pf
