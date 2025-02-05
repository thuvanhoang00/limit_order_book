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

    // lob.add_order({"B1", Side::Bid, OrderType::Limit, 100.50, 200, {}});
    // lob.add_order({"A1", Side::Ask, OrderType::Limit, 101.00, 100, {}});
    // lob.add_order({"B2", Side::Bid, OrderType::Limit, 100.75, 150, {}});
    // lob.add_order({"A2", Side::Ask, OrderType::Limit, 100.75, 75, {}}); // Should match B2
    // lob.add_order({"M1", Side::Ask, OrderType::Market, 0.00, 50, {}}); // Market order
    // lob.print_book();

    // std::thread t1(&thu::LimitOrderBook::add_order, &lob, Order(std::string("B1"), Side::Bid, OrderType::Limit, 100.50, 200, {}));
    // std::thread t2(&thu::LimitOrderBook::add_order, &lob, Order(std::string("A1"), Side::Ask, OrderType::Limit, 101.00, 100, {}));
    // std::thread t3(&thu::LimitOrderBook::add_order, &lob, Order(std::string("B2"), Side::Bid, OrderType::Limit, 100.75, 150, {}));
    // std::thread t4(&thu::LimitOrderBook::add_order, &lob, Order(std::string("A2"), Side::Ask, OrderType::Limit, 100.75, 75, {}));
    // std::thread t5(&thu::LimitOrderBook::add_order, &lob, Order(std::string("M1"), Side::Ask, OrderType::Market, 0.00, 50, {}));

    // t1.join();
    // t2.join();
    // t3.join();
    // t4.join();
    // t5.join();
    // lob.print_book();

    // std::cout << "-----------------cancel--------------\n";
    // lob.cancel_order({"B2", Side::Bid, OrderType::Limit, 100.75, 150, {}});
    // lob.print_book();
    // std::cout << "-----------------edit--------------\n";
    // lob.edit_order({"A1", Side::Ask, OrderType::Limit, 101.00, 100, {}}, {"A1", Side::Ask, OrderType::Limit, 100.50, 100, {}});
    // lob.print_book();

    return 0;
}