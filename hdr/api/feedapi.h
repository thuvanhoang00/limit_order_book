#ifndef FEEDAPI_H
#define FEEDAPI_H
namespace thu
{

class IFFeedAPI
{
public:
    virtual ~IFFeedAPI() = default;
    virtual void feed() = 0;
};

}

#endif