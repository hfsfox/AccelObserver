#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE 1
#endif

#include <network/net_platform.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <time.h>
    #include <unistd.h>
    #include <errno.h>
    #include <sys/time.h>
#endif

bool
net_init(void)
{
    #ifdef _WIN32
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    #else
        return true;
    #endif
}

void
net_cleanup(void)
{
    #ifdef _WIN32
        WSACleanup();
    #endif
}

socket_t
net_connect_tcp(const char* host, uint16_t port)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;    /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = NULL;
    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0 || res == NULL) return INVALID_SOCK;

    socket_t sock = INVALID_SOCK;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (!SOCK_VALID(sock)) continue;

        if (connect(sock, p->ai_addr, (socklen_t)p->ai_addrlen) == 0) break;

        SOCK_CLOSE(sock);
        sock = INVALID_SOCK;
    }
    freeaddrinfo(res);
    return sock;
}


bool
net_recv_exact(socket_t sock, uint8_t* buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        int chunk = (int)((len - received) > 65536 ? 65536 : (len - received));
        int n = recv(sock, (char*)(buf + received), chunk, 0);
        if (n <= 0) return false;
        received += (size_t)n;
    }
    return true;
}

bool
net_send_exact(socket_t sock, const void* buf, size_t len)
{
    size_t sent = 0;
    const char* ptr = (const char*)buf;
    while (sent < len) {
        int chunk = (int)((len - sent) > 65536 ? 65536 : (len - sent));
        int n = send(sock, ptr + sent, chunk, 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}


int
net_recv_line(socket_t sock, char* buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return -1;
    size_t pos = 0;
    char c;
    while (pos < buf_size - 1) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') {
            /* remove \r if exist */
            if (pos > 0 && buf[pos - 1] == '\r') --pos;
            buf[pos] = '\0';
            return (int)pos;
        }
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

bool
net_set_recv_timeout(socket_t sock, uint32_t timeout_ms)
{
    #ifdef _WIN32
        DWORD tv = (DWORD)timeout_ms;
        return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                        (const char*)&tv, sizeof(tv)) == 0;
    #else
        struct timeval tv;
        tv.tv_sec  = (time_t)(timeout_ms / 1000);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
        return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                        &tv, sizeof(tv)) == 0;
    #endif
}


const char*
net_last_error(void)
{
    #ifdef _WIN32
        static char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, (DWORD)WSAGetLastError(), 0, buf, sizeof(buf), NULL);
        return buf;
    #else
        return strerror(errno);
    #endif
}


uint64_t
net_time_ms(void)
{
    #ifdef _WIN32
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        // time shift from 01.01.1601 to 01.01.1970 in 100 ns interval
        t -= 116444736000000000ULL;
        return t / 10000ULL; /* msecs */
    #else
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}


void
net_sleep_ms(uint32_t ms)
{
    #ifdef _WIN32
        Sleep((DWORD)ms);
    #else
        struct timespec ts;
        ts.tv_sec  = (time_t)(ms / 1000);
        ts.tv_nsec = (long)((ms % 1000) * 1000000L);
        nanosleep(&ts, NULL);
    #endif
}
