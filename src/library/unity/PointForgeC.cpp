// ---------------------------------------------------------------------------
// PointForgeC.cpp — C API implementation for the Unity native plugin.
//
// Reuses pf::OctreeStore (async disk streaming) unchanged and ports pfview's
// frustum-cull + screen-space-error traversal (src/viewer/main.cpp) verbatim.
// Residency is tracked here instead of in PointRenderer: Unity owns the GPU
// buffers, this layer just mirrors which nodes it was told are uploaded.
// ---------------------------------------------------------------------------
#define PF_UNITY_EXPORTS
#include "PointForgeC.h"

#include "viewer/OctreeStore.h"
#include "common/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---- frustum culling (ported from src/viewer/main.cpp) ---------------------
struct Frustum { std::array<glm::vec4, 6> planes; };

Frustum extractFrustum(const glm::mat4& m) {
    Frustum f;
    auto row = [&](int r) { return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
    glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    f.planes[0] = r3 + r0; // left
    f.planes[1] = r3 - r0; // right
    f.planes[2] = r3 + r1; // bottom
    f.planes[3] = r3 - r1; // top
    f.planes[4] = r3 + r2; // near
    f.planes[5] = r3 - r2; // far
    for (auto& p : f.planes) {
        float len = glm::length(glm::vec3(p));
        if (len > 0) p /= len;
    }
    return f;
}

bool aabbVisible(const Frustum& f, const glm::vec3& mn, const glm::vec3& mx) {
    for (const auto& p : f.planes) {
        glm::vec3 pv(p.x >= 0 ? mx.x : mn.x,
                     p.y >= 0 ? mx.y : mn.y,
                     p.z >= 0 ? mx.z : mn.z);
        if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f) return false;
    }
    return true;
}

std::atomic<PFLogCallback> g_logCallback{nullptr};

} // namespace

struct PFProject {
    pf::OctreeStore store;
    uint64_t frame = 0;

    // Nodes Unity confirmed uploaded (GPU-resident) -> last frame they were visible.
    std::unordered_map<uint32_t, uint64_t> resident;
    // Dequeued loads awaiting PF_ReleaseLoadedNode; owns the CPU vertex buffers.
    std::unordered_map<uint32_t, std::vector<pf::GpuVertex>> awaitingUpload;

    // Results of the last PF_UpdateCamera.
    std::vector<uint32_t> drawList;       // visible AND resident
    uint32_t lastVisibleCount = 0;

    uint64_t residentPoints = 0;
};

