#include "../hdr/limitorderbook.h"
#include "../hdr/log.h"
#include "../hdr/tradingstrategy.h"
namespace thu
{
const int DEPTH = 10;  // Price levels to display
const int BAR_WIDTH = 40;  // Width of depth bars
    
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

// void clear_screen() {
//     #ifdef _WIN32
//     system("cls");
//     #else
//     system("clear");
//     #endif
// }

// int LimitOrderBook::get_max_quantity() 
// {
//     int max_qty = 0;
//     for (const auto& [p, q] : m_bids) max_qty = std::max(max_qty, q);
//     for (const auto& [p, q] : m_asks) max_qty = std::max(max_qty, q);
//     return std::max(max_qty, 1);
// }

// void LimitOrderBook::print_price_level(double price, int qty, bool is_bid) 
// {
//     std::string bar(qty * BAR_WIDTH / get_max_quantity(), '█');
//     std::cout << std::setw(8) << std::fixed << std::setprecision(2) << price 
//               << " | " << std::setw(6) << qty
//               << " " << (is_bid ? "[" : "]") << bar 
//               << (is_bid ? "]" : "[") << "\n";
// }

void LimitOrderBook::print_book() 
{
    // std::lock_guard guard(m_mutex);

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
#include <iomanip>
#include <algorithm>
#include <cmath>

void LimitOrderBook::print_book2() {
    std::lock_guard guard(m_mutex);

    const int MAX_LEVELS = 5;  // Show top 5 bids/asks
    const int BAR_WIDTH = 20;  // Width for depth visualization

    // ANSI color codes
    const std::string GREEN = "\033[32m";
    const std::string RED = "\033[31m";
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";

    // Calculate max quantity for scaling
    unsigned int max_qty = 0;
    for (const auto& [price, orders] : m_bids) {
        unsigned int total = 0;
        for (const auto& o : orders) total += o.quantity.get();
        max_qty = std::max(max_qty, total);
    }
    for (const auto& [price, orders] : m_asks) {
        unsigned int total = 0;
        for (const auto& o : orders) total += o.quantity.get();
        max_qty = std::max(max_qty, total);
    }

    auto print_level = [&](double price, unsigned int total_qty, bool is_bid) {
        std::stringstream line;
        line << std::fixed << std::setprecision(2) << std::setw(8) << price << " ";
        
        // Depth bar
        int bar_length = max_qty ? (total_qty * BAR_WIDTH) / max_qty : 0;
        std::string bar(bar_length, '█');
        
        line << std::setw(6) << total_qty << " "
             << (is_bid ? GREEN : RED)
             << (is_bid ? "▌" : "▐")
             << std::string(BAR_WIDTH - bar_length, ' ')
             << bar << RESET;
        
        return line.str();
    };

    // Header
    std::cout << "\n" << BOLD << "════════════════ ORDER BOOK ════════════════" << RESET << "\n";
    std::cout << BOLD << std::setw(21) << "BIDS" << std::setw(25) << "ASKS" << RESET << "\n";

    // Get top bids and asks
    auto bid_it = m_bids.begin();
    auto ask_it = m_asks.begin();
    
    for (int i = 0; i < MAX_LEVELS; ++i) {
        std::string bid_str, ask_str;

        if (bid_it != m_bids.end()) {
            unsigned int total = 0;
            for (const auto& o : bid_it->second) total += o.quantity.get();
            bid_str = print_level(bid_it->first.get(), total, true);
            ++bid_it;
        }

        if (ask_it != m_asks.end()) {
            unsigned int total = 0;
            for (const auto& o : ask_it->second) total += o.quantity.get();
            ask_str = print_level(ask_it->first.get(), total, false);
            ++ask_it;
        }

        std::cout << std::left << std::setw(30) << bid_str 
                  << " │ " << std::setw(30) << ask_str << "\n";
    }

    // Market stats
    double best_bid = m_bids.empty() ? 0.0 : m_bids.begin()->first.get();
    double best_ask = m_asks.empty() ? 0.0 : m_asks.begin()->first.get();
    double spread = best_ask - best_bid;

    std::cout << BOLD << "\nMarket Summary:\n" << RESET
              << "┌──────────────────────┬──────────────────────┐\n"
              << "│ " << GREEN << "Best Bid: " << std::setw(10) << best_bid << RESET
              << " │ " << RED << "Best Ask: " << std::setw(10) << best_ask << RESET << " │\n"
              << "│ " << "Spread:  " << std::setw(10) << spread
              << " │ " << "Depth Levels: " << std::setw(5) << (m_bids.size() + m_asks.size()) << " │\n"
              << "└──────────────────────┴──────────────────────┘\n\n";
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