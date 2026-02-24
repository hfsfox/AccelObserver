#ifndef __NETWORKSTACK_H__
#define __NETWORKSTACK_H__

// Platform-specific socket headers
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
// Линковка ws2_32 через pragma (MSVC) или через CMake (GCC/MinGW)
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib")
#  endif
using socket_t = SOCKET;
#  define INVALID_SOCK  INVALID_SOCKET
#  define SOCK_CLOSE(s) ::closesocket(s)
using socklen_t = int;
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
using socket_t = int;
#  define INVALID_SOCK  (-1)
#  define SOCK_CLOSE(s) ::close(s)
#endif

#include <cstdint>
#include <string>

namespace core
{
    namespace network
    {
        bool network_init(void);
        socket_t create_socket(uint16_t port, int backlog = 10);
        bool set_nonblocking(socket_t sock, bool nonblocking);
        std::string last_socket_error();
        socket_t accept_client(socket_t server_sock,
                       struct sockaddr_in* addr_out = nullptr,
                       socklen_t* len_out = nullptr);
    }
}

#endif
