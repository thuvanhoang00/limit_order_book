#ifndef FEEDAPI_H
#define FEEDAPI_H
namespace thu
{

class FeedAPI
{
public:
    virtual ~FeedAPI() = default;
    virtual void feed() = 0;
};

}

#endif