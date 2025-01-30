#ifndef LIMITORDERBOOK_H
#define LIMITORDERBOOK_H
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <iomanip>
#include <chrono>
#include <limits>
#include <cmath>
#include <algorithm>
#include <type_traits>

namespace thu
{
    template <typename T>
    std::enable_if_t<not std::numeric_limits<T>::is_integer, bool>
    equal_within_ulps(T x, T y, std::size_t n)
    {
        const T m = std::min(std::fabs(x), std::fabs(y));

        const int exp = m < std::numeric_limits<T>::min() ? std::numeric_limits<T>::min_exponent - 1
                                                          : std::ilogb(m);

        return std::fabs(x - y) <= n * std::ldexp(std::numeric_limits<T>::epsilon(), exp);
    }

enum class OrderType {Limit, Market};
enum class Side {Bid, Ask};

struct Order
{
    std::string id;
    Side side;
    OrderType type;
    double price;
    unsigned int quantity;
    std::chrono::microseconds timestamp; // for time priority
};

class LimitOrderBook
{
public:
    void add_order(Order order);
    void cancel_order(Order order);    
    void print_book() const;
private:
    // price-time priority: map sorts by price (best first)
    std::map<double, std::vector<Order>, std::greater<double>> bids;
    std::map<double, std::vector<Order>> asks;

    template<typename Book, typename OppositeBook>
    void process_order(Order& order, Book& books, OppositeBook& opposite_side)
    {
        match_order(order, opposite_side); // match order with the opposite side
        // Add remaining quantity to book (if limit order)
        if (order.quantity > 0 && order.type == OrderType::Limit)
        {
            books[order.price].push_back(order);
        }
    }

    template<typename Book>
    void do_cancel(Order& order, Book& book)
    {
        auto order_queue_it = std::find_if(book.begin(), book.end(), [order](std::pair<double, std::vector<Order>> p)
                                           { return equal_within_ulps(p.first, order.price, 10); });
        if(order_queue_it != book.end())
        {
            auto& queue = order_queue_it->second;
            queue.erase(std::remove_if(queue.begin(), queue.end(), [order](const auto &_order)
                                       { return order.id == _order.id; }));

            if(queue.empty())
            {
                book.erase(order_queue_it);
            }
        }
    }

    template<typename OppositeBook>
    void match_order(Order& order, OppositeBook& opposite_side)
    {
        while(!opposite_side.empty() && order.quantity > 0)
        {
            auto& [best_price, orders_at_price] = *opposite_side.begin();
            
            // Check price condition (limit orders only)
            if(order.type == OrderType::Limit)
            {
                if ((order.side == Side::Bid && best_price > order.price) ||
                    (order.side == Side::Ask && best_price < order.price))
                {
                    break;
                }
            }

            // Process orders at best price
            auto it = orders_at_price.begin();
            while(it != orders_at_price.end() && order.quantity > 0)
            {
                unsigned int fill_qty = std::min(order.quantity, it->quantity);

                // Execute trade
                order.quantity -= fill_qty;
                it->quantity -= fill_qty;

                // Generate execution report
                std::cout << "TRADE: "
                          << ((order.side == Side::Bid) ? "BOUGHT " : "SOLD ")
                          << fill_qty << " @ $" << best_price
                          << " (Remaining: " << order.quantity << ")\n";
            
                // Remove filled orders
                if(it->quantity == 0)
                {
                    it = orders_at_price.erase(it);
                }
                else
                {
                    it++;
                }
            }

            // Remove empty price levels
            if(orders_at_price.empty())
            {
                opposite_side.erase(opposite_side.begin());
            }
        }
    }
};


}


#endif