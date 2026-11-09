#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace viplyr {

// A thread-safe FIFO with a size limit, used between the pipeline threads.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

    // Waits up to `timeout` for free space. `item` is moved from only on success.
    bool push(T &item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = notFull_.wait_for(lock, timeout, [this] {
            return aborted_ || items_.size() < capacity_;
        });
        if (!ready || aborted_) return false;
        items_.push_back(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    // Adds an item even when the queue is full (for control messages).
    void pushAlways(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.push_back(std::move(item));
        notEmpty_.notify_one();
    }

    bool pop(T &item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = notEmpty_.wait_for(lock, timeout, [this] {
            return aborted_ || !items_.empty();
        });
        if (!ready || aborted_) return false;
        item = std::move(items_.front());
        items_.pop_front();
        notFull_.notify_one();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
        notFull_.notify_all();
    }

    // Makes every waiting and future push/pop fail until reset().
    void abort() {
        std::lock_guard<std::mutex> lock(mutex_);
        aborted_ = true;
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
        aborted_ = false;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

private:
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::deque<T> items_;
    bool aborted_ = false;
};

}  // namespace viplyr
