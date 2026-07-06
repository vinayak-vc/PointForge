#include "indexer/OctreeIndexer.h"
#include "common/Log.h"

#include <string>
#include <atomic>

#if defined(_WIN32)
  #define PF_CONVERT_API __declspec(dllexport)
#else
  #define PF_CONVERT_API __attribute__((visibility("default")))
#endif

namespace {
    std::atomic<void (*)(const char*)> g_logCallback{nullptr};
}

extern "C" {

PF_CONVERT_API void PF_Convert_SetLogCallback(void (*callback)(const char*)) {
    g_logCallback.store(callback);
    if (callback) {
        pf::setLogSink([](pf::LogLevel, const std::string& line) {
            auto cb = g_logCallback.load();
            if (cb) cb(line.c_str());
        });
    } else {
        pf::setLogSink(nullptr);
    }
}

PF_CONVERT_API int32_t PF_ConvertDataset(
    const char* input, 
    const char* outDir, 
    int32_t gridDepth, 
    float rootSpacing, 
    uint32_t targetLeafSize, 
    int32_t maxDepth, 
    uint64_t flushBudget, 
    int32_t compress
) {
    if (!input || !outDir) return 0;
    
    pf::IndexOptions opts;
    opts.gridDepth = gridDepth;
    opts.rootSpacing = rootSpacing;
    opts.targetLeafSize = targetLeafSize;
    opts.maxDepth = maxDepth;
    opts.flushBudget = flushBudget;
    opts.compress = compress != 0;
    
    pf::logInfo("Starting conversion from API...");
    pf::logInfo(std::string("Input: ") + input);
    pf::logInfo(std::string("Output: ") + outDir);

    if (pf::buildOctree(input, outDir, opts)) {
        pf::logInfo("Conversion finished successfully.");
        return 1;
    } else {
        pf::logError("Conversion failed.");
        return 0;
    }
}

} // extern "C"
