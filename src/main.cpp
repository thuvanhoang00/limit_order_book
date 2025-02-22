#include "../hdr/limitorderbook.h"
#include "../hdr/tradingstrategy.h"
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
            auto o = NormalOrderBuilder()
                          .setSecurityId(std::to_string(i))
                          .setSide(Side::Bid)
                          .setOrderType(OrderType::Limit)
                          .setPrice(100.50)
                          .setQuantity(200)
                          .setTimestamp({})
                          .build();

            lob.add_order(o);
        }
        else
        {
            auto o = NormalOrderBuilder()
                          .setSecurityId(std::to_string(i))
                          .setSide(Side::Ask)
                          .setOrderType(OrderType::Limit)
                          .setPrice(100.50)
                          .setQuantity(200)
                          .setTimestamp({})
                          .build();
                          
            lob.add_order(o);
        }
    }
}

void editOrder_thread(LimitOrderBook &lob)
{

    for (int i = 0; i < 1000; i++)
    {
        if (getRand() == 0)
        {
            auto _o = NormalOrderBuilder()
                           .setSecurityId(std::to_string(i))
                           .setSide(Side::Bid)
                           .setOrderType(OrderType::Limit)
                           .setPrice(100.50)
                           .setQuantity(200)
                           .setTimestamp({})
                           .build();

            auto _n = NormalOrderBuilder()
                           .setSecurityId(std::to_string(i))
                           .setSide(Side::Bid)
                           .setOrderType(OrderType::Limit)
                           .setPrice(100.50)
                           .setQuantity(150)
                           .setTimestamp({})
                           .build();
            lob.edit_order(_o, _n);
        }
        else
        {
            auto _o = NormalOrderBuilder()
                           .setSecurityId(std::to_string(i))
                           .setSide(Side::Ask)
                           .setOrderType(OrderType::Limit)
                           .setPrice(100.50)
                           .setQuantity(200)
                           .setTimestamp({})
                           .build();

            auto _n = NormalOrderBuilder()
                           .setSecurityId(std::to_string(i))
                           .setSide(Side::Ask)
                           .setOrderType(OrderType::Limit)
                           .setPrice(100.50)
                           .setQuantity(150)
                           .setTimestamp({})
                           .build();
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
            auto o = NormalOrderBuilder()
                          .setSecurityId(std::to_string(i))
                          .setSide(Side::Bid)
                          .setOrderType(OrderType::Limit)
                          .setPrice(100.50)
                          .setQuantity(200)
                          .setTimestamp({})
                          .build();

            lob.cancel_order(o);
        }
        else
        {
            auto o = NormalOrderBuilder()
                          .setSecurityId(std::to_string(i))
                          .setSide(Side::Ask)
                          .setOrderType(OrderType::Limit)
                          .setPrice(100.50)
                          .setQuantity(200)
                          .setTimestamp({})
                          .build();

            lob.cancel_order(o);
        }
    }
}

int main()
{
    LimitOrderBook lob;
    ConcreteTradingStrategyListener lis1(&lob);

    std::thread t1(addOrder_thread, std::ref(lob));
    std::thread t2(editOrder_thread, std::ref(lob));
    std::thread t3(cancelOrder_thread, std::ref(lob));
    t1.join();
    t2.join();
    t3.join();

    lob.print_book();

    return 0;
}