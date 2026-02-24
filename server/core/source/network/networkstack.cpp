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
                //#else
                #endif
                    return core::status::STATUS_OK; // POSIX - no-op
                //#endif
            };
            void network_cleanup(void)
            {
                #ifdef _WIN32
                    WSACleanup();
                #endif
            };
        }
    }
}

