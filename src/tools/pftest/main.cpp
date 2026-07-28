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
#include "io/PackageFormat.h"
#include "io/SplatReader.h"

#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <algorithm>
#endif

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
        if (idx >= nodes.size()) { CHECK(false, "child index out of range"); continue; }
        if (seen[idx]) { CHECK(false, "node reached twice (cycle or shared child)"); continue; }
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
            if (r.children[o] < nodes.size()) {
                CHECK(nodes[r.children[o]].level > r.level,
                      "child level does not increase");
            }
            stack.push_back(r.children[o]); // bounds re-checked at pop
        }
    }
    CHECK(reached == nodes.size(), "unreachable nodes in hierarchy");
    // Coarse levels re-sample points that also live in chunk subtrees, so the
    // total across nodes is >= the input count, never less.
    CHECK(chunkTreePoints >= inputPoints, "fewer points stored than input");

    // Exercise the payload DECODE path (byte-identity alone can't tell a
    // correctly-written payload from a consistently-corrupt one): cast a ray
    // straight down through a known synthetic cluster and expect a hit there.
    glm::dvec3 c = store.cubeCenter();
    glm::vec3 origin((float)(26.0 - c.x), (float)(51.0 - c.y), (float)(200.0 - c.z));
    glm::dvec3 hit(0.0);
    bool got = store.pickPoint(origin, glm::vec3(0, 0, -1), 0.02, hit);
    CHECK(got, "pickPoint found nothing above the synthetic cluster");
    if (got) {
        CHECK(hit.x > 24.0 && hit.x < 28.0 && hit.y > 49.0 && hit.y < 53.0,
              "picked point is not inside the expected cluster footprint");
        CHECK(hit.z > 9.0 && hit.z < 13.0,
              "picked point height is outside the cluster range");
    }

    pf::AABB query;
    query.min = pf::Vec3d(24.0, 49.0, 9.0);
    query.max = pf::Vec3d(28.0, 53.0, 13.0);
    std::atomic<bool> cancel{false};
    uint64_t exact = 0;
    uint64_t delivered = store.forEachPointInBox(
        query, -1, &cancel,
        [&](const pf::Point& point) -> bool {
            CHECK(point.position.x >= query.min.x && point.position.x <= query.max.x,
                  "forEachPointInBox delivered x outside query");
            CHECK(point.position.y >= query.min.y && point.position.y <= query.max.y,
                  "forEachPointInBox delivered y outside query");
            CHECK(point.position.z >= query.min.z && point.position.z <= query.max.z,
                  "forEachPointInBox delivered z outside query");
            ++exact;
            return true;
        });
    CHECK(delivered == exact, "forEachPointInBox return count mismatch");
    CHECK(delivered > 0, "forEachPointInBox found no points in known cluster");
    CHECK(store.estimatePointsInBox(query, -1) >= delivered,
          "estimatePointsInBox is lower than exact boxed query");
}

