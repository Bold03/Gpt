#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace copilot {

template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(std::size_t maxSize = 256) : maxSize_(maxSize) {}

    bool push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= maxSize_) return false;
        queue_.push(std::move(value));
        return true;
    }

    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        queue_.swap(empty);
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    std::size_t maxSize_;
};

} // namespace copilot
