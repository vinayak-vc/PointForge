// pfconvert — PointForge importer CLI.
// Converts a point cloud (LAS/LAZ, E57, PLY, PTS/XYZ) into a streamable octree.
#include "indexer/OctreeIndexer.h"
#include "io/PackageFormat.h"
#include "common/Log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

using namespace pf;

static void usage() {
    std::printf(
        "PointForge importer\n"
        "Usage: pfconvert <input> --out <dir> [options]\n\n"
        "  --out <dir>           output octree directory (required)\n"
        "  --chunk-depth <n>     coarse grid depth L; grid is (2^L)^3 (default 4)\n"
        "  --spacing <meters>    root sample spacing; 0 = auto (default 0)\n"
        "  --leaf <n>            target max points per leaf node (default 50000)\n"
        "  --max-depth <n>       hard octree depth cap (default 24)\n"
        "  --flush <points>      chunker memory budget in points (default 16M)\n"
        "  --keep-chunks         keep intermediate chunk files (debug)\n"
        "  --compress            zstd-compress each node payload in octree.bin\n"
        "  --threads <n>         indexer worker threads; 0 = auto (default 0)\n"
        "  --thumbnail <path>    path to thumbnail image (default: auto-generate)\n"
        "  --verbose             debug logging\n\n"
        "Combine mode (multi-cloud package):\n"
        "  pfconvert --combine --out <scene.vxpc> <a.vxpc> <b.vxpc> [...]\n"
        "      Pack >=2 single-cloud .vxpc files into one multi-cloud package.\n\n"
        "Supported inputs: .las .laz .e57 .ply .pts .xyz .txt .csv\n");
}

// pfconvert --combine --out <scene.vxpc> <a.vxpc> <b.vxpc> ...
static int runCombine(int argc, char** argv) {
    std::string outDir;
    std::vector<std::pair<std::string, std::string>> sources;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out") {
            if (i + 1 >= argc) { logError("missing value for --out"); return 1; }
            outDir = argv[++i];
        } else if (a == "--verbose") {
            setLogLevel(LogLevel::Debug);
        } else if (a.rfind("--", 0) == 0) {
            logError("unknown argument in --combine mode: " + a); return 1;
        } else {
            sources.push_back({a, ""});   // display name defaults to file stem
        }
    }
    if (outDir.empty()) { logError("--combine requires --out <scene.vxpc>"); return 1; }
    if (sources.size() < 2) { logError("--combine needs at least 2 source .vxpc files"); return 1; }
    if (outDir.length() < 5 || outDir.substr(outDir.length() - 5) != ".vxpc") outDir += ".vxpc";

    logInfo("pfconvert --combine: " + std::to_string(sources.size()) + " clouds -> " + outDir);
    if (!combineClouds(outDir, sources)) { logError("pfconvert: combine failed"); return 2; }
    logInfo("pfconvert: done");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    if (std::string(argv[1]) == "--combine") return runCombine(argc, argv);

    std::string input = argv[1];
    std::string outDir;
    IndexOptions opts;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { logError(std::string("missing value for ") + name); std::exit(1); }
            return argv[++i];
        };
        if      (a == "--out")          outDir = next("--out");
        else if (a == "--chunk-depth")  opts.gridDepth = std::atoi(next("--chunk-depth"));
        else if (a == "--spacing")      opts.rootSpacing = std::atof(next("--spacing"));
        else if (a == "--leaf")         opts.targetLeafSize = (uint32_t)std::strtoul(next("--leaf"), nullptr, 10);
        else if (a == "--max-depth")    opts.maxDepth = std::atoi(next("--max-depth"));
        else if (a == "--flush")        opts.flushBudget = std::strtoull(next("--flush"), nullptr, 10);
        else if (a == "--keep-chunks")  opts.keepChunks = true;
        else if (a == "--compress")     opts.compress = true;
        else if (a == "--threads")      opts.threads = std::atoi(next("--threads"));
        else if (a == "--thumbnail")    opts.thumbnailPath = next("--thumbnail");
        else if (a == "--verbose")      setLogLevel(LogLevel::Debug);
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { logError("unknown argument: " + a); usage(); return 1; }
    }

    if (outDir.empty()) { logError("--out is required"); usage(); return 1; }
    if (opts.gridDepth < 0 || opts.gridDepth > 10) { logError("--chunk-depth must be 0..10"); return 1; }

    if (outDir.length() < 5 || outDir.substr(outDir.length() - 5) != ".vxpc") {
        if (outDir.back() != '/' && outDir.back() != '\\') outDir += "/";
        outDir += "scan.vxpc";
    }

    logInfo("pfconvert: " + input + " -> " + outDir);
    if (!buildOctree(input, outDir, opts)) {
        logError("pfconvert: conversion failed");
        return 2;
    }
    logInfo("pfconvert: done");
    return 0;
}
