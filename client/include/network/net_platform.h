#ifndef __NET_PLATFORM_H__
#define __NET_PLATFORM_H__


#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    typedef SOCKET   socket_t;
    typedef int      socklen_t;
    #define INVALID_SOCK   INVALID_SOCKET
    #define SOCK_CLOSE(s)  closesocket(s)
    #define SOCK_VALID(s)  ((s) != INVALID_SOCKET)
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    typedef int socket_t;
    #define INVALID_SOCK   (-1)
    #define SOCK_CLOSE(s)  close(s)
    #define SOCK_VALID(s)  ((s) >= 0)
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
    #endif

bool
net_init(void);

void
net_cleanup(void);

socket_t
net_connect_tcp(const char* host, uint16_t port);

bool
net_recv_exact(socket_t sock, uint8_t* buf, size_t len);

// send len bytes. in error cases return false
bool
net_send_exact(socket_t sock, const void* buf, size_t len);

/* read bytes to \r\n. string without \r\n stored to buf.
 * buf_size in bytes
 * return string len >= 0 or -1 in error cases
 */
int
net_recv_line(socket_t sock, char* buf, size_t buf_size);

/*Receive timeout in msecs, 0 -- without timeout*/
bool
net_set_recv_timeout(socket_t sock, uint32_t timeout_ms);

/*Last error text description*/
const char*
net_last_error(void);

/*Current host tile in UTC (UNIX epoch)*/
uint64_t
net_time_ms(void);

/*Platform-specific sleep in msecs*/
void
net_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif
