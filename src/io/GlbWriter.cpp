#include "io/GlbWriter.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace pf {

namespace {

// glTF accessor.componentType constants.
constexpr int CT_UBYTE  = 5121;
constexpr int CT_USHORT = 5123;
constexpr int CT_FLOAT  = 5126;
// bufferView.target
constexpr int TARGET_ARRAY_BUFFER = 34962;

// SH degree-0 basis constant: colour = 0.5 + C0 * sh  =>  sh = (colour - 0.5)/C0.
constexpr float kSH_C0 = 0.28209479177387814f;

std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Compact number formatting for JSON (no trailing junk, enough precision).
std::string num(double v) {
    if (!std::isfinite(v)) return "0";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

// Accumulates the GLB binary payload plus the bufferViews/accessors JSON arrays
// they describe. One bufferView per accessor (tightly packed, non-interleaved).
struct BinBuilder {
    std::vector<uint8_t> data;
    std::vector<std::string> bufferViews;
    std::vector<std::string> accessors;

    void pad4() { while (data.size() % 4u) data.push_back(0); }

    int addView(const void* bytes, size_t len) {
        pad4();
        const size_t offset = data.size();
        const uint8_t* p = static_cast<const uint8_t*>(bytes);
        data.insert(data.end(), p, p + len);
        std::ostringstream v;
        v << "{\"buffer\":0,\"byteOffset\":" << offset
          << ",\"byteLength\":" << len
          << ",\"target\":" << TARGET_ARRAY_BUFFER << "}";
        bufferViews.push_back(v.str());
        return static_cast<int>(bufferViews.size()) - 1;
    }

    // Adds a bufferView + accessor. `minMax` (may be null) points to 2*numComp
    // doubles (min[numComp] then max[numComp]) for the accessor bounds.
    int addAccessor(const void* bytes, size_t len, int componentType,
                    uint64_t count, const char* type, bool normalized,
                    const double* minMax = nullptr, int numComp = 0) {
        const int view = addView(bytes, len);
        std::ostringstream a;
        a << "{\"bufferView\":" << view
          << ",\"componentType\":" << componentType
          << ",\"count\":" << count
          << ",\"type\":\"" << type << "\"";
        if (normalized) a << ",\"normalized\":true";
        if (minMax && numComp > 0) {
            a << ",\"min\":[";
            for (int i = 0; i < numComp; ++i) a << (i ? "," : "") << num(minMax[i]);
            a << "],\"max\":[";
            for (int i = 0; i < numComp; ++i) a << (i ? "," : "") << num(minMax[numComp + i]);
            a << "]";
        }
        a << "}";
        accessors.push_back(a.str());
        return static_cast<int>(accessors.size()) - 1;
    }
};

void writeChunk(std::ofstream& out, uint32_t type, const std::vector<uint8_t>& payload,
                uint8_t padByte) {
    uint32_t len = static_cast<uint32_t>(payload.size());
    const uint32_t pad = (4u - (len % 4u)) % 4u;
    len += pad;
    out.write(reinterpret_cast<const char*>(&len), 4);
    out.write(reinterpret_cast<const char*>(&type), 4);
    out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    for (uint32_t i = 0; i < pad; ++i) out.write(reinterpret_cast<const char*>(&padByte), 1);
}

} // namespace

void GlbWriter::beginCloud(const std::string& name,
                           const Vec3d& nodeTranslation,
                           const Vec3d& vertexOrigin) {
    Cloud c;
    c.name = name;
    c.translation[0] = nodeTranslation.x;
    c.translation[1] = nodeTranslation.y;
    c.translation[2] = nodeTranslation.z;
    c.vertexOrigin = vertexOrigin;
    clouds_.push_back(std::move(c));
}

void GlbWriter::addPoint(const Vec3d& worldPos,
                         uint8_t r, uint8_t g, uint8_t b,
                         uint16_t intensity, uint8_t classification) {
    if (clouds_.empty()) return;
    PointPrim& p = clouds_.back().points;
    const Vec3d local = worldPos - clouds_.back().vertexOrigin;
    const float fx = static_cast<float>(local.x);
    const float fy = static_cast<float>(local.y);
    const float fz = static_cast<float>(local.z);
    p.pos.push_back(fx); p.pos.push_back(fy); p.pos.push_back(fz);
    p.col.push_back(r); p.col.push_back(g); p.col.push_back(b); p.col.push_back(255);
    p.inten.push_back(intensity);
    p.cls.push_back(classification);
    if (fx < p.min[0]) p.min[0] = fx; if (fx > p.max[0]) p.max[0] = fx;
    if (fy < p.min[1]) p.min[1] = fy; if (fy > p.max[1]) p.max[1] = fy;
    if (fz < p.min[2]) p.min[2] = fz; if (fz > p.max[2]) p.max[2] = fz;
    ++p.count;
}

void GlbWriter::addSplat(const GaussianSplat& s) {
    if (clouds_.empty()) return;
    SplatPrim& sp = clouds_.back().splats;
    const Vec3d local = s.position - clouds_.back().vertexOrigin;
    const float fx = static_cast<float>(local.x);
    const float fy = static_cast<float>(local.y);
    const float fz = static_cast<float>(local.z);
    sp.pos.push_back(fx); sp.pos.push_back(fy); sp.pos.push_back(fz);
    // Struct stores quaternion as (w, x, y, z); glTF wants (x, y, z, w).
    sp.rot.push_back(s.quaternion[1]);
    sp.rot.push_back(s.quaternion[2]);
    sp.rot.push_back(s.quaternion[3]);
    sp.rot.push_back(s.quaternion[0]);
    sp.scale.push_back(static_cast<float>(s.scale.x));
    sp.scale.push_back(static_cast<float>(s.scale.y));
    sp.scale.push_back(static_cast<float>(s.scale.z));
    sp.opacity.push_back(s.opacity);
    // Diffuse colour -> SH degree-0 coefficient.
    sp.sh0.push_back((s.color[0] - 0.5f) / kSH_C0);
    sp.sh0.push_back((s.color[1] - 0.5f) / kSH_C0);
    sp.sh0.push_back((s.color[2] - 0.5f) / kSH_C0);
    if (fx < sp.min[0]) sp.min[0] = fx; if (fx > sp.max[0]) sp.max[0] = fx;
    if (fy < sp.min[1]) sp.min[1] = fy; if (fy > sp.max[1]) sp.max[1] = fy;
    if (fz < sp.min[2]) sp.min[2] = fz; if (fz > sp.max[2]) sp.max[2] = fz;
    ++sp.count;
}

uint64_t GlbWriter::totalPrimitives() const {
    uint64_t n = 0;
    for (const Cloud& c : clouds_) n += c.points.count + c.splats.count;
    return n;
}

bool GlbWriter::finish(const std::string& path, const Options& opts) {
    error_.clear();
    if (totalPrimitives() == 0) {
        error_ = "nothing to export (no points or splats)";
        return false;
    }

    BinBuilder bin;
    std::vector<std::string> meshes;
    std::vector<std::string> nodes;
    std::vector<int> childNodeIndices;   // cloud nodes parented under the root
    bool anySplats = false;

    // Root node is index 0; cloud nodes follow. Reserve slot 0.
    nodes.push_back("");   // placeholder, filled after children are known

    for (const Cloud& c : clouds_) {
        std::vector<std::string> prims;

        // ---- point primitive -------------------------------------------------
        if (c.points.count > 0) {
            const PointPrim& p = c.points;
            double mm[6] = {p.min[0], p.min[1], p.min[2], p.max[0], p.max[1], p.max[2]};
            const int aPos = bin.addAccessor(p.pos.data(), p.pos.size() * sizeof(float),
                                             CT_FLOAT, p.count, "VEC3", false, mm, 3);
            const int aCol = bin.addAccessor(p.col.data(), p.col.size() * sizeof(uint8_t),
                                             CT_UBYTE, p.count, "VEC4", true);
            std::ostringstream attrs;
            attrs << "\"POSITION\":" << aPos << ",\"COLOR_0\":" << aCol;
            if (opts.includeIntensity) {
                const int a = bin.addAccessor(p.inten.data(), p.inten.size() * sizeof(uint16_t),
                                              CT_USHORT, p.count, "SCALAR", false);
                attrs << ",\"_INTENSITY\":" << a;
            }
            if (opts.includeClassification) {
                const int a = bin.addAccessor(p.cls.data(), p.cls.size() * sizeof(uint8_t),
                                              CT_UBYTE, p.count, "SCALAR", false);
                attrs << ",\"_CLASSIFICATION\":" << a;
            }
            std::ostringstream prim;
            prim << "{\"attributes\":{" << attrs.str() << "},\"mode\":0}";
            prims.push_back(prim.str());
        }

        // ---- splat primitive (KHR_gaussian_splatting) ------------------------
        if (c.splats.count > 0) {
            anySplats = true;
            const SplatPrim& s = c.splats;
            double mm[6] = {s.min[0], s.min[1], s.min[2], s.max[0], s.max[1], s.max[2]};
            const int aPos = bin.addAccessor(s.pos.data(), s.pos.size() * sizeof(float),
                                             CT_FLOAT, s.count, "VEC3", false, mm, 3);
            const int aRot = bin.addAccessor(s.rot.data(), s.rot.size() * sizeof(float),
                                             CT_FLOAT, s.count, "VEC4", false);
            const int aScl = bin.addAccessor(s.scale.data(), s.scale.size() * sizeof(float),
                                             CT_FLOAT, s.count, "VEC3", false);
            const int aOpa = bin.addAccessor(s.opacity.data(), s.opacity.size() * sizeof(float),
                                             CT_FLOAT, s.count, "SCALAR", false);
            const int aSh0 = bin.addAccessor(s.sh0.data(), s.sh0.size() * sizeof(float),
                                             CT_FLOAT, s.count, "VEC3", false);
            std::ostringstream prim;
            prim << "{\"attributes\":{"
                 << "\"POSITION\":" << aPos
                 << ",\"KHR_gaussian_splatting:ROTATION\":" << aRot
                 << ",\"KHR_gaussian_splatting:SCALE\":" << aScl
                 << ",\"KHR_gaussian_splatting:OPACITY\":" << aOpa
                 << ",\"KHR_gaussian_splatting:SH_DEGREE_0_COEF_0\":" << aSh0
                 << "},\"mode\":0,\"extensions\":{\"KHR_gaussian_splatting\":{"
                 << "\"kernel\":\"ellipse\",\"colorSpace\":\"srgb_rec709_display\"}}}";
            prims.push_back(prim.str());
        }

        if (prims.empty()) continue;   // cloud contributed no geometry

        // mesh
        std::ostringstream mesh;
        mesh << "{\"primitives\":[";
        for (size_t i = 0; i < prims.size(); ++i) mesh << (i ? "," : "") << prims[i];
        mesh << "]}";
        const int meshIdx = static_cast<int>(meshes.size());
        meshes.push_back(mesh.str());

        // node
        std::ostringstream node;
        node << "{\"name\":\"" << escapeJson(c.name) << "\""
             << ",\"translation\":[" << num(c.translation[0]) << ","
             << num(c.translation[1]) << "," << num(c.translation[2]) << "]"
             << ",\"mesh\":" << meshIdx << "}";
        const int nodeIdx = static_cast<int>(nodes.size());
        nodes.push_back(node.str());
        childNodeIndices.push_back(nodeIdx);
    }

    if (childNodeIndices.empty()) {
        error_ = "nothing to export (all clouds empty after filtering)";
        return false;
    }

    // Root node: Z-up -> Y-up is a -90 deg rotation about X, quaternion
    // (x,y,z,w) = (-sqrt(2)/2, 0, 0, sqrt(2)/2). Identity when yUp is off.
    std::ostringstream root;
    root << "{\"name\":\"root\"";
    if (opts.yUp) root << ",\"rotation\":[-0.7071067811865476,0,0,0.7071067811865476]";
    root << ",\"children\":[";
    for (size_t i = 0; i < childNodeIndices.size(); ++i)
        root << (i ? "," : "") << childNodeIndices[i];
    root << "]}";
    nodes[0] = root.str();

    // ---- assemble JSON -------------------------------------------------------
    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\""
       << escapeJson(opts.generator) << "\",\"extras\":{"
       << "\"origin\":[" << num(opts.origin.x) << "," << num(opts.origin.y)
       << "," << num(opts.origin.z) << "]";
    if (!opts.sourceName.empty())
        js << ",\"source\":\"" << escapeJson(opts.sourceName) << "\"";
    js << "}}";

    if (anySplats)
        js << ",\"extensionsUsed\":[\"KHR_gaussian_splatting\"]";

    js << ",\"scene\":0,\"scenes\":[{\"nodes\":[0]}]";

    js << ",\"nodes\":[";
    for (size_t i = 0; i < nodes.size(); ++i) js << (i ? "," : "") << nodes[i];
    js << "]";

    js << ",\"meshes\":[";
    for (size_t i = 0; i < meshes.size(); ++i) js << (i ? "," : "") << meshes[i];
    js << "]";

    js << ",\"accessors\":[";
    for (size_t i = 0; i < bin.accessors.size(); ++i) js << (i ? "," : "") << bin.accessors[i];
    js << "]";

    js << ",\"bufferViews\":[";
    for (size_t i = 0; i < bin.bufferViews.size(); ++i) js << (i ? "," : "") << bin.bufferViews[i];
    js << "]";

    js << ",\"buffers\":[{\"byteLength\":" << bin.data.size() << "}]";
    js << "}";

    const std::string jsonStr = js.str();
    std::vector<uint8_t> jsonBytes(jsonStr.begin(), jsonStr.end());

    // ---- write GLB container -------------------------------------------------
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out) {
        error_ = "could not open output file: " + path;
        return false;
    }

    const uint32_t jsonPad = (4u - (static_cast<uint32_t>(jsonBytes.size()) % 4u)) % 4u;
    const uint32_t binPad  = (4u - (static_cast<uint32_t>(bin.data.size()) % 4u)) % 4u;
    const uint32_t total = 12u                                          // header
                         + 8u + static_cast<uint32_t>(jsonBytes.size()) + jsonPad
                         + 8u + static_cast<uint32_t>(bin.data.size())  + binPad;

    const uint32_t magic = 0x46546C67u;   // "glTF"
    const uint32_t version = 2u;
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&total), 4);

    writeChunk(out, 0x4E4F534Au /*"JSON"*/, jsonBytes, 0x20 /*space*/);
    writeChunk(out, 0x004E4942u /*"BIN\0"*/, bin.data, 0x00);

    out.flush();
    if (!out.good()) {
        error_ = "failed while writing output file: " + path;
        return false;
    }
    return true;
}

} // namespace pf
