#ifndef SPINLOCK_H
#define SPINLOCK_H
#include <atomic>
#include <time.h>
namespace thu
{
class SpinLock
{
public:
    SpinLock() : flag_(0) {}

    void lock()
    {
        static const timespec ns = {0, 1};
        for (int i = 0; flag_.load(std::memory_order_relaxed) || flag_.exchange(1, std::memory_order_acquire); ++i)
        {
            if (i == 8)
            {
                i = 0;
                nanosleep(&ns, nullptr);
            }
        }
    }

    void unlock()
    {
        flag_.store(0, std::memory_order_release);
    }

private:
    std::atomic<unsigned int> flag_;
};
}

#endif