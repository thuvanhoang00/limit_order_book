#include "../hdr/server.h"
#include "../hdr/log.h"
#include <sstream>
#include <vector>
#include <iostream>

namespace thu
{
static int id = 0;

Server& Server::getInstance()
{
    static Server instance;
    return instance;
}

void Server::start()
{
    Socket sock;
    sockaddr_in addr;

    if(sock.create() == false){
        LOG("Create socket failed\n");
        return;
    }

    if(bind(sock, addr) > 0){
        LOG("Bind socket failed\n");
        return;
    }

    if(listen(sock) < 0){
        LOG("Listen socket failed\n");
        return;
    }
	std::cout << "Listening on port: " << PORT << std::endl;

    // why need close client_fd?
    int client_fd = accept(sock);
    if(client_fd < 0){
        LOG("Accept failed\n");
        return;
    }

    char buffer[1024];
    int bytesRead;
    while(bytesRead = ::read(client_fd, buffer, sizeof(buffer)) > 0){
        std::cout << "Received: " << buffer << std::endl;

        // Forward message to LOB
        OrderMessageParser objOrderMsg(buffer);
        if(objOrderMsg.getType() == "ASK"){
            double price = std::stold(objOrderMsg.getPrice());
            int quantity = std::stoi(objOrderMsg.getQuantity());            
            id++;

            auto order = NormalOrderBuilder()
                             .setSecurityId(std::to_string(id))
                             .setSide(Side::Ask)
                             .setOrderType(OrderType::Limit)
                             .setPrice(price)
                             .setQuantity(quantity)
                             .setTimestamp({})
                             .build();

            m_lob.add_order(order);
        }
        else if(objOrderMsg.getType() == "BID"){
            double price = std::stold(objOrderMsg.getPrice());
            int quantity = std::stoi(objOrderMsg.getQuantity());            
            id++;

            auto order = NormalOrderBuilder()
                             .setSecurityId(std::to_string(id))
                             .setSide(Side::Bid)
                             .setOrderType(OrderType::Limit)
                             .setPrice(price)
                             .setQuantity(quantity)
                             .setTimestamp({})
                             .build();

            m_lob.add_order(order);
        }
        else{

        }

        // Echo back 
        ::send(client_fd, buffer, strlen(buffer), 0);

        // clear the buffer for next read
        memset(buffer, 0, sizeof(buffer));
    }
    // print the BOOK
    m_lob.print_book();

    close(client_fd);
}

int Server::bind(const Socket& sock, sockaddr_in& addr)
{    
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // listen on any interface
    addr.sin_port = htons(PORT);       // convert port to network byte order
    return ::bind(sock.get_sock_fd(), (struct sockaddr *)&addr, sizeof(addr));
}

int Server::listen(const Socket& sock)
{
    return ::listen(sock.get_sock_fd(), 5);
}

int Server::accept(const Socket& sock)
{
	sockaddr_in client_addr{};
	socklen_t client_len = sizeof(client_addr);
    return ::accept(sock.get_sock_fd(), (struct sockaddr*)&client_addr, &client_len);
}

OrderMessageParser::OrderMessageParser(const char* msg)
{
    std::istringstream message(msg);
    std::vector<std::string> tokens;
    std::string token;
    while(std::getline(message, token, ',')){
        tokens.push_back(token);
    }
    if (tokens.size() == 3)
    {
        m_type = tokens[0];
        m_price = tokens[1];
        m_quantity = tokens[2];
    }
    else{
        std::cout << "Size is not 3\n";
    }
}


}