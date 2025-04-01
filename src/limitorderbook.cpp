#include "../hdr/limitorderbook.h"
#include "../hdr/log.h"
#include "../hdr/tradingstrategy.h"
namespace thu
{
void LimitOrderBook::add_order(NormalOrder order)
{
    // std::cout << "add: " << order.id << ", type: " << static_cast<int>(order.side) << std::endl; 
    // LOG("ORDER: id ", order.id , " TYPE " , static_cast<int>(order.type));

    // Set timestamp if not provided
    if (order.timestamp.count() == 0)
    {
        order.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    }

    // Attempt to match immediately
    if (order.side == Side::Bid)
    {
        do_add(order, m_bids, m_asks);
    }
    else if (order.side == Side::Ask)
    {
        do_add(order, m_asks, m_bids);
    }
}

void LimitOrderBook::cancel_order(NormalOrder order)
{
    // std::cout << "cancel: " << order.id << ", type: " << static_cast<int>(order.side) << std::endl; 
    // LOG("ORDER: id ", order.id , " TYPE " , static_cast<int>(order.type));

    // if already matched
    // cancel remaining or nothing
    if(order.side == Side::Bid)
    {
        do_cancel(order, m_bids);
    }
    if(order.side == Side::Ask)
    {
        do_cancel(order, m_asks);
    }
}

void LimitOrderBook::edit_order(NormalOrder before, NormalOrder after)
{
    // std::cout << "editing: " << _old.id << ", type: " << static_cast<int>(_old.side) << std::endl; 
    // LOG("ORDER: id ", before.id , " TYPE " , static_cast<int>(before.type));

    if(after.timestamp.count()==0)
    {
        after.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        );
    }

    if(after.side == Side::Bid)
    {
        // after editing done then match_order
        do_edit(before, after, m_bids, m_asks);
    }
    if(after.side == Side::Ask)
    {
        // after editing done then match_order
        do_edit(before, after, m_asks, m_bids);
    }
}

void LimitOrderBook::print_book() 
{
    std::lock_guard guard(m_mutex);

    std::cout << "\n------ ORDER BOOK ------\n";
    std::cout << "BIDS:\n";
    for (const auto &[price, orders] : m_bids)
    {
        std::cout << std::setw(6) << price << " | ";
        for (const auto &o : orders)
            std::cout << o.quantity << " ";
        std::cout << "\n";
    }

    std::cout << "\nASKS:\n";
    for (const auto &[price, orders] : m_asks)
    {
        std::cout << std::setw(6) << price << " | ";
        for (const auto &o : orders)
            std::cout << o.quantity << " ";
        std::cout << "\n";
    }
    std::cout << "--------------------------\n\n";

}


void LimitOrderBook::subscribe(IFTradingStrategyListener* listener)
{
    listeners.insert(listener);
}

void LimitOrderBook::notify()
{
    for (auto e : listeners)
    {
        e->getNotice();
    }
}


}