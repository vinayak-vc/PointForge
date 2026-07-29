#pragma once
#include "common/Vec3.h"
#include <string>
#include <vector>
#include <cstdint>

namespace pf {

// Single 3D Gaussian Splat attribute representation
struct GaussianSplat {
    Vec3d    position{0.0, 0.0, 0.0};   // Center XYZ (world/model space)
    Vec3d    scale{1.0, 1.0, 1.0};      // 3D scale factors (S_x, S_y, S_z)
    float    quaternion[4]{1,0,0,0};    // Normalized rotation quaternion (w, x, y, z)
    float    opacity = 1.0f;            // Opacity in [0, 1] range
    float    color[3]{1, 1, 1};         // Base RGB color in [0, 1] range
    std::vector<float> sh_rest;         // Higher-order Spherical Harmonics (optional)
};

// Container for a loaded 3D Gaussian Splat dataset
struct SplatCloudData {
    std::vector<GaussianSplat> splats;
    Vec3d boxMin{1e30, 1e30, 1e30};
    Vec3d boxMax{-1e30, -1e30, -1e30};
    Vec3d center{0.0, 0.0, 0.0};
    bool ok = false;
    std::string errorMessage;

    uint64_t count() const { return splats.size(); }
};

enum class SplatFileFormat {
    Unknown,
    SplatBinary,  // 32-byte .splat format
    PlyGaussians   // 3DGS .ply format
};

class SplatReader {
public:
    // Auto-detect format by extension/header and parse whole dataset
    static SplatCloudData loadFromFile(const std::string& path);

    // Explicit format loaders
    static SplatCloudData loadSplatBinary(const std::string& path);
    static SplatCloudData loadSplatBinaryFromMemory(const uint8_t* data, size_t size);
    static SplatCloudData loadPlyGaussians(const std::string& path);

    // Detect file type
    static SplatFileFormat detectFormat(const std::string& path);
};

} // namespace pf
