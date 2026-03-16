#include <network/net_platform.hpp>
#include <cstring>
#include <cerrno>
#include <string>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <fcntl.h>
#  include <cerrno>
#endif

namespace server
{
    namespace platform
    {
        bool net_init()
        {
            #ifdef _WIN32
                WSADATA wsa;
                int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
                return rc == 0;
            #else
                return true;
            #endif
        }

        void net_cleanup()
        {
            #ifdef _WIN32
                WSACleanup();
            #endif
        }


        socket_t create_server_socket(uint16_t port, int backlog)
        {
            socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCK) return INVALID_SOCK;


            int opt = 1;

            #ifdef _WIN32
                ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                            reinterpret_cast<const char*>(&opt), sizeof(opt));
            #else
                ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            #endif

            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port        = htons(port);

            if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
                SOCK_CLOSE(sock);
                return INVALID_SOCK;
            }
            if (::listen(sock, backlog) != 0) {
                SOCK_CLOSE(sock);
                return INVALID_SOCK;
            }
            return sock;
        }


        bool set_nonblocking(socket_t sock, bool nonblocking)
        {
            #ifdef _WIN32
                u_long mode = nonblocking ? 1u : 0u;
                return ::ioctlsocket(sock, FIONBIO, &mode) == 0;
            #else
                int flags = ::fcntl(sock, F_GETFL, 0);
                if (flags < 0) return false;
                if (nonblocking)
                    flags |= O_NONBLOCK;
                else
                    flags &= ~O_NONBLOCK;
                return ::fcntl(sock, F_SETFL, flags) == 0;
            #endif
        }


        std::string last_socket_error()
        {
            #ifdef _WIN32
                char buf[256] = {};
                FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr,
                            static_cast<DWORD>(WSAGetLastError()),
                            0, buf, sizeof(buf), nullptr);
                return buf;
            #else
                return std::strerror(errno);
            #endif
        }

        socket_t accept_client(socket_t server_sock,
                                struct sockaddr_in* addr_out,
                                socklen_t* len_out)
        {
            struct sockaddr_in tmp_addr;
            socklen_t tmp_len = sizeof(tmp_addr);

            struct sockaddr_in* paddr = addr_out ? addr_out : &tmp_addr;
            socklen_t*          plen  = len_out  ? len_out  : &tmp_len;

            *plen = sizeof(struct sockaddr_in);

            socket_t client;
            do {
                client = ::accept(server_sock,
                                reinterpret_cast<struct sockaddr*>(paddr),
                                plen);
        #ifndef _WIN32
            } while (client == INVALID_SOCK && errno == EINTR);
        #else
            } while (false);
        #endif
            return client;
        }

    } // namespace platform
} // namespace server
