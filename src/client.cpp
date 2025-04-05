#include <iostream>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include "../hdr/client.h"
#include "../hdr/log.h"
#include "../hdr/nlohmann/json.hpp"

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
#ifdef USER_INPUT
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
#else if RANDOM
        std::string message;
        message = rand()%2==0 ? NormalOrder::createAsk() : NormalOrder::createBid();
#endif
        // Send messasge to server
        ::send(sock.get_sock_fd(), message.c_str(), message.size(), 0);
        int bytesRead = ::read(sock.get_sock_fd(), buffer, sizeof(buffer));
        if(bytesRead > 0){
            std::cout << "Echo from server: " << std::string(buffer, bytesRead) << std::endl;
        }

        // clear buffer
        ::memset(buffer, 0, sizeof(buffer));

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

int Client::connect(const Socket& sock, const sockaddr_in& server_addr)
{
    return ::connect(sock.get_sock_fd(), (struct sockaddr*)&server_addr, sizeof(server_addr));
}


std::string NormalOrder::createAsk()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> int_distribute(30, 100);
    std::uniform_real_distribution<> double_distribute(50.00, 300.00);

    nlohmann::json res = {
        {"side", ASK},
        {"price", double_distribute(gen)},
        {"quantity", int_distribute(gen)}
    };

    return nlohmann::to_string(res);    
}

std::string NormalOrder::createBid()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> int_distribute(30, 100);
    std::uniform_real_distribution<> double_distribute(50.00, 300.00);

    nlohmann::json res = {
        {"side", BID},
        {"price", double_distribute(gen)},
        {"quantity", int_distribute(gen)}
    };

    return nlohmann::to_string(res);    
}

}