// Phase 11: plugin-data namespace, entry listing, and the core-ignore
// contract. Self-contained — builds a package by hand, no conversion.
static void testPluginData(const std::string& work) {
    const std::string pkgPath = work + "/plugin_test.vxpc";
    const std::string core = "hello core payload";
    const std::string blobA = "acme plugin state blob";
    const std::vector<uint8_t> blobB = {1, 2, 3, 4, 5, 250, 128, 0, 99};

    {
        pf::PackageWriter w;
        CHECK(w.Create(pkgPath), "plugin: PackageWriter::Create failed");
        // A non-plugin entry (compressed) + two plugin blobs (one compressed).
        CHECK(w.AddMemory("core.bin", core.data(), core.size(),
                          pf::PackageWriter::Compression::ZSTD),
              "plugin: AddMemory core failed");
        CHECK(w.AddPluginData("acme/state.bin", blobA.data(), blobA.size(),
                              pf::PackageWriter::Compression::ZSTD),
              "plugin: AddPluginData acme failed");
        CHECK(w.AddPluginData("umbrella/raw.bin", blobB.data(), blobB.size()),
              "plugin: AddPluginData umbrella failed");
        // Over-long names must be rejected, not silently truncated (64-byte field).
        std::string tooLong(80, 'x');
        CHECK(!w.AddMemory(tooLong, core.data(), core.size()),
              "plugin: over-long filename was NOT rejected");
        CHECK(w.Finalize(), "plugin: Finalize failed");
    }

    pf::PackageReader r;
    CHECK(r.Open(pkgPath), "plugin: PackageReader::Open failed");

    // Listing: all entries include core + both plugin blobs (order preserved).
    auto all = r.ListEntries();
    CHECK(all.size() == 3, "plugin: expected 3 entries");
    // Plugin namespace filter.
    auto plugins = r.ListPlugins();
    CHECK(plugins.size() == 2, "plugin: expected 2 plugin entries");
    bool sawAcme = false, sawUmbrella = false;
    for (const auto& n : plugins) {
        CHECK(n.rfind("plugins/", 0) == 0, "plugin: ListPlugins returned a non-plugins/ name");
        if (n == "plugins/acme/state.bin") sawAcme = true;
        if (n == "plugins/umbrella/raw.bin") sawUmbrella = true;
    }
    CHECK(sawAcme && sawUmbrella, "plugin: expected acme + umbrella plugin names");

    // Round-trip payloads (both compression modes) via the CRC-checked Read().
    auto gotA = r.Read("plugins/acme/state.bin");
    CHECK(gotA.size() == blobA.size() &&
          std::memcmp(gotA.data(), blobA.data(), blobA.size()) == 0,
          "plugin: acme blob round-trip mismatch");
    auto gotB = r.Read("plugins/umbrella/raw.bin");
    CHECK(gotB.size() == blobB.size() &&
          std::memcmp(gotB.data(), blobB.data(), blobB.size()) == 0,
          "plugin: umbrella blob round-trip mismatch");

    // Core-ignore contract: a non-plugin entry is still readable and is NOT
    // reported under the plugins/ prefix.
    auto gotCore = r.Read("core.bin");
    CHECK(gotCore.size() == core.size(), "plugin: core.bin unreadable alongside plugin blobs");
    for (const auto& n : plugins) CHECK(n != "core.bin", "plugin: core.bin leaked into plugin list");

    std::error_code ec;
    fs::remove(pkgPath, ec);
}

// Phase 7: RepackPackage — verbatim copy of existing entries (octree.bin must
// be byte-identical, not re-(de)compressed) + add/replace/remove of small
// JSON entries. `pkgPath` is a real converted package; its store must already
// be closed (Windows can't rename over an open file).
static void testRepack(const std::string& pkgPath) {
    std::vector<uint8_t> octreeBefore;
    {
        pf::PackageReader r;
        CHECK(r.Open(pkgPath), "repack: pre-open failed");
        pf::VXPCDirectoryEntry e;
        octreeBefore = r.ReadRaw("octree.bin", e);
        CHECK(!octreeBefore.empty(), "repack: octree.bin empty pre-repack");
    }

    const std::string bm = "{\"version\":1,\"bookmarks\":[{\"name\":\"A\",\"px\":1.5}]}";
    const std::string cp = "{\"version\":1,\"keys\":[]}";
    // Phase 8: a measurements.json entry rides the same repack path.
    const std::string ms = "{\"version\":1,\"measurements\":[{\"type\":\"polyline\","
                           "\"points\":[[1.0,2.0,3.0],[4.0,5.0,6.0]]}]}";
    // Phase 9: annotations.json (label with quotes exercises JSON safety).
    const std::string an = "{\"version\":1,\"annotations\":[{\"p\":[7.0,8.0,9.0],"
                           "\"label\":\"pin \\\"one\\\"\",\"color\":[1.0,0.5,0.25]}]}";
    CHECK(pf::RepackPackage(pkgPath, {
              {"bookmarks.json",    std::vector<uint8_t>(bm.begin(), bm.end())},
              {"campaths.json",     std::vector<uint8_t>(cp.begin(), cp.end())},
              {"measurements.json", std::vector<uint8_t>(ms.begin(), ms.end())},
              {"annotations.json",  std::vector<uint8_t>(an.begin(), an.end())},
          }),
          "repack: add upserts failed");

    size_t entriesAfterAdd = 0;
    {
        pf::PackageReader r;
        CHECK(r.Open(pkgPath), "repack: reopen after add failed");
        // octree.bin copied verbatim (no re-(de)compression).
        pf::VXPCDirectoryEntry e;
        auto after = r.ReadRaw("octree.bin", e);
        CHECK(after == octreeBefore, "repack: octree.bin bytes changed by repack");
        // new entries round-trip through the CRC-checked/decompressing Read().
        auto gotBm = r.Read("bookmarks.json");
        CHECK(std::string(gotBm.begin(), gotBm.end()) == bm, "repack: bookmarks.json round-trip mismatch");
        CHECK(r.Contains("campaths.json"), "repack: campaths.json missing after add");
        auto gotMs = r.Read("measurements.json");
        CHECK(std::string(gotMs.begin(), gotMs.end()) == ms, "repack: measurements.json round-trip mismatch");
        auto gotAn = r.Read("annotations.json");
        CHECK(std::string(gotAn.begin(), gotAn.end()) == an, "repack: annotations.json round-trip mismatch");
        entriesAfterAdd = r.ListEntries().size();
        // core still loads from the repacked package.
        pf::OctreeStore s;
        CHECK(s.load(pkgPath), "repack: octree unloadable after repack");
    } // store closed here (handle released before next repack)

    // Replace bookmarks.json (upsert existing, not add) + remove campaths.json.
    const std::string bm2 = "{\"version\":1,\"bookmarks\":[]}";
    CHECK(pf::RepackPackage(pkgPath,
                            {{"bookmarks.json", std::vector<uint8_t>(bm2.begin(), bm2.end())}},
                            {"campaths.json"}),
          "repack: replace+remove failed");
    {
        pf::PackageReader r;
        CHECK(r.Open(pkgPath), "repack: reopen after replace failed");
        auto gotBm = r.Read("bookmarks.json");
        CHECK(std::string(gotBm.begin(), gotBm.end()) == bm2, "repack: replacement did not win");
        CHECK(!r.Contains("campaths.json"), "repack: removal did not drop campaths.json");
        // replace is net-zero, remove drops one -> exactly one fewer entry.
        CHECK(r.ListEntries().size() == entriesAfterAdd - 1, "repack: entry count wrong after replace+remove");
    }
}

// Phase 13: open a .vxpc over http:// and verify range reads match the local
// file. A minimal loopback HTTP/1.1 server (WinSock) serves the package with
// Range support. Windows-only (matches the WinHTTP client); no-op elsewhere.
#ifdef _WIN32
struct RangeHttpServer {
    std::vector<uint8_t> body;
    SOCKET listenSock = INVALID_SOCKET;
    int port = 0;
    std::thread th;
    std::atomic<bool> stop{false};

    bool start(const std::string& file) {
        std::ifstream in(file, std::ios::binary | std::ios::ate);
        if (!in) return false;
        std::streamsize n = in.tellg(); in.seekg(0);
        body.resize((size_t)n);
        if (n > 0) in.read((char*)body.data(), n);

        WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
        listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock == INVALID_SOCKET) return false;
        sockaddr_in addr{}; addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = 0;
        if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) != 0) return false;
        int len = sizeof(addr);
        getsockname(listenSock, (sockaddr*)&addr, &len);
        port = ntohs(addr.sin_port);
        if (listen(listenSock, 8) != 0) return false;
        th = std::thread([this] { serve(); });
        return true;
    }
    void serve() {
        while (!stop.load()) {
            fd_set fds; FD_ZERO(&fds); FD_SET(listenSock, &fds);
            timeval tv{0, 200000};
            if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;
            SOCKET c = accept(listenSock, nullptr, nullptr);
            if (c == INVALID_SOCKET) continue;
            handle(c);
            closesocket(c);
        }
    }
    void handle(SOCKET c) {
        std::string req; char buf[2048];
        for (;;) {
            int r = recv(c, buf, sizeof(buf), 0);
            if (r <= 0) return;
            req.append(buf, r);
            if (req.find("\r\n\r\n") != std::string::npos || req.size() > 65536) break;
        }
        uint64_t start = 0, end = body.empty() ? 0 : body.size() - 1;
        bool ranged = false;
        size_t rp = req.find("Range: bytes=");
        if (rp == std::string::npos) rp = req.find("range: bytes=");
        if (rp != std::string::npos) {
            ranged = true;
            const char* p = req.c_str() + rp + 13;
            start = strtoull(p, (char**)&p, 10);
            if (*p == '-') { ++p; if (*p >= '0' && *p <= '9') end = strtoull(p, nullptr, 10); }
        }
        if (!body.empty()) {
            if (start >= body.size()) start = body.size() - 1;
            if (end >= body.size()) end = body.size() - 1;
        }
        uint64_t clen = (end >= start) ? (end - start + 1) : 0;

        std::string hdr = ranged ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        if (ranged)
            hdr += "Content-Range: bytes " + std::to_string(start) + "-" + std::to_string(end) +
                   "/" + std::to_string(body.size()) + "\r\n";
        hdr += "Accept-Ranges: bytes\r\n";
        hdr += "Content-Length: " + std::to_string(clen) + "\r\n";
        hdr += "Connection: close\r\n\r\n";
        send(c, hdr.data(), (int)hdr.size(), 0);
        uint64_t sent = 0;
        while (sent < clen) {
            int chunk = (int)std::min<uint64_t>(clen - sent, 65536);
            int wn = send(c, (const char*)body.data() + start + sent, chunk, 0);
            if (wn <= 0) break;
            sent += (uint64_t)wn;
        }
    }
    void shutdownServer() {
        stop = true;
        if (th.joinable()) th.join();
        if (listenSock != INVALID_SOCKET) closesocket(listenSock);
        WSACleanup();
    }
};

