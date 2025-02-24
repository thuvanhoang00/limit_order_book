#ifndef ORDERAPI_H
#define ORDERAPI_H
namespace thu
{

class IFOrderAPI
{
public:
    virtual ~IFOrderAPI() = default;
    virtual void request() = 0;
    virtual void response() = 0;
};

}

#endif