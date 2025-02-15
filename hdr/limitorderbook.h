#ifndef LIMITORDERBOOK_H
#define LIMITORDERBOOK_H
#include <iostream>
#include <map>
#include <vector>
#include <list>
#include <string>
#include <iomanip>
#include <chrono>
#include <limits>
#include <cmath>
#include <algorithm>
#include <type_traits>
#include "spinlock.h"
#include <mutex>

namespace thu
{
template <typename T>
std::enable_if_t<not std::numeric_limits<T>::is_integer, bool>
equal_within_ulps(T x, T y, std::size_t n=10)
{
    const T m = std::min(std::fabs(x), std::fabs(y));
    const int exp = m < std::numeric_limits<T>::min() ? std::numeric_limits<T>::min_exponent - 1
                                                      : std::ilogb(m);
    return std::fabs(x - y) <= n * std::ldexp(std::numeric_limits<T>::epsilon(), exp);
}

enum class OrderType
{
    Limit,
    Market
};

enum class Side
{
    Bid,
    Ask
};

struct SecurityId
{
    std::string getSecurityId() const {
        return id;
    }
    bool operator==(const SecurityId& other)
    {
        return this->id == other.id;
    }
private:
    std::string id;
};


struct Price
{
    bool operator<(Price other)
    {
        return this->price < other.price;
    }
    bool operator>(Price other)
    {
        return this->price > other.price;
    }
    bool operator==(const Price& other)
    {
        return equal_within_ulps(this->price, other.price, 10);
    }
private:
    double price;
};

struct Quantity
{
    // unsigned int getQuantity() const{
    //     return quantity;
    // }

    template<typename T>
    bool operator==(T other)
    {
        return this->quantity == static_cast<unsigned int>(other);
    }

    bool operator>(unsigned int amount)
    {
        return this->quantity > amount;
    }

    Quantity& operator-=(unsigned int amount)
    {
        this->quantity -= amount;
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& os, const Quantity& q)
    {
        return os << q.quantity;
    }
private:
    unsigned int quantity;
};

struct Order
{
    using microsec = std::chrono::microseconds;
    Order(SecurityId id_
        , Side side_
        , OrderType type_
        , Price price_
        , Quantity quantity_
        , microsec timestamp_) 
        : id(id_)
        , side(side_)
        , type(type_)
        , price(price_)
        , quantity(quantity_)
        , timestamp(timestamp_) 
        {}
    
    SecurityId id;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity;
    microsec timestamp; // for time priority
};

class LimitOrderBook
{
public:
    void add_order(Order order);
    void cancel_order(Order order);
    void edit_order(Order before, Order after);
    void print_book() const;
private:
    struct PriceComparator
    {
        bool operator()(Price aLhs, Price aRhs) const noexcept{
            return aLhs > aRhs;
        }
    };
    // price-time priority: map sorts by price (best first)
    std::map<Price, std::list<Order>, PriceComparator> m_bids; // queue is better than list but queue dont have erase
    std::map<Price, std::list<Order>> m_asks;
    SpinLock m_spinlock;
    std::mutex m_mutex;
private:

    template<typename Book, typename OppositeBook>
    void do_add(Order order, Book& books, OppositeBook& opposite_side)
    {
        m_spinlock.lock();
        // LOG();
        match_order(order, opposite_side); // match order with the opposite side
        // Add remaining quantity to book (if limit order)
        if (order.quantity > 0 && order.type == OrderType::Limit)
        {
            books[order.price].push_back(order);
        }
        m_spinlock.unlock();
    }

    template<typename Book>
    void do_cancel(Order order, Book& book)
    {
        m_spinlock.lock();
        // LOG();
        auto order_queue_it = std::find_if(book.begin(), book.end(), [order](std::pair<Price, std::list<Order>> p)
                                           { return equal_within_ulps(p.first, order.price, 10); });
        if(order_queue_it != book.end())
        {
            auto& queue = order_queue_it->second;
            auto remove_it = std::find_if(queue.begin(), queue.end(), [order](const auto &_order)
                                       { return order.id == _order.id; });
            if(remove_it != queue.end())
            {
                queue.erase(remove_it);
            }

            if(queue.empty())
            {
                book.erase(order_queue_it);
            }
        }
        m_spinlock.unlock();
    }

    template<typename Book, typename OppositeBook>
    void do_edit(const Order& before, Order after, Book& book, OppositeBook& opposite_book)
    {
        m_spinlock.lock();
        // std::cout << "old: " << static_cast<int>(before.side) << ", price: " << before.price << std::endl;
        auto old_order_queue_it = std::find_if(book.begin(), book.end(), [before](std::pair<Price, std::list<Order>> p)
                                           { return before.price == p.first; });
        if(old_order_queue_it != book.end())
        {
            // std::cout << "found old queue\n";
            auto &queue = old_order_queue_it->second;
            auto remove_it = std::find_if(queue.begin(), queue.end(), [before](const Order &order)
                                          { return before.id == order.id; });
            if (remove_it != queue.end())
            {
                queue.erase(remove_it);
            }

            if(queue.empty())
            {
                book.erase(old_order_queue_it);
            }

            match_order(after, opposite_book);
            // remaining quantity after matching
            if(after.quantity>0 && after.type == OrderType::Limit)
            {
                book[after.price].push_back(after);
            }
        }
        m_spinlock.unlock();
    }

    template<typename OppositeBook>
    void match_order(Order& order, OppositeBook& opposite_side)
    {
        // std::cout << "Calling matching\n";
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