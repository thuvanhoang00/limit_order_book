#include "../hdr/limitorderbook.h"
#include <thread>

using namespace thu;
int main()
{
    LimitOrderBook lob;

    // Test orders
    // lob.add_order({"B1", Side::Bid, OrderType::Limit, 100.50, 200, {}});
    // lob.add_order({"A1", Side::Ask, OrderType::Limit, 101.00, 100, {}});
    // lob.add_order({"B2", Side::Bid, OrderType::Limit, 100.75, 150, {}});
    // lob.add_order({"A2", Side::Ask, OrderType::Limit, 100.75, 75, {}}); // Should match B2
    // lob.add_order({"M1", Side::Ask, OrderType::Market, 0.00, 50, {}}); // Market order

    // lob.print_book();
    /*
    TRADE: SOLD 75 @ $100.75 (Remaining: 0)
    TRADE: SOLD 50 @ $100.75 (Remaining: 0)

------ ORDER BOOK ------
BIDS:
100.75 | 25
 100.5 | 200

ASKS:
   101 | 100
--------------------------
    */
    // Order order1 *()"B1", Side::Bid, OrderType::Limit, 100.50, 200, {}};
    std::thread t1(&thu::LimitOrderBook::add_order, &lob, Order(std::string("B1"), Side::Bid, OrderType::Limit, 100.50, 200, {}));
    std::thread t2(&thu::LimitOrderBook::add_order, &lob, Order(std::string("A1"), Side::Ask, OrderType::Limit, 101.00, 100, {}));
    std::thread t3(&thu::LimitOrderBook::add_order, &lob, Order(std::string("B2"), Side::Bid, OrderType::Limit, 100.75, 150, {}));
    std::thread t4(&thu::LimitOrderBook::add_order, &lob, Order(std::string("A2"), Side::Ask, OrderType::Limit, 100.75, 75, {}));
    std::thread t5(&thu::LimitOrderBook::add_order, &lob, Order(std::string("M1"), Side::Ask, OrderType::Market, 0.00, 50, {}));

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    lob.print_book();

/*  RACE CONDITION HERE

TRADE: SOLD 50 @ $100.5 (Remaining: 0)

------ ORDER BOOK ------
BIDS:
100.75 | 150 
 100.5 | 150 

ASKS:
100.75 | 75 
   101 | 100 
--------------------------
*/


//-------------------------------------------------------------
    // std::cout << "-----------------cancel--------------\n";
    // lob.cancel_order({"B2", Side::Bid, OrderType::Limit, 100.75, 150, {}});
    // lob.print_book();
    return 0;
}