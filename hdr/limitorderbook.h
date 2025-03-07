#ifndef LIMITORDERBOOK_H
#define LIMITORDERBOOK_H
#include <map>
#include <set>
#include <vector>
#include <list>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include "spinlock.h"
#include "exchangemodel.h"
#include "templatedefine.h"
namespace thu
{

class IFTradingStrategyListener;

class IFOrderBook
{
public:    
    virtual ~IFOrderBook() = default;
    virtual void add_order(NormalOrder order) = 0;
    virtual void cancel_order(NormalOrder order) = 0;
    virtual void edit_order(NormalOrder before, NormalOrder after) = 0;
    virtual void subscribe(IFTradingStrategyListener* listener) = 0;
};

class LimitOrderBook : public IFOrderBook
{
public:
    void add_order(NormalOrder order) override;
    void cancel_order(NormalOrder order) override;
    void edit_order(NormalOrder before, NormalOrder after) override;
    void print_book() const;

    // observer function
    void subscribe(IFTradingStrategyListener* listener) override;
    void notify();
    
private:
    struct PriceComparator
    {
        bool operator()(Price aLhs, Price aRhs) const noexcept{
            return aLhs > aRhs;
        }
    };
    // price-time priority: map sorts by price (best first)
    std::map<Price, std::list<NormalOrder>, PriceComparator> m_bids; // queue is better than list but queue dont have erase
    std::map<Price, std::list<NormalOrder>> m_asks;
    SpinLock m_spinlock;
    std::mutex m_mutex;

    // 
    std::set<IFTradingStrategyListener*> listeners;
private:

    template<typename Book, typename OppositeBook>
    void do_add(NormalOrder order, Book& books, OppositeBook& opposite_side)
    {
        m_spinlock.lock();

        match_order(order, opposite_side); // match order with the opposite side
        // Add remaining quantity to book (if limit order)
        if (order.quantity.get() > 0 && order.type == OrderType::Limit)
        {
            books[order.price].push_back(order);
        }
        
        // Notify to listeners
        notify();

        m_spinlock.unlock();
    }

    template<typename Book>
    void do_cancel(NormalOrder order, Book& book)
    {
        m_spinlock.lock();

        auto order_queue_it = std::find_if(book.begin(), book.end(), [order](std::pair<Price, std::list<NormalOrder>> p)
                                           { return equal_within_ulps(p.first.get(), order.price.get(), 10); });
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

        // Notify to listeners
        notify();

        m_spinlock.unlock();
    }

    template<typename Book, typename OppositeBook>
    void do_edit(const NormalOrder& before, NormalOrder after, Book& book, OppositeBook& opposite_book)
    {
        m_spinlock.lock();

        auto old_order_queue_it = std::find_if(book.begin(), book.end(), [before](std::pair<Price, std::list<NormalOrder>> p)
                                           { return before.price == p.first; });
        if(old_order_queue_it != book.end())
        {
            auto &queue = old_order_queue_it->second;
            auto remove_it = std::find_if(queue.begin(), queue.end(), [before](const NormalOrder &order)
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
            if(after.quantity.get() > 0 && after.type == OrderType::Limit)
            {
                book[after.price].push_back(after);
            }
        }

        // Notify to listeners
        notify();

        m_spinlock.unlock();
    }

    template<typename OppositeBook>
    void match_order(NormalOrder& order, OppositeBook& opposite_side)
    {
        // maybe deadlock here if call spin.lock()
        while(!opposite_side.empty() && order.quantity.get() > 0)
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
            while(it != orders_at_price.end() && order.quantity.get() > 0)
            {
                unsigned int fill_qty = std::min(order.quantity.get(), it->quantity.get());

                // Execute trade
                order.quantity.set(order.quantity.get()-fill_qty);
                it->quantity.set(it->quantity.get()-fill_qty);

                // Generate execution report
                std::cout << "TRADE: "
                          << ((order.side == Side::Bid) ? "BOUGHT " : "SOLD ")
                          << fill_qty << " @ $" << best_price
                          << " (Remaining: " << order.quantity << ")\n";
            
                // Remove filled orders
                if(it->quantity.get() == 0)
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