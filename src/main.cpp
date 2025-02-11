#include "../hdr/limitorderbook.h"
#include <thread>
#include <random>
using namespace thu;

int getRand()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(gen);
}

void addOrder_thread(LimitOrderBook &lob)
{

    for (int i = 0; i < 1000; i++)
    {
        if (getRand() == 0)
        {
            lob.add_order({std::to_string(i), Side::Bid, OrderType::Limit, 100.50, 200, {}});
        }
        else
        {
            lob.add_order({std::to_string(i), Side::Ask, OrderType::Limit, 100.50, 200, {}});
        }
    }
}

void editOrder_thread(LimitOrderBook &lob)
{

    for (int i = 0; i < 1000; i++)
    {
        if (getRand() == 0)
        {
            Order _o = {std::to_string(i), Side::Bid, OrderType::Limit, 100.50, 200, {}};
            Order _n = {std::to_string(i), Side::Bid, OrderType::Limit, 100.50, 150, {}};
            lob.edit_order(_o, _n);
        }
        else
        {
            Order _o = {std::to_string(i), Side::Ask, OrderType::Limit, 100.50, 200, {}};
            Order _n = {std::to_string(i), Side::Ask, OrderType::Limit, 100.50, 150, {}};
            lob.edit_order(_o, _n);
        }
    }
}

void cancelOrder_thread(LimitOrderBook &lob)
{

    for (int i = 0; i < 1000; i++)
    {
        if (getRand() == 0)
        {
            lob.cancel_order({std::to_string(i), Side::Bid, OrderType::Limit, 100.50, 200, {}});
        }
        else
        {
            lob.cancel_order({std::to_string(i), Side::Ask, OrderType::Limit, 100.50, 200, {}});
        }
    }
}

int main()
{
    LimitOrderBook lob;

    std::thread t1(addOrder_thread, std::ref(lob));
    std::thread t2(editOrder_thread, std::ref(lob));
    std::thread t3(cancelOrder_thread, std::ref(lob));
    t1.join();
    t2.join();
    t3.join();

    return 0;
}