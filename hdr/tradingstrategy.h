#ifndef TRADINGSTRATEGY_H
#define TRADINGSTRATEGY_H
namespace thu
{
class IFOrderBook;
    
class IFTradingStrategyListener
{
public:
    virtual ~IFTradingStrategyListener() = default;
    virtual void getNotice() = 0;
};

class ConcreteTradingStrategyListener : public IFTradingStrategyListener
{
public:
    ConcreteTradingStrategyListener(IFOrderBook* orderbook);
    void getNotice() override;
};

}


#endif