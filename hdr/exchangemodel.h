#ifndef EXCHANGEMODEL_H
#define EXCHANGEMODEL_H
#include <iostream>
#include <chrono>
#include <string>
#include <atomic>
#include "templatedefine.h"

namespace thu
{

using microsec = std::chrono::microseconds;

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
    SecurityId(std::string _id = "") : id(_id) {}

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
    Price(double _price = 0.00) : price(_price) {}

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
    Quantity(unsigned int _quantity = 0) : quantity(_quantity) {}

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
    IcebergQuantity(unsigned int _quantity = 0) : quantity(_quantity) {}

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
    IcebergVisibleSize(unsigned int _size = 0) : visible_size(_size) {}
private:
    unsigned int visible_size;
};

struct NormalOrder
{  
    SecurityId id;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity;
    microsec timestamp; // for time priority

private:
    NormalOrder(SecurityId id_
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

    friend class NormalOrderBuilder;
};

class NormalOrderBuilder
{
public:
    NormalOrderBuilder& setSecurityId(const SecurityId& id_){id = id_; return *this;}
    NormalOrderBuilder& setSide(const Side& side_){side = side_; return *this;}
    NormalOrderBuilder& setOrderType(const OrderType& type_){type = type_; return *this;}
    NormalOrderBuilder& setPrice(const Price& price_){price = price_; return *this;}
    NormalOrderBuilder& setQuantity(const Quantity& quantity_){quantity = quantity_; return *this;}
    NormalOrderBuilder& setTimestamp(const microsec& timestamp_){timestamp = timestamp_; return *this;}

    NormalOrder build() const
    {
        return NormalOrder(id, side, type, price, quantity, timestamp);
    }

private:
    SecurityId id;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity{0};
    microsec timestamp; // for time priority
};


struct IcebergOrder
{  
    SecurityId id;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity;
    IcebergVisibleSize visible_size{0};
    microsec timestamp;
    IcebergQuantity iceberg_quantity{0};
    IcebergQuantity hidden_quantity() const
    {
        return IcebergQuantity(iceberg_quantity.get() - quantity.get());
    }

private:
    IcebergOrder(SecurityId id_
        , Side side_
        , OrderType type_
        , Price price_
        , Quantity quantity_
        , IcebergVisibleSize visiblesize_
        , microsec timestamp_
        , IcebergQuantity icebergquantity_)
        : id(id_)
        , side(side_)
        , type(type_)
        , price(price_)
        , quantity(quantity_)
        , visible_size(visiblesize_)
        , timestamp(timestamp_)
        , iceberg_quantity(icebergquantity_)
    {}

    friend class IcebergOrderBuilder;
};

class IcebergOrderBuilder
{
public:
    IcebergOrderBuilder& setSecurityId(const SecurityId& id_){id = id_; return *this;}
    IcebergOrderBuilder& setSide(const Side& side_){side = side_; return *this;}
    IcebergOrderBuilder& setOrderType(const OrderType& type_){type = type_; return *this;}
    IcebergOrderBuilder& setPrice(const Price& price_){price = price_; return *this;}
    IcebergOrderBuilder& setQuantity(const Quantity& quantity_){quantity = quantity_; return *this;}
    IcebergOrderBuilder& setIcebergVisibleSize(const IcebergVisibleSize& visiblesize_){visible_size = visiblesize_; return *this;}
    IcebergOrderBuilder& setTimestamp(const microsec& timestamp_){timestamp = timestamp_; return *this;}
    IcebergOrderBuilder& setIcebergQuantity(const IcebergQuantity& icebergquantity_){iceberg_quantity = icebergquantity_; return *this;}

    IcebergOrder build() const
    {
        return IcebergOrder(id, side, type, price, quantity, visible_size, timestamp, iceberg_quantity);
    }

private:
    SecurityId id;
    Side side;
    OrderType type;
    Price price;
    Quantity quantity{0};
    IcebergVisibleSize visible_size{0};
    microsec timestamp; // for time priority
    IcebergQuantity iceberg_quantity;
};

} // namespace thu
#endif