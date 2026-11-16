#include "worker_pool.hpp"

WorkerPool::WorkerPool(unsigned threadCount, ConfigureThread configureThread){
    if(threadCount < 1){
        threadCount = 1;
    }
    threads.reserve(threadCount - 1);
    for(unsigned i = 1; i < threadCount; i++){
        if(configureThread){
            configureThread(i);
        }
        threads.emplace_back(&WorkerPool::workerLoop, this, i);
    }
}

WorkerPool::~WorkerPool(){
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    startSignal.notify_all();
    for(std::thread& thread : threads){
        thread.join();
    }
}

void WorkerPool::dispatch(JobFunction function, const void* context){
    if(threads.empty()){
        function(context, 0);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        jobFunction = function;
        jobContext = context;
        pending = static_cast<unsigned>(threads.size());
        generation++;
    }
    startSignal.notify_all();

    function(context, 0);

    std::unique_lock<std::mutex> lock(mutex);
    doneSignal.wait(lock, [this]{ return pending == 0; });
}

void WorkerPool::workerLoop(unsigned workerIndex){
    uint32_t seenGeneration = 0;
    std::unique_lock<std::mutex> lock(mutex);
    for(;;){
        startSignal.wait(lock, [&]{ return stopping || generation != seenGeneration; });
        if(stopping){
            return;
        }
        seenGeneration = generation;
        const JobFunction function = jobFunction;
        const void* context = jobContext;

        lock.unlock();
        function(context, workerIndex);
        lock.lock();

        if(--pending == 0){
            doneSignal.notify_one();
        }
    }
}