extern "C" {

PF_API uint32_t PF_GetVersion(void) { return 1u; }

PF_API void PF_SetLogCallback(PFLogCallback callback) {
    g_logCallback.store(callback);
    if (callback) {
        pf::setLogSink([](pf::LogLevel, const std::string& line) {
            PFLogCallback cb = g_logCallback.load();
            if (cb) cb(line.c_str());
        });
    } else {
        pf::setLogSink(nullptr);
    }
}

PF_API PFProject* PF_OpenProject(const char* directoryUtf8) {
    if (!directoryUtf8 || !*directoryUtf8) return nullptr;
    PFProject* p = new (std::nothrow) PFProject();
    if (!p) return nullptr;
    if (!p->store.load(directoryUtf8)) {
        delete p;
        return nullptr;
    }
    return p;
}

PF_API void PF_CloseProject(PFProject* project) {
    delete project; // OctreeStore dtor joins its worker thread
}

PF_API int32_t PF_GetMetadata(PFProject* project, PFMetadata* out) {
    if (!project || !out) return 0;
    const pf::FileMetadata& m = project->store.meta();
    std::memset(out, 0, sizeof(*out));
    out->pointCount = m.pointCount;
    for (int i = 0; i < 3; ++i) {
        out->bbMin[i]   = m.bbMin[i];
        out->bbMax[i]   = m.bbMax[i];
        out->cubeMin[i] = m.cubeMin[i];
    }
    out->cubeSize          = m.cubeSize;
    out->rootSpacing       = m.rootSpacing;
    out->nodeCount         = m.nodeCount;
    out->rootNodeIndex     = m.rootNodeIndex;
    out->hasColor          = m.hasColor;
    out->hasClassification = m.hasClassification;
    out->compressionType   = m.compressionType;
    out->bytesPerPoint     = m.bytesPerPoint;
    return 1;
}

PF_API int32_t PF_GetNodeInfo(PFProject* project, uint32_t nodeIndex, PFNodeInfo* out) {
    if (!project || !out) return 0;
    if (nodeIndex >= project->store.nodes().size()) return 0;
    const pf::NodeRecord& rec = project->store.nodes()[nodeIndex];
    const pf::NodeCube&   nc  = project->store.cube(nodeIndex);
    const glm::dvec3 center = project->store.cubeCenter();
    out->index      = nodeIndex;
    out->pointCount = rec.pointCount;
    out->level      = rec.level;
    out->childMask  = rec.childMask;
    out->min[0] = (float)(nc.min[0] - center.x);
    out->min[1] = (float)(nc.min[1] - center.y);
    out->min[2] = (float)(nc.min[2] - center.z);
    out->size   = (float)nc.size;
    return 1;
}

PF_API int32_t PF_UpdateCamera(PFProject* project, const PFCameraState* camera) {
    if (!project || !camera) return -1;
    if (camera->viewportHeight == 0 || camera->fovYDegrees <= 0.0f) return -1;

    PFProject& p = *project;
    ++p.frame;

    const glm::mat4 vp = glm::make_mat4(camera->viewProj);
    const Frustum frustum = extractFrustum(vp);
    const glm::vec3 camPos(camera->cameraPos[0], camera->cameraPos[1], camera->cameraPos[2]);
    const double ssFactor = (camera->viewportHeight * 0.5)
                          / std::tan(glm::radians((double)camera->fovYDegrees) * 0.5);
    const double sseBudget = camera->sseBudgetPixels > 0.0f ? camera->sseBudgetPixels : 6.0;
    const uint32_t maxRequests = camera->maxLoadRequests;

    const pf::OctreeStore& store = p.store;
    const glm::dvec3 center = store.cubeCenter();

    p.drawList.clear();
    uint32_t visible = 0;
    uint32_t requested = 0;

    // Same traversal as pfview's renderPass visit() (src/viewer/main.cpp).
    // Explicit stack instead of recursion: hierarchy depth can reach --max-depth 24+.
    std::vector<uint32_t> stack;
    stack.push_back(store.rootIndex());
    while (!stack.empty()) {
        const uint32_t idx = stack.back();
        stack.pop_back();

        const pf::NodeRecord& rec = store.nodes()[idx];
        const pf::NodeCube&   nc  = store.cube(idx);
        const glm::vec3 mn((float)(nc.min[0] - center.x),
                           (float)(nc.min[1] - center.y),
                           (float)(nc.min[2] - center.z));
        const glm::vec3 mx = mn + glm::vec3((float)nc.size);
        if (!aabbVisible(frustum, mn, mx)) continue;

        ++visible;
        auto residentIt = p.resident.find(idx);
        if (residentIt != p.resident.end()) {
            residentIt->second = p.frame;
            p.drawList.push_back(idx);
        } else if (!p.awaitingUpload.count(idx)) {
            if (maxRequests == 0 || requested < maxRequests) {
                p.store.requestLoad(idx, p.frame);
                ++requested;
            }
        }

        const glm::vec3 nodeCenter = (mn + mx) * 0.5f;
        float dist = glm::length(nodeCenter - camPos);
        if (dist < 1e-3f) dist = 1e-3f;
        const double spacing = store.nodeSpacing(rec.level);
        const double pixels  = spacing * ssFactor / dist;

        if (pixels > sseBudget && rec.childMask != 0) {
            for (int o = 0; o < 8; ++o)
                if (rec.children[o] != pf::kNoChild) stack.push_back(rec.children[o]);
        }
    }

    // Same queue hygiene as pfview: drop requests the camera moved past.
    p.store.purgeStale(p.frame, 120, 64);

    p.lastVisibleCount = visible;
    return (int32_t)visible;
}

PF_API int32_t PF_GetVisibleNodes(PFProject* project, uint32_t* outIndices, int32_t capacity) {
    if (!project || !outIndices || capacity <= 0) return 0;
    const int32_t n = (int32_t)std::min<size_t>(project->drawList.size(), (size_t)capacity);
    std::memcpy(outIndices, project->drawList.data(), (size_t)n * sizeof(uint32_t));
    return n;
}

PF_API int32_t PF_DequeueLoadedNode(PFProject* project, PFLoadedNode* out) {
    if (!project || !out) return 0;
    pf::LoadResult res;
    if (!project->store.popResult(res)) return 0;
    // A stale result can arrive for a node Unity already holds; drop it.
    if (project->resident.count(res.nodeIndex) ||
        project->awaitingUpload.count(res.nodeIndex)) {
        return 0;
    }
    auto& buf = project->awaitingUpload[res.nodeIndex];
    buf = std::move(res.verts);
    out->nodeIndex  = res.nodeIndex;
    out->pointCount = (uint32_t)buf.size();
    out->vertexData = buf.data();
    return 1;
}

PF_API void PF_ReleaseLoadedNode(PFProject* project, uint32_t nodeIndex, int32_t uploadedToGpu) {
    if (!project) return;
    auto it = project->awaitingUpload.find(nodeIndex);
    if (it == project->awaitingUpload.end()) return;
    const uint64_t points = it->second.size();
    project->awaitingUpload.erase(it);
    if (uploadedToGpu) {
        project->resident[nodeIndex] = project->frame;
        project->residentPoints += points;
    } else {
        // Discarded: let the store hand it out again if it becomes visible.
        project->store.markEvicted(nodeIndex);
    }
}

PF_API void PF_UnloadNode(PFProject* project, uint32_t nodeIndex) {
    if (!project) return;
    auto it = project->resident.find(nodeIndex);
    if (it == project->resident.end()) return;
    if (nodeIndex < project->store.nodes().size()) {
        const uint64_t points = project->store.nodes()[nodeIndex].pointCount;
        project->residentPoints = (project->residentPoints >= points)
                                ? project->residentPoints - points : 0;
    }
    project->resident.erase(it);
    project->store.markEvicted(nodeIndex);
}

PF_API int32_t PF_GetEvictionCandidates(PFProject* project, uint64_t budgetBytes,
                                        uint32_t* outIndices, int32_t capacity) {
    if (!project || !outIndices || capacity <= 0) return 0;

    const uint64_t bytesPerVertex = sizeof(pf::GpuVertex); // 20
    uint64_t residentBytes = project->residentPoints * bytesPerVertex;
    if (residentBytes <= budgetBytes) return 0;

    // Collect resident nodes not in the current draw list, LRU first.
    std::vector<std::pair<uint64_t, uint32_t>> lru; // (lastVisibleFrame, index)
    lru.reserve(project->resident.size());
    std::unordered_set<uint32_t> drawn(project->drawList.begin(), project->drawList.end());
    for (const auto& kv : project->resident) {
        if (!drawn.count(kv.first)) lru.emplace_back(kv.second, kv.first);
    }
    std::sort(lru.begin(), lru.end());

    int32_t written = 0;
    for (const auto& e : lru) {
        if (residentBytes <= budgetBytes || written >= capacity) break;
        const uint32_t idx = e.second;
        outIndices[written++] = idx;
        residentBytes -= (uint64_t)project->store.nodes()[idx].pointCount * bytesPerVertex;
    }
    return written;
}

PF_API void PF_GetStatistics(PFProject* project, PFStatistics* out) {
    if (!project || !out) return;
    std::memset(out, 0, sizeof(*out));
    out->frameIndex          = project->frame;
    out->visibleNodeCount    = project->lastVisibleCount;
    out->renderableNodeCount = (uint32_t)project->drawList.size();
    out->residentNodeCount   = (uint32_t)project->resident.size();
    out->pendingLoadCount    = (uint32_t)project->store.pendingRequests();
    out->awaitingUploadCount = (uint32_t)project->awaitingUpload.size();
    out->residentPointCount  = project->residentPoints;
    out->residentByteCount   = project->residentPoints * sizeof(pf::GpuVertex);
}

} // extern "C"
