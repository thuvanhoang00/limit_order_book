#ifndef EXCHANGEMODEL_H
#define EXCHANGEMODEL_H
#include <iostream>
#include <chrono>
#include <string>
#include <atomic>
#include "templatedefine.h"

namespace thu
{

enum class OrderType
{
    Limit,
    Market,
    Iceberg
};

enum class Side
{
    Bid,
    Ask
};

enum class OrderStatus
{
    New,
    PartialFill,
    Filled,
    Canceled,
};

struct SecurityId
{
    SecurityId(std::string _id) : id(_id) {}

    std::string getSecurityId() const
    {
        return id;
    }
    bool operator==(const SecurityId &other) const
    {
        return this->id == other.id;
    }

    friend std::ostream &operator<<(std::ostream &os, SecurityId secId)
    {
        return os << secId.id;
    }

    std::string get() const
    {
        return id;
    }

private:
    std::string id;
};

struct Price
{
    Price(double _price) : price(_price) {}

    bool operator<(Price other) const
    {
        return this->price < other.price;
    }
    bool operator>(Price other) const
    {
        return this->price > other.price;
    }
    bool operator==(const Price &other) const
    {
        return equal_within_ulps(this->price, other.price, 10);
    }

    friend std::ostream &operator<<(std::ostream &os, Price p)
    {
        return os << p.price;
    }

    double get() const
    {
        return price;
    }

private:
    double price;
};

struct Quantity
{
    // unsigned int getQuantity() const{
    //     return quantity;
    // }
    Quantity(unsigned int _quantity) : quantity(_quantity) {}

    bool operator==(Quantity other) const
    {
        return this->quantity == other.quantity;
    }

    // bool operator>(unsigned int amount) const
    // {
    //     return this->quantity > amount;
    // }

    // Quantity& operator-=(unsigned int amount)
    // {
    //     this->quantity -= amount;
    //     return *this;
    // }

    friend std::ostream &operator<<(std::ostream &os, const Quantity &q)
    {
        return os << q.quantity;
    }

    unsigned int get() const
    {
        return quantity;
    }
    void set(unsigned int quantity)
    {
        this->quantity = quantity;
    }

private:
    unsigned int quantity;
};
    
struct IcebergQuantity
{
    IcebergQuantity(unsigned int _quantity) : quantity(_quantity) {}

    friend std::ostream &operator<<(std::ostream &os, const IcebergQuantity &q)
    {
        return os << q.quantity;
    }

    unsigned int get() const
    {
        return quantity;
    }
    void set(unsigned int quantity)
    {
        this->quantity = quantity;
    }

private:
    unsigned int quantity;
};

struct IcebergVisibleSize
{
    IcebergVisibleSize(unsigned int _size) : visible_size(_size) {}
private:
    unsigned int visible_size;
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
    IcebergVisibleSize visible_size{0};
    microsec timestamp; // for time priority
    // std::atomic<OrderStatus> status{OrderStatus::New};

    // Iceberg tracking
    IcebergQuantity iceberg_quantity{0};
    IcebergQuantity hidden_quantity() const
    {
        return IcebergQuantity(iceberg_quantity.get() - quantity.get());
    }
};

} // namespace thu
#endif