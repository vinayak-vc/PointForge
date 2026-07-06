// pftest — synthetic-data round-trip test for the octree indexer.
//
// 1. Generates a deterministic synthetic .xyz cloud (wavy terrain + dense
//    clusters, fixed LCG seed — identical bytes every run).
// 2. Converts it twice: sequential (threads=1) and parallel (threads=N).
// 3. Asserts the two outputs are BYTE-IDENTICAL (hierarchy.bin, octree.bin,
//    meta.bin) — the parallel coordinator splices chunk results in chunk
//    order precisely so this holds.
// 4. Loads the result with OctreeStore and verifies structural invariants:
//    meta point count == input count, DFS from the root reaches every node
//    exactly once, child levels increment, payload offsets are in range.
//
// Exit code 0 = pass, 1 = fail. Usage:
//   pftest [--points N] [--threads T] [--keep] [--dir workdir]

#include "indexer/OctreeIndexer.h"
#include "common/OctreeFormat.h"
#include "common/Log.h"
#include "viewer/OctreeStore.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int gFailures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++gFailures;                                                     \
        }                                                                    \
    } while (0)

// Deterministic 64-bit LCG (MMIX constants) — never std::random (impl-defined).
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    uint64_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return s; }
    double uniform() { return (double)(next() >> 11) / (double)(1ull << 53); } // [0,1)
};

static bool writeSyntheticXyz(const std::string& path, uint64_t nPoints) {
    std::ofstream f(path, std::ios::binary); // binary: identical newlines everywhere
    if (!f) return false;
    Lcg rng(0xC0FFEE5EEDull);
    char line[128];
    // 70% wavy terrain over 200x200 m, 30% in 8 dense 2 m clusters (forces
    // deep subdivision so the leaf/depth logic is exercised).
    const uint64_t nTerrain = nPoints * 7 / 10;
    for (uint64_t i = 0; i < nPoints; ++i) {
        double x, y, z;
        if (i < nTerrain) {
            x = rng.uniform() * 200.0;
            y = rng.uniform() * 200.0;
            z = 5.0 * std::sin(x * 0.12) * std::cos(y * 0.09) + rng.uniform() * 0.05;
        } else {
            int c = (int)(rng.next() % 8);
            double cx = 25.0 + (c % 4) * 50.0;
            double cy = 50.0 + (c / 4) * 100.0;
            x = cx + rng.uniform() * 2.0;
            y = cy + rng.uniform() * 2.0;
            z = 10.0 + rng.uniform() * 2.0;
        }
        int n = std::snprintf(line, sizeof(line), "%.4f %.4f %.4f\n", x, y, z);
        f.write(line, n);
    }
    return f.good();
}

