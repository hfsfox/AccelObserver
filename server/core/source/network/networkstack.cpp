#include <core/network/networkstack.h>

namespace server
{
    namespace core
    {
        namespace network
        {
            status_code_t network_init(void)
            {
                #ifdef _WIN32
                    WSADATA wsa;
                    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
                        if(rc == 0)
                            return core::status::STATUS_OK;
                #endif
                    return core::status::STATUS_OK; // POSIX - no-op
            };
            void network_cleanup(void)
            {
                #ifdef _WIN32
                    WSACleanup();
                #endif
            };
            std::string last_socket_error(void)
            {
                #ifdef _WIN32
                    char buf[256] = {};
                    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM \
                    | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr,
                               static_cast<DWORD>(WSAGetLastError()),
                               0, buf, sizeof(buf), nullptr);
                    return buf;
                #else
                    return std::strerror(errno);
                #endif
            }

        }
    }
}

