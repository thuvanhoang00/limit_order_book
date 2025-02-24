#ifndef EVENTQUEUE_H
#define EVENTQUEUE_H
namespace thu
{

class IFEventQueue
{
public:
    virtual ~IFEventQueue() = default;
    virtual void feed() = 0;
    virtual void request() = 0;
    virtual void response() = 0;
};
}

#endif