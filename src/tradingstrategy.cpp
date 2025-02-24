#include "../hdr/limitorderbook.h"
#include "../hdr/tradingstrategy.h"
#include <iostream>

namespace thu
{

ConcreteTradingStrategyListener::ConcreteTradingStrategyListener(IFOrderBook* orderbook)
{
    orderbook->subscribe(this);
}

void ConcreteTradingStrategyListener::getNotice()
{
    std::cout << "got notice \n";
}

}