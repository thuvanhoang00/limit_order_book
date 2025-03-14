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

void test_consume_release()
{
    std::atomic<std::string *> ptr{};
    int data;
    std::string *p{};
    std::jthread producer([&](){
        p = new std::string("Hello");
        data = 42;
        ptr.store(p, std::memory_order_release); 
    });

    std::jthread consumer([&](){
        std::string* p2{};
        while(!(p2 = ptr.load(std::memory_order_consume)));

        assert(*p == "Hello"); // always true
        assert(*p2 == "Hello"); // always true: *p2 carries dependency from ptr
        if(data != 42) std::cout << "data != 42\n";
        assert(data == 42); // may and may not be true: data does not carry dependency from ptr
        
        delete p2;
    });
}

void test_acquire_release()
{
    std::atomic<std::string*> ptr{};
    int data;
    std::string* p{};
    std::jthread producer([&](){
        p = new std::string("hello");
        data = 42;
        ptr.store(p, std::memory_order_release);
    });

    std::jthread consumer([&](){
        std::string* p2{};
        while(!(p2 = ptr.load(std::memory_order_acquire)));

        assert(*p2 == "hello"); // always true
        assert(data == 42); // always true
        assert(*p2 == "hello"); // always truee

        delete p2;
    });
}

int main()
{
    // test_atomic_relaxed();
    // test_seq_cst();
    test_consume_release();
    test_acquire_release();
    return 0;
}