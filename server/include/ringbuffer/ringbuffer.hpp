#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstddef>

namespace server
{

    template<typename T>
    class RingBuffer {
    public:
        explicit RingBuffer(std::size_t capacity)
            : buffer_(capacity)
            , capacity_(capacity)
            , head_(0)
            , tail_(0)
            , count_(0)
            , stopped_(false)
        {}

        // Non-copyable
        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        bool try_push(const T& item)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count_ == capacity_) return false;
            buffer_[tail_] = item;
            tail_ = (tail_ + 1) % capacity_;
            ++count_;
            cv_.notify_one();
            return true;
        }

        std::vector<T> drain()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return count_ > 0 || stopped_; });

            std::vector<T> result;
            result.reserve(count_);
            while (count_ > 0)
            {
                result.push_back(buffer_[head_]);
                head_ = (head_ + 1) % capacity_;
                --count_;
            }
            return result;
        }
        // drain_nowait — aquire all without waiting (final rest after stop())
        std::vector<T> drain_nowait()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::vector<T> result;
            result.reserve(count_);
            while (count_ > 0)
            {
                result.push_back(buffer_[head_]);
                head_ = (head_ + 1) % capacity_;
                --count_;
            }
            return result;
        }

        // notify consumer about shutdown
        void stop()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
            cv_.notify_all();
        }

        std::size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return count_;
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return count_ == 0;
        }

    private:
        std::vector<T>          buffer_;
        const std::size_t       capacity_;
        std::size_t             head_;
        std::size_t             tail_;
        std::size_t             count_;
        bool                    stopped_;
        mutable std::mutex      mutex_;
        std::condition_variable cv_;
    };

}

#endif
