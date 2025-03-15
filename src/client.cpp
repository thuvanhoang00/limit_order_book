#include "../hdr/client.h"
#include "../hdr/log.h"
#include <iostream>
#include <string>

#define ASK "ASK"
#define BID "BID"

namespace thu
{
const char* SERVER_IP = "127.0.0.1"; // localhost

Client::Client(){}

void Client::run()
{
    Socket sock;
    if(sock.create() == false){
        LOG("Create socket failed\n");
        return;
    }

    // Server address
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    // convert ipv4 to binary
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // Connect to server
    if(connect(sock, server_addr) < 0){
        LOG("Connect to server failed\n");
        return;
    }

    // Read user input, send to server, and display the echoed response
    std::string input;
    char buffer[1024] = {0};
    while(true){
        std::cout << "Enter type \n1. Ask\n2. Bid\n(type 'exit' to quit): " ;
        std::getline(std::cin, input);
        if(input == "exit") break;

        std::string message;
        if(input == "1")
            message = NormalOrder::createAsk();
        else if(input == "2")
            message = NormalOrder::createBid();
        else 
            continue;

        // Send messasge to server
        ::send(sock.get_sock_fd(), message.c_str(), message.size(), 0);
        int bytesRead = ::read(sock.get_sock_fd(), buffer, sizeof(buffer));
        if(bytesRead > 0){
            std::cout << "Echo from server: " << std::string(buffer, bytesRead) << std::endl;
        }

        // clear buffer
        ::memset(buffer, 0, sizeof(buffer));
    }
}

int Client::connect(const Socket& sock, const sockaddr_in& server_addr)
{
    return ::connect(sock.get_sock_fd(), (struct sockaddr*)&server_addr, sizeof(server_addr));
}


std::string NormalOrder::createAsk()
{
    std::string side = ASK;
    std::string price = "100";
    std::string quantity = "50";
    std::string res;
    res += side;
    res += ",";
    res += price;
    res += ",";
    res += quantity;

    return res;    
}

std::string NormalOrder::createBid()
{
    std::string side = BID;
    std::string price = "100";
    std::string quantity = "50";
    std::string res;
    res += side;
    res += ",";
    res += price;
    res += ",";
    res += quantity;

    return res;    
}

}