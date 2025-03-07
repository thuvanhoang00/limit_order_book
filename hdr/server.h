#ifndef SERVER_H
#define SERVER_H
#include <string>
#include <iostream>
#include "socket.h"

#define PORT 54000

namespace thu
{

class Server
{
public:
    static Server& getInstance();
    Server& operator=(const Server& other) = delete;
    Server(const Server& other) = delete;
    void start();
private:
    Server(){}
    int bind(const Socket& sock, sockaddr_in& addr);
    int listen(const Socket& sock);
    int accept(const Socket& sock);
private:

};


}

#endif