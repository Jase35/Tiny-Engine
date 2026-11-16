#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// A fixed set of persistent threads that all run the same job together.
// Uses only std::thread, so it works on desktop and on ESP-IDF (where std::thread is
// backed by pthreads/FreeRTOS tasks; see the configureThread hook for stack size/pinning).
class WorkerPool{
    public:
    // Called on the constructing thread right before each worker thread is created, with the
    // worker's index (1..threadCount-1). On ESP-IDF, call esp_pthread_set_cfg() here to set
    // the stack size and core affinity of that worker.
    using ConfigureThread = void (*)(unsigned workerIndex);

    // threadCount includes the calling thread, which acts as worker 0. Values < 1 mean 1.
    explicit WorkerPool(unsigned threadCount, ConfigureThread configureThread = nullptr);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    unsigned size() const { return static_cast<unsigned>(threads.size()) + 1; }

    // Runs job(workerIndex) once on every worker, with the caller as worker 0, and returns
    // once all of them have finished. Doesn't allocate, so it's safe to call every frame.
    template<typename Job>
    void run(const Job& job){
        dispatch([](const void* context, unsigned workerIndex){
            (*static_cast<const Job*>(context))(workerIndex);
        }, &job);
    }

    private:
    using JobFunction = void (*)(const void* context, unsigned workerIndex);

    void dispatch(JobFunction function, const void* context);
    void workerLoop(unsigned workerIndex);

    std::vector<std::thread> threads;
    std::mutex mutex;
    std::condition_variable startSignal;
    std::condition_variable doneSignal;
    JobFunction jobFunction = nullptr;
    const void* jobContext = nullptr;
    uint32_t generation = 0; // Bumped for every job so workers can tell a new one arrived.
    unsigned pending = 0;    // Workers (excluding the caller) still running the current job.
    bool stopping = false;
};
