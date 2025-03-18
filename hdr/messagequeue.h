#ifndef MESSAGEQUEUE_H
#define MESSAGEQUEUE_H
#include <queue>
#include <atomic>
#include <memory>
#include <stdexcept>
#include "log.h"
namespace thu
{
template<typename T>
class MessageQueue
{
public:
    MessageQueue() : pimpl(std::make_unique<Impl>()) {}
    size_t size(){return pimpl->size();}
    T pop(){return pimpl->pop();}
    void push(const T& e){pimpl->push(e);}
    bool empty(){return pimpl->empty();}
private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

template<typename T>
class MessageQueue<T>::Impl
{
public:
    Impl() : m_flag(0) {}

    size_t size(){
        int expected = m_flag.load();
        // spin
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acquire));
        size_t res = m_queue.size();
        m_flag.store(0, std::memory_order_release);
        return res;
    }

    T pop(){
        int expected = m_flag.load();
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acq_rel));
        if(m_queue.empty()) {
            LOG("Queue is empty\n");
            throw std::runtime_error("Empty queue\n");
        }
        T res = m_queue.front();
        m_queue.pop();
        m_flag.store(0, std::memory_order_release);
        return res;
    }

    void push(const T& e){
        int expected = m_flag.load();
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acquire));
        m_queue.push(e);
        m_flag.store(0, std::memory_order_release);
    }

    bool empty(){
        int expected = m_flag.load();  
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_release));
        bool res = m_queue.empty();
        m_flag.store(0, std::memory_order_release);
        return res;
    }
private:
    std::queue<T> m_queue;
    std::atomic<int> m_flag;
};

}

#endif