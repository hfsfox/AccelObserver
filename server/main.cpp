//#include <confparser.h>
//#include <core/common/systypes.h>
//#include <core/protocol/transportprotocol.h>
//#include <core/config/serverconfig.h>
//#include <string>
//#include <cstddef>
//#include <cstdint>
#include <core/core.h>

/*
namespace server
{
    namespace core
    {
        namespace types
        {
            enum class transport_type
            {
                mqtt,
                websocket
            };
        }
    }
    namespace confparser
    {
        struct config
        {
            //server::core::types::protocol_type_t protocol = server::core::types::protocol_type_t::websocket;
            server::core::types::transport_type protocol = server::core::types::transport_type::websocket;
            std::string     host = "localhost";
            uint16_t        port = 8080;
            std::size_t     buffer_capacity = 4096;   ///< Максимальный размер кольцевого буфера (пакеты)
            std::size_t     flush_interval_ms = 500;  ///< Период сброса буфера на диск (мс)
        };
    }
}
*/

/*
static server::confparser::config parse_args(int argc, char* argv[])
{
    server::confparser::config conf;

    return conf;
}
*/

int main(int argc, char* argv[])
{
    //server::confparser::config conf = parse_args(argc, argv);
    server::core::types::config_t conf = server::core::parse_args(argc, argv);

    if(server::core::network::network_init() != server::core::status::STATUS_OK)
    {
        server::core::network::network_cleanup();
    }

    return 0;
}
