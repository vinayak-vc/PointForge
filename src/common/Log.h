#pragma once
#include <functional>
#include <string>

namespace pf {

enum class LogLevel { Debug, Info, Warn, Error };

void log(LogLevel level, const std::string& msg);

// Optional secondary sink: receives every message regardless of the minimum
// print level (the consumer does its own filtering). Used by the viewer's
// Console panel. May be called from any thread; the callback must be
// thread-safe and fast.
using LogSink = std::function<void(LogLevel, const std::string&)>;
void setLogSink(LogSink sink);

inline void logDebug(const std::string& m) { log(LogLevel::Debug, m); }
inline void logInfo (const std::string& m) { log(LogLevel::Info,  m); }
inline void logWarn (const std::string& m) { log(LogLevel::Warn,  m); }
inline void logError(const std::string& m) { log(LogLevel::Error, m); }

// Set the minimum level that is actually printed (default Info).
void setLogLevel(LogLevel level);

} // namespace pf
