#ifndef LIMITORDERBOOK_H
#define LIMITORDERBOOK_H
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <iomanip>
#include <chrono>

namespace thu
{

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