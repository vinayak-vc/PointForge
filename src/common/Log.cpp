#include "common/Log.h"
#include <cstdio>
#include <mutex>

namespace pf {
namespace {
    LogLevel g_minLevel = LogLevel::Info;
    std::mutex g_mutex;

    const char* prefix(LogLevel l) {
        switch (l) {
            case LogLevel::Debug: return "[debug] ";
            case LogLevel::Info:  return "[info ] ";
            case LogLevel::Warn:  return "[warn ] ";
            case LogLevel::Error: return "[error] ";
        }
        return "";
    }
}

void setLogLevel(LogLevel level) { g_minLevel = level; }

void log(LogLevel level, const std::string& msg) {
    if (static_cast<int>(level) < static_cast<int>(g_minLevel)) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    FILE* out = (level == LogLevel::Error || level == LogLevel::Warn) ? stderr : stdout;
    std::fprintf(out, "%s%s\n", prefix(level), msg.c_str());
    std::fflush(out);
}

} // namespace pf
