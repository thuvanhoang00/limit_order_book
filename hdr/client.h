#ifndef CLIENT_H
#define CLIENT_H
#include "socket.h"
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
}
#endif