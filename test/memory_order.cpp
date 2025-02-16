#include <atomic>
#include <iostream>
#include <thread>
#include <random>
#include <vector>
#include <cassert>
#include <chrono>

using namespace std::chrono_literals;

void test_atomic_relaxed()
{
    // Atomic operations tagged memory_order_relaxed are not synchronization operations;
    // they do not impose an order among concurrent memory accesses.
    // They only guarantee atomicity and modification order consistency.

    std::atomic<int> x{0};
    std::atomic<int> y{0};
    std::jthread t1([&](){
        auto r1 = y.load(std::memory_order_relaxed); // A
        x.fetch_add(r1, std::memory_order_relaxed);  // B
    });

    std::jthread t2([&](){
        auto r2 = x.load(std::memory_order_relaxed); // C
        y.fetch_add(42, std::memory_order_relaxed);  // D
    });
    
    t1.join();
    t2.join();

    // Possible outcomes
    // CDAB: r1=42  , r2=0
    // ABCD: r1=0   , r2=42
    // DABC: r1=42  , r2=42
    // ....
    std::cout << "x: " << x << ", y: " << y << std::endl;
}

void test_seq_cst()
{
    // A load operation with this memory order performs an acquire operation
    // a store performs a release operation
    // and read-modify-write performs both an acquire and release operation, plus a single total order exists
    // in which all threads observe all modifications in the same order

    std::atomic<bool> x{false};
    std::atomic<bool> y{false};
    std::atomic<int>  z{0};
    
    {
        std::jthread write_x([&](){
            // all operations above this "store" will become visible to other threads
            x.store(true, std::memory_order_seq_cst);
        });
        std::jthread write_y([&](){
            // all operations above this "store" will become visible to other threads
            y.store(true, std::memory_order_seq_cst);
        });
        std::jthread read_x_then_y([&](){
            // Any "use" of x will no be moved above of "load"
            while(!x.load(std::memory_order_seq_cst));

            if(y.load(std::memory_order_seq_cst))
            {
                ++z;
            }
        });
        std::jthread read_y_then_x([&](){
            // Any "use" of y will no be moved above of "load"
            while(!y.load(std::memory_order_seq_cst));

            if(x.load(std::memory_order_seq_cst))
            {
                ++z;
            }
        });
    }

    assert(z.load() != 0);
}

int main()
{
    test_atomic_relaxed();
    test_seq_cst();
    return 0;
}