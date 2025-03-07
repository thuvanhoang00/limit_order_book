#ifndef SOCKET_H
#define SOCKET_H
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/ip.h> // for inet_pton
#include <unistd.h> // for close()
#include <cstring> // for exit()
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


}

#endif