static void testHttpStreaming(const std::string& pkgPath) {
    std::vector<uint8_t> refOctree;
    size_t refEntries = 0;
    {
        pf::PackageReader r;
        CHECK(r.Open(pkgPath), "http: local open failed");
        refEntries = r.ListEntries().size();
        pf::VXPCDirectoryEntry e;
        refOctree = r.ReadRaw("octree.bin", e);
    }

    RangeHttpServer srv;
    CHECK(srv.start(pkgPath), "http: loopback server failed to start");
    if (srv.port == 0) return;

    const std::string url = "http://127.0.0.1:" + std::to_string(srv.port) + "/scan.vxpc";
    {
        pf::PackageReader r;
        CHECK(r.Open(url), "http: PackageReader::Open(url) failed");
        CHECK(r.Validate(), "http: package invalid over http");
        CHECK(r.ListEntries().size() == refEntries, "http: entry count differs over http");
        // meta.bin: exercises decompress + CRC over a ranged read.
        auto meta = r.Read("meta.bin");
        CHECK(!meta.empty(), "http: meta.bin unreadable over http");
        // octree.bin raw span must match the local bytes exactly (multi-block).
        pf::VXPCDirectoryEntry e;
        auto got = r.ReadRaw("octree.bin", e);
        CHECK(got == refOctree, "http: octree.bin raw bytes differ over http");
    }
    srv.shutdownServer();
}
#else
static void testHttpStreaming(const std::string&) {}
#endif

// Phase 10: combine two single-cloud .vxpc into one multi-cloud package, then
// load each namespaced cloud back and verify it matches its source.
static void testMultiCloudPackage(const std::string& a, const std::string& b, const std::string& work) {
    const std::string scenePkg = work + "/scene.vxpc";
    CHECK(pf::combineClouds(scenePkg, {{a, "alpha"}, {b, "beta"}}), "combine: combineClouds failed");

    {
        pf::PackageReader r;
        CHECK(r.Open(scenePkg), "combine: reopen scene.vxpc failed");
        CHECK(r.Contains("scene.json"), "combine: scene.json missing");
        CHECK(r.Contains("clouds/0/meta.bin") && r.Contains("clouds/0/octree.bin"), "combine: cloud 0 entries missing");
        CHECK(r.Contains("clouds/1/meta.bin") && r.Contains("clouds/1/octree.bin"), "combine: cloud 1 entries missing");
        auto sj = r.Read("scene.json");
        std::string s(sj.begin(), sj.end());
        CHECK(s.find("clouds/0/") != std::string::npos && s.find("alpha") != std::string::npos, "combine: manifest cloud 0 wrong");
        CHECK(s.find("clouds/1/") != std::string::npos && s.find("beta") != std::string::npos, "combine: manifest cloud 1 wrong");

        // Cloud 0 must match its source; then full structural verification
        // through the namespaced octree offset (DFS + boxed query + pick).
        pf::OctreeStore src;
        CHECK(src.load(a), "combine: source load failed");
        pf::OctreeStore c0;
        CHECK(c0.load(scenePkg, "clouds/0/"), "combine: cloud 0 namespaced load failed");
        CHECK(c0.meta().pointCount == src.meta().pointCount, "combine: cloud 0 point count mismatch vs source");
        CHECK(c0.nodes().size() == src.nodes().size(), "combine: cloud 0 node count mismatch vs source");
        verifyStructure(c0, c0.meta().pointCount, r.GetSize("clouds/0/octree.bin"));

        pf::OctreeStore c1;
        CHECK(c1.load(scenePkg, "clouds/1/"), "combine: cloud 1 namespaced load failed");
        verifyStructure(c1, c1.meta().pointCount, r.GetSize("clouds/1/octree.bin"));
    } // stores + reader closed before removing the file

    std::error_code ec;
    fs::remove(scenePkg, ec);
}

