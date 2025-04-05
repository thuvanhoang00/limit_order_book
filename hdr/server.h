#ifndef SERVER_H
#define SERVER_H
#include <string>
#include "socket.h"
#include "limitorderbook.h"
#include "messagequeue.h"

#define PORT 54000

namespace thu
{
class OrderMessageParser;
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
    void handle_client(int client_fd);
    void sendToLimitOrderBook(const OrderMessageParser& objOrderMessage);
    
private:
    MessageQueue<std::string> m_MsgQueue;
    LimitOrderBook m_lob;
};

class OrderMessageParser
{
public:
    OrderMessageParser(const std::string& msg);
    std::string getSide() const {return m_side;}
    std::string getPrice() const {return m_price;}
    std::string getQuantity() const {return m_quantity;}
private:
    std::string m_side;
    std::string m_price;
    std::string m_quantity;
};

}

#endif