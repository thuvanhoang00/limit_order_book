#ifndef TRADINGSTRATEGY_H
#define TRADINGSTRATEGY_H
#include <iostream>
#include "limitorderbook.h"
namespace thu
{
class IOrderBook;

class ITradingStrategyListener
{
public:
    virtual ~ITradingStrategyListener() = default;
    virtual void getNotice() = 0;
};

class ConcreteTradingStrategyListener : public ITradingStrategyListener
{
public:
    ConcreteTradingStrategyListener(IOrderBook* orderbook)
    {
        orderbook->subscribe(this);
    }
    void getNotice() override
    {
        std::cout << "got notice \n";
    }

};

}


#endif