// Phase 18: AES-256-GCM at-rest encryption. Self-contained; Windows-only
// (matches the BCrypt backend). Verifies: entries flagged + framed (IV|tag|ct),
// plaintext absent from ciphertext, read-without-password refused, wrong
// password rejected, correct password round-trips (both compression modes),
// and encrypted entries survive a repack (copied verbatim, still decryptable).
#ifdef _WIN32
static void testEncryption(const std::string& work) {
    const std::string pkg = work + "/enc.vxpc";
    const std::string pw = "correct horse battery staple";
    const std::string secretA = "top secret metadata payload that should be encrypted at rest";
    const std::vector<uint8_t> secretB = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 255, 254, 253};

    {
        pf::PackageWriter w;
        CHECK(w.Create(pkg), "enc: Create failed");
        CHECK(w.SetEncryption(pw), "enc: SetEncryption failed");
        CHECK(w.AddMemory("a.txt", secretA.data(), secretA.size(), pf::PackageWriter::Compression::ZSTD), "enc: add a");
        CHECK(w.AddMemory("b.bin", secretB.data(), secretB.size(), pf::PackageWriter::Compression::None), "enc: add b");
        CHECK(w.Finalize(), "enc: Finalize failed");
    }

    {
        pf::PackageReader r;
        CHECK(r.Open(pkg), "enc: open failed");
        CHECK(r.isEncrypted(), "enc: isEncrypted() false");
        pf::VXPCDirectoryEntry e;
        auto raw = r.ReadRaw("a.txt", e);
        CHECK((e.flags & pf::VXPC_FLAG_ENCRYPTED) != 0, "enc: entry not flagged encrypted");
        CHECK(raw.size() == 28 + e.compressedSize, "enc: stored size != IV+tag+ciphertext");
        std::string rawStr(raw.begin(), raw.end());
        CHECK(rawStr.find("top secret") == std::string::npos, "enc: plaintext leaked into ciphertext!");
        CHECK(r.Read("a.txt").empty(), "enc: read without password returned data");
    }

    {
        pf::PackageReader r;
        CHECK(r.Open(pkg), "enc: reopen failed");
        CHECK(!r.SetPassword("wrong password"), "enc: wrong password ACCEPTED");
        CHECK(r.SetPassword(pw), "enc: correct password rejected");
        auto a = r.Read("a.txt");
        CHECK(std::string(a.begin(), a.end()) == secretA, "enc: a.txt round-trip mismatch");
        auto b = r.Read("b.bin");
        CHECK(b == secretB, "enc: b.bin round-trip mismatch");
    }

    {
        const std::string plain = "{\"note\":\"added later, plaintext\"}";
        CHECK(pf::RepackPackage(pkg, {{"note.json", std::vector<uint8_t>(plain.begin(), plain.end())}}),
              "enc: repack failed");
        pf::PackageReader r;
        CHECK(r.Open(pkg), "enc: reopen after repack");
        CHECK(r.SetPassword(pw), "enc: password rejected after repack");
        auto a = r.Read("a.txt");
        CHECK(std::string(a.begin(), a.end()) == secretA, "enc: a.txt lost across repack");
        auto n = r.Read("note.json");
        CHECK(std::string(n.begin(), n.end()) == plain, "enc: plaintext upsert wrong");
    }

    std::error_code ec;
    fs::remove(pkg, ec);
}
#else
static void testEncryption(const std::string&) {}
#endif

