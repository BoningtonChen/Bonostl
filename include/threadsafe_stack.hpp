#pragma once

#include "bonostlpch.h"

namespace Bonostl
{
    template<typename T>
    class threadsafe_stack
    {
    public:
        threadsafe_stack() = default;

        threadsafe_stack(const threadsafe_stack& other)
        {
            std::lock_guard lock(other.mutex_);
            data_ = other.data_;
        }

        threadsafe_stack(threadsafe_stack&& other) noexcept
            : data_(std::move(other.data_))
        {
        }

        threadsafe_stack& operator=(const threadsafe_stack&) = delete;
        threadsafe_stack& operator=(threadsafe_stack&&) = delete;

        void push(T new_value)
        {
            std::lock_guard lock(mutex_);
            data_.push(std::move(new_value));
        }

        std::optional<T> pop()
        {
            std::lock_guard lock(mutex_);

            if (data_.empty())
            {
                return std::nullopt;
            }

            std::optional<T> result = std::move(data_.top());
            data_.pop();
            return result;
        }

        bool try_pop(T& value)
        {
            std::lock_guard lock(mutex_);

            if (data_.empty())
            {
                return false;
            }

            value = std::move(data_.top());
            data_.pop();
            return true;
        }

        bool empty() const
        {
            std::lock_guard lock(mutex_);
            return data_.empty();
        }

    private:
        std::stack<T> data_;
        mutable std::mutex mutex_;
    };
}
