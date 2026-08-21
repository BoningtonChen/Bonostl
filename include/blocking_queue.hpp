#pragma once

#include "bonostlpch.h"

#include <stdexcept>

namespace Bonostl
{
    /// Bounded blocking queue for producer-consumer backpressure.
    /// push/wait_and_pop block while the queue is full/empty, unlike the
    /// unbounded threadsafe_queue which never blocks on push.
    template<typename T>
    class blocking_queue
    {
    public:
        explicit blocking_queue(std::size_t capacity)
            : capacity_(capacity)
        {
            if (capacity == 0)
            {
                throw std::invalid_argument("blocking_queue capacity must be non-zero");
            }
        }

        blocking_queue(const blocking_queue&) = delete;
        blocking_queue& operator=(const blocking_queue&) = delete;

        void push(T value)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_full_.wait(lock, [&] { return data_.size() < capacity_; });

            data_.push_back(std::move(value));

            lock.unlock();
            not_empty_.notify_one();
        }

        T wait_and_pop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_.wait(lock, [&] { return !data_.empty(); });

            T value = std::move(data_.front());
            data_.pop_front();

            lock.unlock();
            not_full_.notify_one();
            return value;
        }

        bool try_push(T value)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (data_.size() >= capacity_)
                {
                    return false;
                }
                data_.push_back(std::move(value));
            }
            not_empty_.notify_one();
            return true;
        }

        std::optional<T> try_pop()
        {
            std::optional<T> value;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (data_.empty())
                {
                    return std::nullopt;
                }
                value = std::move(data_.front());
                data_.pop_front();
            }
            not_full_.notify_one();
            return value;
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return data_.empty();
        }

        std::size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return data_.size();
        }

        std::size_t capacity() const noexcept
        {
            return capacity_;
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable not_full_;
        std::condition_variable not_empty_;
        std::deque<T> data_;
        std::size_t capacity_;
    };
}