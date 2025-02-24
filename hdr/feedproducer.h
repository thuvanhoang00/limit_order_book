#ifndef FEEDPRODUCER_H
#define FEEDPRODUCER_H
namespace thu
{

class IFFeedProducer
{
public:
    virtual ~IFFeedProducer() = default;
    virtual void feed() = 0;
};

}

#endif