#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "io/SplatReader.h"
#include "common/Log.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace pf {

template <typename T>
static T getMin(T a, T b) { return a < b ? a : b; }

template <typename T>
static T getMax(T a, T b) { return a > b ? a : b; }

template <typename T>
static T getClamp(T val, T low, T high) { return val < low ? low : (val > high ? high : val); }

static constexpr float SH_C0 = 0.28209479177387814f;

static float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

SplatFileFormat SplatReader::detectFormat(const std::string& path) {
    if (path.length() >= 6 && path.substr(path.length() - 6) == ".splat") {
        return SplatFileFormat::SplatBinary;
    }
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".ply") {
        return SplatFileFormat::PlyGaussians;
    }

    // Try reading header bytes
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return SplatFileFormat::Unknown;

    char header[14] = {0};
    file.read(header, 14);
    if (std::memcmp(header, "ply\n", 4) == 0 || std::memcmp(header, "ply\r\n", 5) == 0) {
        return SplatFileFormat::PlyGaussians;
    }

    return SplatFileFormat::SplatBinary;
}

SplatCloudData SplatReader::loadFromFile(const std::string& path) {
    SplatFileFormat fmt = detectFormat(path);
    if (fmt == SplatFileFormat::SplatBinary) {
        return loadSplatBinary(path);
    } else if (fmt == SplatFileFormat::PlyGaussians) {
        return loadPlyGaussians(path);
    }

    SplatCloudData res;
    res.ok = false;
    res.errorMessage = "Unsupported file format for " + path;
    return res;
}

SplatCloudData SplatReader::loadSplatBinaryFromMemory(const uint8_t* data, size_t size) {
    SplatCloudData res;
    if (!data || size < 32) {
        res.errorMessage = "Invalid memory buffer for splat loading";
        return res;
    }

    if (size % 32 != 0) {
        logWarn("SplatReader: Memory buffer size (" + std::to_string(size) + ") is not a multiple of 32 bytes");
    }

    uint64_t count = size / 32;
    res.splats.reserve(count);

#pragma pack(push, 1)
    struct RawSplat32 {
        float x, y, z;
        float sx, sy, sz;
        uint8_t r, g, b, a;
        uint8_t qw, qx, qy, qz;  // antimatter15 .splat stores rotation w-first
    };
#pragma pack(pop)

    const RawSplat32* srcBatch = reinterpret_cast<const RawSplat32*>(data);

    for (size_t i = 0; i < count; ++i) {
        const auto& src = srcBatch[i];
        GaussianSplat s;
        s.position = Vec3d(src.x, src.y, src.z);
        s.scale = Vec3d(src.sx, src.sy, src.sz);

        s.color[0] = src.r / 255.0f;
        s.color[1] = src.g / 255.0f;
        s.color[2] = src.b / 255.0f;
        s.opacity  = src.a / 255.0f;

        // Byte quaternion (0..255) -> [-1,1], stored (w, x, y, z) to match the
        // 3DGS .ply convention (rot_0 = w) used by loadPlyGaussians().
        float qw = (src.qw - 128.0f) / 128.0f;
        float qx = (src.qx - 128.0f) / 128.0f;
        float qy = (src.qy - 128.0f) / 128.0f;
        float qz = (src.qz - 128.0f) / 128.0f;

        float norm = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
        if (norm > 1e-6f) {
            s.quaternion[0] = qw / norm;
            s.quaternion[1] = qx / norm;
            s.quaternion[2] = qy / norm;
            s.quaternion[3] = qz / norm;
        } else {
            s.quaternion[0] = 1.0f;
            s.quaternion[1] = s.quaternion[2] = s.quaternion[3] = 0.0f;
        }

        // Update bounds
        res.boxMin.x = getMin(res.boxMin.x, s.position.x);
        res.boxMin.y = getMin(res.boxMin.y, s.position.y);
        res.boxMin.z = getMin(res.boxMin.z, s.position.z);
        res.boxMax.x = getMax(res.boxMax.x, s.position.x);
        res.boxMax.y = getMax(res.boxMax.y, s.position.y);
        res.boxMax.z = getMax(res.boxMax.z, s.position.z);

        res.splats.push_back(s);
    }

    if (!res.splats.empty()) {
        res.center = (res.boxMin + res.boxMax) * 0.5;
        res.ok = true;
    } else {
        res.errorMessage = "No splats read from memory buffer";
    }

    return res;
}

