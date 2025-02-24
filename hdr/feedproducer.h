#ifndef FEEDPRODUCER_H
#define FEEDPRODUCER_H
namespace thu
{

class FeedProducer
{
public:
    virtual ~FeedProducer() = default;
    virtual void feed() = 0;
};

}

#endif