#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstddef>

namespace server
{
    namespace sctl
    {

        template<typename T>
        class ringbuffer
        {
            public:
                explicit ringbuffer(/*std::*/size_t capacity)
                :
                _buffer(capacity)
                ,_capacity(capacity)
                ,_head(0)
                ,_tail(0)
                ,_count(0)
                ,_stopped(false)
                {
                }
                // Non-copyable
                ringbuffer(const ringbuffer&) = delete;
                ringbuffer& operator=(const ringbuffer&) = delete;

                bool try_push(const T& item)
                {
                    std::lock_guard<std::mutex> lock(_mutex);

                    if (_count == _capacity) return false;
                    _buffer[_tail] = item;
                    _tail = (_tail + 1) % _capacity;
                    ++_count;
                    _cv.notify_one();
                    return true;
                }
                std::vector<T> drain()
                {
                    std::unique_lock<std::mutex> lock(_mutex);
                    _cv.wait(lock, [this] { return _count > 0 || _stopped; });

                    std::vector<T> result;

                    result.reserve(_count);
                    while (_count > 0) {
                        result.push_back(_buffer[_head]);
                        _head = (_head + 1) % _capacity;
                        --_count;
                    }

                    return result;
                }
                // drain_nowait — aquire all without waiting (final rest after stop())
                std::vector<T> drain_nowait()
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    std::vector<T> result;
                    result.reserve(_count);
                    while (_count > 0) {
                        result.push_back(_buffer[_head]);
                        _head = (_head + 1) % _capacity;
                        --_count;
                    }
                    return result;
                }
                // notify consumer about shutdown
                void stop()
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _stopped = true;
                    _cv.notify_all();
                }
                std::size_t size() const
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    return _count;
                }
                bool empty() const
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    return _count == 0;
                }
            private:
                const size_t        _capacity;
                std::vector<T>      _buffer;
                size_t              _head;
                size_t              _tail;
                size_t              _count;
                bool                _stopped;
                mutable std::mutex  _mutex;
                std::condition_variable _cv;
        };
    }
}

#endif
