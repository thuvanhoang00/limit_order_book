#ifndef CLIENT_H
#define CLIENT_H
#include "socket.h"
#include <string>
#define PORT 54000
namespace thu
{
    
class Client
{
public:
    Client();
    void run();
    int connect(const Socket& sock, const sockaddr_in& server_addr);
private:
    
};

class NormalOrder
{
public:
    static std::string createAsk();
    static std::string createBid();
};

}
#endif