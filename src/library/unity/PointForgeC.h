#pragma once
// ---------------------------------------------------------------------------
// PointForgeC.h — flat C API over the PointForge streaming octree reader,
// consumed by the Unity native plugin (P/Invoke). Wraps pf::OctreeStore plus
// the same frustum-cull + screen-space-error traversal pfview uses; pfcore
// itself is untouched.
//
// Contract:
//  - All functions must be called from a single thread (Unity main thread).
//    Disk streaming happens on OctreeStore's own worker thread internally.
//  - Only POD structs cross the boundary. Strings are UTF-8.
//  - Coordinates are "centred" PointForge space: metres relative to the octree
//    cube centre, Z-up right-handed — identical to what pfview uploads to GL.
//  - Vertex payload is the 20-byte pf::GpuVertex layout:
//      float x,y,z; uint8 r,g,b,a; uint16 intensity; uint8 classification, pad
// ---------------------------------------------------------------------------
#include <stdint.h>

#if defined(_WIN32)
  #if defined(PF_UNITY_EXPORTS)
    #define PF_API __declspec(dllexport)
  #else
    #define PF_API __declspec(dllimport)
  #endif
#else
  #define PF_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PFProject PFProject;   // opaque handle

// Mirrors FileMetadata (meta.bin) minus internals Unity has no use for.
typedef struct PFMetadata {
    uint64_t pointCount;
    double   bbMin[3];          // true AABB, world coords
    double   bbMax[3];
    double   cubeMin[3];        // octree root cube origin, world coords
    double   cubeSize;
    double   rootSpacing;
    uint32_t nodeCount;
    uint32_t rootNodeIndex;
    uint32_t hasColor;
    uint32_t hasClassification;
    uint32_t compressionType;   // 0 = none, 1 = zstd per node
    uint32_t bytesPerPoint;     // on-disk PackedPoint size (22)
} PFMetadata;

typedef struct PFNodeInfo {
    uint32_t index;
    uint32_t pointCount;
    uint32_t level;
    uint32_t childMask;
    float    min[3];            // node cube, centred space
    float    size;
} PFNodeInfo;

typedef struct PFCameraState {
    float    viewProj[16];      // column-major, transforms centred PF space -> clip
    float    cameraPos[3];      // centred PF space
    float    fovYDegrees;       // vertical FOV (perspective assumed)
    uint32_t viewportWidth;
    uint32_t viewportHeight;
    float    sseBudgetPixels;   // descend while projected spacing > budget (pfview default 6)
    uint32_t maxLoadRequests;   // cap on new load requests issued per update (0 = unlimited)
} PFCameraState;

typedef struct PFLoadedNode {
    uint32_t    nodeIndex;
    uint32_t    pointCount;
    const void* vertexData;     // pointCount * 20 bytes; valid until PF_ReleaseLoadedNode
} PFLoadedNode;

typedef struct PFStatistics {
    uint64_t frameIndex;
    uint32_t visibleNodeCount;    // passed frustum+SSE last update
    uint32_t renderableNodeCount; // visible AND resident last update
    uint32_t residentNodeCount;   // nodes Unity currently holds on GPU
    uint32_t pendingLoadCount;    // queued/in-flight disk reads
    uint32_t awaitingUploadCount; // dequeued but not yet released
    uint32_t pad;
    uint64_t residentPointCount;
    uint64_t residentByteCount;   // residentPointCount * 20
} PFStatistics;

// ---- lifecycle ------------------------------------------------------------
// Opens a converted octree directory (meta.bin / hierarchy.bin / octree.bin).
// Returns NULL on failure (bad path, bad magic, unreadable files).
PF_API PFProject* PF_OpenProject(const char* directoryUtf8);
PF_API void       PF_CloseProject(PFProject* project);
PF_API uint32_t   PF_GetVersion(void);           // API version, currently 1

// ---- metadata / hierarchy ---------------------------------------------------
PF_API int32_t PF_GetMetadata(PFProject* project, PFMetadata* out);      // 1 ok, 0 fail
PF_API int32_t PF_GetNodeInfo(PFProject* project, uint32_t nodeIndex, PFNodeInfo* out);

// ---- per-frame streaming loop ----------------------------------------------
// Traverses the octree with frustum culling + screen-space error, requests
// async loads for visible non-resident nodes, refreshes the draw list.
// Returns the number of visible nodes (renderable subset via PF_GetVisibleNodes).
PF_API int32_t PF_UpdateCamera(PFProject* project, const PFCameraState* camera);

// Fills outIndices with the current draw list (visible AND GPU-resident).
// Returns the number written (<= capacity).
PF_API int32_t PF_GetVisibleNodes(PFProject* project, uint32_t* outIndices, int32_t capacity);

// Pops one finished disk load. Returns 1 and fills `out` if available, else 0.
// The vertexData pointer stays valid until PF_ReleaseLoadedNode for that node.
PF_API int32_t PF_DequeueLoadedNode(PFProject* project, PFLoadedNode* out);

// Releases the CPU-side buffer of a dequeued node. uploadedToGpu != 0 marks the
// node GPU-resident (it will appear in draw lists); 0 discards it so the node
// can be re-requested later.
PF_API void PF_ReleaseLoadedNode(PFProject* project, uint32_t nodeIndex, int32_t uploadedToGpu);

// Tells the engine Unity freed a node's GPU buffer. The node becomes loadable again.
PF_API void PF_UnloadNode(PFProject* project, uint32_t nodeIndex);

// LRU eviction planning: fills outIndices with resident nodes to unload (least
// recently visible first, never nodes in the current draw list) until resident
// bytes fit budgetBytes. Returns the number written. Unity must free each
// buffer and call PF_UnloadNode per index.
PF_API int32_t PF_GetEvictionCandidates(PFProject* project, uint64_t budgetBytes,
                                        uint32_t* outIndices, int32_t capacity);

// ---- diagnostics -------------------------------------------------------------
PF_API void PF_GetStatistics(PFProject* project, PFStatistics* out);

// Optional log callback (message is UTF-8, valid only during the call).
typedef void (*PFLogCallback)(const char* message);
PF_API void PF_SetLogCallback(PFLogCallback callback);

#ifdef __cplusplus
} // extern "C"
#endif
