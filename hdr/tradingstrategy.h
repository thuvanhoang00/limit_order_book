#ifndef TRADINGSTRATEGY_H
#define TRADINGSTRATEGY_H
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
    ConcreteTradingStrategyListener(IOrderBook* orderbook);
    void getNotice() override;
};

}


#endif