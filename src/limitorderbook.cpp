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
        do_add(order, bids, asks);
    }
    else if (order.side == Side::Ask)
    {
        do_add(order, asks, bids);
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

void LimitOrderBook::edit_order(Order _old, Order _new)
{
    std::cout << "editing: " << _old.id << ", type: " << static_cast<int>(_old.side) << std::endl; 
    if(_new.timestamp.count()==0)
    {
        _new.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        );
    }

    if(_new.side == Side::Bid)
    {
        // after editing done then match_order
        do_edit(_old, _new, bids, asks);
    }
    if(_new.side == Side::Ask)
    {
        // after editing done then match_order
        do_edit(_old, _new, asks, bids);
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