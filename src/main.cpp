#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <iomanip>
#include <chrono>

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
    void add_order(Order order)
    {
        // Set timestamp if not provided
        if(order.timestamp.count() == 0)
        {
            order.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch());
        }

        // Attempt to match immediately
        if(order.side == Side::Bid)
        {
            match_order(order, bids);
            // Add remaining quantity to book (if limit order)
            if (order.quantity > 0 && order.type == OrderType::Limit)
            {
                bids[order.price].push_back(order);
            }
        }
        else if (order.side == Side::Ask)
        {
            match_order(order, asks);
            // Add remaining quantity to book (if limit order)
            if (order.quantity > 0 && order.type == OrderType::Limit)
            {
                asks[order.price].push_back(order);
            }
        }
    }

    void print_book() const
    {
        std::cout << "\n------ ORDER BOOK ------\n";
        std::cout << "BIDS:\n";
        for(const auto& [price, orders] : bids)
        {
            std::cout << std::setw(6) << price << " | ";
            for(const auto& o : orders) std:: cout << o.quantity << " ";
            std::cout << "\n";
        }

        std::cout << "\nASKS:\n";
        for(const auto& [price, orders] : asks)
        {
            std::cout << std::setw(6) << price << " | ";
            for(const auto& o : orders) std::cout << o.quantity << " ";
            std::cout << "\n";
        }
        std::cout << "--------------------------\n\n";
    }
private:
    // price-time priority: map sorts by price (best first)
    std::map<double, std::vector<Order>, std::greater<double>> bids;
    std::map<double, std::vector<Order>> asks;

    template<typename MapType>
    void match_order(Order& order, MapType& book)
    {
        auto& opposite_side = book;

        // auto opposite_compare = (order.side == Side::Bid) ?
        //     [](double a, double b){ return a < b; }: // For bids: match lowest asks first
        //     [](double a, double b){ return a > b; }; // For asks: match highest bids first

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