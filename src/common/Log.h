#pragma once
#include <string>

namespace pf {

enum class LogLevel { Debug, Info, Warn, Error };

void log(LogLevel level, const std::string& msg);

inline void logDebug(const std::string& m) { log(LogLevel::Debug, m); }
inline void logInfo (const std::string& m) { log(LogLevel::Info,  m); }
inline void logWarn (const std::string& m) { log(LogLevel::Warn,  m); }
inline void logError(const std::string& m) { log(LogLevel::Error, m); }

// Set the minimum level that is actually printed (default Info).
void setLogLevel(LogLevel level);

} // namespace pf