SplatCloudData SplatReader::loadSplatBinary(const std::string& path) {
    SplatCloudData res;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        res.errorMessage = "Could not open file: " + path;
        return res;
    }

    std::fseek(f, 0, SEEK_END);
    long long fileSize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> buffer(fileSize);
    if (fileSize > 0) {
        std::fread(buffer.data(), 1, fileSize, f);
    }
    std::fclose(f);

    res = loadSplatBinaryFromMemory(buffer.data(), buffer.size());
    if (res.ok) {
        logInfo("SplatReader: Loaded " + std::to_string(res.splats.size()) + " splats from binary " + path);
    }
    return res;
}

SplatCloudData SplatReader::loadPlyGaussians(const std::string& path) {
    SplatCloudData res;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        res.errorMessage = "Could not open file: " + path;
        return res;
    }

    std::string line;
    bool inHeader = true;
    uint64_t vertexCount = 0;

    struct PropDef {
        std::string name;
        std::string type;
        size_t size = 4;
        size_t offset = 0;
    };

    std::vector<PropDef> props;
    size_t currentStride = 0;

    int ix = -1, iy = -1, iz = -1;
    int is0 = -1, is1 = -1, is2 = -1;
    int iop = -1;
    int ir0 = -1, ir1 = -1, ir2 = -1, ir3 = -1;
    int idc0 = -1, idc1 = -1, idc2 = -1;

    std::vector<int> irest;

    while (inHeader && std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "element") {
            std::string elemName;
            iss >> elemName;
            if (elemName == "vertex") {
                iss >> vertexCount;
            }
        } else if (token == "property") {
            std::string typeStr, nameStr;
            iss >> typeStr >> nameStr;
            PropDef p;
            p.name = nameStr;
            p.type = typeStr;
            p.size = (typeStr == "double" || typeStr == "float64" || typeStr == "uint64" || typeStr == "int64") ? 8 : 4;
            p.offset = currentStride;
            currentStride += p.size;

            int idx = static_cast<int>(props.size());
            props.push_back(p);

            if (nameStr == "x") ix = idx;
            else if (nameStr == "y") iy = idx;
            else if (nameStr == "z") iz = idx;
            else if (nameStr == "scale_0") is0 = idx;
            else if (nameStr == "scale_1") is1 = idx;
            else if (nameStr == "scale_2") is2 = idx;
            else if (nameStr == "opacity") iop = idx;
            else if (nameStr == "rot_0") ir0 = idx;
            else if (nameStr == "rot_1") ir1 = idx;
            else if (nameStr == "rot_2") ir2 = idx;
            else if (nameStr == "rot_3") ir3 = idx;
            else if (nameStr == "f_dc_0") idc0 = idx;
            else if (nameStr == "f_dc_1") idc1 = idx;
            else if (nameStr == "f_dc_2") idc2 = idx;
            else if (nameStr.rfind("f_rest_", 0) == 0) irest.push_back(idx);
        } else if (token == "end_header") {
            inHeader = false;
        }
    }

    if (ix < 0 || iy < 0 || iz < 0) {
        res.errorMessage = "PLY header missing xyz coordinates: " + path;
        return res;
    }

    res.splats.reserve(vertexCount);

    auto getFloat = [&](const std::vector<char>& buf, int propIdx, float defaultVal = 0.0f) -> float {
        if (propIdx < 0 || propIdx >= static_cast<int>(props.size())) return defaultVal;
        const auto& p = props[propIdx];
        if (p.size == 4) {
            float v;
            std::memcpy(&v, buf.data() + p.offset, 4);
            return v;
        } else if (p.size == 8) {
            double v;
            std::memcpy(&v, buf.data() + p.offset, 8);
            return static_cast<float>(v);
        }
        return defaultVal;
    };

    std::vector<char> recordBuf(currentStride);
    for (uint64_t i = 0; i < vertexCount; ++i) {
        file.read(recordBuf.data(), currentStride);
        if (!file.good()) break;

        GaussianSplat s;
        s.position.x = getFloat(recordBuf, ix);
        s.position.y = getFloat(recordBuf, iy);
        s.position.z = getFloat(recordBuf, iz);

        // Raw log-scales -> exp(scale)
        s.scale.x = std::exp(getFloat(recordBuf, is0, 0.0f));
        s.scale.y = std::exp(getFloat(recordBuf, is1, 0.0f));
        s.scale.z = std::exp(getFloat(recordBuf, is2, 0.0f));

        // Raw logit opacity -> sigmoid(opacity)
        s.opacity = sigmoid(getFloat(recordBuf, iop, 0.0f));

        // Quaternions rot_0..3 (rot_0 is w, rot_1..3 is x, y, z)
        float r0 = getFloat(recordBuf, ir0, 1.0f);
        float r1 = getFloat(recordBuf, ir1, 0.0f);
        float r2 = getFloat(recordBuf, ir2, 0.0f);
        float r3 = getFloat(recordBuf, ir3, 0.0f);

        float norm = std::sqrt(r0 * r0 + r1 * r1 + r2 * r2 + r3 * r3);
        if (norm > 1e-6f) {
            s.quaternion[0] = r0 / norm;
            s.quaternion[1] = r1 / norm;
            s.quaternion[2] = r2 / norm;
            s.quaternion[3] = r3 / norm;
        } else {
            s.quaternion[0] = 1.0f;
            s.quaternion[1] = s.quaternion[2] = s.quaternion[3] = 0.0f;
        }

        // Color SH0: f_dc_0..2 -> RGB
        float fdc0 = getFloat(recordBuf, idc0, 0.0f);
        float fdc1 = getFloat(recordBuf, idc1, 0.0f);
        float fdc2 = getFloat(recordBuf, idc2, 0.0f);

        s.color[0] = getClamp(0.5f + SH_C0 * fdc0, 0.0f, 1.0f);
        s.color[1] = getClamp(0.5f + SH_C0 * fdc1, 0.0f, 1.0f);
        s.color[2] = getClamp(0.5f + SH_C0 * fdc2, 0.0f, 1.0f);

        // Higher-order SH coefficients if present
        if (!irest.empty()) {
            s.sh_rest.reserve(irest.size());
            for (int rIdx : irest) {
                s.sh_rest.push_back(getFloat(recordBuf, rIdx));
            }
        }

        // Update bounds
        res.boxMin.x = getMin(res.boxMin.x, s.position.x);
        res.boxMin.y = getMin(res.boxMin.y, s.position.y);
        res.boxMin.z = getMin(res.boxMin.z, s.position.z);
        res.boxMax.x = getMax(res.boxMax.x, s.position.x);
        res.boxMax.y = getMax(res.boxMax.y, s.position.y);
        res.boxMax.z = getMax(res.boxMax.z, s.position.z);

        res.splats.push_back(s);
    }

    if (!res.splats.empty()) {
        res.center = (res.boxMin + res.boxMax) * 0.5;
        res.ok = true;
    } else {
        res.errorMessage = "No Gaussian splats read from PLY " + path;
    }

    logInfo("SplatReader: Loaded " + std::to_string(res.splats.size()) + " splats from 3DGS PLY " + path);
    return res;
}

} // namespace pf
