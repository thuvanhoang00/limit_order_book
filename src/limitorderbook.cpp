#include "../hdr/limitorderbook.h"

namespace thu
{



void LimitOrderBook::add_order(Order order)
{
    // Set timestamp if not provided
    if (order.timestamp.count() == 0)
    {
        order.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    }

    // Attempt to match immediately
    if (order.side == Side::Bid)
    {
        process_order(order, bids, asks);
    }
    else if (order.side == Side::Ask)
    {
        process_order(order, asks, bids);
    }
}

void LimitOrderBook::cancel_order(Order order)
{
    // if already matched
    // cancel remaining or nothing
    if(order.side == Side::Bid)
    {
        do_cancel(order, bids);
    }
    if(order.side == Side::Ask)
    {
        do_cancel(order, asks);
    }
}

void LimitOrderBook::print_book() const
{
    std::cout << "\n------ ORDER BOOK ------\n";
    std::cout << "BIDS:\n";
    for (const auto &[price, orders] : bids)
    {
        std::cout << std::setw(6) << price << " | ";
        for (const auto &o : orders)
            std::cout << o.quantity << " ";
        std::cout << "\n";
    }

    std::cout << "\nASKS:\n";
    for (const auto &[price, orders] : asks)
    {
        std::cout << std::setw(6) << price << " | ";
        for (const auto &o : orders)
            std::cout << o.quantity << " ";
        std::cout << "\n";
    }
    std::cout << "--------------------------\n\n";
}
}