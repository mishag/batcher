#include <deque>
#include <mutex>
#include <vector>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <iostream>

using UL = std::unique_lock<std::mutex>;

class UnlockGuard {
private:
    std::mutex& d_mutex;

public:
    explicit UnlockGuard(std::mutex& m)
        : d_mutex(m)
    {
        d_mutex.unlock();
    }

    ~UnlockGuard()
    {
        d_mutex.lock();
    }

    UnlockGuard(const UnlockGuard&) = delete;
    UnlockGuard& operator=(const UnlockGuard&) = delete;
};


// =============
// class Batcher
// =============

template <typename T>
class Batcher {
private:
    enum {
        ST_STARTED,
        ST_STOPPING,
        ST_STOPPED
    };

    std::deque<T>                 d_queue;
    size_t                        d_batchSize;
    size_t                        d_maxQueueSize;
    size_t                        d_waitTime;
    mutable std::mutex            d_mutex;
    std::condition_variable       d_cv;
    int                           d_state;
    std::shared_ptr<std::thread>  d_thread;

private:
    virtual void processBatch(const std::vector<T>& items) = 0;
    void processItems(size_t numItems);
    void run();
    bool shouldStop() const;

public:
    Batcher(size_t maxQueueSize, size_t batchSize, size_t waitTime);
    virtual ~Batcher();

    Batcher(const Batcher&) = delete;
    Batcher& operator=(const Batcher&) = delete;

    int  start();
    void stop();
    int  submit(const T& item);

    size_t size()  const;
    bool   empty() const;
};

// ======================
// Batcher Implementation
// ======================

template <typename T>
Batcher<T>::Batcher(size_t maxQueueSize, size_t batchSize, size_t waitTime)
    : d_queue()
    , d_batchSize(batchSize)
    , d_maxQueueSize(maxQueueSize)
    , d_waitTime(waitTime)
    , d_mutex()
    , d_cv()
    , d_state(ST_STOPPED)
    , d_thread()
{
}

template <typename T>
Batcher<T>::~Batcher()
{
}


template <typename T>
void Batcher<T>::processItems(size_t numItems)
{
    std::vector<T> items;

    {
        UL lock(d_mutex);

        if (d_queue.empty() || !numItems) {
            return;
        }

        numItems = std::min(numItems, d_queue.size());

        for (int i = 0; i < numItems; ++i) {
            items.push_back(d_queue.front());
            d_queue.pop_front();
        }
    }

    processBatch(items);
}

template <typename T>
void Batcher<T>::run()
{
    while (!shouldStop()) {

        UL lock(d_mutex);

        while (d_state == ST_STARTED && d_queue.size() < d_batchSize) {
            d_cv.wait_for(lock, std::chrono::seconds(d_waitTime));

            {
                UnlockGuard guard(d_mutex);
                processItems(d_batchSize);
            }
        }

        {
            UnlockGuard guard(d_mutex);
            processItems(d_batchSize);
        }
    }
}

template <typename T>
int Batcher<T>::start()
{
    UL lock(d_mutex);

    if (d_state == ST_STARTED) {
        return 0;
    }

    if (d_state == ST_STOPPING) {
        std::cerr << "Cannot start batcher while it is stopping.\n";
        return 1;
    }

    d_thread.reset(new std::thread(&Batcher<T>::run, this));
    d_state = ST_STARTED;

    return 0;
}

template <typename T>
void Batcher<T>::stop()
{
    {
        UL lock(d_mutex);

        if (d_state == ST_STOPPED || d_state == ST_STOPPING) {
            return;
        }

        d_state = ST_STOPPING;
    }

    d_cv.notify_one();
    d_thread->join();

    d_state = ST_STOPPED;
}

template <typename T>
int Batcher<T>::submit(const T& item)
{
    UL lock(d_mutex);

    if (d_queue.size() >= d_maxQueueSize) {
        std::cerr << "Failed to submit item. Reached max queue size.\n";
        d_cv.notify_one();
        return 1;
    }

    std::cout << "Submitted item\n";
    d_queue.push_back(item);

    if (d_queue.size() >= d_batchSize) {
        d_cv.notify_one();
    }

    return 0;
}

template <typename T>
size_t Batcher<T>::size() const
{
    UL lock(d_mutex);
    return d_queue.size();
}

template <typename T>
bool Batcher<T>::empty() const
{
    UL lock(d_mutex);
    return d_queue.empty();
}

template <typename T>
bool Batcher<T>::shouldStop() const
{
    UL lock(d_mutex);
    return d_state == ST_STOPPING || d_state == ST_STOPPED;
}
