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
        if(other_sock_fd > 0) close(other_sock_fd);
    }

    void create(){
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        addrlen = sizeof(m_addr);
    }

    void bind(){
        m_addr.sin_family = AF_INET;
        m_addr.sin_addr.s_addr = INADDR_ANY; // listen on any interface
        m_addr.sin_port = htons(PORT); // convert port to network byte order
        
        ::bind(sock_fd, (struct sockaddr*)&m_addr, sizeof(m_addr));    
    }

    void listen(){
        ::listen(sock_fd, 3);
    }

    void accept(){
        other_sock_fd = ::accept(sock_fd, (struct sockaddr*)&m_addr, (socklen_t*)&addrlen);
    }

    void read(){
        int bytesRead = 0;
        while((bytesRead = ::read(other_sock_fd, buffer, sizeof(buffer)) > 0)){
            std::cout << "Received: " << std::string(buffer, bytesRead) << std::endl;
            
            // Echo back
            ::send(other_sock_fd, buffer, bytesRead, 0);
            ::memset(buffer, 0, sizeof(buffer));
        }
    }
private:
    int sock_fd = 0; // sock file description
    int other_sock_fd = 0;
    int addrlen = 0;
    struct sockaddr_in m_addr;
    char buffer[1024];
};

class Server
{


};


}

#endif