#pragma once
// In-app log capture: pf::log() messages are mirrored into this ring buffer
// (via pf::setLogSink) so the viewer's Console panel can display them.
// push() may be called from any thread (converter worker, streaming loader).
#include "common/Log.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

namespace pf {

struct UiLogEntry {
    LogLevel    level;
    std::string text;
};

class UiLogBuffer {
public:
    static UiLogBuffer& instance() {
        static UiLogBuffer b;
        return b;
    }

    void push(LogLevel level, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mMutex);
        mEntries.push_back({level, msg});
        if (mEntries.size() > kMax) mEntries.pop_front();
        if (level == LogLevel::Error || level == LogLevel::Warn) ++unseenIssues;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mMutex);
        mEntries.clear();
    }

    // UI access pattern: lock mutex(), iterate entries(), unlock.
    std::mutex& mutex() { return mMutex; }
    const std::deque<UiLogEntry>& entries() const { return mEntries; }

    // Warnings/errors accumulated since the Console was last visible; the
    // Window-menu badge shows this and the Console resets it when drawn.
    std::atomic<int> unseenIssues{0};

private:
    static constexpr size_t kMax = 4000;
    std::mutex mMutex;
    std::deque<UiLogEntry> mEntries;
};

} // namespace pf
