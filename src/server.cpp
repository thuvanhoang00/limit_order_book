#include <sstream>
#include <vector>
#include <iostream>
#include <string>
#include <thread>
#include "../hdr/server.h"
#include "../hdr/log.h"
#include "../hdr/nlohmann/json.hpp"

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
    int opt = 1;

    if(sock.create() == false){
        LOG("Create socket failed\n");
        return;
    }

    // Allow reusing the address (optional, but useful)
    if (setsockopt(sock.get_sock_fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        LOG("setsockopt");
        exit(EXIT_FAILURE);
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

    while (true)
    {
        // why need close client_fd?
        int client_fd = accept(sock);
        if (client_fd < 0)
        {
            LOG("Accept failed\n");
            return;
        }
        std::thread t(&Server::handle_client, this, client_fd);
        t.detach();
    }
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

void Server::handle_client(int client_fd)
{

    char buffer[1024];
    int bytesRead;
    while (bytesRead = ::read(client_fd, buffer, sizeof(buffer)) > 0)
    {
        // std::cout << "Received: " << buffer << std::endl;

        int expected = 0;
        while(!m_flag.compare_exchange_weak(expected, 1, std::memory_order_acquire)){
            expected = 0;
            std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        }

        // Forward message to LOB
        // OrderMessageParser objOrderMsg(buffer);
        // if (objOrderMsg.getSide() == "ASK")
        // {
        //     double price = std::stold(objOrderMsg.getPrice());
        //     int quantity = std::stoi(objOrderMsg.getQuantity());
        //     id++;

        //     auto order = NormalOrderBuilder()
        //                      .setSecurityId(std::to_string(id))
        //                      .setSide(Side::Ask)
        //                      .setOrderType(OrderType::Limit)
        //                      .setPrice(price)
        //                      .setQuantity(quantity)
        //                      .setTimestamp({})
        //                      .build();

        //     m_lob.add_order(order);
        // }
        // else if (objOrderMsg.getSide() == "BID")
        // {
        //     double price = std::stold(objOrderMsg.getPrice());
        //     int quantity = std::stoi(objOrderMsg.getQuantity());
        //     id++;

        //     auto order = NormalOrderBuilder()
        //                      .setSecurityId(std::to_string(id))
        //                      .setSide(Side::Bid)
        //                      .setOrderType(OrderType::Limit)
        //                      .setPrice(price)
        //                      .setQuantity(quantity)
        //                      .setTimestamp({})
        //                      .build();

        //     m_lob.add_order(order);
        // }

        // // pop the message from queue
        // std::string str;
        // m_MsgQueue.pop(str);

        // Echo back
        ::send(client_fd, buffer, strlen(buffer), 0);

        // clear the buffer for next read
        memset(buffer, 0, sizeof(buffer));

        // print the BOOK
        // m_lob.print_book();

        // push message to queue
        m_MsgQueue.push(std::string(buffer));
        count++;
        if(count % 2 == 0){
            std::string str;
            m_MsgQueue.pop(str);
        }
        
        std::cout << "Received total msg: " << count << std::endl;

        m_flag.store(0, std::memory_order_release);

    }
}

OrderMessageParser::OrderMessageParser(const char* msg)
{
    try{
        nlohmann::json jOrder = nlohmann::json::parse(std::string(msg));
        m_side = jOrder["side"];
        double price = jOrder["price"];
        int quantity = jOrder["quantity"];
        m_price = std::to_string(price);
        m_quantity = std::to_string(quantity);
    }
    catch(const nlohmann::json::parse_error& e){
        LOG("message: ", e.what(), '\n', 
            "exception id: ", e.id, '\n',
            "byte position of error: ", e.byte, '\n');
    }
}


}