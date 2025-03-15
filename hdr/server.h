#ifndef SERVER_H
#define SERVER_H
#include <string>
#include "socket.h"
#include "limitorderbook.h"

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
    LimitOrderBook m_lob;
};

class OrderMessageParser
{
public:
    OrderMessageParser(const char *msg);
    std::string getType() const {return m_type;}
    std::string getPrice() const {return m_price;}
    std::string getQuantity() const {return m_quantity;}
private:
    std::string m_type;
    std::string m_price;
    std::string m_quantity;
};

}

#endif