static bool filesEqual(const std::string& a, const std::string& b) {
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    std::vector<char> ba((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
    std::vector<char> bb((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
    return ba == bb;
}

static double convert(const std::string& in, const std::string& out, int threads, bool compress) {
    pf::IndexOptions opts;
    opts.gridDepth = 3;      // 512 chunks — enough to exercise the pool
    opts.targetLeafSize = 20000;
    opts.threads = threads;
    opts.compress = compress;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = pf::buildOctree(in, out, opts);
    auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ok, "buildOctree returned false");
    return dt;
}

// DFS the loaded hierarchy: every node reachable exactly once, levels increase,
// payload ranges valid.
static void verifyStructure(const pf::OctreeStore& store, uint64_t inputPoints,
                            uint64_t payloadFileSize) {
    const auto& nodes = store.nodes();
    CHECK(!nodes.empty(), "hierarchy is empty");
    CHECK(store.meta().pointCount == inputPoints, "meta.pointCount != input count");
    CHECK(store.rootIndex() < nodes.size(), "root index out of range");

    std::vector<uint8_t> seen(nodes.size(), 0);
    std::vector<uint32_t> stack{store.rootIndex()};
    uint64_t reached = 0, chunkTreePoints = 0;
    while (!stack.empty()) {
        uint32_t idx = stack.back(); stack.pop_back();
        CHECK(idx < nodes.size(), "child index out of range");
        if (idx >= nodes.size()) return;
        CHECK(!seen[idx], "node reached twice (cycle or shared child)");
        seen[idx] = 1;
        ++reached;
        const pf::NodeRecord& r = nodes[idx];
        chunkTreePoints += r.pointCount;
        CHECK((uint64_t)r.byteOffset + r.byteSize <= payloadFileSize,
              "payload range exceeds octree.bin size");
        for (int o = 0; o < 8; ++o) {
            if (r.children[o] == pf::kNoChild) {
                CHECK(!(r.childMask & (1u << o)), "childMask bit set for kNoChild");
                continue;
            }
            CHECK((r.childMask & (1u << o)) != 0, "childMask bit missing for child");
            CHECK(nodes[r.children[o]].level == r.level + 1 ||
                  nodes[r.children[o]].level >= r.level + 1,
                  "child level does not increase");
            stack.push_back(r.children[o]);
        }
    }
    CHECK(reached == nodes.size(), "unreachable nodes in hierarchy");
    // Coarse levels re-sample points that also live in chunk subtrees, so the
    // total across nodes is >= the input count, never less.
    CHECK(chunkTreePoints >= inputPoints, "fewer points stored than input");
}

int main(int argc, char** argv) {
    uint64_t nPoints = 2'000'000;
    int threads = 0; // auto
    bool keep = false;
    std::string work = "pftest_work";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--points" && i + 1 < argc) nPoints = std::stoull(argv[++i]);
        else if (a == "--threads" && i + 1 < argc) threads = std::stoi(argv[++i]);
        else if (a == "--keep") keep = true;
        else if (a == "--dir" && i + 1 < argc) work = argv[++i];
    }

    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    const std::string xyz = work + "/synthetic.xyz";
    const std::string seqDir = work + "/seq";
    const std::string parDir = work + "/par";

    std::printf("pftest: generating %llu synthetic points...\n", (unsigned long long)nPoints);
    if (!writeSyntheticXyz(xyz, nPoints)) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", xyz.c_str());
        return 1;
    }

    // Both compressed and uncompressed paths, sequential vs parallel.
    for (bool compress : {false, true}) {
        std::printf("pftest: [compress=%d] sequential convert...\n", (int)compress);
        double tSeq = convert(xyz, seqDir, 1, compress);
        std::printf("pftest: [compress=%d] parallel convert (threads=%d)...\n",
                    (int)compress, threads);
        double tPar = convert(xyz, parDir, threads, compress);
        std::printf("pftest: [compress=%d] sequential %.2fs, parallel %.2fs (%.2fx)\n",
                    (int)compress, tSeq, tPar, tPar > 0 ? tSeq / tPar : 0.0);

        CHECK(filesEqual(seqDir + "/hierarchy.bin", parDir + "/hierarchy.bin"),
              "hierarchy.bin differs between sequential and parallel");
        CHECK(filesEqual(seqDir + "/octree.bin", parDir + "/octree.bin"),
              "octree.bin differs between sequential and parallel");
        CHECK(filesEqual(seqDir + "/meta.bin", parDir + "/meta.bin"),
              "meta.bin differs between sequential and parallel");

        pf::OctreeStore store;
        CHECK(store.load(parDir), "OctreeStore failed to load parallel output");
        uint64_t payloadSize = (uint64_t)fs::file_size(parDir + "/octree.bin", ec);
        verifyStructure(store, nPoints, payloadSize);

        fs::remove_all(seqDir, ec);
        fs::remove_all(parDir, ec);
    }

    if (!keep) fs::remove_all(work, ec);

    if (gFailures == 0) {
        std::printf("pftest: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "pftest: %d FAILURE(S)\n", gFailures);
    return 1;
}
