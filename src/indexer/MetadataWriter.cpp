#include "indexer/MetadataWriter.h"
#include "common/Log.h"
#include <cstdio>
#include <fstream>

namespace pf {

bool writeMetaBin(const std::string& outDir, const FileMetadata& meta) {
    std::ofstream f(outDir + "/meta.bin", std::ios::binary);
    if (!f) { logError("writeMetaBin: cannot open meta.bin"); return false; }
    f.write(reinterpret_cast<const char*>(&meta), sizeof(meta));
    return f.good();
}

bool writeHierarchy(const std::string& outDir, const std::vector<NodeRecord>& nodes) {
    std::ofstream f(outDir + "/hierarchy.bin", std::ios::binary);
    if (!f) { logError("writeHierarchy: cannot open hierarchy.bin"); return false; }
    f.write(reinterpret_cast<const char*>(nodes.data()),
            (std::streamsize)(nodes.size() * sizeof(NodeRecord)));
    return f.good();
}

bool writeMetadataJson(const std::string& outDir, const FileMetadata& m) {
    FILE* f = std::fopen((outDir + "/metadata.json").c_str(), "wb");
    if (!f) { logError("writeMetadataJson: cannot open metadata.json"); return false; }
    std::fprintf(f,
        "{\n"
        "  \"version\": %u,\n"
        "  \"pointCount\": %llu,\n"
        "  \"boundingBox\": { \"min\": [%.6f, %.6f, %.6f], \"max\": [%.6f, %.6f, %.6f] },\n"
        "  \"cube\": { \"min\": [%.6f, %.6f, %.6f], \"size\": %.6f },\n"
        "  \"scale\": [%.9g, %.9g, %.9g],\n"
        "  \"offset\": [%.6f, %.6f, %.6f],\n"
        "  \"rootSpacing\": %.6f,\n"
        "  \"bytesPerPoint\": %u,\n"
        "  \"hasColor\": %u,\n"
        "  \"nodeCount\": %u,\n"
        "  \"rootNodeIndex\": %u,\n"
        "  \"hasClassification\": %u,\n"
        "  \"compressionType\": %u\n"
        "}\n",
        m.version,
        (unsigned long long)m.pointCount,
        m.bbMin[0], m.bbMin[1], m.bbMin[2], m.bbMax[0], m.bbMax[1], m.bbMax[2],
        m.cubeMin[0], m.cubeMin[1], m.cubeMin[2], m.cubeSize,
        m.scale[0], m.scale[1], m.scale[2],
        m.offset[0], m.offset[1], m.offset[2],
        m.rootSpacing,
        m.bytesPerPoint, m.hasColor, m.nodeCount, m.rootNodeIndex,
        m.hasClassification, m.compressionType);
    std::fclose(f);
    return true;
}

} // namespace pf
