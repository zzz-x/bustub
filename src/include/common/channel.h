#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <mutex>
#include <queue>
#include <condition_variable>

namespace bustub
{

//multiple producer,multiple consumer
template<class T>
class Channel
{
public:
    Channel()=default;
    ~Channel()=default;

    void put(T element)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_q_.push(std::move(element));
        lock.unlock();
        m_cv.notify_all();
    }

    auto get()->T
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_cv.wait(lock,[&](){return !m_q_.empty();});
        T element = std::move(m_q_.front());
        m_q_.pop();
        return element;
    }

private:
    std::condition_variable m_cv;
    std::mutex m_lock;
    std::queue<T> m_q_;
}

};



#endif