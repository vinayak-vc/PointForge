#pragma once
// Background job queue for the viewer. Conversion (and, later, any other
// long-running work: batch imports, segmentation, ...) is dispatched here and
// monitored from the Jobs panel / status-bar pill. One worker thread executes
// jobs sequentially — conversion is disk/CPU bound, parallel jobs would fight
// for the same resources. The UI polls job state each frame (all cross-thread
// fields are atomics or mutex-guarded).
#include "indexer/OctreeIndexer.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pf {

struct ConvertJob {
    enum class State { Queued, Running, Succeeded, Failed, Canceled };

    std::string  input;               // source scan file
    std::string  output;              // destination octree directory
    std::string  name;                // display name (input basename)
    IndexOptions opts;                // cancel/progressCb are overwritten by the worker
    bool         loadWhenDone = true; // main thread loads the result on success

    std::atomic<State> state{State::Queued};
    std::atomic<float> progress{0.0f};
    std::atomic<bool>  cancelFlag{false};
    bool               notified = false; // main thread has handled completion (main thread only)

    void setMessage(const std::string& m) { std::lock_guard<std::mutex> lk(mMutex); mMessage = m; }
    std::string message() { std::lock_guard<std::mutex> lk(mMutex); return mMessage; }

    bool finished() const {
        State s = state.load();
        return s == State::Succeeded || s == State::Failed || s == State::Canceled;
    }

private:
    std::mutex  mMutex;
    std::string mMessage;
};

class JobQueue {
public:
    ~JobQueue() { shutdown(); }

    std::shared_ptr<ConvertJob> enqueue(const std::string& input, const std::string& output,
                                        const IndexOptions& opts, bool loadWhenDone) {
        auto job = std::make_shared<ConvertJob>();
        job->input = input;
        job->output = output;
        job->opts = opts;
        job->loadWhenDone = loadWhenDone;
        size_t slash = input.find_last_of("/\\");
        job->name = (slash == std::string::npos) ? input : input.substr(slash + 1);
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mJobs.push_back(job);
            if (!mWorker.joinable()) mWorker = std::thread([this] { workerLoop(); });
        }
        mCv.notify_one();
        return job;
    }

    // Snapshot for UI iteration (shared_ptr copies; job lists are small).
    std::vector<std::shared_ptr<ConvertJob>> jobs() {
        std::lock_guard<std::mutex> lk(mMutex);
        return mJobs;
    }

    // The job to surface in the status-bar pill: running first, else next queued.
    std::shared_ptr<ConvertJob> active() {
        std::lock_guard<std::mutex> lk(mMutex);
        for (auto& j : mJobs)
            if (j->state.load() == ConvertJob::State::Running) return j;
        for (auto& j : mJobs)
            if (j->state.load() == ConvertJob::State::Queued) return j;
        return nullptr;
    }

    size_t unfinishedCount() {
        std::lock_guard<std::mutex> lk(mMutex);
        size_t n = 0;
        for (auto& j : mJobs)
            if (!j->finished()) ++n;
        return n;
    }

    static void cancel(const std::shared_ptr<ConvertJob>& job) {
        job->cancelFlag = true;
        // A queued job can be canceled immediately; a running one stops at the
        // converter's next cancellation checkpoint.
        ConvertJob::State expected = ConvertJob::State::Queued;
        job->state.compare_exchange_strong(expected, ConvertJob::State::Canceled);
    }

    void clearFinished() {
        std::lock_guard<std::mutex> lk(mMutex);
        mJobs.erase(std::remove_if(mJobs.begin(), mJobs.end(),
                                   [](const std::shared_ptr<ConvertJob>& j) { return j->finished(); }),
                    mJobs.end());
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mMutex);
            mStop = true;
            for (auto& j : mJobs) j->cancelFlag = true;
        }
        mCv.notify_all();
        if (mWorker.joinable()) mWorker.join();
    }

private:
    void workerLoop() {
        for (;;) {
            std::shared_ptr<ConvertJob> job;
            {
                std::unique_lock<std::mutex> lk(mMutex);
                mCv.wait(lk, [this] { return mStop || nextQueuedLocked() != nullptr; });
                if (mStop) return;
                job = nextQueuedLocked();
                if (!job) continue;
                job->state = ConvertJob::State::Running;
            }
            job->setMessage("Starting...");
            IndexOptions opts = job->opts;
            opts.cancel = &job->cancelFlag;
            ConvertJob* raw = job.get();
            opts.progressCb = [raw](float pct, const std::string& msg) {
                raw->progress = pct;
                raw->setMessage(msg);
            };
            bool ok = false;
            try {
                ok = buildOctree(job->input, job->output, opts);
            } catch (const std::exception& ex) {
                job->setMessage(std::string("Exception: ") + ex.what());
                ok = false;
            } catch (...) {
                job->setMessage("Unknown exception during conversion");
                ok = false;
            }
            if (job->cancelFlag.load()) {
                job->state = ConvertJob::State::Canceled;
            } else if (ok) {
                job->progress = 1.0f;
                job->setMessage("Done");
                job->state = ConvertJob::State::Succeeded;
            } else {
                job->state = ConvertJob::State::Failed;
            }
        }
    }

    // requires mMutex held
    std::shared_ptr<ConvertJob> nextQueuedLocked() {
        for (auto& j : mJobs)
            if (j->state.load() == ConvertJob::State::Queued) return j;
        return nullptr;
    }

    std::mutex mMutex;
    std::condition_variable mCv;
    std::thread mWorker;
    bool mStop = false;
    std::vector<std::shared_ptr<ConvertJob>> mJobs;
};

} // namespace pf
