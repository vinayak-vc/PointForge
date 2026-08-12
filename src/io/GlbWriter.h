#pragma once

#include "common/Vec3.h"
#include "io/SplatReader.h"   // GaussianSplat
#include <cstdint>
#include <string>
#include <vector>

namespace pf {

// Zero-dependency streaming glTF-2.0 binary (.glb) writer.
//
// The importer/viewer holds pure point data (position + colour + intensity +
// classification) and 3D Gaussian splats (position + rotation + scale + opacity
// + colour). Both map cleanly onto glTF's POINTS primitive:
//   - point clouds  -> POSITION + COLOR_0 (+ optional custom _INTENSITY /
//                      _CLASSIFICATION attributes)
//   - splats        -> the Khronos KHR_gaussian_splatting extension (POSITION +
//                      KHR_gaussian_splatting:{ROTATION,SCALE,OPACITY,
//                      SH_DEGREE_0_COEF_0}) on a POINTS primitive.
//
// No triangulation is performed; nothing about this writer needs OpenGL, so it
// lives in pfcore next to the other format writers (DxfWriter). JSON is emitted
// by hand (pfcore does not link a JSON library) and the binary payload is
// assembled into a single GLB BIN chunk.
//
// Coordinate handling
// -------------------
// Positions are fed in WORLD/double space and stored relative to a per-cloud
// `vertexOrigin` as float (GLB positions are float32, so keeping them local to
// the cloud preserves precision for large georeferenced coordinates). Each
// cloud becomes a child node translated by `nodeTranslation`; the true world
// origin is recorded in asset.extras so the offset is never lost. Z-up -> Y-up
// (glTF's standard up axis) is expressed as a rotation on a single parent node,
// so per-vertex data — including splat orientations — stays in source space and
// the conversion is lossless.
//
// Memory: geometry is buffered in RAM until finish(). Callers that export very
// large clouds must decimate (LOD / voxel / point-budget) before feeding points
// here — GLB is a single-file, load-in-memory format, so an un-capped export is
// unusable regardless of how it is written.
class GlbWriter {
public:
    GlbWriter() = default;
    GlbWriter(const GlbWriter&) = delete;
    GlbWriter& operator=(const GlbWriter&) = delete;

    struct Options {
        bool yUp = true;                     // rotate root Z-up -> Y-up
        bool includeIntensity = false;       // add _INTENSITY to point meshes
        bool includeClassification = false;  // add _CLASSIFICATION to point meshes
        Vec3d origin{0.0, 0.0, 0.0};         // true world origin of local coords
        std::string sourceName;              // recorded in asset.extras
        std::string generator = "PointForge";
    };

    // Start a new cloud (glTF node + mesh). `nodeTranslation` is applied to the
    // child node (local, origin-relative — float). `vertexOrigin` is subtracted
    // from every subsequent addPoint/addSplat position so stored coordinates
    // stay small. Call add* between beginCloud() calls.
    void beginCloud(const std::string& name,
                    const Vec3d& nodeTranslation,
                    const Vec3d& vertexOrigin);

    // Append one point to the current cloud. Colour components are 8-bit (0..255)
    // — callers with 16-bit colour must narrow first. Safe to call only after a
    // beginCloud().
    void addPoint(const Vec3d& worldPos,
                  uint8_t r, uint8_t g, uint8_t b,
                  uint16_t intensity, uint8_t classification);

    // Append one Gaussian splat to the current cloud (uses the splat's own
    // position/rotation/scale/opacity/colour fields).
    void addSplat(const GaussianSplat& splat);

    // Serialize all buffered clouds to `path` as a .glb file. Returns false and
    // sets error() on failure (no clouds, empty geometry, or I/O error).
    bool finish(const std::string& path, const Options& opts);

    const std::string& error() const { return error_; }
    uint64_t totalPrimitives() const;   // points + splats across all clouds

private:
    struct PointPrim {
        std::vector<float>    pos;   // xyz interleaved (3 per point)
        std::vector<uint8_t>  col;   // rgba interleaved (4 per point)
        std::vector<uint16_t> inten; // one per point (optional)
        std::vector<uint8_t>  cls;   // one per point (optional)
        double min[3]{1e300, 1e300, 1e300};
        double max[3]{-1e300, -1e300, -1e300};
        uint64_t count = 0;
    };
    struct SplatPrim {
        std::vector<float> pos;      // xyz (3)
        std::vector<float> rot;      // xyzw (4) — glTF quaternion order
        std::vector<float> scale;    // xyz (3), linear
        std::vector<float> opacity;  // 1
        std::vector<float> sh0;      // xyz (3) — SH degree-0 coefficient
        double min[3]{1e300, 1e300, 1e300};
        double max[3]{-1e300, -1e300, -1e300};
        uint64_t count = 0;
    };
    struct Cloud {
        std::string name;
        double translation[3]{0, 0, 0};
        Vec3d vertexOrigin{0, 0, 0};
        PointPrim points;
        SplatPrim splats;
    };

    std::vector<Cloud> clouds_;
    std::string error_;
};

} // namespace pf