static void testSplatReader(const std::string& workDir) {
    // 1. Synthetic 32-byte .splat binary test
    std::string splatPath = (fs::path(workDir) / "test.splat").string();
    {
        std::ofstream out(splatPath, std::ios::binary);
#pragma pack(push, 1)
        struct RawSplat32 {
            float x, y, z;
            float sx, sy, sz;
            uint8_t r, g, b, a;
            uint8_t qx, qy, qz, qw;
        };
#pragma pack(pop)
        RawSplat32 s1{ 1.0f, 2.0f, 3.0f, 0.5f, 0.5f, 0.5f, 255, 128, 64, 200, 128, 128, 128, 255 };
        RawSplat32 s2{ -5.0f, 10.0f, 0.0f, 1.0f, 2.0f, 3.0f, 0, 255, 128, 255, 128, 128, 128, 255 };
        out.write(reinterpret_cast<const char*>(&s1), sizeof(s1));
        out.write(reinterpret_cast<const char*>(&s2), sizeof(s2));
    }

    pf::SplatCloudData splatData = pf::SplatReader::loadFromFile(splatPath);
    CHECK(splatData.ok, "SplatReader binary failed to load");
    CHECK(splatData.count() == 2, "SplatReader binary count mismatch");
    if (splatData.count() == 2) {
        CHECK(std::abs(splatData.splats[0].position.x - 1.0f) < 1e-4f, "Splat 0 pos x mismatch");
        CHECK(std::abs(splatData.splats[0].color[0] - 1.0f) < 1e-4f, "Splat 0 color r mismatch");
        CHECK(std::abs(splatData.splats[1].position.x - (-5.0f)) < 1e-4f, "Splat 1 pos x mismatch");
    }

    // 2. Synthetic 3DGS .ply header + binary records test
    std::string plyPath = (fs::path(workDir) / "test_gsplat.ply").string();
    {
        std::ofstream out(plyPath, std::ios::binary);
        std::string header = 
            "ply\n"
            "format binary_little_endian 1.0\n"
            "element vertex 2\n"
            "property float x\n"
            "property float y\n"
            "property float z\n"
            "property float scale_0\n"
            "property float scale_1\n"
            "property float scale_2\n"
            "property float opacity\n"
            "property float rot_0\n"
            "property float rot_1\n"
            "property float rot_2\n"
            "property float rot_3\n"
            "property float f_dc_0\n"
            "property float f_dc_1\n"
            "property float f_dc_2\n"
            "end_header\n";
        out.write(header.c_str(), header.size());

#pragma pack(push, 1)
        struct PlySplatRec {
            float x, y, z;
            float s0, s1, s2;
            float op;
            float r0, r1, r2, r3;
            float fdc0, fdc1, fdc2;
        };
#pragma pack(pop)
        PlySplatRec p1{ 10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
        PlySplatRec p2{ -1.0f, -2.0f, -3.0f, 1.0f, 1.0f, 1.0f, 2.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, -1.0f };
        out.write(reinterpret_cast<const char*>(&p1), sizeof(p1));
        out.write(reinterpret_cast<const char*>(&p2), sizeof(p2));
    }

    pf::SplatCloudData plyData = pf::SplatReader::loadFromFile(plyPath);
    CHECK(plyData.ok, "SplatReader PLY 3DGS failed to load");
    CHECK(plyData.count() == 2, "SplatReader PLY 3DGS count mismatch");
    if (plyData.count() == 2) {
        CHECK(std::abs(plyData.splats[0].position.x - 10.0f) < 1e-4f, "PLY Splat 0 pos x mismatch");
        CHECK(std::abs(plyData.splats[0].scale.x - 1.0f) < 1e-4f, "PLY Splat 0 exp scale x mismatch"); // exp(0) = 1
        CHECK(std::abs(plyData.splats[1].position.x - (-1.0f)) < 1e-4f, "PLY Splat 1 pos x mismatch");
    }

    // 3. Test .vxpc container packaging of splat.bin (Phase 7)
    std::string vxpcSplatPath = (fs::path(workDir) / "test_gsplat_package.vxpc").string();
    {
        pf::PackageWriter writer;
        CHECK(writer.Create(vxpcSplatPath), "Splat vxpc create failed");

        std::ifstream splatStream(splatPath, std::ios::binary);
        std::vector<uint8_t> rawSplatData((std::istreambuf_iterator<char>(splatStream)), std::istreambuf_iterator<char>());
        CHECK(writer.AddMemory("splat.bin", rawSplatData.data(), rawSplatData.size(), pf::PackageWriter::Compression::ZSTD), "AddMemory splat.bin failed");

        std::string manifest = "{\"version\":1,\"clouds\":[{\"prefix\":\"\",\"name\":\"test_gsplat\",\"type\":\"gsplat\"}]}";
        std::vector<uint8_t> manifestData(manifest.begin(), manifest.end());
        CHECK(writer.AddMemory("scene.json", manifestData.data(), manifestData.size(), pf::PackageWriter::Compression::None), "AddMemory scene.json failed");
        CHECK(writer.Finalize(), "Finalize gsplat vxpc failed");
    }

    {
        pf::PackageReader reader;
        CHECK(reader.Open(vxpcSplatPath), "Open gsplat vxpc package failed");
        CHECK(reader.Contains("splat.bin"), "Package missing splat.bin");
        CHECK(reader.Contains("scene.json"), "Package missing scene.json");

        auto splatBuf = reader.Read("splat.bin");
        pf::SplatCloudData pkgSplatData = pf::SplatReader::loadSplatBinaryFromMemory(splatBuf.data(), splatBuf.size());
        CHECK(pkgSplatData.ok, "loadSplatBinaryFromMemory from vxpc failed");
        CHECK(pkgSplatData.count() == 2, "loadSplatBinaryFromMemory count mismatch");
    }
}

int main(int argc, char** argv) {
    uint64_t nPoints = 2'000'000;
    // Default to a fixed thread count > 1 so the parallel leg is never
    // vacuously equal to the sequential one (auto could resolve to 1 core).
    int threads = 8;
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
    const std::string seqFile = work + "/seq.vxpc";
    const std::string parFile = work + "/par.vxpc";

    std::printf("pftest: [phase 11] plugin-data namespace + listing...\n");
    testPluginData(work);

    std::printf("pftest: [phase 18] AES-256-GCM at-rest encryption...\n");
    testEncryption(work);

    std::printf("pftest: [phase 1 3dgs] SplatReader (.splat & .ply)...\n");
    testSplatReader(work);

    std::printf("pftest: generating %llu synthetic points...\n", (unsigned long long)nPoints);
    if (!writeSyntheticXyz(xyz, nPoints)) {
        std::fprintf(stderr, "FAIL: cannot write %s\n", xyz.c_str());
        return 1;
    }

    // Both compressed and uncompressed paths, sequential vs parallel.
    for (bool compress : {false, true}) {
        std::printf("pftest: [compress=%d] sequential convert...\n", (int)compress);
        double tSeq = convert(xyz, seqFile, 1, compress);
        std::printf("pftest: [compress=%d] parallel convert (threads=%d)...\n",
                    (int)compress, threads);
        double tPar = convert(xyz, parFile, threads, compress);
        std::printf("pftest: [compress=%d] sequential %.2fs, parallel %.2fs (%.2fx)\n",
                    (int)compress, tSeq, tPar, tPar > 0 ? tSeq / tPar : 0.0);

        CHECK(filesEqual(seqFile, parFile),
              "vxpc file differs between sequential and parallel");

        pf::OctreeStore store;
        CHECK(store.load(parFile), "OctreeStore failed to load parallel output");

        {
            // Scope the reader: it holds parFile open (Phase 13 ByteSource), so
            // it must be closed before the repack below renames over parFile.
            pf::PackageReader pkg;
            CHECK(pkg.Open(parFile), "PackageReader failed to open vxpc");
            uint64_t payloadSize = pkg.GetSize("octree.bin");
            verifyStructure(store, nPoints, payloadSize);
        }

        // Phase 7: repack the freshly-built package. Close the store first so
        // the .vxpc handle is released (Windows rename-over-open guard).
        store.clear();
        std::printf("pftest: [phase 7] repack (verbatim copy + upsert/remove)...\n");
        testRepack(parFile);

        // Phase 13: stream the repacked package over http:// (once).
        if (!compress) {
            std::printf("pftest: [phase 13] open .vxpc over http (range reads)...\n");
            testHttpStreaming(parFile);
            std::printf("pftest: [phase 10] combine clouds into one multi-cloud .vxpc...\n");
            testMultiCloudPackage(seqFile, parFile, work);
        }

        fs::remove(seqFile, ec);
        fs::remove(parFile, ec);
    }

    if (!keep) fs::remove_all(work, ec);

    if (gFailures == 0) {
        std::printf("pftest: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "pftest: %d FAILURE(S)\n", gFailures);
    return 1;
}
