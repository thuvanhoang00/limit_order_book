#ifndef SERVER_H
#define SERVER_H
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h> // for inet_pton
#include <unistd.h> // for close()
#include <cstring> // for exit()
#include <string>
#include <iostream>
#define PORT 54000

namespace thu
{
class Socket
{
public:
    Socket(){}
    ~Socket(){
        if(sock_fd > 0) close(sock_fd);
    }

    bool create(){
        sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        return true;
    }

    int get_sock_fd() const{
        return sock_fd;
    }
private:
    int sock_fd = 0; // sock file description
};

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