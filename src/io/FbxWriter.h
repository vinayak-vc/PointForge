#pragma once

#include "common/Vec3.h"
#include "io/SplatReader.h"   // GaussianSplat
#include <cstdint>
#include <string>
#include <vector>

namespace pf {

// Zero-dependency ASCII FBX (7400 / FBX 2014-15) writer for point data.
//
// FBX has no native point-cloud primitive, so — like CloudCompare's FBX point
// export — each cloud is written as a Mesh whose control points are the points
// and which has NO polygons. Per-point colour rides on a `LayerElementColor`
// with `ByControlPoint` / `Direct` mapping. This imports as vertices into Unity,
// Blender and (vertex-only) Unreal. Intensity/classification and true Gaussian
// splats have no FBX representation: splats are degraded to coloured points
// (centroid + diffuse colour) and scalar attributes are dropped — use GLB for a
// lossless export.
//
// ASCII (not binary) keeps the writer dependency-free and human-inspectable, at
// the cost of larger files; callers bound size with the shared decimation /
// max-point budget before feeding points here.
//
// Coordinate handling mirrors GlbWriter: positions are stored relative to a
// per-cloud `vertexOrigin` (FBX ASCII vertices are double, so precision is not
// the concern GLB's float32 is, but keeping them local keeps the numbers small),
// each cloud is a Model node translated by `nodeTranslation`. Z-up -> Y-up is
// baked into the vertices when `yUp` is set (points carry no orientation), and
// `GlobalSettings.UpAxis` is set to match; the true world origin is recorded in
// the Model's user properties.
class FbxWriter {
public:
    FbxWriter() = default;
    FbxWriter(const FbxWriter&) = delete;
    FbxWriter& operator=(const FbxWriter&) = delete;

    struct Options {
        bool yUp = true;                 // bake Z-up -> Y-up and set UpAxis=Y
        Vec3d origin{0.0, 0.0, 0.0};     // true world origin of local coords
        double unitScaleCm = 100.0;      // FBX UnitScaleFactor (cm per unit); 100 = metres
        std::string sourceName;
        std::string creator = "PointForge";
        // includeIntensity/includeClassification are accepted for API symmetry
        // with GlbWriter but ignored — FBX has no place for them here.
        bool includeIntensity = false;
        bool includeClassification = false;
    };

    void beginCloud(const std::string& name,
                    const Vec3d& nodeTranslation,
                    const Vec3d& vertexOrigin);

    void addPoint(const Vec3d& worldPos,
                  uint8_t r, uint8_t g, uint8_t b,
                  uint16_t intensity, uint8_t classification);

    // Splats degrade to coloured points (centroid + diffuse colour in [0,1]).
    void addSplat(const GaussianSplat& splat);

    bool finish(const std::string& path, const Options& opts);

    const std::string& error() const { return error_; }
    uint64_t totalPrimitives() const;

private:
    struct Cloud {
        std::string name;
        double translation[3]{0, 0, 0};
        Vec3d vertexOrigin{0, 0, 0};
        std::vector<double> verts;   // xyz interleaved (3 per point)
        std::vector<float>  cols;    // rgb interleaved (3 per point), 0..1
        uint64_t count = 0;
    };

    std::vector<Cloud> clouds_;
    std::string error_;
};

} // namespace pf
