#include "../hdr/limitorderbook.h"
using namespace thu;
int main()
{
    LimitOrderBook lob;

    // Test orders
    lob.add_order({"B1", Side::Bid, OrderType::Limit, 100.50, 200, {}});
    lob.add_order({"A1", Side::Ask, OrderType::Limit, 101.00, 100, {}});
    lob.add_order({"B2", Side::Bid, OrderType::Limit, 100.75, 150, {}});
    lob.add_order({"A2", Side::Ask, OrderType::Limit, 100.75, 75, {}}); // Should match B2
    lob.add_order({"M1", Side::Ask, OrderType::Market, 0.00, 50, {}}); // Market order

    lob.print_book();
    return 0;
}