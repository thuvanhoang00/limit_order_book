#ifndef ORDERAPI_H
#define ORDERAPI_H
namespace thu
{

class OrderAPI
{
public:
    virtual ~OrderAPI() = default;
    virtual void request() = 0;
    virtual void response() = 0;
};

}

#endif