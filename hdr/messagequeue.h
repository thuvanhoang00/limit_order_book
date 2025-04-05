#ifndef MESSAGEQUEUE_H
#define MESSAGEQUEUE_H
#include <queue>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <chrono>
#include "log.h"
namespace thu
{
template<typename T>
class MessageQueue
{
public:
    MessageQueue() : pimpl(std::make_unique<Impl>()) {}
    bool size(size_t& s){
        return pimpl->size(s);
    }
    bool pop(T& e){
        return pimpl->pop(e);
    }
    void push(const T& e){
        pimpl->push(e);
    }
    bool empty(bool& res){
        return pimpl->empty(res);
    }
private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

template<typename T>
class MessageQueue<T>::Impl
{
public:
    Impl() : m_flag(0) {}

    bool size(size_t& s){
        int expected = 0;
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acquire)){
            expected = 0;
            std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        }
        s = m_queue.size();
        m_flag.store(0, std::memory_order_release);
        return true;
    }

    // T pop(){
    //     int expected = m_flag.load();
    //     while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acq_rel));
    //     if(m_queue.empty()) {
    //         LOG("Queue is empty\n");
    //         throw std::runtime_error("Empty queue\n");
    //     }
    //     T res = m_queue.front();
    //     m_queue.pop();
    //     m_flag.store(0, std::memory_order_release);
    //     return res;
    // }
    bool pop(T& e)
    {
        int expected = 0;
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acquire)){
            expected = 0;
            std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        }
        if(m_queue.empty()) {
            LOG("Queue is empty\n");
            return false;
        }
        e = m_queue.front();
        m_queue.pop();
        m_flag.store(0, std::memory_order_release);
        return true;
    }

    void push(const T& e){
        int expected = 0;
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acquire)){
            expected = 0;
            std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        }
        m_queue.push(e);
        m_flag.store(0, std::memory_order_release);
    }

    bool empty(bool& res){
        int expected = 0;
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acquire)){
            expected = 0;
            std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        }
        res = m_queue.empty();
        m_flag.store(0, std::memory_order_release);
        return res;
    }
private:
    std::queue<T> m_queue;
    std::atomic<int> m_flag;
};

}

#endif